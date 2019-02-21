//
//  BuildDracoMeshTask.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/02/20.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_BuildDracoMeshTask_h
#define hifi_BuildDracoMeshTask_h

#include <hfm/HFM.h>
#include <shared/HifiTypes.h>

#include "Engine.h"
#include "BakerTypes.h"

using GetMaterialIDCallback = std::function <int(int)>;

// BuildDracoMeshTask is disabled by default
class BuildDracoMeshConfig : public baker::JobConfig {
    Q_OBJECT
public:
    BuildDracoMeshConfig() : baker::JobConfig(false) {}

    // TODO: Configure FBXBaker to set this
    GetMaterialIDCallback materialIDCallback { nullptr };
};

class BuildDracoMeshTask {
public:
    using Config = BuildDracoMeshConfig;
    // TODO: Tangent support (Needs changes to FBXSerializer_Mesh as well)
    using Input = baker::VaryingSet3<std::vector<hfm::Mesh>, baker::NormalsPerMesh, baker::TangentsPerMesh>;
    using Output = std::vector<hifi::ByteArray>;
    using JobModel = baker::Job::ModelIO<BuildDracoMeshTask, Input, Output, Config>;

    void configure(const Config& config);
    void run(const baker::BakeContextPointer& context, const Input& input, Output& output);

protected:
    GetMaterialIDCallback _materialIDCallback { nullptr };
};

#endif // hifi_BuildDracoMeshTask_h
