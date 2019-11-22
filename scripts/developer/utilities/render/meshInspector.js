//
//  meshInspector.js
//
//  Created by Sabrina Shanman on 2019-11-15
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or https://www.apache.org/licenses/LICENSE-2.0.html
//
"use strict";

Script.include("inspector.js");

var activeWindow;

function getMeshData(objectID, meshIndex) {
    var model = Graphics.getModel(objectID);
    if (model !== undefined) {
        var meshes = model.meshes;
        if (meshIndex < meshes.length) {
            return meshes[meshIndex];
        }
    }
    return undefined;
}

function getEntityJointProperties(entityID, jointIndex, jointName) {
    var jointProperties = {};
    jointProperties.jointIndex = jointIndex;
    jointProperties.parentJointIndex = Entities.getJointParent(entityID, jointIndex);
    jointProperties.name = jointName;
    jointProperties.localRotation = Entities.getLocalJointRotation(entityID, jointIndex);
    jointProperties.localTranslation = Entities.getLocalJointTranslation(entityID, jointIndex);
    // TODO: Scale
    
    return jointProperties;
}

function getJointData(objectID, jointIndex) {
    if (jointIndex === -1 || Uuid.isNull(objectID)) {
        return undefined;
    }
    
    var entityJointNames = Entities.getJointNames(objectID);
    if (jointIndex < entityJointNames.length) {
        var entityJointData = {};
        var jointsToRoot = [];
        for (var lineageJointID = jointIndex;
                lineageJointID >= 0 && lineageJointID < entityJointNames.length;
                lineageJointID = jointsToRoot[jointsToRoot.length-1].parentJointIndex) {
            var entityJointProperties = getEntityJointProperties(objectID, lineageJointID, entityJointNames[lineageJointID]);
            jointsToRoot.push(entityJointProperties);
        }
        entityJointData.jointsToRoot = jointsToRoot;
        return entityJointData;
    }
    
    // TODO: MyAvatar, other avatars
    if (objectID == MyAvatar.sessionUUID) {
        var avatarJointNames = MyAvatar.getJointNames();
        // ...
    }
    
    return undefined;
}

function onInspectShape(result) {
    toQml({method: "setObjectInfo", params: {
            id: result.id,
            type: result.type,
            shapeIndex: result.shapeIndex,
            meshIndex: result.meshIndex,
            meshPartIndex: result.meshPartIndex,
            jointIndex: result.jointIndex
        }
    });
    var meshData = getMeshData(result.id, result.meshIndex);
    toQml({method: "setMeshData", params: {
            meshData: meshData,
            meshDataText: meshData !== undefined ? JSON.stringify(meshData, null, 2) : "undefined"
        }
    });
    // TODO: Some sort of overlay visualization
    var jointData = getJointData(result.id, result.jointIndex);
    toQml({method: "setJointData", params: {
            jointData: jointData,
            jointDataText: jointData !== undefined ? JSON.stringify(jointData, null, 2) : "Not yet implemented"
        }
    });
}

var meshInspector = new Inspector(onInspectShape);

function toQml(message) {
    if (activeWindow === undefined) {
        return; // Shouldn't happen
    }
    
    activeWindow.sendToQml(message);
}

function fromQml(message) {
    // No cases currently
}

function setWindow(window) {
    if (activeWindow !== undefined) {
        activeWindow.fromQml.disconnect(fromQml);
        activeWindow.close();
    }
    if (window !== undefined) {
        window.fromQml.connect(fromQml);
    }
    activeWindow = window;
    meshInspector.setWindow(window);
}

function cleanup() {
    setWindow(undefined);
    meshInspector.cleanup();
}

Script.scriptEnding.connect(cleanup);

module.exports = {
    setWindow: setWindow
};
