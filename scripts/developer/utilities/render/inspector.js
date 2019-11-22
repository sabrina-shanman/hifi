//
//  inspector.js
//
//  Created by Sabrina Shanman on 2019-11-14
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or https://www.apache.org/licenses/LICENSE-2.0.html
//

// Use an inspector to click on visual geometry on a ray-pickable entity/avatar and get information.

var inspectors = [];
// All inspectors use the same blacklist so they avoid hitting visualization entities created by other inspectors.
// But they keep track of which elements in the blacklist belong to them.
var globalBlacklist = [];

var pressedID;
var pressedShape;
var pressedType;

var SELECT_LIST = "luci_materialInspector_SelectionList";
Selection.enableListHighlight(SELECT_LIST, {
    outlineUnoccludedColor: { red: 125, green: 255, blue: 225 }
});
function setInspectedObject(id, type) {
    Selection.clearSelectedItemsList(SELECT_LIST);
    if (id !== undefined && !Uuid.isNull(id)) {
        Selection.addToSelectedItemsList(SELECT_LIST, type.toLowerCase(), id);
    }
}

function shouldInspect() {
    var shouldInspect = false;
    for (var i = 0; i < inspectors.length; i+=1) {
        var window = inspectors[i].window;
        if (window !== undefined) {
            shouldInspect = true;
            break;
        }
    }
    return shouldInspect;
}

function updateInspectState() {
    if (shouldInspect() && pressedID != Uuid.NULL) {
        setInspectedObject(pressedID, pressedType);
    } else {
        pressedID = Uuid.NULL;
        setInspectedObject(Uuid.NULL, "");
    }
}

function updateGlobalBlacklist() {
    globalBlacklist = [];
    for (var i = 0; i < inspectors.length; i+=1) {
        var blacklist = inspectors[i].blacklist;
        for (var j = 0; j < blacklist.length; j+=1) {
            var blacklistedItem = blacklist[j];
            if (globalBlacklist.indexOf(blacklistedItem) == -1) {
                globalBlacklist.push(blacklistedItem);
            }
        }
    }
}

Inspector = function(onInspectShape) {
    this.onInspectShape = onInspectShape;
    this.blacklist = [];
    this.window = undefined; // Do not show inspector glow if no inspector window is open
    inspectors.push(this);
};

Inspector.prototype.cleanup = function() {
    this._setBlacklist([]);
    this.setWindow(undefined);
    var inspectorIndex = inspectors.indexOf(this);
    inspectors.splice(inspectorIndex, 1); // Remove thyself
};

Inspector.prototype.setVisualizationEntities = function(visualizationEntities) {
    this._setBlacklist(visualizationEntities);
};

Inspector.prototype._setBlacklist = function(blacklist) {
    var different = blacklist !== this.blacklist;
    this.blacklist = blacklist;
    if (different) {
        updateGlobalBlacklist();
    }
};

Inspector.prototype.setWindow = function(window) {
    this.window = window;
    updateInspectState();
};

Inspector.prototype._shouldIgnore = function(entityOrAvatar) {
    return globalBlacklist.indexOf(entityOrAvatar) !== -1;
};

Inspector.prototype._inspectShape = function(shapeData) {
    if (this.onInspectShape !== undefined) {
        this.onInspectShape(shapeData);
    }
};

// Adapted from Samuel G's material painting script
function getHoveredShape(event) {
    // TODO: Async picking
    var pickRay = Camera.computePickRay(event.x, event.y);
    var closest;
    var id;
    var type = "Entity";
    var avatar = AvatarManager.findRayIntersection(pickRay, [], globalBlacklist);
    var entity = Entities.findRayIntersection(pickRay, true, [], globalBlacklist);
    var overlay = Overlays.findRayIntersection(pickRay, true, [], globalBlacklist);

    closest = entity;
    id = entity.entityID;

    if (avatar.intersects && avatar.distance < closest.distance) {
        closest = avatar;
        id = avatar.avatarID;
        type = "Avatar";
    } else if (overlay.intersects && overlay.distance < closest.distance) {
        closest = overlay;
        id = overlay.overlayID;
        type = "Overlay";
    }

    if (closest.intersects) {
        var hasShape = (closest.extraInfo.shapeID !== undefined);
        return {
            type: type,
            id: id,
            shapeIndex: (hasShape ? closest.extraInfo.shapeID : -1),
            meshIndex: (hasShape ? closest.extraInfo.subMeshIndex : -1),
            meshPartIndex: (hasShape ? closest.extraInfo.partIndex : -1),
            jointIndex: (hasShape ? closest.extraInfo.jointIndex : -1)
        };
    } else {
        return undefined;
    }
}

function mousePressEvent(event) {
    if (!event.isLeftButton) {
        return;
    }
    
    var result = getHoveredShape(event);

    if (result !== undefined) {
        pressedID = result.id;
        pressedShape = result.shapeIndex;
        pressedType = result.type;
    }
}

function mouseReleaseEvent(event) {
    if (!event.isLeftButton) {
        return;
    }
    
    var result = getHoveredShape(event);
    
    if (result !== undefined && result.id === pressedID && result.shapeIndex === pressedShape) {
        for (var i = 0; i < inspectors.length; i+=1) {
            var inspector = inspectors[i];
            inspector._inspectShape(result);
        }
    }
    
    updateInspectState();
}

Controller.mousePressEvent.connect(mousePressEvent);
Controller.mouseReleaseEvent.connect(mouseReleaseEvent);

function cleanup() {
    for (var i = 0; i < inspectors.length; i+=1) {
        var inspector = inspectors[i];
        inspector.cleanup();
    }
    inspectors = [];
    globalBlacklist = [];
    Controller.mousePressEvent.disconnect(mousePressEvent);
    Controller.mouseReleaseEvent.disconnect(mouseReleaseEvent);
    Selection.disableListHighlight(SELECT_LIST);
}

Script.scriptEnding.connect(cleanup);

module.exports = {
    Inspector: Inspector
};
