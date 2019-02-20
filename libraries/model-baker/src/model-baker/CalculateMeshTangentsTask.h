//
//  CalculateMeshTangentsTask.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/01/07.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_CalculateMeshTangentsTask_h
#define hifi_CalculateMeshTangentsTask_h

#include <hfm/HFM.h>

#include "Engine.h"
#include "BakerTypes.h"

using CalculateMeshTangentsConfig = baker::PassthroughConfig;

// Calculate mesh tangents if not already present in the mesh
class CalculateMeshTangentsTask {
public:
    using NormalsPerMesh = std::vector<std::vector<glm::vec3>>;

    using Config = CalculateMeshTangentsConfig;
    using Input = baker::VaryingSet3<baker::NormalsPerMesh, std::vector<hfm::Mesh>, QHash<QString, hfm::Material>>;
    using Output = baker::TangentsPerMesh;
    using JobModel = baker::Job::ModelIO<CalculateMeshTangentsTask, Input, Output, Config>;

    void configure(const Config& config);
    void run(const baker::BakeContextPointer& context, const Input& input, Output& output);

protected:
    bool _passthrough { false };
};

#endif // hifi_CalculateMeshTangentsTask_h
