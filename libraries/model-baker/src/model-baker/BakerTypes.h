//
//  BakerTypes.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2018/12/10.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_BakerTypes_h
#define hifi_BakerTypes_h

#include <QUrl>
#include <hfm/HFM.h>

namespace baker {
    using MeshPartsPerMesh = std::vector<std::vector<hfm::MeshPart>>;
    using VerticesPerMesh = std::vector<std::vector<glm::vec3>>;
    using NormalsPerMesh = std::vector<std::vector<glm::vec3>>;
    using TangentsPerMesh = std::vector<std::vector<glm::vec3>>;
    using TexCoordsPerMesh = std::vector<std::vector<glm::vec2>>;

    using Blendshapes = std::vector<hfm::Blendshape>;
    using BlendshapesPerMesh = std::vector<std::vector<hfm::Blendshape>>;
    using VerticesPerBlendshape = std::vector<std::vector<glm::vec3>>;
    using NormalsPerBlendshape = std::vector<std::vector<glm::vec3>>;
    using IndicesPerBlendshape = std::vector<std::vector<int>>;
    using TangentsPerBlendshape = std::vector<std::vector<glm::vec3>>;

    using MeshIndicesToModelNames = QHash<int, QString>;
};

#endif // hifi_BakerTypes_h
