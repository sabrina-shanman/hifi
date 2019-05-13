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

#include "CalculateBlendshapeNormalsTask.h"

#include "ModelMath.h"

void CalculateBlendshapeNormalsTask::run(const baker::BakeContextPointer& context, const Input& input, Output& output) {
    const auto& verticesPerMesh = input.get0();
    const auto& meshPartsPerMesh = input.get1();
    const auto& indicesPerBlendshapePerMesh = input.get2();
    const auto& verticesPerBlendshapePerMesh = input.get3();
    const auto& normalsPerBlendshapePerMeshIn = input.get4();
    auto& normalsPerBlendshapePerMeshOut = output;

    size_t numMeshes = std::min(verticesPerMesh.size(), meshPartsPerMesh.size());
    numMeshes = std::min(numMeshes, indicesPerBlendshapePerMesh.size());
    numMeshes = std::min(numMeshes, verticesPerBlendshapePerMesh.size());
    normalsPerBlendshapePerMeshOut.reserve(numMeshes);
    for (size_t i = 0; i < numMeshes; i++) {
        const auto& meshVertices = verticesPerMesh[i];
        const auto& meshParts = meshPartsPerMesh[i];
        const auto& indicesPerBlendshape = indicesPerBlendshapePerMesh[i];
        const auto& verticesPerBlendshape = verticesPerBlendshapePerMesh[i];
        const auto& normalsPerBlendshapeIn = normalsPerBlendshapePerMeshIn[i];
        normalsPerBlendshapePerMeshOut.emplace_back();
        auto& normalsPerBlendshapeOut = normalsPerBlendshapePerMeshOut.back();

        size_t numBlendshapes = verticesPerBlendshape.size();
        normalsPerBlendshapeOut.reserve(numBlendshapes);
        for (size_t j = 0; j < numBlendshapes; j++) {
            const auto& blendshapeIndices = indicesPerBlendshape[j];
            const auto& blendshapeVertices = verticesPerBlendshape[j];
            const auto& blendshapeNormalsIn = baker::safeGet(normalsPerBlendshapeIn, j);
            // Check if normals are already defined. Otherwise, calculate them from existing blendshape vertices.
            if (!blendshapeNormalsIn.empty()) {
                normalsPerBlendshapeOut.push_back(blendshapeNormalsIn);
            } else {
                // Create lookup to get index in blendshape from vertex index in mesh
                std::vector<int> reverseIndices;
                reverseIndices.resize(blendshapeVertices.size());
                std::iota(reverseIndices.begin(), reverseIndices.end(), 0);
                for (int indexInBlendShape = 0; indexInBlendShape < blendshapeIndices.size(); ++indexInBlendShape) {
                    auto indexInMesh = blendshapeIndices[indexInBlendShape];
                    reverseIndices[indexInMesh] = indexInBlendShape;
                }

                normalsPerBlendshapeOut.emplace_back();
                auto& blendshapeNormals = normalsPerBlendshapeOut[normalsPerBlendshapeOut.size()-1];
                blendshapeNormals.resize(blendshapeVertices.size()); // TODO: Why mesh.vertices.size()? Why not just the blendshape vertices?
                baker::calculateNormals(meshParts,
                    [&reverseIndices, &blendshapeVertices, &blendshapeNormals](int normalIndex) /* NormalAccessor */ {
                        const auto lookupIndex = reverseIndices[normalIndex];
                        if (lookupIndex < blendshapeVertices.size()) {
                            return &blendshapeNormals[lookupIndex];
                        } else {
                            // Index isn't in the blendshape. Request that the normal not be calculated.
                            return (glm::vec3*)nullptr;
                        }
                    },
                    [&reverseIndices, &blendshapeVertices](int vertexIndex, glm::vec3& outVertex) /* VertexSetter */ {
                        const auto lookupIndex = reverseIndices[vertexIndex];
                        if (lookupIndex < blendshapeVertices.size()) {
                            outVertex = blendshapeVertices[lookupIndex];
                        } else {
                            // Index isn't in the blendshape, so return vertex from mesh
                            outVertex = baker::safeGet(mesh.vertices, lookupIndex);
                        }
                    });
            }
        }
    }
}
