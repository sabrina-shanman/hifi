//
//  FBXBaker.cpp
//  tools/baking/src
//
//  Created by Stephen Birarda on 3/30/17.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "FBXBaker.h"

#include <cmath> // need this include so we don't get an error looking for std::isnan

#include <QtConcurrent>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QThread>

#include <mutex>

#include <NetworkAccessManager.h>
#include <SharedUtil.h>

#include <PathUtils.h>

#include <FBXSerializer.h>
#include <FBXWriter.h>

#include "ModelBakingLoggingCategory.h"
#include "TextureBaker.h"

#ifdef HIFI_DUMP_FBX
#include "FBXToJSON.h"
#endif

void FBXBaker::bakeSourceCopy() {
    // load the scene from the FBX file
    importScene();

    if (shouldStop()) {
        return;
    }

    // enumerate the models and textures found in the scene and start a bake for them
    rewriteAndBakeSceneTextures();

    if (shouldStop()) {
        return;
    }

    rewriteAndBakeSceneModels();

    if (shouldStop()) {
        return;
    }

    // check if we're already done with textures (in case we had none to re-write)
    checkIfTexturesFinished();
}

void FBXBaker::importScene() {
    qDebug() << "file path: " << _originalModelFilePath.toLocal8Bit().data() << QDir(_originalModelFilePath).exists();

    QFile fbxFile(_originalModelFilePath);
    if (!fbxFile.open(QIODevice::ReadOnly)) {
        handleError("Error opening " + _originalModelFilePath + " for reading");
        return;
    }

    FBXSerializer fbxSerializer;

    qCDebug(model_baking) << "Parsing" << _modelURL;
    _rootNode = fbxSerializer._rootNode = fbxSerializer.parseFBX(&fbxFile);

#ifdef HIFI_DUMP_FBX
    {
        FBXToJSON fbxToJSON;
        fbxToJSON << _rootNode;
        QFileInfo modelFile(_originalModelFilePath);
        QString outFilename(_bakedOutputDir + "/" + modelFile.completeBaseName() + "_FBX.json");
        QFile jsonFile(outFilename);
        if (jsonFile.open(QIODevice::WriteOnly)) {
            jsonFile.write(fbxToJSON.str().c_str(), fbxToJSON.str().length());
            jsonFile.close();
        }
    }
#endif

    _hfmModel = fbxSerializer.extractHFMModel({}, _modelURL.toString());
    _textureContentMap = fbxSerializer._textureContent;
}

void FBXBaker::rewriteAndBakeSceneModels() {
    unsigned int meshIndex = 0;
    bool hasDeformers { false };
    for (FBXNode& rootChild : _rootNode.children) {
        if (rootChild.name == "Objects") {
            for (FBXNode& objectChild : rootChild.children) {
                if (objectChild.name == "Deformer") {
                    hasDeformers = true;
                    break;
                }
            }
        }
        if (hasDeformers) {
            break;
        }
    }
    for (FBXNode& rootChild : _rootNode.children) {
        if (rootChild.name == "Objects") {
            for (FBXNode& objectChild : rootChild.children) {
                if (objectChild.name == "Geometry") {

                    // TODO Pull this out of _hfmModel instead so we don't have to reprocess it
                    auto extractedMesh = FBXSerializer::extractMesh(objectChild, meshIndex, false);
                    
                    // Callback to get MaterialID
                    GetMaterialIDCallback materialIDcallback = [&extractedMesh](int partIndex) {
                        return extractedMesh.partMaterialTextures[partIndex].first;
                    };
                    
                    // Compress mesh information and store in dracoMeshNode
                    FBXNode dracoMeshNode;
                    bool success = compressMesh(extractedMesh.mesh, hasDeformers, dracoMeshNode, materialIDcallback);
                    
                    // if bake fails - return, if there were errors and continue, if there were warnings.
                    if (!success) {
                        if (hasErrors()) {
                            return;
                        } else if (hasWarnings()) {
                            continue;
                        }
                    } else {
                        objectChild.children.push_back(dracoMeshNode);

                        static const std::vector<QString> nodeNamesToDelete {
                            // Node data that is packed into the draco mesh
                            "Vertices",
                            "PolygonVertexIndex",
                            "LayerElementNormal",
                            "LayerElementColor",
                            "LayerElementUV",
                            "LayerElementMaterial",
                            "LayerElementTexture",

                            // Node data that we don't support
                            "Edges",
                            "LayerElementTangent",
                            "LayerElementBinormal",
                            "LayerElementSmoothing"
                        };
                        auto& children = objectChild.children;
                        auto it = children.begin();
                        while (it != children.end()) {
                            auto begin = nodeNamesToDelete.begin();
                            auto end = nodeNamesToDelete.end();
                            if (find(begin, end, it->name) != end) {
                                it = children.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                }  // Geometry Object

            } // foreach root child
        }
    }
}

void FBXBaker::rewriteAndBakeSceneTextures() {
    using namespace image::TextureUsage;
    QHash<QString, image::TextureUsage::Type> textureTypes;

    // enumerate the materials in the extracted geometry so we can determine the texture type for each texture ID
    for (const auto& material : _hfmModel->materials) {
        if (material.normalTexture.isBumpmap) {
            textureTypes[material.normalTexture.id] = BUMP_TEXTURE;
        } else {
            textureTypes[material.normalTexture.id] = NORMAL_TEXTURE;
        }

        textureTypes[material.albedoTexture.id] = ALBEDO_TEXTURE;
        textureTypes[material.glossTexture.id] = GLOSS_TEXTURE;
        textureTypes[material.roughnessTexture.id] = ROUGHNESS_TEXTURE;
        textureTypes[material.specularTexture.id] = SPECULAR_TEXTURE;
        textureTypes[material.metallicTexture.id] = METALLIC_TEXTURE;
        textureTypes[material.emissiveTexture.id] = EMISSIVE_TEXTURE;
        textureTypes[material.occlusionTexture.id] = OCCLUSION_TEXTURE;
        textureTypes[material.lightmapTexture.id] = LIGHTMAP_TEXTURE;
    }

    // enumerate the children of the root node
    for (FBXNode& rootChild : _rootNode.children) {

        if (rootChild.name == "Objects") {

            // enumerate the objects
            auto object = rootChild.children.begin();
            while (object != rootChild.children.end()) {
                if (object->name == "Texture") {

                    // double check that we didn't get an abort while baking the last texture
                    if (shouldStop()) {
                        return;
                    }

                    // enumerate the texture children
                    for (FBXNode& textureChild : object->children) {

                        if (textureChild.name == "RelativeFilename") {
                            QString hfmTextureFileName { textureChild.properties.at(0).toString() };
                            
                            // grab the ID for this texture so we can figure out the
                            // texture type from the loaded materials
                            auto textureID { object->properties[0].toString() };
                            auto textureType = textureTypes[textureID];

                            // Compress the texture information and return the new filename to be added into the FBX scene
                            auto bakedTextureFile = compressTexture(hfmTextureFileName, textureType);

                            // If no errors or warnings have occurred during texture compression add the filename to the FBX scene
                            if (!bakedTextureFile.isNull()) {
                                textureChild.properties[0] = bakedTextureFile;
                            } else {
                                // if bake fails - return, if there were errors and continue, if there were warnings.
                                if (hasErrors()) {
                                    return;
                                } else if (hasWarnings()) {
                                    continue;
                                }
                            }
                        }
                    }

                    ++object;

                } else if (object->name == "Video") {
                    // this is an embedded texture, we need to remove it from the FBX
                    object = rootChild.children.erase(object);
                } else {
                    ++object;
                }
            }
        }
    }
}
