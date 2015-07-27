(function() {
var t = db.server19559;

t.drop();
t.ensureIndex({otherField : 1}); // This must be created before {data:1}.
t.ensureIndex({data : 1});

function getDiskLoc() {
    return t.find()._addSpecial('$showDiskLoc', 1).next().$diskLoc;
}

function assertCountEq(count) {
    assert.eq(t.find().itcount(), count);
    assert.eq(t.find().hint({_id:1}).itcount(), count);
    assert.eq(t.find().hint({otherField:1}).itcount(), count);
}

// insert document with large value
db.adminCommand({setParameter:1, failIndexKeyTooLong:false});
var bigStrX = new Array(1024).join('x');
var bigStrY = new Array(1024).join('y'); // same size as bigStrX
assert.writeOK(t.insert({_id : 1, data: bigStrX}));
assertCountEq(1);
var origLoc = getDiskLoc();
db.adminCommand({setParameter:1, failIndexKeyTooLong:true});

// update to document needing move will fail when it tries to index oversized field. Ensure that it
// fully reverts to the original state.
var biggerStr = new Array(4096).join('y');
assert.writeError(t.update({_id:1}, {$set: {data: biggerStr}}));
assert.eq(getDiskLoc(), origLoc);
assertCountEq(1);

// non-moving update that will fail
assert.writeError(t.update({_id:1}, {$set: {otherField:1, data:bigStrY}}));
assert.eq(getDiskLoc(), origLoc);
assertCountEq(1);

// moving update that will succeed since not changing indexed fields.
assert.writeOK(t.update({_id:1}, {$set: {otherField:1, notIndexed:bigStrY}}));
assert(!friendlyEqual(getDiskLoc(), origLoc));
assertCountEq(1);

// remove and assert there are no orphan entries.
assert.writeOK(t.remove({}));
assertCountEq(0);
})();