//
//  MaterialBaker.cpp
//  libraries/baking/src
//
//  Created by Sam Gondelman on 2/26/2019
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "MaterialBaker.h"

#include "QJsonObject"
#include "QJsonDocument"

#include "MaterialBakingLoggingCategory.h"

#include <SharedUtil.h>
#include <PathUtils.h>

#include <graphics-scripting/GraphicsScriptingInterface.h>

std::function<QThread*()> MaterialBaker::_getNextOvenWorkerThreadOperator;

static int materialNum = 0;

MaterialBaker::MaterialBaker(const QString& materialData, bool isURL, const QString& bakedOutputDir, const QUrl& destinationPath, bool rebakeOriginals) :
    _materialData(materialData),
    _isURL(isURL),
    _bakedOutputDir(bakedOutputDir),
    _textureOutputDir(bakedOutputDir + "/materialTextures/" + QString::number(materialNum++)),
    _destinationPath(destinationPath),
    _rebakeOriginals(rebakeOriginals)
{
}

void MaterialBaker::bake() {
    qDebug(material_baking) << "Material Baker" << _materialData << "bake starting";

    // once our script is loaded, kick off a the processing
    connect(this, &MaterialBaker::originalMaterialLoaded, this, &MaterialBaker::processMaterial);

    if (!_materialResource) {
        // first load the material (either locally or remotely)
        loadMaterial();
    } else {
        // we already have a material passed to us, use that
        if (_materialResource->isLoaded()) {
            processMaterial();
        } else {
            connect(_materialResource.data(), &Resource::finished, this, &MaterialBaker::originalMaterialLoaded);
        }
    }
}

void MaterialBaker::abort() {
    Baker::abort();

    for (auto& textureBaker : _textureBakers) {
        textureBaker->abort();
    }
}

void MaterialBaker::loadMaterial() {
    if (!_isURL) {
        qCDebug(material_baking) << "Loading local material" << _materialData;

        _materialResource = NetworkMaterialResourcePointer(new NetworkMaterialResource());
        // TODO: add baseURL to allow these to reference relative files next to them
        _materialResource->parsedMaterials = NetworkMaterialResource::parseJSONMaterials(QJsonDocument::fromJson(_materialData.toUtf8()), QUrl());
    } else {
        qCDebug(material_baking) << "Downloading material" << _materialData;
        _materialResource = MaterialCache::instance().getMaterial(_materialData);
    }

    if (_materialResource) {
        if (_materialResource->isLoaded()) {
            emit originalMaterialLoaded();
        } else {
            connect(_materialResource.data(), &Resource::finished, this, &MaterialBaker::originalMaterialLoaded);
        }
    } else {
        handleError("Error loading " + _materialData);
    }
}

void MaterialBaker::processMaterial() {
    if (!_materialResource || _materialResource->parsedMaterials.networkMaterials.size() == 0) {
        handleError("Error processing " + _materialData);
        return;
    }

    if (QDir(_textureOutputDir).exists()) {
        qWarning() << "Output path" << _textureOutputDir << "already exists. Continuing.";
    } else {
        qCDebug(material_baking) << "Creating materialTextures output folder" << _textureOutputDir;
        if (!QDir().mkpath(_textureOutputDir)) {
            handleError("Failed to create materialTextures output folder " + _textureOutputDir);
        }
    }

    for (auto networkMaterial : _materialResource->parsedMaterials.networkMaterials) {
        if (networkMaterial.second) {
            auto textures = networkMaterial.second->getTextures();
            for (auto texturePair : textures) {
                auto mapChannel = texturePair.first;
                auto textureMap = texturePair.second;
                if (textureMap.texture && textureMap.texture->_textureSource) {
                    auto type = textureMap.texture->getTextureType();

                    QByteArray content;
                    QUrl textureURL;
                    {
                        bool foundEmbeddedTexture = false;
                        auto textureContentMapIter = _textureContentMap.find(networkMaterial.second->getName());
                        if (textureContentMapIter != _textureContentMap.end()) {
                            auto textureUsageIter = textureContentMapIter->second.find(type);
                            if (textureUsageIter != textureContentMapIter->second.end()) {
                                content = textureUsageIter->second.first;
                                textureURL = textureUsageIter->second.second;
                                foundEmbeddedTexture = true;
                            }
                        }
                        if (!foundEmbeddedTexture && textureMap.texture->_textureSource) {
                            textureURL = textureMap.texture->_textureSource->getUrl().adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment);
                        }
                    }

                    QString cleanURL = textureURL.toDisplayString();
                    auto idx = cleanURL.lastIndexOf('.');
                    QString extension = idx >= 0 ? cleanURL.mid(idx + 1).toLower() : "";

                    if (QImageReader::supportedImageFormats().contains(extension.toLatin1()) || (_rebakeOriginals && textureURL.fileName().endsWith(TEXTURE_META_EXTENSION))) {
                        QPair<QUrl, image::TextureUsage::Type> textureKey(textureURL, type);
                        if (!_textureBakers.contains(textureKey)) {
                            auto baseTextureFileName = _textureFileNamer.createBaseTextureFileName(textureURL.fileName(), type);

                            QSharedPointer<TextureBaker> textureBaker {
                                new TextureBaker(textureURL, type, _textureOutputDir, "", baseTextureFileName, content),
                                &TextureBaker::deleteLater
                            };
                            textureBaker->setMapChannel(mapChannel);
                            connect(textureBaker.data(), &TextureBaker::finished, this, &MaterialBaker::handleFinishedTextureBaker);
                            _textureBakers.insert(textureKey, textureBaker);
                            textureBaker->moveToThread(_getNextOvenWorkerThreadOperator ? _getNextOvenWorkerThreadOperator() : thread());
                            // By default, Qt will invoke this bake immediately if the TextureBaker is on the same worker thread as this MaterialBaker.
                            // We don't want that, because threads may be waiting for work while this thread is stuck processing a TextureBaker.
                            // On top of that, _textureBakers isn't fully populated.
                            // So, use Qt::QueuedConnection.
                            // TODO: Better thread utilization at the top level, not just the MaterialBaker level
                            QMetaObject::invokeMethod(textureBaker.data(), "bake", Qt::QueuedConnection);
                        }
                    } else {
                        qCDebug(material_baking) << "Texture extension not supported: " << extension;
                    }
                }
            }
        }
    }

    if (_textureBakers.empty()) {
        outputMaterial();
    }
}

void MaterialBaker::handleFinishedTextureBaker() {
    auto baker = qobject_cast<TextureBaker*>(sender());

    if (baker) {
        QPair<QUrl, image::TextureUsage::Type> textureKey = { baker->getTextureURL(), baker->getTextureType() };
        if (!baker->hasErrors()) {
            // this TextureBaker is done and everything went according to plan
            qCDebug(material_baking) << "Re-writing texture references to" << baker->getTextureURL();

            auto newURL = QUrl(_textureOutputDir).resolved(baker->getMetaTextureFileName());
            auto relativeURL = QDir(_bakedOutputDir).relativeFilePath(newURL.toString());

            // Queue old texture URLs to be replaced
            if (!_isURL) {
                // Absolute paths are needed for embedded textures
                _materialRewrites[textureKey] = _destinationPath.resolved(relativeURL);
            } else {
                _materialRewrites[textureKey] = relativeURL;
            }
        } else {
            // this texture failed to bake - this doesn't fail the entire bake but we need to add the errors from
            // the texture to our warnings
            _warningList << baker->getWarnings();
        }

        _textureBakers.remove(textureKey);

        if (_textureBakers.empty()) {
            outputMaterial();
        }
    } else {
        handleWarning("Unidentified baker finished and signaled to material baker to handle texture. Material: " + _materialData);
    }
}

scriptable::ScriptableMaterial getRemappedMaterial(const std::shared_ptr<NetworkMaterial>& networkMaterial, const QHash<QPair<QUrl, image::TextureUsage::Type>, QUrl>& materialRewrites) {
    scriptable::ScriptableMaterial remappedMaterial { networkMaterial };

    const static auto remapMaterial = [](const QHash<QPair<QUrl, image::TextureUsage::Type>, QUrl>& materialRewrites, QString& materialMap, image::TextureUsage::Type textureType) {
        if (!materialMap.isEmpty()) {
            const auto rewriteIt = materialRewrites.constFind({ materialMap, textureType });
            if (rewriteIt != materialRewrites.cend()) {
                materialMap = (*rewriteIt).toString();
            }
        }
    };

    remapMaterial(materialRewrites, remappedMaterial.emissiveMap, image::TextureUsage::EMISSIVE_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.albedoMap, image::TextureUsage::ALBEDO_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.opacityMap, image::TextureUsage::OPACITY_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.metallicMap, image::TextureUsage::METALLIC_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.specularMap, image::TextureUsage::SPECULAR_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.roughnessMap, image::TextureUsage::ROUGHNESS_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.glossMap, image::TextureUsage::GLOSS_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.normalMap, image::TextureUsage::NORMAL_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.bumpMap, image::TextureUsage::BUMP_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.occlusionMap, image::TextureUsage::OCCLUSION_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.lightmapMap, image::TextureUsage::LIGHTMAP_TEXTURE);
    remapMaterial(materialRewrites, remappedMaterial.scatteringMap, image::TextureUsage::SCATTERING_TEXTURE);

    return remappedMaterial;
}

void MaterialBaker::outputMaterial() {
    if (_materialResource) {
        QJsonObject json;
        if (_materialResource->parsedMaterials.networkMaterials.size() == 1) {
            auto networkMaterial = _materialResource->parsedMaterials.networkMaterials.begin();
            auto remappedMaterial = getRemappedMaterial(networkMaterial->second, _materialRewrites);
            QVariant materialVariant = scriptable::scriptableMaterialToScriptValue(&_scriptEngine, remappedMaterial).toVariant();
            json.insert("materials", QJsonDocument::fromVariant(materialVariant).object());
        } else {
            QJsonArray materialArray;
            for (auto networkMaterial : _materialResource->parsedMaterials.networkMaterials) {
                auto remappedMaterial = getRemappedMaterial(networkMaterial.second, _materialRewrites);
                QVariant materialVariant = scriptable::scriptableMaterialToScriptValue(&_scriptEngine, remappedMaterial).toVariant();
                materialArray.append(QJsonDocument::fromVariant(materialVariant).object());
            }
            json.insert("materials", materialArray);
        }

        QByteArray outputMaterial = QJsonDocument(json).toJson(QJsonDocument::Compact);
        if (_isURL) {
            auto fileName = QUrl(_materialData).fileName();
            QString baseName;
            if (fileName.endsWith(BAKED_MATERIAL_EXTENSION)) {
                baseName = fileName.left(fileName.lastIndexOf(BAKED_MATERIAL_EXTENSION));
            } else {
                baseName = fileName.left(fileName.lastIndexOf('.'));
            }
            auto bakedFilename = baseName + BAKED_MATERIAL_EXTENSION;

            _bakedMaterialData = _bakedOutputDir + "/" + bakedFilename;

            QFile bakedFile;
            bakedFile.setFileName(_bakedMaterialData);
            if (!bakedFile.open(QIODevice::WriteOnly)) {
                handleError("Error opening " + _bakedMaterialData + " for writing");
                return;
            }

            bakedFile.write(outputMaterial);

            // Export successful
            _outputFiles.push_back(_bakedMaterialData);
            qCDebug(material_baking) << "Exported" << _materialData << "to" << _bakedMaterialData;
        } else {
            _bakedMaterialData = QString(outputMaterial);
            qCDebug(material_baking) << "Converted" << _materialData << "to" << _bakedMaterialData;
        }
    }

    // emit signal to indicate the material baking is finished
    emit finished();
}

void MaterialBaker::addTexture(const QString& materialName, image::TextureUsage::Type textureUsage, const hfm::Texture& texture) {
    auto& textureUsageMap = _textureContentMap[materialName.toStdString()];
    if (textureUsageMap.find(textureUsage) == textureUsageMap.end() && !texture.content.isEmpty()) {
        textureUsageMap[textureUsage] = { texture.content, texture.filename };
    }
};

void MaterialBaker::setMaterials(const QHash<QString, hfm::Material>& materials, const QString& baseURL) {
    _materialResource = NetworkMaterialResourcePointer(new NetworkMaterialResource(), [](NetworkMaterialResource* ptr) { ptr->deleteLater(); });
    for (auto& material : materials) {
        _materialResource->parsedMaterials.names.push_back(material.name.toStdString());
        _materialResource->parsedMaterials.networkMaterials[material.name.toStdString()] = std::make_shared<NetworkMaterial>(material, baseURL);

        // Store any embedded texture content
        addTexture(material.name, image::TextureUsage::NORMAL_TEXTURE, material.normalTexture);
        addTexture(material.name, image::TextureUsage::ALBEDO_TEXTURE, material.albedoTexture);
        addTexture(material.name, image::TextureUsage::GLOSS_TEXTURE, material.glossTexture);
        addTexture(material.name, image::TextureUsage::ROUGHNESS_TEXTURE, material.roughnessTexture);
        addTexture(material.name, image::TextureUsage::SPECULAR_TEXTURE, material.specularTexture);
        addTexture(material.name, image::TextureUsage::METALLIC_TEXTURE, material.metallicTexture);
        addTexture(material.name, image::TextureUsage::EMISSIVE_TEXTURE, material.emissiveTexture);
        addTexture(material.name, image::TextureUsage::OCCLUSION_TEXTURE, material.occlusionTexture);
        addTexture(material.name, image::TextureUsage::SCATTERING_TEXTURE, material.scatteringTexture);
        addTexture(material.name, image::TextureUsage::LIGHTMAP_TEXTURE, material.lightmapTexture);
    }
}