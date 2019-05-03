//
//  SanitizeMeshIndicesTask.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/05/02.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_SanitizeMeshIndicesTask_h
#define hifi_SanitizeMeshIndicesTask_h

#include "Baker.h"
#include "BakerTypes.h"
#include "ModelMath.h"

// TODO: Use this
class SanitizeMeshIndicesTask {
public:
    using Input = baker::VaryingSet2<std::vector<std::vector<hfm::MeshPart>>, baker::VerticesPerMesh>;
    using Output = std::vector<std::vector<hfm::MeshPart>>;
    using JobModel = baker::Job::ModelIO<SanitizeMeshIndicesTask, Input, Output>;

    void run(const baker::BakeContextPointer& context, const Input& input, Output& output);
};

#endif // hifi_SanitizeMeshIndicesTask_h
