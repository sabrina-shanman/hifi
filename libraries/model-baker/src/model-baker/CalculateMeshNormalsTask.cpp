//
//  CalculateMeshNormalsTask.cpp
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/01/22.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "CalculateMeshNormalsTask.h"

#include "ModelMath.h"

void CalculateMeshNormalsTask::run(const baker::BakeContextPointer& context, const Input& input, Output& output) {
    const auto& meshPartsPerMeshIn = input.get0();
    const auto& verticesPerMeshIn = input.get1();
    const auto& normalsPerMeshIn = input.get2();
    auto& normalsPerMeshOut = output;

    size_t meshCount = meshPartsPerMeshIn.size();
    normalsPerMeshOut.reserve(meshCount);
    for (size_t i = 0; i < meshCount; i++) {
        const auto& meshPartsIn = meshPartsPerMeshIn[i];
        const auto& verticesIn = verticesPerMeshIn[i];
        const auto& normalsIn = normalsPerMeshIn[i];
        normalsPerMeshOut.emplace_back();
        auto& normalsOut = normalsPerMeshOut.back();
        // Only calculate normals if this mesh doesn't already have them
        if (!normalsIn.empty()) {
            normalsOut = normalsIn;
        } else {
            normalsOut.resize(verticesIn.size());
            baker::calculateNormals(meshPartsIn,
                [&normalsOut](int normalIndex) /* NormalAccessor */ {
                    return &normalsOut[normalIndex];
                },
                [&verticesIn](int vertexIndex, glm::vec3& outVertex) /* VertexSetter */ {
                    outVertex = verticesIn[vertexIndex];
                }
            );
        }
    }
}
