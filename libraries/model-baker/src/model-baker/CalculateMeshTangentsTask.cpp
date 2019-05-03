//
//  CalculateMeshTangentsTask.cpp
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/01/22.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "CalculateMeshTangentsTask.h"

#include "ModelMath.h"

bool needTangents(const std::vector<hfm::MeshPart>& meshParts, const QHash<QString, hfm::Material>& materials) {
    // Check if we actually need to calculate the tangents
    for (const auto& meshPart : meshParts) {
        auto materialIt = materials.find(meshPart.materialID);
        if (materialIt != materials.end() && (*materialIt).needTangentSpace()) {
            return true;
        }
    }
    return false;
}

void CalculateMeshTangentsTask::run(const baker::BakeContextPointer& context, const Input& input, Output& output) {
    const auto& meshPartsPerMesh = input.get0();
    const auto& verticesPerMesh = input.get1();
    const auto& normalsPerMesh = input.get2();
    const auto& tangentsPerMeshIn = input.get3();
    const auto& texCoordsPerMesh = input.get4();
    const auto& materials = input.get5();
    auto& tangentsPerMeshOut = output;

    size_t tangentsSize = meshPartsPerMesh.size();
    tangentsSize = std::min(tangentsSize, verticesPerMesh.size());

    tangentsPerMeshOut.reserve(tangentsSize);
    for (size_t i = 0; i < tangentsSize; i++) {
        const auto& meshParts = meshPartsPerMesh[i];
        const auto& vertices = verticesPerMesh[i];
        const auto& normals = normalsPerMesh[i];
        const auto& tangentsIn = tangentsPerMeshIn[i];
        const auto& texCoords = texCoordsPerMesh[i];
        tangentsPerMeshOut.emplace_back();
        auto& tangentsOut = tangentsPerMeshOut.back();

        // Check if we already have tangents and therefore do not need to do any calculation
        // Otherwise confirm if we have the normals needed, and need to calculate the tangents
        if (!tangentsIn.empty()) {
            tangentsOut = tangentsIn;
        } else if (!normals.empty() && needTangents(meshParts, materials)) {
            tangentsOut.resize(normals.size());
            baker::calculateTangents(meshParts,
            [&vertices, &normals, &texCoords, &tangentsOut](int firstIndex, int secondIndex, glm::vec3* outVertices, glm::vec2* outTexCoords, glm::vec3& outNormal) {
                outVertices[0] = vertices[firstIndex];
                outVertices[1] = vertices[secondIndex];
                outNormal = normals[firstIndex];
                outTexCoords[0] = texCoords[firstIndex];
                outTexCoords[1] = texCoords[secondIndex];
                return &(tangentsOut[firstIndex]);
            });
        }
    }
}
