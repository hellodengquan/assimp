/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2026, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

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

#include "UnitTestPCH.h"

#include "PostProcessing/ConvertToLHProcess.h"
#include <assimp/SceneCombiner.h>
#include <assimp/anim.h>
#include <assimp/camera.h>
#include <assimp/light.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

using namespace Assimp;

namespace {

constexpr ai_real kEps = ai_real(1e-6);

// Builds a mesh with position/normal/tangent/bitangent streams on both the
// base mesh and one morph target, plus a bone and a populated bounding box.
aiMesh *BuildMirrorMesh() {
    aiMesh *mesh = new aiMesh;
    mesh->mNumVertices = 2;
    mesh->mVertices = new aiVector3D[2];
    mesh->mNormals = new aiVector3D[2];
    mesh->mTangents = new aiVector3D[2];
    mesh->mBitangents = new aiVector3D[2];
    mesh->mTextureCoords[0] = new aiVector3D[2];
    for (unsigned int i = 0; i < 2; ++i) {
        mesh->mVertices[i] = aiVector3D(ai_real(i) + 1, ai_real(i) - 3, ai_real(2 * i) + 1);
        mesh->mNormals[i] = aiVector3D(ai_real(0.1 * (i + 1)), ai_real(0.2), ai_real(0.3 - 0.1 * i));
        mesh->mTangents[i] = aiVector3D(ai_real(1), ai_real(i), ai_real(0.5 * (i + 1)));
        mesh->mBitangents[i] = aiVector3D(ai_real(i), ai_real(1), ai_real(-0.25 * (i + 1)));
        mesh->mTextureCoords[0][i] = aiVector3D(ai_real(i), ai_real(0.25 * i), 0);
    }

    aiAnimMesh *morph = new aiAnimMesh;
    morph->mNumVertices = 2;
    morph->mVertices = new aiVector3D[2];
    morph->mNormals = new aiVector3D[2];
    morph->mTangents = new aiVector3D[2];
    morph->mBitangents = new aiVector3D[2];
    morph->mTextureCoords[0] = new aiVector3D[2];
    for (unsigned int i = 0; i < 2; ++i) {
        morph->mVertices[i] = aiVector3D(ai_real(i) + 4, ai_real(i), ai_real(i) - 7);
        morph->mNormals[i] = aiVector3D(ai_real(0.2), ai_real(0.3 * (i + 1)), ai_real(0.4 - i));
        morph->mTangents[i] = aiVector3D(ai_real(0.5), ai_real(i + 1), ai_real(2 * i + 1));
        morph->mBitangents[i] = aiVector3D(ai_real(i + 1), ai_real(0.5), ai_real(i) - 2);
        morph->mTextureCoords[0][i] = aiVector3D(ai_real(i), ai_real(0.5 * i), 0);
    }
    mesh->mAnimMeshes = new aiAnimMesh *[1];
    mesh->mAnimMeshes[0] = morph;
    mesh->mNumAnimMeshes = 1;

    mesh->mNumBones = 1;
    mesh->mBones = new aiBone *[1];
    aiBone *bone = mesh->mBones[0] = new aiBone;
    bone->mName.Set("bone");
    bone->mOffsetMatrix = aiMatrix4x4(
            1, 2, 3, 4,
            5, 6, 7, 8,
            9, 10, 11, 12,
            13, 14, 15, 16);

    mesh->mAABB = aiAABB(aiVector3D(-1, -2, -3), aiVector3D(4, 5, 6));

    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 2;
    mesh->mFaces[0].mIndices = new unsigned int[2]{0, 1};
    return mesh;
}

aiNode *BuildNodeTree() {
    aiNode *root = new aiNode("root");
    root->mTransformation = aiMatrix4x4(
            0.5f, 1.0f, 1.5f, 2.0f,
            2.5f, 3.0f, 3.5f, 4.0f,
            4.5f, 5.0f, 5.5f, 6.0f,
            6.5f, 7.0f, 7.5f, 8.0f);

    aiNode *child = new aiNode("child");
    child->mTransformation = aiMatrix4x4(
            1.0f, 0.0f, 0.0f, 2.0f,
            0.0f, 1.0f, 0.0f, -3.0f,
            0.0f, 0.0f, 1.0f, 5.0f,
            0.0f, 0.0f, 0.0f, 1.0f);

    root->addChildren(1, &child);
    return root;
}

aiNodeAnim *BuildNodeAnim() {
    aiNodeAnim *anim = new aiNodeAnim;
    anim->mNodeName.Set("child");

    anim->mNumPositionKeys = 1;
    anim->mPositionKeys = new aiVectorKey[1];
    anim->mPositionKeys[0] = aiVectorKey(0.0, aiVector3D(1, -2, 3));

    anim->mNumRotationKeys = 1;
    anim->mRotationKeys = new aiQuatKey[1];
    const aiQuaternion q(0.1f, 0.2f, 0.3f, 0.4f);
    anim->mRotationKeys[0] = aiQuatKey(0.0, q);

    anim->mNumScalingKeys = 1;
    anim->mScalingKeys = new aiVectorKey[1];
    anim->mScalingKeys[0] = aiVectorKey(0.0, aiVector3D(2, 3, 4));
    return anim;
}

// Expected result of F * m * F with F = diag(1, 1, -1, 1).
aiMatrix4x4 MirroredMatrix(aiMatrix4x4 m) {
    aiMatrix4x4 out = m;
    out.a3 = -out.a3;
    out.b3 = -out.b3;
    out.d3 = -out.d3;
    out.c1 = -out.c1;
    out.c2 = -out.c2;
    out.c4 = -out.c4;
    return out;
}

void ExpectMatrixNear(const aiMatrix4x4 &a, const aiMatrix4x4 &b) {
    EXPECT_NEAR(a.a1, b.a1, kEps);
    EXPECT_NEAR(a.a2, b.a2, kEps);
    EXPECT_NEAR(a.a3, b.a3, kEps);
    EXPECT_NEAR(a.a4, b.a4, kEps);
    EXPECT_NEAR(a.b1, b.b1, kEps);
    EXPECT_NEAR(a.b2, b.b2, kEps);
    EXPECT_NEAR(a.b3, b.b3, kEps);
    EXPECT_NEAR(a.b4, b.b4, kEps);
    EXPECT_NEAR(a.c1, b.c1, kEps);
    EXPECT_NEAR(a.c2, b.c2, kEps);
    EXPECT_NEAR(a.c3, b.c3, kEps);
    EXPECT_NEAR(a.c4, b.c4, kEps);
    EXPECT_NEAR(a.d1, b.d1, kEps);
    EXPECT_NEAR(a.d2, b.d2, kEps);
    EXPECT_NEAR(a.d3, b.d3, kEps);
    EXPECT_NEAR(a.d4, b.d4, kEps);
}

// Runs the three conversion steps on a fresh copy of the source scene.
aiScene *RunPipeline(aiScene *source, bool lh, bool flipUV, bool flipWinding) {
    aiScene *copy = nullptr;
    SceneCombiner::CopyScene(&copy, source);

    if (flipWinding) {
        FlipWindingOrderProcess step;
        step.Execute(copy);
    }
    if (flipUV) {
        FlipUVsProcess step;
        step.Execute(copy);
    }
    if (lh) {
        MakeLeftHandedProcess step;
        step.Execute(copy);
    }
    return copy;
}

} // namespace

class utMakeLeftHandedProcess : public ::testing::Test {
public:
    void SetUp() override {
        mScene = new aiScene;
        mScene->mRootNode = BuildNodeTree();

        mMesh = BuildMirrorMesh();
        mScene->mMeshes = new aiMesh *[1];
        mScene->mMeshes[0] = mMesh;
        mScene->mNumMeshes = 1;

        mAnim = new aiAnimation;
        mAnim->mChannels = new aiNodeAnim *[1];
        mAnim->mChannels[0] = BuildNodeAnim();
        mAnim->mNumChannels = 1;
        mScene->mAnimations = new aiAnimation *[1];
        mScene->mAnimations[0] = mAnim;
        mScene->mNumAnimations = 1;

        mCamera = new aiCamera;
        mCamera->mName.Set("cam");
        mCamera->mPosition = aiVector3D(2, 3, 5);
        mCamera->mLookAt = aiVector3D(2, 3, -7);
        mCamera->mUp = aiVector3D(0, 1, 2);
        mScene->mCameras = new aiCamera *[1];
        mScene->mCameras[0] = mCamera;
        mScene->mNumCameras = 1;

        mLight = new aiLight;
        mLight->mName.Set("spot");
        mLight->mType = aiLightSource_SPOT;
        mLight->mPosition = aiVector3D(-1, 4, 6);
        mLight->mDirection = aiVector3D(1, -1, 2);
        mLight->mUp = aiVector3D(0, 2, -3);
        mScene->mLights = new aiLight *[1];
        mScene->mLights[0] = mLight;
        mScene->mNumLights = 1;

        mSkeleton = new aiSkeleton;
        mSkeleton->mNumBones = 1;
        mSkeleton->mBones = new aiSkeletonBone *[1];
        mSkeleton->mBones[0] = new aiSkeletonBone;
        mSkeleton->mBones[0]->mOffsetMatrix = mMesh->mBones[0]->mOffsetMatrix;
        mSkeleton->mBones[0]->mLocalMatrix = aiMatrix4x4(
                2, 0, 0, 1,
                0, 2, 0, 2,
                0, 0, 2, 3,
                0, 0, 0, 1);
        mScene->mSkeletons = new aiSkeleton *[1];
        mScene->mSkeletons[0] = mSkeleton;
        mScene->mNumSkeletons = 1;
    }

    void TearDown() override {
        delete mScene;
    }

protected:
    aiScene *mScene = nullptr;
    aiMesh *mMesh = nullptr;
    aiAnimation *mAnim = nullptr;
    aiCamera *mCamera = nullptr;
    aiLight *mLight = nullptr;
    aiSkeleton *mSkeleton = nullptr;
};

// All point- and direction-like vertex streams are mirrored across Z,
// identically on the base mesh and on every morph target.
TEST_F(utMakeLeftHandedProcess, MeshAndMorphTargetMirrorTest) {
    MakeLeftHandedProcess step;
    step.Execute(mScene);

    for (unsigned int i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(mMesh->mVertices[i].x, ai_real(i) + 1);
        EXPECT_FLOAT_EQ(mMesh->mVertices[i].y, ai_real(i) - 3);
        EXPECT_FLOAT_EQ(mMesh->mVertices[i].z, -(ai_real(2 * i) + 1));

        EXPECT_FLOAT_EQ(mMesh->mNormals[i].z, -(ai_real(0.3 - 0.1 * i)));
        EXPECT_FLOAT_EQ(mMesh->mTangents[i].z, -(0.5f * (i + 1)));
        EXPECT_FLOAT_EQ(mMesh->mBitangents[i].z, -(-0.25f * (i + 1)));
    }

    aiAnimMesh *morph = mMesh->mAnimMeshes[0];
    for (unsigned int i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(morph->mVertices[i].z, -(ai_real(i) - 7));
        EXPECT_FLOAT_EQ(morph->mNormals[i].z, -(0.4f - ai_real(i)));
        EXPECT_FLOAT_EQ(morph->mTangents[i].z, -ai_real(2 * i + 1));
        EXPECT_FLOAT_EQ(morph->mBitangents[i].z, -(ai_real(i) - 2));
        // texture coordinates are not touched by the handedness conversion
        EXPECT_FLOAT_EQ(morph->mTextureCoords[0][i].y, 0.5f * i);
    }
}

// LH conversion alone must not reverse the bitangent: it keeps pointing along
// the v-increase direction of the (unchanged) texture coordinates. The extra
// reversal only happens when FlipUVs is requested as well.
TEST_F(utMakeLeftHandedProcess, BitangentCombinationWithFlipUVsTest) {
    const aiVector3D originalBitangent = mMesh->mBitangents[0];

    {
        aiScene *lhOnly = RunPipeline(mScene, true, false, false);
        const aiMesh *mesh = lhOnly->mMeshes[0];

        // pure Z mirror of the original bitangent, no extra reversal
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].x, originalBitangent.x);
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].y, originalBitangent.y);
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].z, -originalBitangent.z);
        // UVs are untouched, so the bitangent still follows increasing v
        EXPECT_FLOAT_EQ(mesh->mTextureCoords[0][0].y, 0.f);
        delete lhOnly;
    }

    {
        aiScene *lhAndFlipUV = RunPipeline(mScene, true, true, false);
        const aiMesh *mesh = lhAndFlipUV->mMeshes[0];

        // mirror plus the reversal belonging to the v coordinate flip
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].x, -originalBitangent.x);
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].y, -originalBitangent.y);
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].z, originalBitangent.z);
        delete lhAndFlipUV;
    }

    {
        // flipping UVs without a handedness conversion reverses the bitangent too
        aiScene *flipUVOnly = RunPipeline(mScene, false, true, false);
        const aiMesh *mesh = flipUVOnly->mMeshes[0];
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].x, -originalBitangent.x);
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].y, -originalBitangent.y);
        EXPECT_FLOAT_EQ(mesh->mBitangents[0].z, -originalBitangent.z);
        delete flipUVOnly;
    }
}

// The morph target tangent space follows the same rules as the base mesh.
TEST_F(utMakeLeftHandedProcess, MorphTargetBitangentConsistencyTest) {
    const aiVector3D originalMorphBitangent = mMesh->mAnimMeshes[0]->mBitangents[0];

    aiScene *lhOnly = RunPipeline(mScene, true, false, false);
    const aiAnimMesh *morph = lhOnly->mMeshes[0]->mAnimMeshes[0];
    EXPECT_FLOAT_EQ(morph->mBitangents[0].x, originalMorphBitangent.x);
    EXPECT_FLOAT_EQ(morph->mBitangents[0].y, originalMorphBitangent.y);
    EXPECT_FLOAT_EQ(morph->mBitangents[0].z, -originalMorphBitangent.z);
    delete lhOnly;

    aiScene *lhAndFlipUV = RunPipeline(mScene, true, true, false);
    const aiAnimMesh *flippedMorph = lhAndFlipUV->mMeshes[0]->mAnimMeshes[0];
    EXPECT_FLOAT_EQ(flippedMorph->mBitangents[0].x, -originalMorphBitangent.x);
    EXPECT_FLOAT_EQ(flippedMorph->mBitangents[0].y, -originalMorphBitangent.y);
    EXPECT_FLOAT_EQ(flippedMorph->mBitangents[0].z, originalMorphBitangent.z);
    delete lhAndFlipUV;
}

// Position and look-at target are mirrored as points and the up vector as a
// direction, so the camera keeps looking at the mirrored target region.
TEST_F(utMakeLeftHandedProcess, CameraLooksAtMirroredRegionTest) {
    MakeLeftHandedProcess step;
    step.Execute(mScene);

    EXPECT_FLOAT_EQ(mCamera->mPosition.x, 2);
    EXPECT_FLOAT_EQ(mCamera->mPosition.y, 3);
    EXPECT_FLOAT_EQ(mCamera->mPosition.z, -5);

    EXPECT_FLOAT_EQ(mCamera->mLookAt.x, 2);
    EXPECT_FLOAT_EQ(mCamera->mLookAt.y, 3);
    EXPECT_FLOAT_EQ(mCamera->mLookAt.z, 7);

    // the mirrored viewing direction points at the mirror of the original target
    const aiVector3D viewDir = mCamera->mLookAt - mCamera->mPosition;
    EXPECT_FLOAT_EQ(viewDir.z, 12);

    EXPECT_FLOAT_EQ(mCamera->mUp.x, 0);
    EXPECT_FLOAT_EQ(mCamera->mUp.y, 1);
    EXPECT_FLOAT_EQ(mCamera->mUp.z, -2);
}

// Light position is mirrored as a point, direction and up as free vectors.
TEST_F(utMakeLeftHandedProcess, LightTransformTest) {
    MakeLeftHandedProcess step;
    step.Execute(mScene);

    EXPECT_FLOAT_EQ(mLight->mPosition.x, -1);
    EXPECT_FLOAT_EQ(mLight->mPosition.y, 4);
    EXPECT_FLOAT_EQ(mLight->mPosition.z, -6);

    EXPECT_FLOAT_EQ(mLight->mDirection.x, 1);
    EXPECT_FLOAT_EQ(mLight->mDirection.y, -1);
    EXPECT_FLOAT_EQ(mLight->mDirection.z, -2);

    EXPECT_FLOAT_EQ(mLight->mUp.x, 0);
    EXPECT_FLOAT_EQ(mLight->mUp.y, 2);
    EXPECT_FLOAT_EQ(mLight->mUp.z, 3);
}

// Node matrices are conjugated with the reflection and animation keys use the
// matching point/quaternion conventions; scaling keys are left untouched.
TEST_F(utMakeLeftHandedProcess, NodeAndAnimationTransformTest) {
    const aiMatrix4x4 originalRoot = mScene->mRootNode->mTransformation;
    const aiMatrix4x4 originalChild = mScene->mRootNode->mChildren[0]->mTransformation;

    MakeLeftHandedProcess step;
    step.Execute(mScene);

    ExpectMatrixNear(mScene->mRootNode->mTransformation, MirroredMatrix(originalRoot));
    ExpectMatrixNear(mScene->mRootNode->mChildren[0]->mTransformation, MirroredMatrix(originalChild));

    aiNodeAnim *anim = mAnim->mChannels[0];
    EXPECT_FLOAT_EQ(anim->mPositionKeys[0].mValue.x, 1);
    EXPECT_FLOAT_EQ(anim->mPositionKeys[0].mValue.y, -2);
    EXPECT_FLOAT_EQ(anim->mPositionKeys[0].mValue.z, -3);

    const aiQuaternion &r = anim->mRotationKeys[0].mValue;
    EXPECT_FLOAT_EQ(r.w, 0.1f);
    EXPECT_FLOAT_EQ(r.x, -0.2f);
    EXPECT_FLOAT_EQ(r.y, -0.3f);
    EXPECT_FLOAT_EQ(r.z, 0.4f);

    const aiVector3D &s = anim->mScalingKeys[0].mValue;
    EXPECT_FLOAT_EQ(s.x, 2);
    EXPECT_FLOAT_EQ(s.y, 3);
    EXPECT_FLOAT_EQ(s.z, 4);
}

// Mesh bone offset matrices and standalone skeleton matrices share the same
// conjugation as scene node matrices.
TEST_F(utMakeLeftHandedProcess, BoneAndSkeletonMatrixTest) {
    const aiMatrix4x4 originalBoneOffset = mMesh->mBones[0]->mOffsetMatrix;
    const aiMatrix4x4 originalSkeletonOffset = mSkeleton->mBones[0]->mOffsetMatrix;
    const aiMatrix4x4 originalLocal = mSkeleton->mBones[0]->mLocalMatrix;

    MakeLeftHandedProcess step;
    step.Execute(mScene);

    ExpectMatrixNear(mMesh->mBones[0]->mOffsetMatrix, MirroredMatrix(originalBoneOffset));
    ExpectMatrixNear(mSkeleton->mBones[0]->mOffsetMatrix, MirroredMatrix(originalSkeletonOffset));
    ExpectMatrixNear(mSkeleton->mBones[0]->mLocalMatrix, MirroredMatrix(originalLocal));
}

// The bounding box is mirrored and keeps min <= max on every axis.
TEST_F(utMakeLeftHandedProcess, BoundingBoxMirrorTest) {
    MakeLeftHandedProcess step;
    step.Execute(mScene);

    EXPECT_FLOAT_EQ(mMesh->mAABB.mMin.x, -1);
    EXPECT_FLOAT_EQ(mMesh->mAABB.mMin.y, -2);
    EXPECT_FLOAT_EQ(mMesh->mAABB.mMin.z, -6);
    EXPECT_FLOAT_EQ(mMesh->mAABB.mMax.x, 4);
    EXPECT_FLOAT_EQ(mMesh->mAABB.mMax.y, 5);
    EXPECT_FLOAT_EQ(mMesh->mAABB.mMax.z, 3);
}

// The conversion is self-inverse: applying it twice restores the input scene.
TEST_F(utMakeLeftHandedProcess, ApplyingTwiceRestoresOriginalTest) {
    aiScene *reference = nullptr;
    SceneCombiner::CopyScene(&reference, mScene);

    MakeLeftHandedProcess step;
    step.Execute(mScene);
    step.Execute(mScene);

    ExpectMatrixNear(mScene->mRootNode->mTransformation, reference->mRootNode->mTransformation);
    ExpectMatrixNear(mScene->mRootNode->mChildren[0]->mTransformation,
            reference->mRootNode->mChildren[0]->mTransformation);

    for (unsigned int i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(mScene->mMeshes[0]->mVertices[i].z, reference->mMeshes[0]->mVertices[i].z);
        EXPECT_FLOAT_EQ(mScene->mMeshes[0]->mBitangents[i].z, reference->mMeshes[0]->mBitangents[i].z);
    }
    delete reference;
}

// Any ordering of the three steps must produce the same scene.
TEST_F(utMakeLeftHandedProcess, StepOrderIndependenceTest) {
    // Two different execution orders for the full conversion pipeline.
    auto buildOrderA = [this]() {
        aiScene *copy = nullptr;
        SceneCombiner::CopyScene(&copy, mScene);
        FlipWindingOrderProcess winding;
        winding.Execute(copy);
        FlipUVsProcess uv;
        uv.Execute(copy);
        MakeLeftHandedProcess lh;
        lh.Execute(copy);
        return copy;
    };
    auto buildOrderB = [this]() {
        aiScene *copy = nullptr;
        SceneCombiner::CopyScene(&copy, mScene);
        MakeLeftHandedProcess lh;
        lh.Execute(copy);
        FlipUVsProcess uv;
        uv.Execute(copy);
        FlipWindingOrderProcess winding;
        winding.Execute(copy);
        return copy;
    };

    aiScene *orderA = buildOrderA();
    aiScene *orderB = buildOrderB();

    const aiMesh *a = orderA->mMeshes[0];
    const aiMesh *b = orderB->mMeshes[0];
    for (unsigned int i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(a->mVertices[i].z, b->mVertices[i].z);
        EXPECT_FLOAT_EQ(a->mTangents[i].z, b->mTangents[i].z);
        EXPECT_FLOAT_EQ(a->mBitangents[i].x, b->mBitangents[i].x);
        EXPECT_FLOAT_EQ(a->mBitangents[i].y, b->mBitangents[i].y);
        EXPECT_FLOAT_EQ(a->mBitangents[i].z, b->mBitangents[i].z);
        EXPECT_FLOAT_EQ(a->mTextureCoords[0][i].y, b->mTextureCoords[0][i].y);
    }
    ASSERT_EQ(a->mNumFaces, b->mNumFaces);
    ASSERT_EQ(a->mFaces[0].mNumIndices, b->mFaces[0].mNumIndices);
    for (unsigned int i = 0; i < a->mFaces[0].mNumIndices; ++i) {
        EXPECT_EQ(a->mFaces[0].mIndices[i], b->mFaces[0].mIndices[i]);
    }

    ExpectMatrixNear(orderA->mRootNode->mTransformation, orderB->mRootNode->mTransformation);
    EXPECT_FLOAT_EQ(orderA->mCameras[0]->mLookAt.z, orderB->mCameras[0]->mLookAt.z);
    EXPECT_FLOAT_EQ(orderA->mLights[0]->mDirection.z, orderB->mLights[0]->mDirection.z);

    delete orderA;
    delete orderB;
}

// The step only activates when the corresponding post-processing flag is set,
// so imports without the flag keep their original results.
TEST_F(utMakeLeftHandedProcess, InactiveWithoutFlagTest) {
    MakeLeftHandedProcess lh;
    FlipUVsProcess uv;
    FlipWindingOrderProcess winding;

    EXPECT_TRUE(lh.IsActive(aiProcess_MakeLeftHanded));
    EXPECT_FALSE(lh.IsActive(0));
    EXPECT_FALSE(lh.IsActive(aiProcess_FlipUVs | aiProcess_FlipWindingOrder));

    EXPECT_TRUE(uv.IsActive(aiProcess_FlipUVs));
    EXPECT_FALSE(uv.IsActive(0));

    EXPECT_TRUE(winding.IsActive(aiProcess_FlipWindingOrder));
    EXPECT_FALSE(winding.IsActive(0));
}

// A scene that never runs the conversion keeps its original coordinate data.
TEST_F(utMakeLeftHandedProcess, UnconvertedSceneRemainsUntouchedTest) {
    const aiVector3D originalPosition = mCamera->mPosition;
    const aiVector3D originalDirection = mLight->mDirection;
    const aiVector3D originalVertex = mMesh->mVertices[0];
    const aiMatrix4x4 originalNode = mScene->mRootNode->mTransformation;

    // Running unrelated steps must not change any spatial data.
    FlipUVsProcess uv;
    uv.Execute(mScene);
    FlipWindingOrderProcess winding;
    winding.Execute(mScene);

    EXPECT_FLOAT_EQ(mCamera->mPosition.z, originalPosition.z);
    EXPECT_FLOAT_EQ(mLight->mDirection.z, originalDirection.z);
    EXPECT_FLOAT_EQ(mMesh->mVertices[0].z, originalVertex.z);
    ExpectMatrixNear(mScene->mRootNode->mTransformation, originalNode);
}
