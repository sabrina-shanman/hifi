//
//  materialInspector.js
//
//  Created by Sabrina Shanman on 2019-01-17
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or https://www.apache.org/licenses/LICENSE-2.0.html
//
"use strict";

Script.include("inspector.js");

var activeWindow;

function onInspectShapeMaterial(result) {
    updateMaterial(result);
}

var materialInspector = new Inspector(onInspectShapeMaterial);

// Adapted from Samuel G's material painting script
function getTopMaterial(multiMaterial) {
    // For non-models: multiMaterial[0] will be the top material
    // For models, multiMaterial[0] is the base material, and multiMaterial[1] is the highest priority applied material
    if (multiMaterial.length > 1) {
        if (multiMaterial[1].priority > multiMaterial[0].priority) {
            return multiMaterial[1];
        }
    }

    return multiMaterial[0];
}

function updateMaterial(result) {
    var mesh = Graphics.getModel(result.id);
    var meshPartString = result.meshPartIndex.toString();
    if (!mesh) {
        return;
    }
    var materials = mesh.materialLayers;
    if (!materials[meshPartString] || materials[meshPartString].length <= 0) {
        return;
    }
    
    var topMaterial = getTopMaterial(materials[meshPartString]);
    var materialJSONText = JSON.stringify({
        materialVersion: 1,
        materials: topMaterial.material
    }, null, 2);
    
    toQml({method: "setObjectInfo", params: {
            id: result.id,
            type: result.type,
            shapeIndex: result.shapeIndex,
            meshIndex: result.meshIndex,
            meshPartIndex: result.meshPartIndex,
            jointIndex: result.jointIndex
        }
    });
    toQml({method: "setMaterialJSON", params: {materialJSONText: materialJSONText}});
}

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
    materialInspector.setWindow(window);
}

function cleanup() {
    setWindow(undefined);
    materialInspector.cleanup();
}

Script.scriptEnding.connect(cleanup);

module.exports = {
    setWindow: setWindow
};
