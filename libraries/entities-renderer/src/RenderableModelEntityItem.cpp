//
//  RenderableModelEntityItem.cpp
//  interface/src
//
//  Created by Brad Hefta-Gaub on 8/6/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderableModelEntityItem.h"

#include <set>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include <QtCore/QJsonDocument>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QUrlQuery>

#include <AbstractViewStateInterface.h>
#include <Model.h>
#include <PerfStat.h>
#include <render/Scene.h>
#include <DependencyManager.h>
#include <AnimationCache.h>
#include <shared/QtHelpers.h>

#include "EntityTreeRenderer.h"
#include "EntitiesRendererLogging.h"


void ModelEntityWrapper::setModel(const ModelPointer& model) {
    withWriteLock([&] {
        if (_model != model) {
            _model = model;
            if (_model) {
                _needsInitialSimulation = true;
            }
        }
    });
}

ModelPointer ModelEntityWrapper::getModel() const {
    return resultWithReadLock<ModelPointer>([&] {
        return _model;
    });
}

bool ModelEntityWrapper::isModelLoaded() const {
    return resultWithReadLock<bool>([&] {
        return _model.operator bool() && _model->isLoaded();
    });
}

EntityItemPointer RenderableModelEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    EntityItemPointer entity(new RenderableModelEntityItem(entityID, properties.getDimensionsInitialized()),
                             [](EntityItem* ptr) { ptr->deleteLater(); });
    
    entity->setProperties(properties);

    return entity;
}

RenderableModelEntityItem::RenderableModelEntityItem(const EntityItemID& entityItemID, bool dimensionsInitialized) :
    ModelEntityWrapper(entityItemID),
    _dimensionsInitialized(dimensionsInitialized) {
    
    
}

RenderableModelEntityItem::~RenderableModelEntityItem() { }

void RenderableModelEntityItem::setUnscaledDimensions(const glm::vec3& value) {
    glm::vec3 newDimensions = glm::max(value, glm::vec3(0.0f)); // can never have negative dimensions
    if (getUnscaledDimensions() != newDimensions) {
        _dimensionsInitialized = true;
        ModelEntityItem::setUnscaledDimensions(value);
    }
}

void RenderableModelEntityItem::doInitialModelSimulation() {
    DETAILED_PROFILE_RANGE(simulation_physics, __FUNCTION__);
    ModelPointer model = getModel();
    if (!model) {
        return;
    }
    // The machinery for updateModelBounds will give existing models the opportunity to fix their
    // translation/rotation/scale/registration.  The first two are straightforward, but the latter two have guards to
    // make sure they don't happen after they've already been set.  Here we reset those guards. This doesn't cause the
    // entity values to change -- it just allows the model to match once it comes in.
    model->setScaleToFit(false, getScaledDimensions());
    model->setSnapModelToRegistrationPoint(false, getRegistrationPoint());

    // now recalculate the bounds and registration
    model->setScaleToFit(true, getScaledDimensions());
    model->setSnapModelToRegistrationPoint(true, getRegistrationPoint());
    model->setRotation(getWorldOrientation());
    model->setTranslation(getWorldPosition());

    glm::vec3 scale = model->getScale();
    model->setUseDualQuaternionSkinning(!isNonUniformScale(scale));

    if (_needsInitialSimulation) {
        model->simulate(0.0f);
        _needsInitialSimulation = false;
    }
}

void RenderableModelEntityItem::autoResizeJointArrays() {
    ModelPointer model = getModel();
    if (model && model->isLoaded() && !_needsInitialSimulation) {
        resizeJointArrays(model->getJointStateCount());
    }
}

bool RenderableModelEntityItem::needsUpdateModelBounds() const {
    DETAILED_PROFILE_RANGE(simulation_physics, __FUNCTION__);
    ModelPointer model = getModel();
    if (!hasModel() || !model) {
        return false;
    }

    if (!_dimensionsInitialized || !model->isActive()) {
        return false;
    }

    if (model->needsReload()) {
        return true;
    }

    if (isAnimatingSomething()) {
        return true;
    }

    if (_needsInitialSimulation || _needsJointSimulation) {
        return true;
    }

    if (model->getScaleToFitDimensions() != getScaledDimensions()) {
        return true;
    }

    if (model->getRegistrationPoint() != getRegistrationPoint()) {
        return true;
    }

    bool success;
    auto transform = getTransform(success);
    if (success) {
        if (model->getTranslation() != transform.getTranslation()) {
            return true;
        }
        if (model->getRotation() != transform.getRotation()) {
            return true;
        }
    }

    return false;
}

void RenderableModelEntityItem::updateModelBounds() {
    DETAILED_PROFILE_RANGE(simulation_physics, "updateModelBounds");

    if (!_dimensionsInitialized || !hasModel()) {
        return;
    }

    ModelPointer model = getModel();
    if (!model || !model->isLoaded()) {
        return;
    }

    bool updateRenderItems = false;
    if (model->needsReload()) {
        model->updateGeometry();
        updateRenderItems = true;
    }

    bool overridingModelTransform = model->isOverridingModelTransformAndOffset();
    if (!overridingModelTransform &&
        (model->getScaleToFitDimensions() != getScaledDimensions() ||
         model->getRegistrationPoint() != getRegistrationPoint() ||
         !model->getIsScaledToFit())) {
        // The machinery for updateModelBounds will give existing models the opportunity to fix their
        // translation/rotation/scale/registration.  The first two are straightforward, but the latter two
        // have guards to make sure they don't happen after they've already been set.  Here we reset those guards.
        // This doesn't cause the entity values to change -- it just allows the model to match once it comes in.
        model->setScaleToFit(false, getScaledDimensions());
        model->setSnapModelToRegistrationPoint(false, getRegistrationPoint());

        // now recalculate the bounds and registration
        model->setScaleToFit(true, getScaledDimensions());
        model->setSnapModelToRegistrationPoint(true, getRegistrationPoint());
        updateRenderItems = true;
        model->scaleToFit();
    }

    bool success;
    auto transform = getTransform(success);
    if (success && (model->getTranslation() != transform.getTranslation() ||
            model->getRotation() != transform.getRotation())) {
        model->setTransformNoUpdateRenderItems(transform);
        updateRenderItems = true;
    }

    if (_needsInitialSimulation || _needsJointSimulation || isAnimatingSomething()) {
        // NOTE: on isAnimatingSomething() we need to call Model::simulate() which calls Rig::updateRig()
        // TODO: there is opportunity to further optimize the isAnimatingSomething() case.
        model->simulate(0.0f);
        _needsInitialSimulation = false;
        _needsJointSimulation = false;
        updateRenderItems = true;
    }

    if (updateRenderItems) {
        glm::vec3 scale = model->getScale();
        model->setUseDualQuaternionSkinning(!isNonUniformScale(scale));
        model->updateRenderItems();
    }
}


EntityItemProperties RenderableModelEntityItem::getProperties(const EntityPropertyFlags& desiredProperties, bool allowEmptyDesiredProperties) const {
    EntityItemProperties properties = ModelEntityItem::getProperties(desiredProperties, allowEmptyDesiredProperties); // get the properties from our base class
    if (_originalTexturesRead) {
        properties.setTextureNames(_originalTextures);
    }

    ModelPointer model = getModel();
    if (model) {
        properties.setRenderInfoVertexCount(model->getRenderInfoVertexCount());
        properties.setRenderInfoTextureCount(model->getRenderInfoTextureCount());
        properties.setRenderInfoTextureSize(model->getRenderInfoTextureSize());
        properties.setRenderInfoDrawCalls(model->getRenderInfoDrawCalls());
        properties.setRenderInfoHasTransparent(model->getRenderInfoHasTransparent());

        if (model->isLoaded()) {
            // TODO: improve naturalDimensions in the future,
            //       for now we've added this hack for setting natural dimensions of models
            Extents meshExtents = model->getHFMModel().getUnscaledMeshExtents();
            properties.setNaturalDimensions(meshExtents.maximum - meshExtents.minimum);
            properties.calculateNaturalPosition(meshExtents.minimum, meshExtents.maximum);
        }
    }



    return properties;
}

bool RenderableModelEntityItem::supportsDetailedIntersection() const {
    return true;
}

bool RenderableModelEntityItem::findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                         OctreeElementPointer& element, float& distance, BoxFace& face,
                         glm::vec3& surfaceNormal, QVariantMap& extraInfo, bool precisionPicking) const {
    auto model = getModel();
    if (!model || !isModelLoaded()) {
        return false;
    }

    return model->findRayIntersectionAgainstSubMeshes(origin, direction, distance,
               face, surfaceNormal, extraInfo, precisionPicking, false);
}

bool RenderableModelEntityItem::findDetailedParabolaIntersection(const glm::vec3& origin, const glm::vec3& velocity,
                        const glm::vec3& acceleration, OctreeElementPointer& element, float& parabolicDistance, BoxFace& face,
                        glm::vec3& surfaceNormal, QVariantMap& extraInfo, bool precisionPicking) const {
    auto model = getModel();
    if (!model || !isModelLoaded()) {
        return false;
    }

    return model->findParabolaIntersectionAgainstSubMeshes(origin, velocity, acceleration, parabolicDistance,
        face, surfaceNormal, extraInfo, precisionPicking, false);
}

void RenderableModelEntityItem::fetchCollisionGeometryResource() {
    _collisionGeometryResource = DependencyManager::get<ModelCache>()->getCollisionModelResource(getCollisionShapeURL());
}

bool RenderableModelEntityItem::unableToLoadCollisionShape() {
    if (!_collisionGeometryResource) {
        fetchCollisionGeometryResource();
    }
    return (_collisionGeometryResource && _collisionGeometryResource->isFailed());
}

void RenderableModelEntityItem::setShapeType(ShapeType type) {
    ModelEntityItem::setShapeType(type);
    auto shapeType = getShapeType();
    if (shapeType == SHAPE_TYPE_COMPOUND || shapeType == SHAPE_TYPE_SIMPLE_COMPOUND) {
        if (!_collisionGeometryResource && !getCollisionShapeURL().isEmpty()) {
            fetchCollisionGeometryResource();
        }
    } else if (_collisionGeometryResource && !getCompoundShapeURL().isEmpty()) {
        // the compoundURL has been set but the shapeType does not agree
        _collisionGeometryResource.reset();
    }
}

void RenderableModelEntityItem::setCompoundShapeURL(const QString& url) {
    auto currentCompoundShapeURL = getCompoundShapeURL();
    ModelEntityItem::setCompoundShapeURL(url);
    if (getCompoundShapeURL() != currentCompoundShapeURL || !getModel()) {
        if (getShapeType() == SHAPE_TYPE_COMPOUND) {
            fetchCollisionGeometryResource();
        }
    }
}

bool RenderableModelEntityItem::isReadyToComputeShape() const {
    ShapeType type = getShapeType();
    auto model = getModel();
    auto shapeType = getShapeType();
    if (shapeType == SHAPE_TYPE_COMPOUND || shapeType == SHAPE_TYPE_SIMPLE_COMPOUND) {
        auto shapeURL = getCollisionShapeURL();

        if (!model || shapeURL.isEmpty()) {
            return false;
        }

        if (model->getURL().isEmpty() || !_dimensionsInitialized) {
            // we need a render geometry with a scale to proceed, so give up.
            return false;
        }

        if (model->isLoaded()) {
            if (!shapeURL.isEmpty() && !_collisionGeometryResource) {
                const_cast<RenderableModelEntityItem*>(this)->fetchCollisionGeometryResource();
            }

            if (_collisionGeometryResource && _collisionGeometryResource->isLoaded()) {
                // we have both URLs AND both geometries AND they are both fully loaded.
                if (_needsInitialSimulation) {
                    // the _model's offset will be wrong until _needsInitialSimulation is false
                    DETAILED_PERFORMANCE_TIMER("_model->simulate");
                    const_cast<RenderableModelEntityItem*>(this)->doInitialModelSimulation();
                }
                return true;
            }
        }

        // the model is still being downloaded.
        return false;
    } else if (type >= SHAPE_TYPE_SIMPLE_HULL && type <= SHAPE_TYPE_STATIC_MESH) {
        return isModelLoaded();
    }
    return true;
}

glm::mat4 getLocalTransformForShape(const hfm::Shape& shape, const hfm::Model& hfmModel, const glm::mat4& shapeInfoPreTransform, const std::vector<glm::mat4>& rigJointTransforms) {
    if (shape.joint != hfm::UNDEFINED_KEY) {
        auto rigJointTransform = rigJointTransforms[shape.joint];
        if (shape.skinDeformer != hfm::UNDEFINED_KEY) {
            const auto& skinDeformer = hfmModel.skinDeformers[shape.skinDeformer];
            glm::mat4 inverseBindMatrix;
            if (!skinDeformer.clusters.empty()) {
                const auto& cluster = skinDeformer.clusters.back();
                inverseBindMatrix = cluster.inverseBindMatrix;
            }
            return shapeInfoPreTransform * rigJointTransform * inverseBindMatrix;
        } else {
            return shapeInfoPreTransform * rigJointTransform;
        }
    } else {
        return shapeInfoPreTransform;
    }
}

void computeShapeInfoForModel(ShapeInfo& shapeInfo, const hfm::Model& hfmModel, const QString& modelURL, const glm::mat4& shapeInfoPreTransform, const std::vector<glm::mat4>& rigJointTransforms) {
    const ShapeType& desiredShapeType = shapeInfo.getType();

    ShapeInfo::TriangleIndices& triangleIndices = shapeInfo.getTriangleIndices();
    triangleIndices.clear();
    ShapeInfo::PointCollection& pointCollection = shapeInfo.getPointCollection();
    pointCollection.clear();

    const bool hasPointListPerShape = (desiredShapeType == SHAPE_TYPE_COMPOUND || desiredShapeType == SHAPE_TYPE_SIMPLE_COMPOUND);
    const bool copyVerticesUsingInputIndices = (desiredShapeType == SHAPE_TYPE_COMPOUND);
    const bool copyInputMeshVertices = (desiredShapeType == SHAPE_TYPE_SIMPLE_HULL || desiredShapeType == SHAPE_TYPE_SIMPLE_COMPOUND || desiredShapeType == SHAPE_TYPE_STATIC_MESH);
    const bool copyMeshIndices = (desiredShapeType == SHAPE_TYPE_STATIC_MESH); // Index offset will be applied if needed
    const bool generateIndicesFromVertices = (desiredShapeType == SHAPE_TYPE_SIMPLE_COMPOUND); // TODO: Do not require indices for SHAPE_TYPE_SIMPLE_COMPOUND, and have a more compact way of distinguishing mesh parts in ShapeInfo.
    const bool hasIndices = copyMeshIndices || generateIndicesFromVertices;
    
    uint32_t pointListCount = 1;
    if (hasPointListPerShape) {
        uint32_t collisionMeshCount = 0;
        uint32_t lastMesh = hfm::UNDEFINED_KEY;
        for (const auto& shape : hfmModel.shapes) {
            if (shape.mesh != lastMesh) {
                lastMesh = shape.mesh;
                ++collisionMeshCount;
            }
        }

        const uint32_t MAX_ALLOWED_MESH_COUNT = 1000;
        if (collisionMeshCount > MAX_ALLOWED_MESH_COUNT) {
            // too many will cause the deadlock timer to throw...
            qWarning() << "model" << modelURL << "has too many collision meshes" << collisionMeshCount << "and will collide as a box.";
            shapeInfo.setParams(SHAPE_TYPE_BOX, shapeInfo.getHalfExtents());
            return;
        }

        pointListCount = collisionMeshCount;
    }
    
    {
        size_t vertexCount = 0;
        uint32_t lastMesh = hfm::UNDEFINED_KEY;
        for (const auto& shape : hfmModel.shapes) {
            if (shape.mesh != lastMesh) {
                lastMesh = shape.mesh;
                const auto& mesh = hfmModel.meshes[shape.mesh];
                const auto& triangleListMesh = mesh.triangleListMesh;
                // Added once per instance per mesh
                vertexCount += triangleListMesh.vertices.size();
            }
        }
        const size_t MAX_VERTICES_PER_STATIC_MESH = 1e6;
        if (vertexCount > MAX_VERTICES_PER_STATIC_MESH) {
            qWarning() << "model" << modelURL << "has too many vertices" << vertexCount << "and will collide as a box.";
            shapeInfo.setParams(SHAPE_TYPE_BOX, shapeInfo.getHalfExtents());
            return;
        }
    }

    pointCollection.reserve(pointListCount);
    if (!hasPointListPerShape) {
        pointCollection.emplace_back();
    }

    uint32_t numHFMShapes = (uint32_t)hfmModel.shapes.size();
    uint32_t lastMesh = hfm::UNDEFINED_KEY;
    uint32_t meshIndexOffset = 0;
    for (uint32_t s = 0; s != numHFMShapes; ++s) {
        const hfm::Shape& shape = hfmModel.shapes[s];
        const auto& mesh = hfmModel.meshes[shape.mesh];
        const auto& triangleListMesh = mesh.triangleListMesh;
        const auto& part = triangleListMesh.parts[shape.meshPart];
        int partStart = part.x;
        int partSize = part.y;
        int partEnd = partStart + partSize;

        glm::mat4 localTransform = getLocalTransformForShape(shape, hfmModel, shapeInfoPreTransform, rigJointTransforms);

        if (hasPointListPerShape) {
            pointCollection.emplace_back();
        }
        ShapeInfo::PointList& pointList = pointCollection.back();

        if (copyVerticesUsingInputIndices) {
            meshIndexOffset = (uint32_t)pointList.size();
            pointCollection.reserve(pointCollection.size() + partSize);
            for (int i = partStart; i < partEnd; ++i) {
                const auto index = triangleListMesh.indices[(size_t)i];
                const glm::vec3 point = triangleListMesh.vertices[index];
                const glm::vec3 transformedPoint = glm::vec3(localTransform * glm::vec4(point, 1.0f));
                pointList.push_back(transformedPoint);
            }
        } else if (copyInputMeshVertices) {
            if (shape.mesh != lastMesh) {
                meshIndexOffset = (uint32_t)pointList.size();
                pointCollection.reserve(pointCollection.size() + triangleListMesh.vertices.size());
                for (const auto& point : triangleListMesh.vertices) {
                    const glm::vec3 transformedPoint = glm::vec3(localTransform * glm::vec4(point, 1.0f));
                    pointList.push_back(transformedPoint);
                }
            }
        }
        const uint32_t pointsAddedLast = (uint32_t)pointList.size() - meshIndexOffset;

        if (hasIndices) {
            if (copyMeshIndices) {
                triangleIndices.reserve(triangleIndices.size() + partSize);
                for (int i = partStart; i < partEnd; ++i) {
                    const auto index = triangleListMesh.indices[(size_t)i];
                    triangleIndices.push_back(index + meshIndexOffset);
                }
            } else if (generateIndicesFromVertices) {
                size_t triangleIndexStart = triangleIndices.size();
                triangleIndices.resize(triangleIndexStart + pointsAddedLast);
                auto indexGenBegin = triangleIndices.begin() + triangleIndexStart;
                auto indexGenEnd = indexGenBegin + pointsAddedLast;
                std::iota(indexGenBegin, indexGenEnd, meshIndexOffset);
            }

            if (hasPointListPerShape) {
                if (s + 1 != numHFMShapes) {
                    const hfm::Shape& nextShape = hfmModel.shapes[s + 1];
                    if (nextShape.mesh != shape.mesh) {
                        triangleIndices.push_back(END_OF_MESH_PART);
                        triangleIndices.push_back(END_OF_MESH);
                    } else if (nextShape.meshPart != shape.meshPart) {
                        triangleIndices.push_back(END_OF_MESH_PART);
                    }
                } else {
                    triangleIndices.push_back(END_OF_MESH_PART);
                    triangleIndices.push_back(END_OF_MESH);
                }
            }
        }

        lastMesh = shape.mesh;
    }
}

void RenderableModelEntityItem::computeShapeInfo(ShapeInfo& shapeInfo) {
    ShapeType type = getShapeType();

    if (type != SHAPE_TYPE_COMPOUND &&
        type != SHAPE_TYPE_SIMPLE_HULL &&
        type != SHAPE_TYPE_SIMPLE_COMPOUND &&
        type != SHAPE_TYPE_STATIC_MESH) {
        EntityItem::computeShapeInfo(shapeInfo);
        return;
    }

    auto model = getModel();
    if (!model || !model->isLoaded()) {
        type = SHAPE_TYPE_NONE;
        EntityItem::computeShapeInfo(shapeInfo);
        return;
    }

    if (type == SHAPE_TYPE_COMPOUND) {
        if (!_collisionGeometryResource || !_collisionGeometryResource->isLoaded()) {
            return;
        }
    }

    updateModelBounds();
    if (type != SHAPE_TYPE_COMPOUND) {
        model->updateGeometry();
    }

    const QString modelURL = (type == SHAPE_TYPE_COMPOUND) ? getCompoundShapeURL() : getModelURL();
    const hfm::Model& hfmModel = (type == SHAPE_TYPE_COMPOUND) ? _collisionGeometryResource->getHFMModel() : model->getHFMModel();
    const glm::vec3 dimensions = getScaledDimensions();
    std::vector<glm::mat4> rigJointTransforms;
    {
        const auto& rig = model->getRig();
        for (uint32_t jointIndex = 0; jointIndex < hfmModel.joints.size(); ++jointIndex) {
            rigJointTransforms.push_back(rig.getJointTransform(jointIndex));
        }
    }

    // scale and shift
    const glm::vec3 extentsSize = hfmModel.meshExtents.size();
    glm::vec3 scaleToFit = dimensions / extentsSize;
    for (int32_t i = 0; i < 3; ++i) {
        if (extentsSize[i] < 1.0e-6f) {
            scaleToFit[i] = 1.0f;
        }
    }
    const glm::mat4 scaleToFitTransform = glm::scale(scaleToFit);
    const glm::mat4 invRegistrationOffset = glm::translate(dimensions * (getRegistrationPoint() - ENTITY_ITEM_DEFAULT_REGISTRATION_POINT));
    const glm::mat4 shapeInfoPreTransform = scaleToFitTransform * invRegistrationOffset;

    // Set the shape type and default half extents. The collision will revert to a box with these dimensions if the geometry is too complicated
    shapeInfo.setParams(shapeInfo.getType(), 0.5f * dimensions);

    // Now, initialize the shapeInfo for the model case, either copying points/indices depending on what ShapeFactory wants, or reverting to a box if there is too much to copy.
    computeShapeInfoForModel(shapeInfo, hfmModel, modelURL, shapeInfoPreTransform, rigJointTransforms);

    if (shapeInfo.getType() != SHAPE_TYPE_BOX) {
        if (type == SHAPE_TYPE_COMPOUND) {
            shapeInfo.setParams(type, dimensions, getCompoundShapeURL());
        } else {
            shapeInfo.setParams(type, 0.5f * dimensions, getModelURL());
        }
        adjustShapeInfoByRegistration(shapeInfo);
    }
}

void RenderableModelEntityItem::setJointMap(std::vector<int> jointMap) {
    if (jointMap.size() > 0) {
        _jointMap = jointMap;
        _jointMapCompleted = true;
        return;
    }

    _jointMapCompleted = false;
};

int RenderableModelEntityItem::avatarJointIndex(int modelJointIndex) {
    int result = -1;
    int mapSize = (int) _jointMap.size();
    if (modelJointIndex >=0 && modelJointIndex < mapSize) {
        result = _jointMap[modelJointIndex];
    }

    return result;
}

bool RenderableModelEntityItem::contains(const glm::vec3& point) const {
    auto model = getModel();
    if (EntityItem::contains(point) && model && _collisionGeometryResource && _collisionGeometryResource->isLoaded()) {
        glm::mat4 worldToHFMMatrix = model->getWorldToHFMMatrix();
        glm::vec3 hfmPoint = worldToHFMMatrix * glm::vec4(point, 1.0f);
        return _collisionGeometryResource->getHFMModel().convexHullContains(hfmPoint);
    }

    return false;
}

bool RenderableModelEntityItem::shouldBePhysical() const {
    auto model = getModel();
    // If we have a model, make sure it hasn't failed to download.
    // If it has, we'll report back that we shouldn't be physical so that physics aren't held waiting for us to be ready.
    ShapeType shapeType = getShapeType();
    if (model) {
        if ((shapeType == SHAPE_TYPE_COMPOUND || shapeType == SHAPE_TYPE_SIMPLE_COMPOUND) && model->didCollisionGeometryRequestFail()) {
            return false;
        } else if (shapeType != SHAPE_TYPE_NONE && model->didVisualGeometryRequestFail()) {
            return false;
        }
    }
    return !isDead() && shapeType != SHAPE_TYPE_NONE && !isLocalEntity() && QUrl(_modelURL).isValid();
}

int RenderableModelEntityItem::getJointParent(int index) const {
    auto model = getModel();
    if (model) {
        return model->getRig().getJointParentIndex(index);
    }
    return -1;
}

glm::quat RenderableModelEntityItem::getAbsoluteJointRotationInObjectFrame(int index) const {
    auto model = getModel();
    if (model) {
        glm::quat result;
        if (model->getAbsoluteJointRotationInRigFrame(index, result)) {
            return result;
        }
    }
    return glm::quat();
}

glm::vec3 RenderableModelEntityItem::getAbsoluteJointTranslationInObjectFrame(int index) const {
    auto model = getModel();
    if (model) {
        glm::vec3 result;
        if (model->getAbsoluteJointTranslationInRigFrame(index, result)) {
            return result;
        }
    }
    return glm::vec3(0.0f);
}

bool RenderableModelEntityItem::setAbsoluteJointRotationInObjectFrame(int index, const glm::quat& rotation) {
    auto model = getModel();
    if (!model) {
        return false;
    }
    const Rig& rig = model->getRig();
    int jointParentIndex = rig.getJointParentIndex(index);
    if (jointParentIndex == -1) {
        return setLocalJointRotation(index, rotation);
    }

    bool success;
    AnimPose jointParentPose;
    success = rig.getAbsoluteJointPoseInRigFrame(jointParentIndex, jointParentPose);
    if (!success) {
        return false;
    }
    AnimPose jointParentInversePose = jointParentPose.inverse();

    AnimPose jointAbsolutePose; // in rig frame
    success = rig.getAbsoluteJointPoseInRigFrame(index, jointAbsolutePose);
    if (!success) {
        return false;
    }
    jointAbsolutePose.rot() = rotation;

    AnimPose jointRelativePose = jointParentInversePose * jointAbsolutePose;
    return setLocalJointRotation(index, jointRelativePose.rot());
}

bool RenderableModelEntityItem::setAbsoluteJointTranslationInObjectFrame(int index, const glm::vec3& translation) {
    auto model = getModel();
    if (!model) {
        return false;
    }
    const Rig& rig = model->getRig();

    int jointParentIndex = rig.getJointParentIndex(index);
    if (jointParentIndex == -1) {
        return setLocalJointTranslation(index, translation);
    }

    bool success;
    AnimPose jointParentPose;
    success = rig.getAbsoluteJointPoseInRigFrame(jointParentIndex, jointParentPose);
    if (!success) {
        return false;
    }
    AnimPose jointParentInversePose = jointParentPose.inverse();

    AnimPose jointAbsolutePose; // in rig frame
    success = rig.getAbsoluteJointPoseInRigFrame(index, jointAbsolutePose);
    if (!success) {
        return false;
    }
    jointAbsolutePose.trans() = translation;

    AnimPose jointRelativePose = jointParentInversePose * jointAbsolutePose;
    return setLocalJointTranslation(index, jointRelativePose.trans());
}

bool RenderableModelEntityItem::getJointMapCompleted() {
    return _jointMapCompleted;
}

glm::quat RenderableModelEntityItem::getLocalJointRotation(int index) const {
    auto model = getModel();
    if (model) {
        glm::quat result;
        if (model->getJointRotation(index, result)) {
            return result;
        }
    }
    return glm::quat();
}

glm::vec3 RenderableModelEntityItem::getLocalJointTranslation(int index) const {
    auto model = getModel();
    if (model) {
        glm::vec3 result;
        if (model->getJointTranslation(index, result)) {
            return result;
        }
    }
    return glm::vec3();
}

void RenderableModelEntityItem::setOverrideTransform(const Transform& transform, const glm::vec3& offset) {
    auto model = getModel();
    if (model) {
        model->overrideModelTransformAndOffset(transform, offset);
    }
}

bool RenderableModelEntityItem::setLocalJointRotation(int index, const glm::quat& rotation) {
    autoResizeJointArrays();
    bool result = false;
    _jointDataLock.withWriteLock([&] {
        _jointRotationsExplicitlySet = true;
        if (index >= 0 && index < _localJointData.size()) {
            auto& jointData = _localJointData[index];
            if (jointData.joint.rotation != rotation) {
                jointData.joint.rotation = rotation;
                jointData.joint.rotationSet = true;
                jointData.rotationDirty = true;
                result = true;
                _needsJointSimulation = true;
            }
        }
    });
    return result;
}

bool RenderableModelEntityItem::setLocalJointTranslation(int index, const glm::vec3& translation) {
    autoResizeJointArrays();
    bool result = false;
    _jointDataLock.withWriteLock([&] {
        _jointTranslationsExplicitlySet = true;
        if (index >= 0 && index < _localJointData.size()) {
            auto& jointData = _localJointData[index];
            if (jointData.joint.translation != translation) {
                jointData.joint.translation = translation;
                jointData.joint.translationSet = true;
                jointData.translationDirty = true;
                result = true;
                _needsJointSimulation = true;
            }
        }
    });
    return result;
}

void RenderableModelEntityItem::setJointRotations(const QVector<glm::quat>& rotations) {
    ModelEntityItem::setJointRotations(rotations);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::setJointRotationsSet(const QVector<bool>& rotationsSet) {
    ModelEntityItem::setJointRotationsSet(rotationsSet);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::setJointTranslations(const QVector<glm::vec3>& translations) {
    ModelEntityItem::setJointTranslations(translations);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::setJointTranslationsSet(const QVector<bool>& translationsSet) {
    ModelEntityItem::setJointTranslationsSet(translationsSet);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::locationChanged(bool tellPhysics, bool tellChildren) {
    DETAILED_PERFORMANCE_TIMER("locationChanged");
    EntityItem::locationChanged(tellPhysics, tellChildren);
    auto model = getModel();
    if (model && model->isLoaded()) {
        model->updateRenderItems();
    }
}

int RenderableModelEntityItem::getJointIndex(const QString& name) const {
    auto model = getModel();
    return (model && model->isActive()) ? model->getRig().indexOfJoint(name) : -1;
}

QStringList RenderableModelEntityItem::getJointNames() const {
    QStringList result;
    auto model = getModel();
    if (model && model->isActive()) {
        const Rig& rig = model->getRig();
        int jointCount = rig.getJointStateCount();
        for (int jointIndex = 0; jointIndex < jointCount; jointIndex++) {
            result << rig.nameOfJoint(jointIndex);
        }
    }
    return result;
}

scriptable::ScriptableModelBase render::entities::ModelEntityRenderer::getScriptableModel() {
    auto model = resultWithReadLock<ModelPointer>([this]{ return _model; });

    if (!model || !model->isLoaded()) {
        return scriptable::ScriptableModelBase();
    }

    auto result = _model->getScriptableModel();
    result.objectID = getEntity()->getID();
    {
        std::lock_guard<std::mutex> lock(_materialsLock);
        result.appendMaterials(_materials);
    }
    return result;
}

bool render::entities::ModelEntityRenderer::canReplaceModelMeshPart(int meshIndex, int partIndex) {
    // TODO: for now this method is just used to indicate that this provider generally supports mesh updates
    auto model = resultWithReadLock<ModelPointer>([this]{ return _model; });
    return model && model->isLoaded();
}

bool render::entities::ModelEntityRenderer::replaceScriptableModelMeshPart(scriptable::ScriptableModelBasePointer newModel, int meshIndex, int partIndex) {
    auto model = resultWithReadLock<ModelPointer>([this]{ return _model; });

    if (!model || !model->isLoaded()) {
        return false;
    }

    return model->replaceScriptableModelMeshPart(newModel, meshIndex, partIndex);
}

void RenderableModelEntityItem::simulateRelayedJoints() {
    ModelPointer model = getModel();
    if (model && model->isLoaded()) {
        copyAnimationJointDataToModel();
        model->simulate(0.0f);
        model->updateRenderItems();
    }
}

void RenderableModelEntityItem::stopModelOverrideIfNoParent() {
    auto model = getModel();
    if (model) {
        bool overriding = model->isOverridingModelTransformAndOffset();
        QUuid parentID = getParentID();
        if (overriding && (!_relayParentJoints || parentID.isNull())) {
            model->stopTransformAndOffsetOverride();
        }
    }
}

void RenderableModelEntityItem::copyAnimationJointDataToModel() {
    auto model = getModel();
    if (!model || !model->isLoaded()) {
        return;
    }

    bool changed { false };
    // relay any inbound joint changes from scripts/animation/network to the model/rig
    _jointDataLock.withWriteLock([&] {
        for (int index = 0; index < _localJointData.size(); ++index) {
            auto& jointData = _localJointData[index];
            if (jointData.rotationDirty) {
                model->setJointRotation(index, true, jointData.joint.rotation, 1.0f);
                jointData.rotationDirty = false;
                changed = true;
            }
            if (jointData.translationDirty) {
                model->setJointTranslation(index, true, jointData.joint.translation, 1.0f);
                jointData.translationDirty = false;
                changed = true;
            }
        }
    });

    if (changed) {
        locationChanged(true, true);
    }
}

bool RenderableModelEntityItem::readyToAnimate() const {
    return resultWithReadLock<bool>([&] {
        float firstFrame = _animationProperties.getFirstFrame();
        return (firstFrame >= 0.0f) && (firstFrame <= _animationProperties.getLastFrame());
    });
}

using namespace render;
using namespace render::entities;

ModelEntityRenderer::ModelEntityRenderer(const EntityItemPointer& entity) : Parent(entity) {

}

void ModelEntityRenderer::setKey(bool didVisualGeometryRequestSucceed) {
    auto builder = ItemKey::Builder().withTypeMeta().withTagBits(getTagMask()).withLayer(getHifiRenderLayer());

    if (_model && _model->isGroupCulled()) {
        builder.withMetaCullGroup();
    }

    if (didVisualGeometryRequestSucceed) {
        _itemKey = builder.build();
    } else {
        _itemKey = builder.withTypeShape().build();
    }
}

ItemKey ModelEntityRenderer::getKey() {
    return _itemKey;
}

uint32_t ModelEntityRenderer::metaFetchMetaSubItems(ItemIDs& subItems) const {
    if (_model) {
        auto metaSubItems = _model->fetchRenderItemIDs();
        subItems.insert(subItems.end(), metaSubItems.begin(), metaSubItems.end());
        return (uint32_t)metaSubItems.size();
    }
    return 0;
}

void ModelEntityRenderer::removeFromScene(const ScenePointer& scene, Transaction& transaction) {
    if (_model) {
        _model->removeFromScene(scene, transaction);
    }
    Parent::removeFromScene(scene, transaction);
}

void ModelEntityRenderer::onRemoveFromSceneTyped(const TypedEntityPointer& entity) {
    entity->setModel({});
}

void ModelEntityRenderer::animate(const TypedEntityPointer& entity) {
    if (!_animation || !_animation->isLoaded()) {
        return;
    }

    QVector<EntityJointData> jointsData;

    const QVector<HFMAnimationFrame>&  frames = _animation->getFramesReference(); // NOTE: getFrames() is too heavy
    int frameCount = frames.size();
    if (frameCount <= 0) {
        return;
    }

    {
        float currentFrame = fmod(entity->getAnimationCurrentFrame(), (float)(frameCount));
        if (currentFrame < 0.0f) {
            currentFrame += (float)frameCount;
        }
        int currentIntegerFrame = (int)(glm::floor(currentFrame));
        if (currentIntegerFrame == _lastKnownCurrentFrame) {
            return;
        }
        _lastKnownCurrentFrame = currentIntegerFrame;
    }

    if (_jointMapping.size() != _model->getJointStateCount()) {
        qCWarning(entitiesrenderer) << "RenderableModelEntityItem::getAnimationFrame -- joint count mismatch"
                    << _jointMapping.size() << _model->getJointStateCount();
        return;
    }

    QStringList animationJointNames = _animation->getHFMModel().getJointNames();
    auto& hfmJoints = _animation->getHFMModel().joints;

    auto& originalHFMJoints = _model->getHFMModel().joints;
    auto& originalHFMIndices = _model->getHFMModel().jointIndices;

    bool allowTranslation = entity->getAnimationAllowTranslation();

    const QVector<glm::quat>& rotations = frames[_lastKnownCurrentFrame].rotations;
    const QVector<glm::vec3>& translations = frames[_lastKnownCurrentFrame].translations;

    jointsData.resize(_jointMapping.size());
    for (int j = 0; j < _jointMapping.size(); j++) {
        int index = _jointMapping[j];

        if (index >= 0) {
            glm::mat4 translationMat;

            if (allowTranslation) {
                if (index < translations.size()) {
                    translationMat = glm::translate(translations[index]);
                }
            } else if (index < animationJointNames.size()) {
                QString jointName = hfmJoints[index].name; // Pushing this here so its not done on every entity, with the exceptions of those allowing for translation
                if (originalHFMIndices.contains(jointName)) {
                    // Making sure the joint names exist in the original model the animation is trying to apply onto. If they do, then remap and get it's translation.
                    int remappedIndex = originalHFMIndices[jointName] - 1; // JointIndeces seem to always start from 1 and the found index is always 1 higher than actual.
                    translationMat = glm::translate(originalHFMJoints[remappedIndex].translation);
                }
            }
            glm::mat4 rotationMat;
            if (index < rotations.size()) {
                rotationMat = glm::mat4_cast(hfmJoints[index].preRotation * rotations[index] * hfmJoints[index].postRotation);
            } else {
                rotationMat = glm::mat4_cast(hfmJoints[index].preRotation * hfmJoints[index].postRotation);
            }

            glm::mat4 finalMat = (translationMat * hfmJoints[index].preTransform *
                rotationMat * hfmJoints[index].postTransform);
            auto& jointData = jointsData[j];
            jointData.translation = extractTranslation(finalMat);
            jointData.translationSet = true;
            jointData.rotation = glmExtractRotation(finalMat);
            jointData.rotationSet = true;
        }
    }
    // Set the data in the entity
    entity->setAnimationJointsData(jointsData);

    entity->copyAnimationJointDataToModel();
}

bool ModelEntityRenderer::needsRenderUpdate() const {
    if (resultWithReadLock<bool>([&] {
        if (_moving || _animating) {
            return true;
        }

        if (!_texturesLoaded) {
            return true;
        }

        if (!_prevModelLoaded) {
            return true;
        }

        return false;
    })) {
        return true;
    }

    ModelPointer model;
    QUrl parsedModelURL;
    withReadLock([&] {
        model = _model;
        parsedModelURL = _parsedModelURL;
    });

    if (model) {
        // When the individual mesh parts of a model finish fading, they will mark their Model as needing updating
        // we will watch for that and ask the model to update it's render items
        if (parsedModelURL != model->getURL()) {
            return true;
        }

        if (model->needsReload()) {
            return true;
        }

        if (model->needsFixupInScene()) {
            return true;
        }

        if (model->getRenderItemsNeedUpdate()) {
            return true;
        }
    }
    return Parent::needsRenderUpdate();
}

bool ModelEntityRenderer::needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const {
    if (resultWithReadLock<bool>([&] {
        if (entity->hasModel() != _hasModel) {
            return true;
        }

        // No model to render, early exit
        if (!_hasModel) {
            return false;
        }

        if (_animating != entity->isAnimatingSomething()) {
            return true;
        }

        return false;
    })) { return true; }

    ModelPointer model;
    withReadLock([&] {
        model = _model;
    });

    if (model && model->isLoaded()) {
        if (!entity->_dimensionsInitialized || entity->_needsInitialSimulation || !entity->_originalTexturesRead) {
            return true;
       } 

        // Check to see if we need to update the model bounds
        if (entity->needsUpdateModelBounds()) {
            return true;
        }

        // Check to see if we need to update the model bounds
        auto transform = entity->getTransform();
        if (model->getTranslation() != transform.getTranslation() ||
            model->getRotation() != transform.getRotation()) {
            return true;
        }

        if (model->getScaleToFitDimensions() != entity->getScaledDimensions() ||
            model->getRegistrationPoint() != entity->getRegistrationPoint()) {
            return true;
        }
    }

    return false;
}

void ModelEntityRenderer::doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) {
    DETAILED_PROFILE_RANGE(simulation_physics, __FUNCTION__);
    if (_hasModel != entity->hasModel()) {
        withWriteLock([&] {
            _hasModel = entity->hasModel();
        });
    }

    withWriteLock([&] {
        _animating = entity->isAnimatingSomething();
        if (_parsedModelURL != entity->getModelURL()) {
            _parsedModelURL = QUrl(entity->getModelURL());
        }
    });

    ModelPointer model;
    withReadLock([&] { model = _model; });

    withWriteLock([&] {
        bool visuallyReady = true;
        if (_hasModel) {
            if (model && _didLastVisualGeometryRequestSucceed) {
                visuallyReady = (_prevModelLoaded && _texturesLoaded);
            }
        }
        entity->setVisuallyReady(visuallyReady);
    });

    // Check for removal
    if (!_hasModel) {
        if (model) {
            model->removeFromScene(scene, transaction);
            entity->bumpAncestorChainRenderableVersion();
            withWriteLock([&] { _model.reset(); });
            emit DependencyManager::get<scriptable::ModelProviderFactory>()->
                modelRemovedFromScene(entity->getEntityItemID(), NestableType::Entity, _model);
        }
        setKey(false);
        _didLastVisualGeometryRequestSucceed = false;
        return;
    }

    // Check for addition
    if (_hasModel && !model) {
        model = std::make_shared<Model>(nullptr, entity.get(), _created);
        connect(model.get(), &Model::requestRenderUpdate, this, &ModelEntityRenderer::requestRenderUpdate);
        connect(model.get(), &Model::setURLFinished, this, [&](bool didVisualGeometryRequestSucceed) {
            setKey(didVisualGeometryRequestSucceed);
            _model->setTagMask(getTagMask());
            _model->setHifiRenderLayer(getHifiRenderLayer());
            emit requestRenderUpdate();
            if(didVisualGeometryRequestSucceed) {
                emit DependencyManager::get<scriptable::ModelProviderFactory>()->
                    modelAddedToScene(entity->getEntityItemID(), NestableType::Entity, _model);
            }
            _didLastVisualGeometryRequestSucceed = didVisualGeometryRequestSucceed;
        });
        model->setLoadingPriority(EntityTreeRenderer::getEntityLoadingPriority(*entity));
        entity->setModel(model);
        withWriteLock([&] { _model = model; });
    }

    // From here on, we are guaranteed a populated model
    if (_parsedModelURL != model->getURL()) {
        withWriteLock([&] {
            _texturesLoaded = false;
            _jointMappingCompleted = false;
            model->setURL(_parsedModelURL);
        });
    }

    // Nothing else to do unless the model is loaded
    if (!model->isLoaded()) {
        withWriteLock([&] {
            _prevModelLoaded = false;
        });
        emit requestRenderUpdate();
        return;
    } else if (!_prevModelLoaded) {
        withWriteLock([&] {
            _prevModelLoaded = true;
        });
    }

    // Check for initializing the model
    // FIXME: There are several places below here where we are modifying the entity, which we should not be doing from the renderable
    if (!entity->_dimensionsInitialized) {
        EntityItemProperties properties;
        properties.setLastEdited(usecTimestampNow()); // we must set the edit time since we're editing it
        auto extents = model->getMeshExtents();
        properties.setDimensions(extents.maximum - extents.minimum);
        qCDebug(entitiesrenderer) << "Autoresizing"
            << (!entity->getName().isEmpty() ? entity->getName() : entity->getModelURL())
            << "from mesh extents";

        QMetaObject::invokeMethod(DependencyManager::get<EntityScriptingInterface>().data(), "editEntity",
            Qt::QueuedConnection, Q_ARG(QUuid, entity->getEntityItemID()), Q_ARG(EntityItemProperties, properties));
    }

    if (!entity->_originalTexturesRead) {
        // Default to _originalTextures to avoid remapping immediately and lagging on load
        entity->_originalTextures = model->getTextures();
        entity->_originalTexturesRead = true;
    }

    if (_textures != entity->getTextures()) {
        QVariantMap newTextures;
        withWriteLock([&] {
            _texturesLoaded = false;
            _textures = entity->getTextures();
            newTextures = parseTexturesToMap(_textures, entity->_originalTextures);
        });
        model->setTextures(newTextures);
    }
    if (entity->_needsJointSimulation) {
        entity->copyAnimationJointDataToModel();
    }
    entity->updateModelBounds();
    entity->stopModelOverrideIfNoParent();

    if (model->isVisible() != _visible) {
        model->setVisibleInScene(_visible, scene);
    }

    if (model->isCauterized() != _cauterized) {
        model->setCauterized(_cauterized, scene);
    }

    render::hifi::Tag tagMask = getTagMask();
    if (model->getTagMask() != tagMask) {
        model->setTagMask(tagMask, scene);
    }

    // TODO? early exit here when not visible?

    if (model->canCastShadow() != _canCastShadow) {
        model->setCanCastShadow(_canCastShadow, scene);
    }

    {
        bool groupCulled = entity->getGroupCulled();
        if (model->isGroupCulled() != groupCulled) {
            model->setGroupCulled(groupCulled);
            setKey(_didLastVisualGeometryRequestSucceed);
        }
    }

    {
        DETAILED_PROFILE_RANGE(simulation_physics, "Fixup");
        if (model->needsFixupInScene()) {
            model->removeFromScene(scene, transaction);
            render::Item::Status::Getters statusGetters;
            makeStatusGetters(entity, statusGetters);
            model->addToScene(scene, transaction, statusGetters);
            entity->bumpAncestorChainRenderableVersion();
            processMaterials();
        }
    }

    if (!_texturesLoaded && model->getNetworkModel() && model->getNetworkModel()->areTexturesLoaded()) {
        withWriteLock([&] {
            _texturesLoaded = true;
        });
        model->updateRenderItems();
    } else if (!_texturesLoaded) {
        emit requestRenderUpdate();
    }

    // When the individual mesh parts of a model finish fading, they will mark their Model as needing updating
    // we will watch for that and ask the model to update it's render items
    if (model->getRenderItemsNeedUpdate()) {
        model->updateRenderItems();
    }

    // The code to deal with the change of properties is now in ModelEntityItem.cpp
    // That is where _currentFrame and _lastAnimated were updated.
    if (_animating) {
        DETAILED_PROFILE_RANGE(simulation_physics, "Animate");

        auto animationURL = entity->getAnimationURL();
        bool animationChanged = _animationURL != animationURL;
        if (animationChanged) {
            _animationURL = animationURL;

            if (_animation) {
                //(_animation->getURL().toString() != entity->getAnimationURL())) { // bad check
                // the joints have been mapped before but we have a new animation to load
                _animation.reset();
                _jointMappingCompleted = false;
            }
        }

        if (!_jointMappingCompleted) {
            mapJoints(entity, model);
        }
        if (entity->readyToAnimate()) {
            animate(entity);
        }
        emit requestRenderUpdate();
    }
}

void ModelEntityRenderer::setIsVisibleInSecondaryCamera(bool value) {
    Parent::setIsVisibleInSecondaryCamera(value);
    setKey(_didLastVisualGeometryRequestSucceed);
    if (_model) {
        _model->setTagMask(getTagMask());
    }
}

void ModelEntityRenderer::setRenderLayer(RenderLayer value) {
    Parent::setRenderLayer(value);
    setKey(_didLastVisualGeometryRequestSucceed);
    if (_model) {
        _model->setHifiRenderLayer(getHifiRenderLayer());
    }
}

void ModelEntityRenderer::setPrimitiveMode(PrimitiveMode value) {
    Parent::setPrimitiveMode(value);
    if (_model) {
        _model->setPrimitiveMode(_primitiveMode);
    }
}

// NOTE: this only renders the "meta" portion of the Model, namely it renders debugging items
void ModelEntityRenderer::doRender(RenderArgs* args) {
    DETAILED_PROFILE_RANGE(render_detail, "MetaModelRender");
    DETAILED_PERFORMANCE_TIMER("RMEIrender");

    // If the model doesn't have visual geometry, render our bounding box as green wireframe
    static glm::vec4 greenColor(0.0f, 1.0f, 0.0f, 1.0f);
    gpu::Batch& batch = *args->_batch;
    batch.setModelTransform(getModelTransform()); // we want to include the scale as well
    auto geometryCache = DependencyManager::get<GeometryCache>();
    geometryCache->renderWireCubeInstance(args, batch, greenColor, geometryCache->getShapePipelinePointer(false, false, args->_renderMethod == Args::RenderMethod::FORWARD));

#if WANT_EXTRA_DEBUGGING
    ModelPointer model;
    withReadLock([&] {
        model = _model;
    });
    if (model) {
        model->renderDebugMeshBoxes(batch, args->_renderMethod == Args::RenderMethod::FORWARD);
    }
#endif
}

void ModelEntityRenderer::mapJoints(const TypedEntityPointer& entity, const ModelPointer& model) {
    // if we don't have animation, or we're already joint mapped then bail early
    if (!entity->hasAnimation()) {
        return;
    }

    if (!_animation) {
        _animation = DependencyManager::get<AnimationCache>()->getAnimation(_animationURL);
    }

    if (_animation && _animation->isLoaded()) {
        QStringList animationJointNames = _animation->getJointNames();

        auto modelJointNames = model->getJointNames();
        if (modelJointNames.size() > 0 && animationJointNames.size() > 0) {
            _jointMapping.resize(modelJointNames.size());
            for (int i = 0; i < modelJointNames.size(); i++) {
                _jointMapping[i] = animationJointNames.indexOf(modelJointNames[i]);
            }
            _jointMappingCompleted = true;
        }
    }
}

void ModelEntityRenderer::addMaterial(graphics::MaterialLayer material, const std::string& parentMaterialName) {
    Parent::addMaterial(material, parentMaterialName);
    if (_model && _model->fetchRenderItemIDs().size() > 0) {
        _model->addMaterial(material, parentMaterialName);
    }
}

void ModelEntityRenderer::removeMaterial(graphics::MaterialPointer material, const std::string& parentMaterialName) {
    Parent::removeMaterial(material, parentMaterialName);
    if (_model && _model->fetchRenderItemIDs().size() > 0) {
        _model->removeMaterial(material, parentMaterialName);
    }
}

void ModelEntityRenderer::processMaterials() {
    assert(_model);
    std::lock_guard<std::mutex> lock(_materialsLock);
    for (auto& shapeMaterialPair : _materials) {
        auto material = shapeMaterialPair.second;
        while (!material.empty()) {
            _model->addMaterial(material.top(), shapeMaterialPair.first);
            material.pop();
        }
    }
}
