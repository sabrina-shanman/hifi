//
//  Baker.cpp
//  model-baker/src/model-baker
//
//  Created by Sabrina Shanman on 2018/12/04.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Baker.h"

#include "BakerTypes.h"
#include "ModelMath.h"
#include "BuildGraphicsMeshTask.h"
#include "CalculateMeshNormalsTask.h"
#include "CalculateMeshTangentsTask.h"
#include "CalculateBlendshapeNormalsTask.h"
#include "CalculateBlendshapeTangentsTask.h"
#include "PrepareJointsTask.h"
#include "BuildDracoMeshTask.h"
#include "ParseFlowDataTask.h"
#include "SanitizeMeshIndicesTask.h"

namespace baker {

    class GetModelPartsTask {
    public:
        using Input = hfm::Model::Pointer;
        using Output = VaryingSet6<std::vector<hfm::Mesh>, hifi::URL, MeshIndicesToModelNames, BlendshapesPerMesh, QHash<QString, hfm::Material>, std::vector<hfm::Joint>>;
        using JobModel = Job::ModelIO<GetModelPartsTask, Input, Output>;

        void run(const BakeContextPointer& context, const Input& input, Output& output) {
            const auto& hfmModelIn = input;
            output.edit0() = hfmModelIn->meshes.toStdVector();
            output.edit1() = hfmModelIn->originalURL;
            output.edit2() = hfmModelIn->meshIndicesToModelNames;
            auto& blendshapesPerMesh = output.edit3();
            blendshapesPerMesh.reserve(hfmModelIn->meshes.size());
            for (int i = 0; i < hfmModelIn->meshes.size(); i++) {
                blendshapesPerMesh.push_back(hfmModelIn->meshes[i].blendshapes.toStdVector());
            }
            output.edit4() = hfmModelIn->materials;
            output.edit5() = hfmModelIn->joints.toStdVector();
        }
    };

    class GetMeshPartsTask {
    public:
        using Input = std::vector<hfm::Mesh>;
        using Output = VaryingSet5<MeshPartsPerMesh, VerticesPerMesh, NormalsPerMesh, TangentsPerMesh, TexCoordsPerMesh>;
        using JobModel = Job::ModelIO<GetMeshPartsTask, Input, Output>;

        void run(const BakeContextPointer& context, const Input& input, Output& output) {
            const auto& meshes = input;
            auto& meshPartsPerMesh = output.edit0();
            auto& verticesPerMesh = output.edit1();
            auto& normalsPerMesh = output.edit2();
            auto& tangentsPerMesh = output.edit3();
            auto& texCoordsPerMesh = output.edit4();

            meshPartsPerMesh.reserve(meshes.size());
            verticesPerMesh.reserve(meshes.size());
            normalsPerMesh.reserve(meshes.size());
            tangentsPerMesh.reserve(meshes.size());
            texCoordsPerMesh.reserve(meshes.size());
            for (const auto& mesh : meshes) {
                meshPartsPerMesh.push_back(mesh.parts.toStdVector());
                verticesPerMesh.push_back(mesh.vertices.toStdVector());
                normalsPerMesh.push_back(mesh.normals.toStdVector());
                tangentsPerMesh.push_back(mesh.tangents.toStdVector());
                texCoordsPerMesh.push_back(mesh.texCoords.toStdVector());
            }
        }
    };

    // TODO: Use this
    class GetBlendshapePartsTask {
        using Input = BlendshapesPerMesh;
        using Output = VaryingSet4<std::vector<IndicesPerBlendshape>, std::vector<VerticesPerBlendshape>, std::vector<NormalsPerBlendshape>, std::vector<TangentsPerBlendshape>>;
        using JobModel = Job::ModelIO<GetBlendshapePartsTask, Input, Output>;

        void run(const BakeContextPointer& context, const Input& input, Output& output) {
            const auto& blendshapesPerMesh = input;
            auto& indicesPerBlendshapePerMesh = output.edit0();
            auto& verticesPerBlendshapePerMesh = output.edit1();
            auto& normalsPerBlendshapePerMesh = output.edit2();
            auto& tangentsPerBlendshapePerMesh = output.edit3();

            for (size_t i = 0; i < blendshapesPerMesh.size(); i++) {
                const auto& blendshapes = blendshapesPerMesh[i];
                indicesPerBlendshapePerMesh.emplace_back();
                auto& indicesPerBlendshape = indicesPerBlendshapePerMesh.back();
                verticesPerBlendshapePerMesh.emplace_back();
                auto& verticesPerBlendshape = verticesPerBlendshapePerMesh.back();
                normalsPerBlendshapePerMesh.emplace_back();
                auto& normalsPerBlendshape = normalsPerBlendshapePerMesh.back();
                tangentsPerBlendshapePerMesh.emplace_back();
                auto& tangentsPerBlendshape = tangentsPerBlendshapePerMesh.back();

                indicesPerBlendshape.reserve(blendshapes.size());
                verticesPerBlendshape.reserve(blendshapes.size());
                normalsPerBlendshape.reserve(blendshapes.size());
                tangentsPerBlendshape.reserve(blendshapes.size());
                for (const auto& blendshape : blendshapes) {
                    indicesPerBlendshape.push_back(blendshape.indices.toStdVector());
                    verticesPerBlendshape.push_back(blendshape.vertices.toStdVector());
                    normalsPerBlendshape.push_back(blendshape.normals.toStdVector());
                    tangentsPerBlendshape.push_back(blendshape.tangents.toStdVector());
                }
            }
        }
    };

    class BuildBlendshapesTask {
    public:
        using Input = VaryingSet4<std::vector<IndicesPerBlendshape>, std::vector<VerticesPerBlendshape>, std::vector<NormalsPerBlendshape>, std::vector<TangentsPerBlendshape>>;
        using Output = BlendshapesPerMesh;
        using JobModel = Job::ModelIO<BuildBlendshapesTask, Input, Output>;

        void run(const BakeContextPointer& context, const Input& input, Output& output) {
            const auto& indicesPerBlendshapePerMesh = input.get0();
            const auto& verticesPerBlendshapePerMesh = input.get1();
            const auto& normalsPerBlendshapePerMesh = input.get2();
            const auto& tangentsPerBlendshapePerMesh = input.get3();
            auto& blendshapesPerMeshOut = output;

            size_t numMeshes = indicesPerBlendshapePerMesh.size();
            numMeshes = std::min(numMeshes, verticesPerBlendshapePerMesh.size());
            numMeshes = std::min(numMeshes, normalsPerBlendshapePerMesh.size());
            numMeshes = std::min(numMeshes, tangentsPerBlendshapePerMesh.size());

            blendshapesPerMeshOut.resize(numMeshes);
            for (size_t i = 0; i < numMeshes; i++) {
                const auto& indicesPerBlendshape = indicesPerBlendshapePerMesh[i];
                const auto& verticesPerBlendshape = verticesPerBlendshapePerMesh[i];
                const auto& normalsPerBlendshape = normalsPerBlendshapePerMesh[i];
                const auto& tangentsPerBlendshape = tangentsPerBlendshapePerMesh[i];
                auto& blendshapesOut = blendshapesPerMeshOut[i];
                
                size_t numBlendshapes = indicesPerBlendshape.size();
                numBlendshapes = std::min(numBlendshapes, verticesPerBlendshape.size());
                numBlendshapes = std::min(numBlendshapes, normalsPerBlendshape.size());
                numBlendshapes = std::min(numBlendshapes, tangentsPerBlendshape.size());

                blendshapesOut.resize(numBlendshapes);
                for (size_t j = 0; j < numBlendshapes; j++) {
                    const auto& indices = indicesPerBlendshape[j];
                    const auto& vertices = verticesPerBlendshape[j];
                    const auto& normals = normalsPerBlendshape[j];
                    const auto& tangents = tangentsPerBlendshape[j];
                    auto& blendshape = blendshapesOut[j];

                    blendshape.indices = QVector<int>::fromStdVector(indices);
                    blendshape.vertices = QVector<glm::vec3>::fromStdVector(vertices);
                    blendshape.normals = QVector<glm::vec3>::fromStdVector(normals);
                    blendshape.tangents = QVector<glm::vec3>::fromStdVector(tangents);
                }
            }
        }
    };

    class BuildMeshesTask {
    public:
        using Input = VaryingSet6<std::vector<hfm::Mesh>, MeshPartsPerMesh, std::vector<graphics::MeshPointer>, NormalsPerMesh, TangentsPerMesh, BlendshapesPerMesh>;
        using Output = std::vector<hfm::Mesh>;
        using JobModel = Job::ModelIO<BuildMeshesTask, Input, Output>;

        void run(const BakeContextPointer& context, const Input& input, Output& output) {
            auto& meshesIn = input.get0();
            int numMeshes = (int)meshesIn.size();
            auto& meshPartsIn = input.get1();
            auto& graphicsMeshesIn = input.get2();
            auto& normalsPerMeshIn = input.get3();
            auto& tangentsPerMeshIn = input.get4();
            auto& blendshapesPerMeshIn = input.get5();

            auto meshesOut = meshesIn;
            for (int i = 0; i < numMeshes; i++) {
                auto& meshOut = meshesOut[i];
                meshOut.parts = QVector<hfm::MeshPart>::fromStdVector(safeGet(meshPartsIn, i));
                meshOut._mesh = safeGet(graphicsMeshesIn, i);
                meshOut.normals = QVector<glm::vec3>::fromStdVector(safeGet(normalsPerMeshIn, i));
                meshOut.tangents = QVector<glm::vec3>::fromStdVector(safeGet(tangentsPerMeshIn, i));
                meshOut.blendshapes = QVector<hfm::Blendshape>::fromStdVector(safeGet(blendshapesPerMeshIn, i));
            }
            output = meshesOut;
        }
    };

    class BuildModelTask {
    public:
        using Input = VaryingSet6<hfm::Model::Pointer, std::vector<hfm::Mesh>, std::vector<hfm::Joint>, QMap<int, glm::quat>, QHash<QString, int>, FlowData>;
        using Output = hfm::Model::Pointer;
        using JobModel = Job::ModelIO<BuildModelTask, Input, Output>;

        void run(const BakeContextPointer& context, const Input& input, Output& output) {
            auto hfmModelOut = input.get0();
            hfmModelOut->meshes = QVector<hfm::Mesh>::fromStdVector(input.get1());
            hfmModelOut->joints = QVector<hfm::Joint>::fromStdVector(input.get2());
            hfmModelOut->jointRotationOffsets = input.get3();
            hfmModelOut->jointIndices = input.get4();
            hfmModelOut->flowData = input.get5();
            hfmModelOut->computeKdops();
            output = hfmModelOut;
        }
    };

    class BakerEngineBuilder {
    public:
        using Input = VaryingSet3<hfm::Model::Pointer, hifi::VariantHash, hifi::URL>;
        using Output = VaryingSet4<hfm::Model::Pointer, MaterialMapping, std::vector<hifi::ByteArray>, std::vector<std::vector<hifi::ByteArray>>>;
        using JobModel = Task::ModelIO<BakerEngineBuilder, Input, Output>;
        void build(JobModel& model, const Varying& input, Varying& output) {
            const auto& hfmModelIn = input.getN<Input>(0);
            const auto& mapping = input.getN<Input>(1);
            const auto& materialMappingBaseURL = input.getN<Input>(2);

            // Split up the inputs from hfm::Model
            const auto modelPartsIn = model.addJob<GetModelPartsTask>("GetModelParts", hfmModelIn);
            const auto meshesIn = modelPartsIn.getN<GetModelPartsTask::Output>(0);
            const auto url = modelPartsIn.getN<GetModelPartsTask::Output>(1);
            const auto meshIndicesToModelNames = modelPartsIn.getN<GetModelPartsTask::Output>(2);
            const auto blendshapesPerMeshIn = modelPartsIn.getN<GetModelPartsTask::Output>(3);
            const auto materials = modelPartsIn.getN<GetModelPartsTask::Output>(4);
            const auto jointsIn = modelPartsIn.getN<GetModelPartsTask::Output>(5);
            const auto getMeshPartsOutputs = model.addJob<GetMeshPartsTask>("GetMeshParts", meshesIn);
            const auto meshPartsPerMeshInRaw = getMeshPartsOutputs.getN<GetMeshPartsTask::Output>(0);
            const auto verticesPerMeshIn = getMeshPartsOutputs.getN<GetMeshPartsTask::Output>(1);
            const auto normalsPerMeshIn = getMeshPartsOutputs.getN<GetMeshPartsTask::Output>(2);
            const auto tangentsPerMeshIn = getMeshPartsOutputs.getN<GetMeshPartsTask::Output>(3);
            const auto texCoordsPerMeshIn = getMeshPartsOutputs.getN<GetMeshPartsTask::Output>(4);

            // Do checks on the mesh indices now, so later processing steps can have fewer validation steps
            const auto sanitizeMeshIndicesInputs = SanitizeMeshIndicesTask::Input(meshPartsPerMeshInRaw, verticesPerMeshIn);
            const auto meshPartsPerMeshIn = model.addJob<SanitizeMeshIndicesTask>("SanitizeMeshIndices", sanitizeMeshIndicesInputs);

            // Calculate normals and tangents for meshes and blendshapes if they do not exist
            // Note: Normals are never calculated here for OBJ models. OBJ files optionally define normals on a per-face basis, so for consistency normals are calculated beforehand in OBJSerializer.
            const auto calculateMeshNormalsInputs = CalculateMeshNormalsTask::Input(meshPartsPerMeshIn, verticesPerMeshIn, normalsPerMeshIn);
            const auto normalsPerMeshOut = model.addJob<CalculateMeshNormalsTask>("CalculateMeshNormals", calculateMeshNormalsInputs);
            const auto calculateMeshTangentsInputs = CalculateMeshTangentsTask::Input(meshPartsPerMeshIn, verticesPerMeshIn, normalsPerMeshOut, tangentsPerMeshIn, texCoordsPerMeshIn, materials).asVarying();
            const auto tangentsPerMesh = model.addJob<CalculateMeshTangentsTask>("CalculateMeshTangents", calculateMeshTangentsInputs);
            const auto calculateBlendshapeNormalsInputs = CalculateBlendshapeNormalsTask::Input(blendshapesPerMeshIn, meshesIn).asVarying();
            const auto normalsPerBlendshapePerMesh = model.addJob<CalculateBlendshapeNormalsTask>("CalculateBlendshapeNormals", calculateBlendshapeNormalsInputs);
            const auto calculateBlendshapeTangentsInputs = CalculateBlendshapeTangentsTask::Input(normalsPerBlendshapePerMesh, blendshapesPerMeshIn, meshesIn, materials).asVarying();
            const auto tangentsPerBlendshapePerMesh = model.addJob<CalculateBlendshapeTangentsTask>("CalculateBlendshapeTangents", calculateBlendshapeTangentsInputs);

            // Build the graphics::MeshPointer for each hfm::Mesh
            const auto buildGraphicsMeshInputs = BuildGraphicsMeshTask::Input(meshesIn, url, meshIndicesToModelNames, normalsPerMeshOut, tangentsPerMesh).asVarying();
            const auto graphicsMeshes = model.addJob<BuildGraphicsMeshTask>("BuildGraphicsMesh", buildGraphicsMeshInputs);

            // Prepare joint information
            const auto prepareJointsInputs = PrepareJointsTask::Input(jointsIn, mapping).asVarying();
            const auto jointInfoOut = model.addJob<PrepareJointsTask>("PrepareJoints", prepareJointsInputs);
            const auto jointsOut = jointInfoOut.getN<PrepareJointsTask::Output>(0);
            const auto jointRotationOffsets = jointInfoOut.getN<PrepareJointsTask::Output>(1);
            const auto jointIndices = jointInfoOut.getN<PrepareJointsTask::Output>(2);

            // Parse material mapping
            const auto parseMaterialMappingInputs = ParseMaterialMappingTask::Input(mapping, materialMappingBaseURL).asVarying();
            const auto materialMapping = model.addJob<ParseMaterialMappingTask>("ParseMaterialMapping", parseMaterialMappingInputs);

            // Build Draco meshes
            // NOTE: This task is disabled by default and must be enabled through configuration
            // TODO: Tangent support (Needs changes to FBXSerializer_Mesh as well)
            // NOTE: Due to an unresolved linker error, BuildDracoMeshTask is not functional on Android
            // TODO: Figure out why BuildDracoMeshTask.cpp won't link with draco on Android
            const auto buildDracoMeshInputs = BuildDracoMeshTask::Input(meshesIn, normalsPerMeshOut, tangentsPerMesh).asVarying();
            const auto buildDracoMeshOutputs = model.addJob<BuildDracoMeshTask>("BuildDracoMesh", buildDracoMeshInputs);
            const auto dracoMeshes = buildDracoMeshOutputs.getN<BuildDracoMeshTask::Output>(0);
            const auto materialList = buildDracoMeshOutputs.getN<BuildDracoMeshTask::Output>(1);

            // Parse flow data
            const auto flowData = model.addJob<ParseFlowDataTask>("ParseFlowData", mapping);

            // Combine the outputs into a new hfm::Model
            const auto buildBlendshapesInputs = BuildBlendshapesTask::Input(blendshapesPerMeshIn, normalsPerBlendshapePerMesh, tangentsPerBlendshapePerMesh).asVarying();
            const auto blendshapesPerMeshOut = model.addJob<BuildBlendshapesTask>("BuildBlendshapes", buildBlendshapesInputs);
            const auto buildMeshesInputs = BuildMeshesTask::Input(meshesIn, graphicsMeshes, normalsPerMeshOut, tangentsPerMesh, blendshapesPerMeshOut).asVarying();
            const auto meshesOut = model.addJob<BuildMeshesTask>("BuildMeshes", buildMeshesInputs);
            const auto buildModelInputs = BuildModelTask::Input(hfmModelIn, meshesOut, jointsOut, jointRotationOffsets, jointIndices, flowData).asVarying();
            const auto hfmModelOut = model.addJob<BuildModelTask>("BuildModel", buildModelInputs);

            output = Output(hfmModelOut, materialMapping, dracoMeshes, materialList);
        }
    };

    Baker::Baker(const hfm::Model::Pointer& hfmModel, const hifi::VariantHash& mapping, const hifi::URL& materialMappingBaseURL) :
        _engine(std::make_shared<Engine>(BakerEngineBuilder::JobModel::create("Baker"), std::make_shared<BakeContext>())) {
        _engine->feedInput<BakerEngineBuilder::Input>(0, hfmModel);
        _engine->feedInput<BakerEngineBuilder::Input>(1, mapping);
        _engine->feedInput<BakerEngineBuilder::Input>(2, materialMappingBaseURL);
    }

    std::shared_ptr<TaskConfig> Baker::getConfiguration() {
        return _engine->getConfiguration();
    }

    void Baker::run() {
        _engine->run();
    }

    hfm::Model::Pointer Baker::getHFMModel() const {
        return _engine->getOutput().get<BakerEngineBuilder::Output>().get0();
    }
    
    MaterialMapping Baker::getMaterialMapping() const {
        return _engine->getOutput().get<BakerEngineBuilder::Output>().get1();
    }

    const std::vector<hifi::ByteArray>& Baker::getDracoMeshes() const {
        return _engine->getOutput().get<BakerEngineBuilder::Output>().get2();
    }

    std::vector<std::vector<hifi::ByteArray>> Baker::getDracoMaterialLists() const {
        return _engine->getOutput().get<BakerEngineBuilder::Output>().get3();
    }
};
