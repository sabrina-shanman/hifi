//
//  PrepareJointsTask.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/01/25.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PrepareJointsTask_h
#define hifi_PrepareJointsTask_h

#include <QHash>

#include <hfm/HFM.h>

#include "Engine.h"

using PrepareJointsConfig = baker::PassthroughConfig;

class PrepareJointsTask {
public:
    using Config = PrepareJointsConfig;
    using Input = baker::VaryingSet2<std::vector<hfm::Joint>, QVariantHash /*mapping*/>;
    using Output = baker::VaryingSet3<std::vector<hfm::Joint>, QMap<int, glm::quat> /*jointRotationOffsets*/, QHash<QString, int> /*jointIndices*/>;
    using JobModel = baker::Job::ModelIO<PrepareJointsTask, Input, Output, Config>;

    void configure(const Config& config);
    void run(const baker::BakeContextPointer& context, const Input& input, Output& output);

protected:
    bool _passthrough { false };
};

#endif // hifi_PrepareJointsTask_h