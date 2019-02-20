//
//  CalculateBlendshapeTangentsTask.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/01/07.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_CalculateBlendshapeTangentsTask_h
#define hifi_CalculateBlendshapeTangentsTask_h

#include "Engine.h"
#include "BakerTypes.h"

using CalculateBlendshapeTangentsConfig = baker::PassthroughConfig;

// Calculate blendshape tangents if not already present in the blendshape
class CalculateBlendshapeTangentsTask {
public:
    using Config = CalculateBlendshapeTangentsConfig;
    using Input = baker::VaryingSet4<std::vector<baker::NormalsPerBlendshape>, baker::BlendshapesPerMesh, std::vector<hfm::Mesh>, QHash<QString, hfm::Material>>;
    using Output = std::vector<baker::TangentsPerBlendshape>;
    using JobModel = baker::Job::ModelIO<CalculateBlendshapeTangentsTask, Input, Output, Config>;

    void run(const baker::BakeContextPointer& context, const Input& input, Output& output);
    void configure(const Config& config);

protected:
    bool _passthrough { false };
};

#endif // hifi_CalculateBlendshapeTangentsTask_h
