//
//  FSTBaker.cpp
//  libraries/baking/src/baking
//
//  Created by Sabrina Shanman on 2019/03/06.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "FSTBaker.h"

#include <PathUtils.h>
#include <NetworkAccessManager.h>

#include "BakerLibrary.h"

#include <FSTReader.h>

FSTBaker::FSTBaker(const QUrl& inputMappingURL, TextureBakerThreadGetter inputTextureThreadGetter,
        const QString& bakedOutputDirectory, const QString& originalOutputDirectory, bool hasBeenBaked) :
        _originalMappingURL(inputMappingURL),
        ModelBaker(inputMappingURL, inputTextureThreadGetter, bakedOutputDirectory, originalOutputDirectory, hasBeenBaked) {
    if (hasBeenBaked) {
        // Look for the FST file in the oven output directory
        // We don't know if we are rebaking a rebaked FST, rebaking the original FST, or rebaking a model
        // Assume the first one, and if we fail, we will try something else
        QUrl originalRelativePath = QUrl("../original/" + inputMappingURL.fileName());
        QUrl newInputURL = inputMappingURL.adjusted(QUrl::RemoveFilename).resolved(originalRelativePath);
        _modelURL = newInputURL;
    }
    _mappingURL = inputMappingURL;

    {
        // Unused, but defined for consistency
        auto bakedFilename = _modelURL.fileName();
        bakedFilename.replace(FST_EXTENSION, BAKED_FST_EXTENSION);
        _bakedModelURL = _bakedOutputDir + "/" + bakedFilename;
    }
}

QUrl FSTBaker::getFullOutputMappingURL() const {
    if (_modelBaker) {
        return _modelBaker->getFullOutputMappingURL();
    }
    return QUrl();
}

void FSTBaker::handleModelNotFound() {
    if (_hasBeenBaked) {
        if (_modelURL.toString().endsWith(BAKED_FST_EXTENSION)) {
            auto oldModelURL = _modelURL;
            auto newPath = _modelURL.path();
            newPath = newPath.left(newPath.lastIndexOf(BAKED_FST_EXTENSION)) + FST_EXTENSION;
            _modelURL.setPath(newPath);
            _mappingURL = _modelURL;
            qCDebug(model_baking) << "Could not find" << oldModelURL << ", attempting" << _modelURL;
            saveSourceModel();
        } else if (_modelURL.toString().endsWith(FST_EXTENSION)) {
            auto oldModelURL = _modelURL;
            
            // Use the model name in the baked FST file to guess the original model URL
            // TODO: Support non-local files. Also, we really need to start using the ModelCache. -Sabrina 2019/04/05
            if (!_originalMappingURL.isLocalFile()) {
                


            }

            QFile fstFile(_originalMappingURL.toLocalFile());
            if (!fstFile.open(QIODevice::ReadOnly)) {
                handleError("Error opening baked FST " + _originalMappingURL.toString() + " for reading");
                return;
            }

            hifi::ByteArray fstData = fstFile.readAll();
            auto bakedMapping = FSTReader::readMapping(fstData);

            auto filenameField = bakedMapping[FILENAME_FIELD].toString();
            if (filenameField.isEmpty()) {
                handleError("The '" + FILENAME_FIELD + "' property in the baked FST file '" + fstFile.fileName() + "' could not be found");
                return;
            }

            QUrl originalRelativePath = QUrl("../original/" + filenameField);
            _modelURL = _originalMappingURL.adjusted(QUrl::RemoveFilename).resolved(originalRelativePath);
            _mappingURL = "";
            qCDebug(model_baking) << "Could not find" << oldModelURL << ", attempting" << _modelURL;
            saveSourceModel();
        } else if (_modelURL.toString().endsWith(BAKED_FBX_EXTENSION)) {
            auto oldModelURL = _modelURL;
            auto newPath = _modelURL.path();
            newPath = newPath.left(newPath.lastIndexOf(BAKED_FBX_EXTENSION)) + FBX_EXTENSION;
            _modelURL.setPath(newPath);
            _mappingURL = "";
            qCDebug(model_baking) << "Could not find" << oldModelURL << ", attempting" << _modelURL;
            saveSourceModel();
        } else {
            ModelBaker::handleModelNotFound();
        }
    } else {
        ModelBaker::handleModelNotFound();
    }
}

void FSTBaker::bakeSourceCopy() {
    if (shouldStop()) {
        return;
    }

    QFile fstFile(_originalOutputModelPath);
    if (!fstFile.open(QIODevice::ReadOnly)) {
        handleError("Error opening " + _originalOutputModelPath + " for reading");
        return;
    }
    
    hifi::ByteArray fstData = fstFile.readAll();
    _mapping = FSTReader::readMapping(fstData);
    
    // TODO: Handle baked FST

    auto filenameField = _mapping[FILENAME_FIELD].toString();
    if (filenameField.isEmpty()) {
        handleError("The '" + FILENAME_FIELD + "' property in the FST file '" + _originalOutputModelPath + "' could not be found");
        return;
    }
    auto modelURL = _mappingURL.adjusted(QUrl::RemoveFilename).resolved(filenameField);
    auto bakeableModelURL = getBakeableModelURL(modelURL);
    if (bakeableModelURL.isEmpty()) {
        handleError("The '" + FILENAME_FIELD + "' property in the FST file '" + _originalOutputModelPath + "' could not be resolved to a valid bakeable model url");
        return;
    }

    auto baker = getModelBakerWithOutputDirectories(bakeableModelURL, _textureThreadGetter, _bakedOutputDir, _originalOutputDir);
    _modelBaker = std::unique_ptr<ModelBaker>(dynamic_cast<ModelBaker*>(baker.release()));
    if (!_modelBaker) {
        handleError("The model url '" + bakeableModelURL.toString() + "' from the FST file '" + _originalOutputModelPath + "' (property: '" + FILENAME_FIELD + "') could not be used to initialize a valid model baker");
        return;
    }
    if (dynamic_cast<FSTBaker*>(_modelBaker.get())) {
        // Could be interesting, but for now let's just prevent infinite FST loops in the most straightforward way possible
        handleError("The FST file '" + _originalOutputModelPath + "' (property: '" + FILENAME_FIELD + "') references another FST file. FST chaining is not supported.");
        return;
    }
    _modelBaker->setMappingURL(_mappingURL);
    _modelBaker->setMapping(_mapping);
    // Hold on to the old url userinfo/query/fragment data so ModelBaker::getFullOutputMappingURL retains that data from the original model URL
    _modelBaker->setOutputURLSuffix(modelURL);

    connect(_modelBaker.get(), &ModelBaker::aborted, this, &FSTBaker::handleModelBakerAborted);
    connect(_modelBaker.get(), &ModelBaker::finished, this, &FSTBaker::handleModelBakerFinished);

    // FSTBaker can't do much while waiting for the ModelBaker to finish, so start the bake on this thread.
    _modelBaker->bake();
}

void FSTBaker::handleModelBakerEnded() {
    for (auto& warning : _modelBaker->getWarnings()) {
        _warningList.push_back(warning);
    }
    for (auto& error : _modelBaker->getErrors()) {
        _errorList.push_back(error);
    }

    // Get the output files, including but not limited to the FST file and the baked model file
    for (auto& outputFile : _modelBaker->getOutputFiles()) {
        _outputFiles.push_back(outputFile);
    }

}

void FSTBaker::handleModelBakerAborted() {
    handleModelBakerEnded();
    if (!wasAborted()) {
        setWasAborted(true);
    }
}

void FSTBaker::handleModelBakerFinished() {
    handleModelBakerEnded();
    setIsFinished(true);
}

void FSTBaker::abort() {
    ModelBaker::abort();
    if (_modelBaker) {
        _modelBaker->abort();
    }
}
