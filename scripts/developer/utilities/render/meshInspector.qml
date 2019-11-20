//
//  meshInspector.qml
//
//  Created by Sabrina Shanman on 2019-11-15
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or https://www.apache.org/licenses/LICENSE-2.0.html
//
import QtQuick 2.7
import QtQuick.Controls 2.3 as Original

import stylesUit 1.0

Rectangle {
    HifiConstants { id: hifi;}
    color: Qt.rgba(hifi.colors.baseGray.r, hifi.colors.baseGray.g, hifi.colors.baseGray.b, 0.8);
    id: root;
    
    property var meshData: undefined
    property var jointData: undefined

    function fromScript(message) {
        switch (message.method) {
        case "setObjectInfo":
            var params = message.params;
            entityIDContainer.objectType = params.type;
            entityIDContainer.objectID = params.id;
            entityIDContainer.shapeIndex = params.shapeIndex;
            entityIDContainer.meshIndex = params.meshIndex;
            entityIDContainer.meshPartIndex = params.meshPartIndex;
            entityIDContainer.jointIndex = params.jointIndex;
            break;
        case "setMeshData":
            meshData = message.params.meshData;
            meshJSONText.text = message.params.meshDataText;
            break;
        case "setJointData":
            jointData = message.params.jointData;
            jointJSONText.text = message.params.jointDataText;
            break;
        }
    }
  
    Column {  
        
        anchors.left: parent.left 
        anchors.right: parent.right 

        Rectangle {
            id: entityIDContainer
            
            property var objectType: "Unknown"
            property var objectID: "Undefined"
            property var shapeIndex: -1
            property var meshIndex: -1
            property var meshPartIndex: -1
            property var jointIndex: -1
            
            height: 52
            width: root.width
            color: Qt.rgba(root.color.r * 0.7, root.color.g * 0.7, root.color.b * 0.7, 0.8);
            TextEdit {
                id: entityIDInfo
                text: "Type: " + entityIDContainer.objectType + "\n" +
                    "ID: " + entityIDContainer.objectID + "\n" +
                    "Shape/Mesh/Part/Joint: " + (entityIDContainer.shapeIndex == -1 ? "Unknown" :
                        (entityIDContainer.shapeIndex + "/" +
                        entityIDContainer.meshIndex + "/" +
                        entityIDContainer.meshPartIndex + "/" +
                        entityIDContainer.jointIndex)
                    )
                font.pointSize: 9
                color: "#FFFFFF"
                readOnly: true
                selectByMouse: true
            }
        }

        Original.ScrollView {
            height: (root.height - entityIDContainer.height) / 2
            width: root.width
            clip: true
            Original.ScrollBar.horizontal.policy: Original.ScrollBar.AlwaysOff
            TextEdit {
                id: meshJSONText
                text: "Click an entity to get mesh information"
                width: root.width
                font.pointSize: 10
                color: "#FFFFFF"
                readOnly: true
                selectByMouse: true
                wrapMode: Text.WordWrap
            }
        }

        Original.ScrollView {
            height: (root.height - entityIDContainer.height) / 2
            width: root.width
            clip: true
            Original.ScrollBar.horizontal.policy: Original.ScrollBar.AlwaysOff
            TextEdit {
                id: jointJSONText
                text: "Click an entity to get transform information"
                width: root.width
                font.pointSize: 10
                color: "#FFFFFF"
                readOnly: true
                selectByMouse: true
                wrapMode: Text.WordWrap
            }
        }
    }
}
