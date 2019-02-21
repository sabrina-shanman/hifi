//
//  BuildDracoMeshTask.cpp
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2019/02/20.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "BuildDracoMeshTask.h"

// Fix build warnings due to draco headers not casting size_t
#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4267 )
#endif

#include <draco/compression/encode.h>
#include <draco/mesh/triangle_soup_mesh_builder.h>

#ifdef _WIN32
#pragma warning( pop )
#endif

#include "ModelBakerLogging.h"
#include "ModelMath.h"

std::unique_ptr<draco::Mesh> createDracoMesh(const hfm::Mesh& mesh, const std::vector<glm::vec3>& normals, const std::vector<glm::vec3>& tangents, GetMaterialIDCallback materialIDCallback) {
    Q_ASSERT(normals.size() == 0 || normals.size() == mesh.vertices.size());
    Q_ASSERT(mesh.colors.size() == 0 || mesh.colors.size() == mesh.vertices.size());
    Q_ASSERT(mesh.texCoords.size() == 0 || mesh.texCoords.size() == mesh.vertices.size());

    int64_t numTriangles{ 0 };
    for (auto& part : mesh.parts) {
        int extraQuadTriangleIndices = part.triangleIndices.size() % 3;
        int extraTriangleIndices = part.triangleIndices.size() % 3;
        if (extraQuadTriangleIndices != 0 || extraTriangleIndices != 0) {
            qCWarning(model_baker) << "Found a mesh part with indices not divisible by three. Some indices will be discarded during Draco mesh creation.";
        }
        numTriangles += (part.quadTrianglesIndices.size() - extraQuadTriangleIndices) / 3;
        numTriangles += (part.triangleIndices.size() - extraTriangleIndices) / 3;
    }

    if (numTriangles == 0) {
        return false;
    }

    draco::TriangleSoupMeshBuilder meshBuilder;

    meshBuilder.Start(numTriangles);

    bool hasNormals{ normals.size() > 0 };
    bool hasColors{ mesh.colors.size() > 0 };
    bool hasTexCoords{ mesh.texCoords.size() > 0 };
    bool hasTexCoords1{ mesh.texCoords1.size() > 0 };
    bool hasPerFaceMaterials = (materialIDCallback) ? (mesh.parts.size() > 1 || materialIDCallback(0) != 0 ) : true;
    // TODO: Make sure the mesh is fully defined in FBXBaker, or these won't work (and a bunch of other stuff probably won't work either) (i.e. don't just get the mesh from FBXSerializer::extractMesh; get the mesh from FBXSerializer::extractHFMModel)
    bool hasSkinning { !mesh.clusterIndices.empty() };
    bool needsOriginalIndices{ hasSkinning };

    int normalsAttributeID { -1 };
    int colorsAttributeID { -1 };
    int texCoordsAttributeID { -1 };
    int texCoords1AttributeID { -1 };
    int faceMaterialAttributeID { -1 };
    int originalIndexAttributeID { -1 };

    const int positionAttributeID = meshBuilder.AddAttribute(draco::GeometryAttribute::POSITION,
        3, draco::DT_FLOAT32);
    if (needsOriginalIndices) {
        originalIndexAttributeID = meshBuilder.AddAttribute(
            (draco::GeometryAttribute::Type)DRACO_ATTRIBUTE_ORIGINAL_INDEX,
            1, draco::DT_INT32);
    }

    if (hasNormals) {
        normalsAttributeID = meshBuilder.AddAttribute(draco::GeometryAttribute::NORMAL,
            3, draco::DT_FLOAT32);
    }
    if (hasColors) {
        colorsAttributeID = meshBuilder.AddAttribute(draco::GeometryAttribute::COLOR,
            3, draco::DT_FLOAT32);
    }
    if (hasTexCoords) {
        texCoordsAttributeID = meshBuilder.AddAttribute(draco::GeometryAttribute::TEX_COORD,
            2, draco::DT_FLOAT32);
    }
    if (hasTexCoords1) {
        texCoords1AttributeID = meshBuilder.AddAttribute(
            (draco::GeometryAttribute::Type)DRACO_ATTRIBUTE_TEX_COORD_1,
            2, draco::DT_FLOAT32);
    }
    if (hasPerFaceMaterials) {
        faceMaterialAttributeID = meshBuilder.AddAttribute(
            (draco::GeometryAttribute::Type)DRACO_ATTRIBUTE_MATERIAL_ID,
            1, draco::DT_UINT16);
    }

    auto partIndex = 0;
    draco::FaceIndex face;
    uint16_t materialID;

    for (auto& part : mesh.parts) {
        materialID = (materialIDCallback) ? materialIDCallback(partIndex) : partIndex;

        auto addFace = [&](const QVector<int>& indices, int index, draco::FaceIndex face) {
            int32_t idx0 = indices[index];
            int32_t idx1 = indices[index + 1];
            int32_t idx2 = indices[index + 2];

            if (hasPerFaceMaterials) {
                meshBuilder.SetPerFaceAttributeValueForFace(faceMaterialAttributeID, face, &materialID);
            }

            meshBuilder.SetAttributeValuesForFace(positionAttributeID, face,
                &mesh.vertices[idx0], &mesh.vertices[idx1],
                &mesh.vertices[idx2]);

            if (needsOriginalIndices) {
                meshBuilder.SetAttributeValuesForFace(originalIndexAttributeID, face,
                    &mesh.originalIndices[idx0],
                    &mesh.originalIndices[idx1],
                    &mesh.originalIndices[idx2]);
            }
            if (hasNormals) {
                meshBuilder.SetAttributeValuesForFace(normalsAttributeID, face,
                    &normals[idx0], &normals[idx1],
                    &normals[idx2]);
            }
            if (hasColors) {
                meshBuilder.SetAttributeValuesForFace(colorsAttributeID, face,
                    &mesh.colors[idx0], &mesh.colors[idx1],
                    &mesh.colors[idx2]);
            }
            if (hasTexCoords) {
                meshBuilder.SetAttributeValuesForFace(texCoordsAttributeID, face,
                    &mesh.texCoords[idx0], &mesh.texCoords[idx1],
                    &mesh.texCoords[idx2]);
            }
            if (hasTexCoords1) {
                meshBuilder.SetAttributeValuesForFace(texCoords1AttributeID, face,
                    &mesh.texCoords1[idx0], &mesh.texCoords1[idx1],
                    &mesh.texCoords1[idx2]);
            }
        };

        for (int i = 0; (i + 2) < part.quadTrianglesIndices.size(); i += 3) {
            addFace(part.quadTrianglesIndices, i, face++);
        }

        for (int i = 0; (i + 2) < part.triangleIndices.size(); i += 3) {
            addFace(part.triangleIndices, i, face++);
        }

        partIndex++;
    }

    auto dracoMesh = meshBuilder.Finalize();

    if (!dracoMesh) {
        qCWarning(model_baker) << "Failed to finalize the baking of a draco Geometry node";
        return std::unique_ptr<draco::Mesh>();
    }

    // we need to modify unique attribute IDs for custom attributes
    // so the attributes are easily retrievable on the other side
    if (hasPerFaceMaterials) {
        dracoMesh->attribute(faceMaterialAttributeID)->set_unique_id(DRACO_ATTRIBUTE_MATERIAL_ID);
    }

    if (hasTexCoords1) {
        dracoMesh->attribute(texCoords1AttributeID)->set_unique_id(DRACO_ATTRIBUTE_TEX_COORD_1);
    }

    if (needsOriginalIndices) {
        dracoMesh->attribute(originalIndexAttributeID)->set_unique_id(DRACO_ATTRIBUTE_ORIGINAL_INDEX);
    }
    
    return dracoMesh;
}

void BuildDracoMeshTask::configure(const Config& config) {
    _materialIDCallback = config.materialIDCallback;
}

void BuildDracoMeshTask::run(const baker::BakeContextPointer& context, const Input& input, Output& output) {
    const auto& meshes = input.get0();
    const auto& normalsPerMesh = input.get1();
    const auto& tangentsPerMesh = input.get2();
    auto& dracoBytesPerMesh = output;

    dracoBytesPerMesh.reserve(meshes.size());
    for (size_t i = 0; i < meshes.size(); i++) {
        const auto& mesh = meshes[i];
        const auto& normals = baker::safeGet(normalsPerMesh, i);
        const auto& tangents = baker::safeGet(tangentsPerMesh, i);
        dracoBytesPerMesh.emplace_back();
        auto& dracoBytes = dracoBytesPerMesh.back();

        auto dracoMesh = createDracoMesh(mesh, normals, tangents, _materialIDCallback);

        draco::Encoder encoder;

        encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 14);
        encoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD, 12);
        encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, 10);
        encoder.SetSpeedOptions(0, 5);

        draco::EncoderBuffer buffer;
        encoder.EncodeMeshToBuffer(*dracoMesh, &buffer);

        dracoBytes = hifi::ByteArray(buffer.data(), (int)buffer.size());
    }
}
