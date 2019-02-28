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

MaterialBaker::MaterialBaker(const QString& materialData, bool isURL, const QString& bakedOutputDir) :
    _materialData(materialData),
    _isURL(isURL),
    _bakedOutputDir(bakedOutputDir),
    _textureOutputDir(bakedOutputDir + "/materialTextures/" + QString::number(materialNum++))
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
            emit originalMaterialLoaded();
        } else {
            connect(_materialResource.data(), &Resource::finished, this, &MaterialBaker::originalMaterialLoaded);
        }
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
            auto textureMaps = networkMaterial.second->getTextureMaps();
            for (auto textureMap : textureMaps) {
                if (textureMap.second && textureMap.second->getTextureSource()) {
                    graphics::Material::MapChannel mapChannel = textureMap.first;
                    auto texture = textureMap.second->getTextureSource();

                    QUrl url = texture->getUrl();
                    QString cleanURL = url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment).toDisplayString();
                    auto idx = cleanURL.lastIndexOf('.');
                    auto extension = idx >= 0 ? url.toDisplayString().mid(idx + 1).toLower() : "";

                    if (QImageReader::supportedImageFormats().contains(extension.toLatin1())) {
                        QUrl textureURL = url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment);

                        // FIXME: this isn't properly handling bumpMaps or glossMaps
                        static const std::unordered_map<graphics::Material::MapChannel, image::TextureUsage::Type> MAP_CHANNEL_TO_TEXTURE_USAGE_TYPE_MAP {
                            { graphics::Material::MapChannel::EMISSIVE_MAP, image::TextureUsage::EMISSIVE_TEXTURE },
                            { graphics::Material::MapChannel::ALBEDO_MAP, image::TextureUsage::ALBEDO_TEXTURE },
                            { graphics::Material::MapChannel::METALLIC_MAP, image::TextureUsage::METALLIC_TEXTURE },
                            { graphics::Material::MapChannel::ROUGHNESS_MAP, image::TextureUsage::ROUGHNESS_TEXTURE },
                            { graphics::Material::MapChannel::NORMAL_MAP, image::TextureUsage::NORMAL_TEXTURE },
                            { graphics::Material::MapChannel::OCCLUSION_MAP, image::TextureUsage::OCCLUSION_TEXTURE },
                            { graphics::Material::MapChannel::LIGHTMAP_MAP, image::TextureUsage::LIGHTMAP_TEXTURE },
                            { graphics::Material::MapChannel::SCATTERING_MAP, image::TextureUsage::SCATTERING_TEXTURE }
                        };

                        auto it = MAP_CHANNEL_TO_TEXTURE_USAGE_TYPE_MAP.find(mapChannel);
                        if (it == MAP_CHANNEL_TO_TEXTURE_USAGE_TYPE_MAP.end()) {
                            handleError("Unknown map channel");
                            return;
                        }

                        QPair<QUrl, image::TextureUsage::Type> textureKey = { textureURL, it->second };
                        if (!_textureBakers.contains(textureKey)) {
                            QSharedPointer<TextureBaker> textureBaker {
                                new TextureBaker(textureURL, it->second, _textureOutputDir),
                                &TextureBaker::deleteLater
                            };
                            textureBaker->setMapChannel(mapChannel);
                            connect(textureBaker.data(), &TextureBaker::finished, this, &MaterialBaker::handleFinishedTextureBaker);
                            _textureBakers.insert(textureKey, textureBaker);
                            textureBaker->moveToThread(_getNextOvenWorkerThreadOperator ? _getNextOvenWorkerThreadOperator() : thread());
                            QMetaObject::invokeMethod(textureBaker.data(), "bake");
                        }
                        _materialsNeedingRewrite.insert(textureKey, networkMaterial.second);
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

            // Replace the old texture URLs
            for (auto networkMaterial : _materialsNeedingRewrite.values(textureKey)) {
                networkMaterial->getTextureMap(baker->getMapChannel())->getTextureSource()->setUrl(newURL);
            }
        } else {
            // this texture failed to bake - this doesn't fail the entire bake but we need to add the errors from
            // the texture to our warnings
            _warningList << baker->getWarnings();
        }

        _materialsNeedingRewrite.remove(textureKey);
        _textureBakers.remove(textureKey);

        if (_textureBakers.empty()) {
            outputMaterial();
        }
    }
}

void MaterialBaker::outputMaterial() {
    if (_materialResource) {
        QJsonDocument json;
        if (_materialResource->parsedMaterials.networkMaterials.size() == 1) {
            auto networkMaterial = _materialResource->parsedMaterials.networkMaterials.begin();
            auto scriptableMaterial = scriptable::ScriptableMaterial(networkMaterial->second);
            QVariant materialVariant = scriptable::scriptableMaterialToScriptValue(&_scriptEngine, scriptableMaterial).toVariant();
            json = QJsonDocument::fromVariant(materialVariant);
        } else {
            QJsonArray materialArray;
            for (auto networkMaterial : _materialResource->parsedMaterials.networkMaterials) {
                auto scriptableMaterial = scriptable::ScriptableMaterial(networkMaterial.second);
                QVariant materialVariant = scriptable::scriptableMaterialToScriptValue(&_scriptEngine, scriptableMaterial).toVariant();
                materialArray.append(QJsonDocument::fromVariant(materialVariant).object());
            }
            json.setArray(materialArray);
        }

        QByteArray outputMaterial = json.toJson(QJsonDocument::Compact);
        if (_isURL) {
            auto fileName = QUrl(_materialData).fileName();
            auto baseName = fileName.left(fileName.lastIndexOf('.'));
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
