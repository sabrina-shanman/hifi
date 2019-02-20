//
//  CalculateBlendshapeNormalsTask.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/01/07.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_CalculateBlendshapeNormalsTask_h
#define hifi_CalculateBlendshapeNormalsTask_h

#include "Engine.h"
#include "BakerTypes.h"

using CalculateBlendshapeNormalsConfig = baker::PassthroughConfig;

// Calculate blendshape normals if not already present in the blendshape
class CalculateBlendshapeNormalsTask {
public:
    using Config = CalculateBlendshapeNormalsConfig;
    using Input = baker::VaryingSet2<baker::BlendshapesPerMesh, std::vector<hfm::Mesh>>;
    using Output = std::vector<baker::NormalsPerBlendshape>;
    using JobModel = baker::Job::ModelIO<CalculateBlendshapeNormalsTask, Input, Output, Config>;

    void configure(const Config& config);
    void run(const baker::BakeContextPointer& context, const Input& input, Output& output);

protected:
    bool _passthrough { false };
};

#endif // hifi_CalculateBlendshapeNormalsTask_h
