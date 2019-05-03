//
//  SanitizeMeshIndicesTask.cpp
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/05/02.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SanitizeMeshIndicesTask.h"

void SanitizeMeshIndicesTask::run(const baker::BakeContextPointer& context, const Input& input, Output& output) {
    const auto& meshPartsPerMeshIn = input.get0();
    const auto& verticesPerMesh = input.get1();
    auto& meshPartsPerMeshOut = output;

    meshPartsPerMeshOut.reserve(meshPartsPerMeshIn.size());
    for (size_t i = 0; i < meshPartsPerMeshIn.size(); i++) {
        const auto& meshPartsIn = meshPartsPerMeshIn[i];
        const size_t verticesSize = verticesPerMesh[i].size();
        meshPartsPerMeshOut.emplace_back();
        auto& meshPartsOut = meshPartsPerMeshOut.back();

        meshPartsOut.reserve(meshPartsIn.size());
        for (size_t j = 0; j < meshPartsIn.size(); j++) {
            const auto& meshPartIn = meshPartsIn[j];
            meshPartsOut.push_back(meshPartIn);
            auto& meshPartOut = meshPartsOut.back();
            baker::sanitizeIndices(meshPartOut.quadIndices, verticesSize);
            baker::sanitizeIndices(meshPartOut.quadTrianglesIndices, verticesSize);
            baker::sanitizeIndices(meshPartOut.triangleIndices, verticesSize);
        }
    }
}
