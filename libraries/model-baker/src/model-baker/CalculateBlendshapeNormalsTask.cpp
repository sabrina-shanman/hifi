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
    const auto& meshPartsPerMesh = input.get0();
    const auto& indicesPerBlendshapePerMesh = input.get1();
    const auto& verticesPerBlendshapePerMesh = input.get2();
    const auto& normalsPerBlendshapePerMeshIn = input.get3();
    auto& normalsPerBlendshapePerMeshOut = output;

    size_t numMeshes = std::min(meshPartsPerMesh.size(), indicesPerBlendshapePerMesh.size(), verticesPerBlendshapePerMesh.size());
    normalsPerBlendshapePerMeshOut.reserve(numMeshes);
    for (size_t i = 0; i < numMeshes; i++) {
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
            const auto& blendshapeNormalsIn = normalsPerBlendshapeIn[j];
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
                auto& normals = normalsPerBlendshapeOut[normalsPerBlendshapeOut.size()-1];
                normals.resize(mesh.vertices.size()); // TODO: Why mesh.vertices.size()? Why not just the blendshape vertices?
                baker::calculateNormals(mesh,
                    [&reverseIndices, &blendshape, &normals](int normalIndex) /* NormalAccessor */ {
                        const auto lookupIndex = reverseIndices[normalIndex];
                        if (lookupIndex < blendshape.vertices.size()) {
                            return &normals[lookupIndex];
                        } else {
                            // Index isn't in the blendshape. Request that the normal not be calculated.
                            return (glm::vec3*)nullptr;
                        }
                    },
                    [&mesh, &reverseIndices, &blendshape](int vertexIndex, glm::vec3& outVertex) /* VertexSetter */ {
                        const auto lookupIndex = reverseIndices[vertexIndex];
                        if (lookupIndex < blendshape.vertices.size()) {
                            outVertex = blendshape.vertices[lookupIndex];
                        } else {
                            // Index isn't in the blendshape, so return vertex from mesh
                            outVertex = baker::safeGet(mesh.vertices, lookupIndex);
                        }
                    });
            }
        }
    }
}
