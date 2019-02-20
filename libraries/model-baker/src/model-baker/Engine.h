//
//  Engine.h
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2018/12/04.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_baker_Engine_h
#define hifi_baker_Engine_h

#include <task/Task.h>

namespace baker {

    class BakeContext : public task::JobContext {
    public:
        // No context settings yet for model prep
    };
    using BakeContextPointer = std::shared_ptr<BakeContext>;

    Task_DeclareCategoryTimeProfilerClass(BakerTimeProfiler, trace_baker);
    Task_DeclareTypeAliases(BakeContext, BakerTimeProfiler)

    using EnginePointer = std::shared_ptr<Engine>;

    // The property "passthrough", when enabled, will let inputs flow directly to the outputs where possible, unlike the disabled property, which discards all data
    class PassthroughConfig : public baker::JobConfig {
        Q_OBJECT
        Q_PROPERTY(bool passthrough MEMBER passthrough)
    public:
        bool passthrough { false };
    };
};

#endif // hifi_baker_Engine_h
