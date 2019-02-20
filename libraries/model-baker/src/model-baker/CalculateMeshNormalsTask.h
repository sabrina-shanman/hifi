//
//  CalculateMeshNormalsTask.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/01/07.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_CalculateMeshNormalsTask_h
#define hifi_CalculateMeshNormalsTask_h

#include <hfm/HFM.h>

#include "Engine.h"
#include "BakerTypes.h"

using CalculateMeshNormalsConfig = baker::PassthroughConfig;

// Calculate mesh normals if not already present in the mesh
class CalculateMeshNormalsTask {
public:
    using Config = CalculateMeshNormalsConfig;
    using Input = std::vector<hfm::Mesh>;
    using Output = baker::NormalsPerMesh;
    using JobModel = baker::Job::ModelIO<CalculateMeshNormalsTask, Input, Output, Config>;

    void configure(const Config& config);
    void run(const baker::BakeContextPointer& context, const Input& input, Output& output);

protected:
    bool _passthrough { false };
};

#endif // hifi_CalculateMeshNormalsTask_h
