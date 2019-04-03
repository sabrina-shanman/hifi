//
//  TextureFileNamer.cpp
//  libraries/baking/src/baking
//
//  Created by Sabrina Shanman on 2019/03/14.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TextureFileNamer.h"

QString TextureFileNamer::createBaseTextureFileName(const QFileInfo& textureFileInfo, const image::TextureUsage::Type textureType) {
    // If two textures have the same URL but are used differently, we need to process them separately
    QString addMapChannel = QString::fromStdString("_" + std::to_string(textureType));

    // Detect if we are working with a rebaked file, and if so, remove the extra stuff we added at the end of the filename the last time we did this
    QString baseTextureFileName;
    QString fileInfoBase = textureFileInfo.baseName();
    int fileNamerSuffix = fileInfoBase.indexOf(QRegExp(addMapChannel + "(-\\d+)?$"));
    if (fileNamerSuffix == -1 || fileNamerSuffix == 0) {
        baseTextureFileName = fileInfoBase + addMapChannel;
    } else {
        baseTextureFileName = fileInfoBase.left(fileNamerSuffix) + addMapChannel;
    }

    // make sure we have a unique base name for this texture
    // in case another texture referenced by this model has the same base name
    auto& nameMatches = _textureNameMatchCount[baseTextureFileName];

    if (nameMatches > 0) {
        // there are already nameMatches texture with this name
        // append - and that number to our baked texture file name so that it is unique
        baseTextureFileName += "-" + QString::number(nameMatches);
    }

    // increment the number of name matches
    ++nameMatches;

    return baseTextureFileName;
}
