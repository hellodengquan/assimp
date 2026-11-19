/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2026, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file  MakeLeftHandedProcess.cpp
 *  @brief Implementation of the post processing step to convert all
 *  imported data to a left-handed coordinate system.
 *
 *  Face order & UV flip are also implemented here, for the sake of a
 *  better location.
 */

#include "ConvertToLHProcess.h"
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/light.h>
#include <assimp/camera.h>
#include <assimp/mesh.h>
#include <assimp/anim.h>
#include <assimp/DefaultLogger.hpp>

using namespace Assimp;

#ifndef ASSIMP_BUILD_NO_MAKELEFTHANDED_PROCESS

namespace {

// ------------------------------------------------------------------------------------------------
// Reflection across the local Z = 0 plane. This is the single geometric relationship
// used by the whole RH <-> LH conversion: all spatial data is expressed through this
// same mirror, point-like and direction-like quantities only differ in meaning.
//
// Points and free vectors happen to use the same component transform (Z is negated),
// but the distinction is kept explicit to document the semantics and to centralize
// the handling of matrices and quaternions.
void MirrorPoint(aiVector3D &v) {
    v.z = -v.z;
}

void MirrorDirection(aiVector3D &v) {
    v.z = -v.z;
}

// ------------------------------------------------------------------------------------------------
// Conjugate an affine transformation matrix with the Z reflection.
// If M is the original transform and F = diag(1, 1, -1, 1), the mirrored transform is
// F * M * F, which keeps the whole scene graph consistent with the mirrored vertex data.
// For the row-major aiMatrix4x4 layout this negates the third row and the third column
// (their intersection is negated twice and thus keeps its sign).
void MirrorTransform(aiMatrix4x4 &m) {
    m.a3 = -m.a3;
    m.b3 = -m.b3;
    // m.c3 is part of both the third row and third column.
    m.d3 = -m.d3;

    m.c1 = -m.c1;
    m.c2 = -m.c2;
    m.c4 = -m.c4;
}

// ------------------------------------------------------------------------------------------------
// Conjugate a rotation quaternion with the Z reflection. A reflected rotation is
// F * R * F, which is again a rotation; expressed as a quaternion its X/Y components
// are negated while W and Z keep their signs. The result stays normalized because the
// negated components are the same as in the original quaternion.
void MirrorRotation(aiQuaternion &q) {
    q.x = -q.x;
    q.y = -q.y;
}

template <typename aiMeshType>
void flipUVs(aiMeshType *pMesh) {
    if (pMesh == nullptr) {
        return;
    }
    // mirror texture y coordinate
    for (unsigned int tcIdx = 0; tcIdx < AI_MAX_NUMBER_OF_TEXTURECOORDS; tcIdx++) {
        if (!pMesh->HasTextureCoords(tcIdx)) {
            break;
        }

        for (unsigned int vIdx = 0; vIdx < pMesh->mNumVertices; vIdx++) {
            pMesh->mTextureCoords[tcIdx][vIdx].y = 1.0f - pMesh->mTextureCoords[tcIdx][vIdx].y;
        }
    }

    // The bitangent points along the texture coordinate v axis. Flipping v
    // reverses that axis, so the bitangent has to be negated as well. The
    // tangent (u axis) and the geometric normal are not affected.
    if (pMesh->HasTangentsAndBitangents()) {
        for (unsigned int vIdx = 0; vIdx < pMesh->mNumVertices; vIdx++) {
            pMesh->mBitangents[vIdx] *= -1.0f;
        }
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool MakeLeftHandedProcess::IsActive(unsigned int pFlags) const {
    return 0 != (pFlags & aiProcess_MakeLeftHanded);
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void MakeLeftHandedProcess::Execute(aiScene *pScene) {
    // Check for an existent root node to proceed
    ai_assert(pScene->mRootNode != nullptr);
    ASSIMP_LOG_DEBUG("MakeLeftHandedProcess begin");

    // recursively convert all the nodes
    ProcessNode(pScene->mRootNode, aiMatrix4x4());

    // process the meshes accordingly
    for (unsigned int a = 0; a < pScene->mNumMeshes; ++a) {
        ProcessMesh(pScene->mMeshes[a]);
    }

    // process the materials accordingly
    for (unsigned int a = 0; a < pScene->mNumMaterials; ++a) {
        ProcessMaterial(pScene->mMaterials[a]);
    }

    // transform all animation channels as well
    for (unsigned int a = 0; a < pScene->mNumAnimations; a++) {
        aiAnimation *anim = pScene->mAnimations[a];
        for (unsigned int b = 0; b < anim->mNumChannels; b++) {
            aiNodeAnim *nodeAnim = anim->mChannels[b];
            ProcessAnimation(nodeAnim);
        }
    }

    // process the cameras accordingly
    for (unsigned int a = 0; a < pScene->mNumCameras; ++a) {
        ProcessCamera(pScene->mCameras[a]);
    }

    // process the lights accordingly
    for (unsigned int a = 0; a < pScene->mNumLights; ++a) {
        ProcessLight(pScene->mLights[a]);
    }

    // process the standalone skeletons accordingly
    for (unsigned int a = 0; a < pScene->mNumSkeletons; ++a) {
        ProcessSkeleton(pScene->mSkeletons[a]);
    }
    ASSIMP_LOG_DEBUG("MakeLeftHandedProcess finished");
}

// ------------------------------------------------------------------------------------------------
// Recursively converts a node, all of its children and all of its meshes
void MakeLeftHandedProcess::ProcessNode(aiNode *pNode, const aiMatrix4x4 & /*pParentGlobalRotation*/) {
    // Conjugate the node transformation with the Z reflection so that the
    // transformed node graph reproduces the mirrored mesh data.
    MirrorTransform(pNode->mTransformation);

    // continue for all children
    for (size_t a = 0; a < pNode->mNumChildren; ++a) {
        ProcessNode(pNode->mChildren[a], aiMatrix4x4());
    }
}

// ------------------------------------------------------------------------------------------------
// Converts a single mesh to left handed coordinates.
void MakeLeftHandedProcess::ProcessMesh(aiMesh *pMesh) {
    if (nullptr == pMesh) {
        ASSIMP_LOG_ERROR("Nullptr to mesh found.");
        return;
    }

    // mirror positions, normals, tangents and bitangents along the Z axis.
    // Tangents and bitangents are plain direction vectors: mirroring them keeps
    // the tangent space aligned with the mirrored geometry. The additional
    // bitangent reversal that belongs to a texture coordinate flip is handled by
    // FlipUVsProcess, independently of the execution order of the two steps.
    for (size_t a = 0; a < pMesh->mNumVertices; ++a) {
        MirrorPoint(pMesh->mVertices[a]);
        if (pMesh->HasNormals()) {
            MirrorDirection(pMesh->mNormals[a]);
        }
        if (pMesh->HasTangentsAndBitangents()) {
            MirrorDirection(pMesh->mTangents[a]);
            MirrorDirection(pMesh->mBitangents[a]);
        }
    }

    // mirror the morph targets (anim meshes) with the exact same relationship
    // as the base mesh so that every blend shape stays consistent with it.
    for (size_t m = 0; m < pMesh->mNumAnimMeshes; ++m) {
        aiAnimMesh *animMesh = pMesh->mAnimMeshes[m];
        for (size_t a = 0; a < animMesh->mNumVertices; ++a) {
            if (animMesh->HasPositions()) {
                MirrorPoint(animMesh->mVertices[a]);
            }
            if (animMesh->HasNormals()) {
                MirrorDirection(animMesh->mNormals[a]);
            }
            if (animMesh->HasTangentsAndBitangents()) {
                MirrorDirection(animMesh->mTangents[a]);
                MirrorDirection(animMesh->mBitangents[a]);
            }
        }
    }

    // conjugate the offset matrices of all bones with the Z reflection
    for (size_t a = 0; a < pMesh->mNumBones; ++a) {
        MirrorTransform(pMesh->mBones[a]->mOffsetMatrix);
    }

    // mirror the bounding box, swapping the negated Z extents back into order.
    // A zero box stays zero, so no presence check is required.
    MirrorPoint(pMesh->mAABB.mMin);
    MirrorPoint(pMesh->mAABB.mMax);
    const ai_real minZ = pMesh->mAABB.mMin.z;
    pMesh->mAABB.mMin.z = pMesh->mAABB.mMax.z;
    pMesh->mAABB.mMax.z = minZ;
}

// ------------------------------------------------------------------------------------------------
// Converts a single material to left handed coordinates.
void MakeLeftHandedProcess::ProcessMaterial(aiMaterial *_mat) {
    if (nullptr == _mat) {
        ASSIMP_LOG_ERROR("Nullptr to aiMaterial found.");
        return;
    }

    aiMaterial *mat = (aiMaterial *)_mat;
    for (unsigned int a = 0; a < mat->mNumProperties; ++a) {
        aiMaterialProperty *prop = mat->mProperties[a];

        // Mapping axis for UV mappings?
        if (!::strcmp(prop->mKey.data, "$tex.mapaxis")) {
            ai_assert(prop->mDataLength >= sizeof(aiVector3D)); // something is wrong with the validation if we end up here
            aiVector3D *pff = (aiVector3D *)prop->mData;
            pff->z *= -1.f;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Converts the given animation to LH coordinates.
void MakeLeftHandedProcess::ProcessAnimation(aiNodeAnim *pAnim) {
    // position keys are points: mirror them across the Z = 0 plane
    for (unsigned int a = 0; a < pAnim->mNumPositionKeys; a++) {
        MirrorPoint(pAnim->mPositionKeys[a].mValue);
    }

    // rotation keys are conjugated with the Z reflection, matching the
    // converted node transformations.
    for (unsigned int a = 0; a < pAnim->mNumRotationKeys; a++) {
        MirrorRotation(pAnim->mRotationKeys[a].mValue);
    }

    // scaling keys are axis-local scalar factors and remain unchanged
}

// ------------------------------------------------------------------------------------------------
// Converts a single camera to left handed coordinates.
void MakeLeftHandedProcess::ProcessCamera(aiCamera *pCam) {
    // The camera data is expressed in the coordinate space of its node, whose
    // transformation has already been conjugated with the Z reflection. Mirroring
    // the local vectors the same way therefore keeps the camera looking at the
    // mirrored target region instead of turning it around.
    MirrorPoint(pCam->mPosition);
    MirrorPoint(pCam->mLookAt);
    MirrorDirection(pCam->mUp);
}

// ------------------------------------------------------------------------------------------------
// Converts a single light to left handed coordinates.
void MakeLeftHandedProcess::ProcessLight(aiLight *pLight) {
    if (nullptr == pLight) {
        ASSIMP_LOG_ERROR("Nullptr to aiLight found.");
        return;
    }

    // The position is a point, direction and up are free vectors. All are local
    // to the light's node, which has already been converted, so mirroring them
    // with the same Z reflection keeps spot and directional lights pointing at
    // the mirrored region of the scene.
    MirrorPoint(pLight->mPosition);
    MirrorDirection(pLight->mDirection);
    MirrorDirection(pLight->mUp);
}

// ------------------------------------------------------------------------------------------------
// Converts a standalone skeleton to left handed coordinates.
void MakeLeftHandedProcess::ProcessSkeleton(aiSkeleton *pSkeleton) {
    if (nullptr == pSkeleton) {
        ASSIMP_LOG_ERROR("Nullptr to aiSkeleton found.");
        return;
    }

    // The skeleton matrices describe the same bone hierarchy as the scene nodes
    // and mesh bones, so they use the exact same matrix conjugation.
    for (unsigned int a = 0; a < pSkeleton->mNumBones; ++a) {
        aiSkeletonBone *bone = pSkeleton->mBones[a];
        if (nullptr == bone) {
            continue;
        }
        MirrorTransform(bone->mOffsetMatrix);
        MirrorTransform(bone->mLocalMatrix);
    }
}

#endif // !!  ASSIMP_BUILD_NO_MAKELEFTHANDED_PROCESS
#ifndef ASSIMP_BUILD_NO_FLIPUVS_PROCESS
// # FlipUVsProcess

// ------------------------------------------------------------------------------------------------
// Constructor to be privately used by Importer
FlipUVsProcess::FlipUVsProcess() = default;

// ------------------------------------------------------------------------------------------------
// Destructor, private as well
FlipUVsProcess::~FlipUVsProcess() = default;

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool FlipUVsProcess::IsActive(unsigned int pFlags) const {
    return 0 != (pFlags & aiProcess_FlipUVs);
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void FlipUVsProcess::Execute(aiScene *pScene) {
    ASSIMP_LOG_DEBUG("FlipUVsProcess begin");
    for (unsigned int i = 0; i < pScene->mNumMeshes; ++i)
        ProcessMesh(pScene->mMeshes[i]);

    for (unsigned int i = 0; i < pScene->mNumMaterials; ++i)
        ProcessMaterial(pScene->mMaterials[i]);
    ASSIMP_LOG_DEBUG("FlipUVsProcess finished");
}

// ------------------------------------------------------------------------------------------------
// Converts a single material
void FlipUVsProcess::ProcessMaterial(aiMaterial *_mat) {
    aiMaterial *mat = (aiMaterial *)_mat;
    for (unsigned int a = 0; a < mat->mNumProperties; ++a) {
        aiMaterialProperty *prop = mat->mProperties[a];
        if (!prop) {
            ASSIMP_LOG_VERBOSE_DEBUG("Property is null");
            continue;
        }

        // UV transformation key?
        if (!::strcmp(prop->mKey.data, "$tex.uvtrafo")) {
            ai_assert(prop->mDataLength >= sizeof(aiUVTransform)); // something is wrong with the validation if we end up here
            aiUVTransform *uv = (aiUVTransform *)prop->mData;

            // just flip it, that's everything
            uv->mTranslation.y *= -1.f;
            uv->mRotation *= -1.f;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Converts a single mesh
void FlipUVsProcess::ProcessMesh(aiMesh *pMesh) {
    flipUVs(pMesh);
    for (unsigned int idx = 0; idx < pMesh->mNumAnimMeshes; idx++) {
        flipUVs(pMesh->mAnimMeshes[idx]);
    }
}

#endif // !ASSIMP_BUILD_NO_FLIPUVS_PROCESS
#ifndef ASSIMP_BUILD_NO_FLIPWINDING_PROCESS
// # FlipWindingOrderProcess

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool FlipWindingOrderProcess::IsActive(unsigned int pFlags) const {
    return 0 != (pFlags & aiProcess_FlipWindingOrder);
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void FlipWindingOrderProcess::Execute(aiScene *pScene) {
    ASSIMP_LOG_DEBUG("FlipWindingOrderProcess begin");
    for (unsigned int i = 0; i < pScene->mNumMeshes; ++i)
        ProcessMesh(pScene->mMeshes[i]);
    ASSIMP_LOG_DEBUG("FlipWindingOrderProcess finished");
}

// ------------------------------------------------------------------------------------------------
// Converts a single mesh
void FlipWindingOrderProcess::ProcessMesh(aiMesh *pMesh) {
    // invert the order of all faces in this mesh
    for (unsigned int a = 0; a < pMesh->mNumFaces; a++) {
        aiFace &face = pMesh->mFaces[a];
        for (unsigned int b = 0; b < face.mNumIndices / 2; b++) {
            std::swap(face.mIndices[b], face.mIndices[face.mNumIndices - 1 - b]);
        }
    }

    // invert the order of all components in this mesh anim meshes
    for (unsigned int m = 0; m < pMesh->mNumAnimMeshes; m++) {
        aiAnimMesh *animMesh = pMesh->mAnimMeshes[m];
        unsigned int numVertices = animMesh->mNumVertices;
        if (animMesh->HasPositions()) {
            for (unsigned int a = 0; a < numVertices; a++) {
                std::swap(animMesh->mVertices[a], animMesh->mVertices[numVertices - 1 - a]);
            }
        }
        if (animMesh->HasNormals()) {
            for (unsigned int a = 0; a < numVertices; a++) {
                std::swap(animMesh->mNormals[a], animMesh->mNormals[numVertices - 1 - a]);
            }
        }
        for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; i++) {
            if (animMesh->HasTextureCoords(i)) {
                for (unsigned int a = 0; a < numVertices; a++) {
                    std::swap(animMesh->mTextureCoords[i][a], animMesh->mTextureCoords[i][numVertices - 1 - a]);
                }
            }
        }
        if (animMesh->HasTangentsAndBitangents()) {
            for (unsigned int a = 0; a < numVertices; a++) {
                std::swap(animMesh->mTangents[a], animMesh->mTangents[numVertices - 1 - a]);
                std::swap(animMesh->mBitangents[a], animMesh->mBitangents[numVertices - 1 - a]);
            }
        }
        for (unsigned int v = 0; v < AI_MAX_NUMBER_OF_COLOR_SETS; v++) {
            if (animMesh->HasVertexColors(v)) {
                for (unsigned int a = 0; a < numVertices; a++) {
                    std::swap(animMesh->mColors[v][a], animMesh->mColors[v][numVertices - 1 - a]);
                }
            }
        }
    }
}

#endif // !! ASSIMP_BUILD_NO_FLIPWINDING_PROCESS
