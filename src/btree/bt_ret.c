/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2008-2011 WiredTiger, Inc.
 *	All rights reserved.
 */

#include "wt_internal.h"
#include "btree.i"

/*
 * __wt_return_data --
 *	Return a WT_PAGE/WT_{ROW,COL}_INDX pair to the application.
 */
int
__wt_return_data(SESSION *session, WT_ITEM *key, WT_ITEM *value, int key_return)
{
	BTREE *btree;
	WT_CURSOR *cursor;
	WT_ITEM local_key, local_value;
	WT_COL *cip;
	WT_CELL *cell;
	WT_PAGE *page;
	WT_ROW *rip;
	WT_UPDATE *upd;
	const void *value_ret;
	uint32_t size_ret;
	int (*callback)(BTREE *, WT_ITEM *, WT_ITEM *), ret;

	btree = session->btree;
	cursor = session->cursor;
	callback = NULL; /* TODO: was value->callback */
	ret = 0;

	page = session->srch_page;
	cip = session->srch_ip;
	rip = session->srch_ip;
	upd = session->srch_vupdate;

	/*
	 * Handle the key item -- the key may be unchanged, in which case we
	 * don't touch it, it's already correct.
	 *
	 * If the key/value items are being passed to a callback routine and
	 * there's nothing special about them (they aren't uninstantiated
	 * overflow or compressed items), then give the callback a pointer to
	 * the on-page data.  (We use a local WT_ITEM in this case, so we don't
	 * touch potentially allocated application WT_ITEM memory.)  Else, copy
	 * the items into the application's WT_DATAITEMs.
	 *
	 * If the key/value item are uninstantiated overflow and/or compressed
	 * items, they require processing before being copied into the
	 * WT_DATAITEMs.  Don't allocate WT_ROW/COL memory for key/value items
	 * here.  (We never allocate WT_ROW/COL memory for data items.   We do
	 * allocate WT_ROW/COL memory for keys, but if we are looking at a key
	 * only to return it, it's not that likely to be accessed again (think
	 * of a cursor moving through the tree).  Use memory in the
	 * application's WT_ITEM instead, it is discarded when the SESSION is
	 * discarded.
	 *
	 * Key return implies a reference to a WT_ROW index (we don't return
	 * record number keys yet, that will probably change when I add cursor
	 * support).
	 */
	if (key_return) {
		if (__wt_key_process(rip)) {
			WT_RET(__wt_key_build(session,
			    page, rip, &cursor->key));
			key->data = rip->key;
			key->size = rip->size;
		} else if (callback == NULL) {
			WT_RET(__wt_buf_set(session,
			    &cursor->key, rip->key, rip->size));

			*key = *(WT_ITEM *)&cursor->key;
		} else {
			WT_CLEAR(local_key);
			key = &local_key;
			key->data = rip->key;
			key->size = rip->size;
		}
	}

	/*
	 * Handle the value.
	 *
	 * If the item was ever updated, it's easy, take the last update,
	 * it's just a byte string.
	 */
	if (upd != NULL) {
		if (WT_UPDATE_DELETED_ISSET(upd))
			return (WT_NOTFOUND);
		value->data = WT_UPDATE_DATA(upd);
		value->size = upd->size;
		return (callback == NULL ? 0 : callback(btree, key, value));
	}

	/* Otherwise, take the item from the original page. */
	switch (page->type) {
	case WT_PAGE_COL_FIX:
		value_ret = WT_COL_PTR(page, cip);
		size_ret = btree->fixed_len;
		break;
	case WT_PAGE_COL_RLE:
		value_ret = WT_RLE_REPEAT_DATA(WT_COL_PTR(page, cip));
		size_ret = btree->fixed_len;
		break;
	case WT_PAGE_COL_VAR:
		cell = WT_COL_PTR(page, cip);
		goto cell_set;
	case WT_PAGE_ROW_LEAF:
		if (WT_ROW_EMPTY_ISSET(rip)) {
			value_ret = "";
			size_ret = 0;
			break;
		}
		cell = WT_ROW_PTR(page, rip);
cell_set:	switch (WT_CELL_TYPE(cell)) {
		case WT_CELL_DATA:
			if (btree->huffman_value == NULL) {
				value_ret = WT_CELL_BYTE(cell);
				size_ret = WT_CELL_LEN(cell);
			}
			/* FALLTHROUGH */
		case WT_CELL_DATA_OVFL:
			WT_RET(__wt_cell_process(
			    session, cell, &cursor->value));
			value_ret = cursor->value.data;
			size_ret = cursor->value.size;
			break;
		WT_ILLEGAL_FORMAT(session);
		}
		break;
	WT_ILLEGAL_FORMAT(session);
	}

	/*
	 * When we get here, value_ret and size_ret are set to the byte string
	 * and the length we're going to return.   That byte string has been
	 * decoded, we called __wt_cell_process above in all cases where the
	 * item could be encoded.
	 */
	if (callback == NULL) {
		/*
		 * We're copying the key/value pair out to the caller.  If we
		 * haven't yet copied the value_ret/size_ret pair into the
		 * return WT_ITEM (potentially done by __wt_cell_process), do
		 * so now.
		 */
		if (value_ret != cursor->value.data)
			WT_RET(__wt_buf_set(
			    session, &cursor->value, value_ret, size_ret));

		*value = *(WT_ITEM *)&cursor->value;
	} else {
		/*
		 * If we're given a callback function, use the data_ret/size_ret
		 * fields as set.
		 */
		WT_CLEAR(local_value);
		value = &local_value;
		value->data = value_ret;
		value->size = size_ret;
		ret = callback(btree, key, value);
	}

	return (ret);
}
