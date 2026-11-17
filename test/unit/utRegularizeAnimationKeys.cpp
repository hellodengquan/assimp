/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

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

----------------------------------------------------------------------
*/

#include "UnitTestPCH.h"

#include "PostProcessing/RegularizeAnimationKeysProcess.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/Exceptional.h>
#include <assimp/scene.h>

#include <limits>
#include <string>
#include <vector>

using namespace Assimp;

namespace {

// Helpers to build the minimal animation structures used by the tests.

aiNodeAnim *MakeNodeAnim(const char *name,
        const std::vector<aiVectorKey> &positionKeys,
        const std::vector<aiQuatKey> &rotationKeys,
        const std::vector<aiVectorKey> &scalingKeys) {
    auto *channel = new aiNodeAnim();
    channel->mNodeName = aiString(name);

    channel->mNumPositionKeys = static_cast<unsigned int>(positionKeys.size());
    if (channel->mNumPositionKeys) {
        channel->mPositionKeys = new aiVectorKey[channel->mNumPositionKeys];
        std::copy(positionKeys.begin(), positionKeys.end(),
                channel->mPositionKeys);
    }

    channel->mNumRotationKeys = static_cast<unsigned int>(rotationKeys.size());
    if (channel->mNumRotationKeys) {
        channel->mRotationKeys = new aiQuatKey[channel->mNumRotationKeys];
        std::copy(rotationKeys.begin(), rotationKeys.end(),
                channel->mRotationKeys);
    }

    channel->mNumScalingKeys = static_cast<unsigned int>(scalingKeys.size());
    if (channel->mNumScalingKeys) {
        channel->mScalingKeys = new aiVectorKey[channel->mNumScalingKeys];
        std::copy(scalingKeys.begin(), scalingKeys.end(),
                channel->mScalingKeys);
    }
    return channel;
}

// The scene takes ownership of the animation and frees it on destruction.
aiScene *MakeSceneWithAnimation(aiAnimation *anim) {
    auto *scene = new aiScene();
    scene->mNumAnimations = 1;
    scene->mAnimations = new aiAnimation *[1];
    scene->mAnimations[0] = anim;
    return scene;
}

aiAnimation *MakeSingleChannelAnimation(aiNodeAnim *channel) {
    auto *anim = new aiAnimation();
    anim->mName = aiString("anim");
    anim->mDuration = 10.0;
    anim->mTicksPerSecond = 25.0;
    anim->mNumChannels = 1;
    anim->mChannels = new aiNodeAnim *[1];
    anim->mChannels[0] = channel;
    return anim;
}

bool IsStrictlyIncreasing(const double *times, unsigned int count) {
    for (unsigned int i = 1; i < count; ++i) {
        if (!(times[i] > times[i - 1])) {
            return false;
        }
    }
    return true;
}

} // namespace

class utRegularizeAnimationKeysProcess : public ::testing::Test {
protected:
    void SetUp() override {
        mProcess = new RegularizeAnimationKeysProcess();
    }

    void TearDown() override {
        delete mProcess;
    }

    RegularizeAnimationKeysProcess *mProcess;
};

// ------------------------------------------------------------------------------------------------
// Equivalent duplicate position/scaling keys at the same time are merged and
// the surviving series is strictly increasing. First/last times are kept.
TEST_F(utRegularizeAnimationKeysProcess, mergeEquivalentDuplicateVectorKeys) {
    const std::vector<aiVectorKey> positionKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 2.0f, 3.0f)),
            aiVectorKey(0.0, aiVector3D(1.0f, 2.0f, 3.0f)), // exact duplicate
            aiVectorKey(5.0, aiVector3D(4.0f, 5.0f, 6.0f)),
            aiVectorKey(5.0, aiVector3D(4.0f, 5.0f, 6.0f)),
            aiVectorKey(10.0, aiVector3D(7.0f, 8.0f, 9.0f))
    };
    const std::vector<aiVectorKey> scalingKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 1.0f, 1.0f)),
            aiVectorKey(10.0, aiVector3D(2.0f, 2.0f, 2.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0)),
            aiQuatKey(10.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", positionKeys, rotationKeys, scalingKeys)));

    mProcess->Execute(scene);

    const aiNodeAnim *channel = scene->mAnimations[0]->mChannels[0];
    ASSERT_EQ(3u, channel->mNumPositionKeys);
    EXPECT_DOUBLE_EQ(0.0, channel->mPositionKeys[0].mTime);
    EXPECT_DOUBLE_EQ(5.0, channel->mPositionKeys[1].mTime);
    EXPECT_DOUBLE_EQ(10.0, channel->mPositionKeys[2].mTime);
    EXPECT_EQ(aiVector3D(4.0f, 5.0f, 6.0f),
            channel->mPositionKeys[1].mValue);

    const double times[3] = {channel->mPositionKeys[0].mTime,
            channel->mPositionKeys[1].mTime,
            channel->mPositionKeys[2].mTime};
    EXPECT_TRUE(IsStrictlyIncreasing(times, 3));

    // Duration is untouched.
    EXPECT_DOUBLE_EQ(10.0, scene->mAnimations[0]->mDuration);
    delete scene;
}

// ------------------------------------------------------------------------------------------------
// Out-of-order keys are sorted. This is a lossless operation: every key and
// every sampled pose is retained.
TEST_F(utRegularizeAnimationKeysProcess, sortOutOfOrderKeys) {
    const std::vector<aiVectorKey> positionKeys = {
            aiVectorKey(10.0, aiVector3D(3.0f, 0.0f, 0.0f)),
            aiVectorKey(0.0, aiVector3D(1.0f, 0.0f, 0.0f)),
            aiVectorKey(5.0, aiVector3D(2.0f, 0.0f, 0.0f))
    };
    const std::vector<aiVectorKey> scalingKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 1.0f, 1.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", positionKeys, rotationKeys, scalingKeys)));

    mProcess->Execute(scene);

    const aiNodeAnim *channel = scene->mAnimations[0]->mChannels[0];
    ASSERT_EQ(3u, channel->mNumPositionKeys);
    EXPECT_DOUBLE_EQ(0.0, channel->mPositionKeys[0].mTime);
    EXPECT_DOUBLE_EQ(5.0, channel->mPositionKeys[1].mTime);
    EXPECT_DOUBLE_EQ(10.0, channel->mPositionKeys[2].mTime);
    EXPECT_EQ(aiVector3D(1.0f, 0.0f, 0.0f),
            channel->mPositionKeys[0].mValue);
    EXPECT_EQ(aiVector3D(2.0f, 0.0f, 0.0f),
            channel->mPositionKeys[1].mValue);
    EXPECT_EQ(aiVector3D(3.0f, 0.0f, 0.0f),
            channel->mPositionKeys[2].mValue);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// Conflicting values at the same time stamp reject the animation and leave
// the original key arrays intact.
TEST_F(utRegularizeAnimationKeysProcess, rejectConflictingDuplicateKeys) {
    const std::vector<aiVectorKey> positionKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 0.0f, 0.0f)),
            aiVectorKey(5.0, aiVector3D(2.0f, 0.0f, 0.0f)),
            aiVectorKey(5.0, aiVector3D(9.0f, 0.0f, 0.0f)) // conflict at t=5
    };
    const std::vector<aiVectorKey> scalingKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 1.0f, 1.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", positionKeys, rotationKeys, scalingKeys)));
    aiNodeAnim *originalChannel = scene->mAnimations[0]->mChannels[0];
    aiVectorKey *originalArray = originalChannel->mPositionKeys;

    EXPECT_THROW(mProcess->Execute(scene), DeadlyImportError);

    // The animation is still complete and unmodified.
    EXPECT_EQ(originalChannel, scene->mAnimations[0]->mChannels[0]);
    EXPECT_EQ(originalArray, originalChannel->mPositionKeys);
    EXPECT_EQ(3u, originalChannel->mNumPositionKeys);
    EXPECT_DOUBLE_EQ(5.0, originalChannel->mPositionKeys[1].mTime);
    EXPECT_DOUBLE_EQ(5.0, originalChannel->mPositionKeys[2].mTime);
    EXPECT_EQ(aiVector3D(9.0f, 0.0f, 0.0f),
            originalChannel->mPositionKeys[2].mValue);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// q and -q describe the same orientation: the equivalent sign pair is merged,
// while two genuinely different rotations at one time stamp are rejected.
TEST_F(utRegularizeAnimationKeysProcess, handleQuaternionSignEquivalence) {
    const std::vector<aiVectorKey> vectorKeys = {
            aiVectorKey(0.0, aiVector3D(0.0f, 0.0f, 0.0f)),
            aiVectorKey(10.0, aiVector3D(0.0f, 0.0f, 0.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0)),
            aiQuatKey(5.0, aiQuaternion(0.1, 0.2, 0.3, 0.9)),
            aiQuatKey(5.0, aiQuaternion(-0.1, -0.2, -0.3, -0.9)),
            aiQuatKey(10.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", vectorKeys, rotationKeys, vectorKeys)));

    mProcess->Execute(scene);

    const aiNodeAnim *channel = scene->mAnimations[0]->mChannels[0];
    ASSERT_EQ(3u, channel->mNumRotationKeys);
    EXPECT_DOUBLE_EQ(0.0, channel->mRotationKeys[0].mTime);
    EXPECT_DOUBLE_EQ(5.0, channel->mRotationKeys[1].mTime);
    EXPECT_DOUBLE_EQ(10.0, channel->mRotationKeys[2].mTime);
    // The first representative of the equivalent pair is retained.
    EXPECT_EQ(aiQuaternion(0.1, 0.2, 0.3, 0.9),
            channel->mRotationKeys[1].mValue);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
TEST_F(utRegularizeAnimationKeysProcess, rejectGenuinelyDifferentRotation) {
    const std::vector<aiVectorKey> vectorKeys = {
            aiVectorKey(0.0, aiVector3D(0.0f, 0.0f, 0.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0)),
            aiQuatKey(5.0, aiQuaternion(0.1, 0.2, 0.3, 0.9)),
            aiQuatKey(5.0, aiQuaternion(0.5, 0.5, 0.5, 0.5)) // different pose
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", vectorKeys, rotationKeys, vectorKeys)));
    aiNodeAnim *originalChannel = scene->mAnimations[0]->mChannels[0];

    EXPECT_THROW(mProcess->Execute(scene), DeadlyImportError);
    EXPECT_EQ(3u, originalChannel->mNumRotationKeys);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// A rejection in the second bone's channel rolls back the whole animation:
// the already regularized first channel must never be committed.
TEST_F(utRegularizeAnimationKeysProcess, rejectionKeepsAllChannelsIntact) {
    const std::vector<aiVectorKey> goodKeys = {
            aiVectorKey(0.0, aiVector3D(0.0f, 0.0f, 0.0f)),
            aiVectorKey(0.0, aiVector3D(0.0f, 0.0f, 0.0f)),
            aiVectorKey(10.0, aiVector3D(1.0f, 0.0f, 0.0f))
    };
    const std::vector<aiVectorKey> badKeys = {
            aiVectorKey(0.0, aiVector3D(0.0f, 0.0f, 0.0f)),
            aiVectorKey(5.0, aiVector3D(1.0f, 0.0f, 0.0f)),
            aiVectorKey(5.0, aiVector3D(2.0f, 0.0f, 0.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    auto *anim = new aiAnimation();
    anim->mName = aiString("anim");
    anim->mDuration = 10.0;
    anim->mNumChannels = 2;
    anim->mChannels = new aiNodeAnim *[2];
    anim->mChannels[0] = MakeNodeAnim("good", goodKeys, rotationKeys, goodKeys);
    anim->mChannels[1] = MakeNodeAnim("bad", badKeys, rotationKeys, goodKeys);
    aiNodeAnim *goodChannel = anim->mChannels[0];
    aiVectorKey *goodOriginalArray = goodChannel->mPositionKeys;

    aiScene *scene = MakeSceneWithAnimation(anim);

    EXPECT_THROW(mProcess->Execute(scene), DeadlyImportError);

    EXPECT_EQ(goodChannel, anim->mChannels[0]);
    EXPECT_EQ(goodOriginalArray, goodChannel->mPositionKeys);
    // No partial regularization is visible on the good channel.
    EXPECT_EQ(3u, goodChannel->mNumPositionKeys);
    EXPECT_DOUBLE_EQ(0.0, goodChannel->mPositionKeys[0].mTime);
    EXPECT_DOUBLE_EQ(0.0, goodChannel->mPositionKeys[1].mTime);
    // The rejected channel is untouched as well (its scaling uses goodKeys).
    EXPECT_EQ(3u, anim->mChannels[1]->mNumScalingKeys);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// Large step jumps between distinct time stamps are authored poses and must
// be preserved rather than treated as a reason to reject.
TEST_F(utRegularizeAnimationKeysProcess, preserveStepJumps) {
    const std::vector<aiVectorKey> positionKeys = {
            aiVectorKey(0.0, aiVector3D(0.0f, 0.0f, 0.0f)),
            aiVectorKey(1.0, aiVector3D(1000.0f, -1000.0f, 500.0f)),
            aiVectorKey(2.0, aiVector3D(-1000.0f, 0.0f, 0.0f))
    };
    const std::vector<aiVectorKey> scalingKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 1.0f, 1.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", positionKeys, rotationKeys, scalingKeys)));

    mProcess->Execute(scene);

    const aiNodeAnim *channel = scene->mAnimations[0]->mChannels[0];
    ASSERT_EQ(3u, channel->mNumPositionKeys);
    EXPECT_EQ(aiVector3D(1000.0f, -1000.0f, 500.0f),
            channel->mPositionKeys[1].mValue);
    EXPECT_EQ(aiVector3D(-1000.0f, 0.0f, 0.0f),
            channel->mPositionKeys[2].mValue);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// The configured epsilon merges near-equal vector keys; values beyond the
// tolerance still conflict and reject the animation.
TEST_F(utRegularizeAnimationKeysProcess, applyVectorEpsilon) {
    Importer importer;
    importer.SetPropertyBool(AI_CONFIG_PP_AKR_ENABLE, true);
    importer.SetPropertyFloat(AI_CONFIG_PP_AKR_VECTOR_EPSILON, 0.01f);
    mProcess->SetupProperties(&importer);

    const std::vector<aiVectorKey> nearEqualKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 2.0f, 3.0f)),
            aiVectorKey(0.0, aiVector3D(1.005f, 2.0f, 3.0f))
    };
    const std::vector<aiVectorKey> scalingKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 1.0f, 1.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", nearEqualKeys, rotationKeys, scalingKeys)));

    mProcess->Execute(scene);
    EXPECT_EQ(1u, scene->mAnimations[0]->mChannels[0]->mNumPositionKeys);
    delete scene;

    const std::vector<aiVectorKey> beyondToleranceKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 2.0f, 3.0f)),
            aiVectorKey(0.0, aiVector3D(1.5f, 2.0f, 3.0f))
    };
    aiScene *scene2 = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", beyondToleranceKeys, rotationKeys, scalingKeys)));
    EXPECT_THROW(mProcess->Execute(scene2), DeadlyImportError);
    delete scene2;
}

// ------------------------------------------------------------------------------------------------
// Mesh and morph channels are regularized as well, and morph key payloads
// survive the deep copy.
TEST_F(utRegularizeAnimationKeysProcess, regularizeMeshAndMorphChannels) {
    auto *anim = new aiAnimation();
    anim->mName = aiString("anim");
    anim->mDuration = 4.0;
    anim->mNumMeshChannels = 1;
    anim->mMeshChannels = new aiMeshAnim *[1];
    auto *meshAnim = new aiMeshAnim();
    meshAnim->mName = aiString("mesh");
    meshAnim->mNumKeys = 3;
    meshAnim->mKeys = new aiMeshKey[3];
    meshAnim->mKeys[0] = aiMeshKey(0.0, 0u);
    meshAnim->mKeys[1] = aiMeshKey(2.0, 1u);
    meshAnim->mKeys[2] = aiMeshKey(2.0, 1u); // equivalent duplicate
    anim->mMeshChannels[0] = meshAnim;

    anim->mNumMorphMeshChannels = 1;
    anim->mMorphMeshChannels = new aiMeshMorphAnim *[1];
    auto *morphAnim = new aiMeshMorphAnim();
    morphAnim->mName = aiString("morph");
    morphAnim->mNumKeys = 2;
    morphAnim->mKeys = new aiMeshMorphKey[2];
    morphAnim->mKeys[0].mTime = 0.0;
    morphAnim->mKeys[0].mNumValuesAndWeights = 2;
    morphAnim->mKeys[0].mValues = new unsigned int[2]{0u, 1u};
    morphAnim->mKeys[0].mWeights = new double[2]{0.25, 0.75};
    morphAnim->mKeys[1].mTime = 4.0;
    morphAnim->mKeys[1].mNumValuesAndWeights = 2;
    morphAnim->mKeys[1].mValues = new unsigned int[2]{0u, 1u};
    morphAnim->mKeys[1].mWeights = new double[2]{0.5, 0.5};
    anim->mMorphMeshChannels[0] = morphAnim;

    aiScene *scene = MakeSceneWithAnimation(anim);

    mProcess->Execute(scene);

    ASSERT_EQ(2u, anim->mMeshChannels[0]->mNumKeys);
    EXPECT_DOUBLE_EQ(0.0, anim->mMeshChannels[0]->mKeys[0].mTime);
    EXPECT_DOUBLE_EQ(2.0, anim->mMeshChannels[0]->mKeys[1].mTime);
    ASSERT_EQ(2u, anim->mMorphMeshChannels[0]->mNumKeys);
    EXPECT_DOUBLE_EQ(4.0, anim->mMorphMeshChannels[0]->mKeys[1].mTime);
    EXPECT_EQ(2u, anim->mMorphMeshChannels[0]->mKeys[0].mNumValuesAndWeights);
    EXPECT_DOUBLE_EQ(0.75, anim->mMorphMeshChannels[0]->mKeys[0].mWeights[1]);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// Conflicting morph keys reject the animation without leaking the cloned
// value/weight arrays.
TEST_F(utRegularizeAnimationKeysProcess, rejectConflictingMorphKeys) {
    auto *anim = new aiAnimation();
    anim->mName = aiString("anim");
    anim->mNumMorphMeshChannels = 1;
    anim->mMorphMeshChannels = new aiMeshMorphAnim *[1];
    auto *morphAnim = new aiMeshMorphAnim();
    morphAnim->mName = aiString("morph");
    morphAnim->mNumKeys = 2;
    morphAnim->mKeys = new aiMeshMorphKey[2];
    morphAnim->mKeys[0].mTime = 1.0;
    morphAnim->mKeys[0].mNumValuesAndWeights = 1;
    morphAnim->mKeys[0].mValues = new unsigned int[1]{0u};
    morphAnim->mKeys[0].mWeights = new double[1]{0.5};
    morphAnim->mKeys[1].mTime = 1.0;
    morphAnim->mKeys[1].mNumValuesAndWeights = 1;
    morphAnim->mKeys[1].mValues = new unsigned int[1]{0u};
    morphAnim->mKeys[1].mWeights = new double[1]{0.9}; // different weight
    anim->mMorphMeshChannels[0] = morphAnim;

    aiScene *scene = MakeSceneWithAnimation(anim);

    EXPECT_THROW(mProcess->Execute(scene), DeadlyImportError);
    EXPECT_EQ(2u, anim->mMorphMeshChannels[0]->mNumKeys);
    EXPECT_DOUBLE_EQ(0.9, anim->mMorphMeshChannels[0]->mKeys[1].mWeights[0]);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// A non-finite time stamp is ambiguous input and must be rejected.
TEST_F(utRegularizeAnimationKeysProcess, rejectNonFiniteTime) {
    const std::vector<aiVectorKey> positionKeys = {
            aiVectorKey(0.0, aiVector3D(0.0f, 0.0f, 0.0f)),
            aiVectorKey(std::numeric_limits<double>::quiet_NaN(),
                    aiVector3D(1.0f, 0.0f, 0.0f))
    };
    const std::vector<aiVectorKey> scalingKeys = {
            aiVectorKey(0.0, aiVector3D(1.0f, 1.0f, 1.0f))
    };
    const std::vector<aiQuatKey> rotationKeys = {
            aiQuatKey(0.0, aiQuaternion(0.0, 0.0, 0.0, 1.0))
    };

    aiScene *scene = MakeSceneWithAnimation(MakeSingleChannelAnimation(
            MakeNodeAnim("bone", positionKeys, rotationKeys, scalingKeys)));

    EXPECT_THROW(mProcess->Execute(scene), DeadlyImportError);
    EXPECT_EQ(2u, scene->mAnimations[0]->mChannels[0]->mNumPositionKeys);

    delete scene;
}

// ------------------------------------------------------------------------------------------------
// The step is opt-in: without the property it stays inactive, so the default
// import pipeline behavior is unchanged.
TEST_F(utRegularizeAnimationKeysProcess, disabledByDefaultAndConfigurable) {
    EXPECT_FALSE(mProcess->IsActive(0u));
    EXPECT_FALSE(mProcess->IsActive(0xFFFFFFFFu));

    Importer importer;
    mProcess->SetupProperties(&importer);
    EXPECT_FALSE(mProcess->IsActive(0u));

    importer.SetPropertyBool(AI_CONFIG_PP_AKR_ENABLE, true);
    mProcess->SetupProperties(&importer);
    EXPECT_TRUE(mProcess->IsActive(0u));
}

// ------------------------------------------------------------------------------------------------
// End-to-end: with the property enabled the step runs inside the import
// pipeline and regularizes an otherwise valid animated scene.
TEST_F(utRegularizeAnimationKeysProcess, runsInsideImportPipeline) {
    Importer importer;
    importer.SetPropertyBool(AI_CONFIG_PP_AKR_ENABLE, true);
    const aiScene *scene = importer.ReadFile(
            ASSIMP_TEST_MODELS_DIR "/FBX/huesitos.fbx", 0u);
    ASSERT_NE(nullptr, scene);
    // The model loads successfully and every surviving key series is
    // strictly increasing in time.
    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation *anim = scene->mAnimations[a];
        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim *channel = anim->mChannels[c];
            for (unsigned int k = 1; k < channel->mNumPositionKeys; ++k) {
                EXPECT_GT(channel->mPositionKeys[k].mTime,
                        channel->mPositionKeys[k - 1].mTime);
            }
            for (unsigned int k = 1; k < channel->mNumRotationKeys; ++k) {
                EXPECT_GT(channel->mRotationKeys[k].mTime,
                        channel->mRotationKeys[k - 1].mTime);
            }
            for (unsigned int k = 1; k < channel->mNumScalingKeys; ++k) {
                EXPECT_GT(channel->mScalingKeys[k].mTime,
                        channel->mScalingKeys[k - 1].mTime);
            }
        }
    }
}
