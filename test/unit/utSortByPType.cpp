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
#include "UnitTestPCH.h"

#include "Common/ScenePreprocessor.h"
#include "PostProcessing/SortByPTypeProcess.h"
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace std;
using namespace Assimp;

class SortByPTypeProcessTest : public ::testing::Test {
public:
    SortByPTypeProcessTest() :
            Test(), mProcess1(nullptr), mScene(nullptr) {
        // empty
    }

protected:
    virtual void SetUp();
    virtual void TearDown();

protected:
    SortByPTypeProcess *mProcess1;
    aiScene *mScene;
};

// ------------------------------------------------------------------------------------------------
static unsigned int num[10][4] = {
    { 0, 0, 0, 1000 },
    { 0, 0, 1000, 0 },
    { 0, 1000, 0, 0 },
    { 1000, 0, 0, 0 },
    { 500, 500, 0, 0 },
    { 500, 0, 500, 0 },
    { 0, 330, 330, 340 },
    { 250, 250, 250, 250 },
    { 100, 100, 100, 700 },
    { 0, 100, 0, 900 },
};

// ------------------------------------------------------------------------------------------------
static unsigned int result[10] = {
    aiPrimitiveType_POLYGON,
    aiPrimitiveType_TRIANGLE,
    aiPrimitiveType_LINE,
    aiPrimitiveType_POINT,
    aiPrimitiveType_POINT | aiPrimitiveType_LINE,
    aiPrimitiveType_POINT | aiPrimitiveType_TRIANGLE,
    aiPrimitiveType_TRIANGLE | aiPrimitiveType_LINE | aiPrimitiveType_POLYGON,
    aiPrimitiveType_POLYGON | aiPrimitiveType_LINE | aiPrimitiveType_TRIANGLE | aiPrimitiveType_POINT,
    aiPrimitiveType_POLYGON | aiPrimitiveType_LINE | aiPrimitiveType_TRIANGLE | aiPrimitiveType_POINT,
    aiPrimitiveType_LINE | aiPrimitiveType_POLYGON,
};

// ------------------------------------------------------------------------------------------------
void SortByPTypeProcessTest::SetUp() {
    mProcess1 = new SortByPTypeProcess();
    mScene = new aiScene();

    mScene->mNumMeshes = 10;
    mScene->mMeshes = new aiMesh *[10];

    bool five = false;
    for (unsigned int i = 0; i < 10; ++i) {
        aiMesh *mesh = mScene->mMeshes[i] = new aiMesh();
        mesh->mNumFaces = 1000;
        aiFace *faces = mesh->mFaces = new aiFace[1000];
        aiVector3D *pv = mesh->mVertices = new aiVector3D[mesh->mNumFaces * 5];
        aiVector3D *pn = mesh->mNormals = new aiVector3D[mesh->mNumFaces * 5];

        aiVector3D *pt = mesh->mTangents = new aiVector3D[mesh->mNumFaces * 5];
        aiVector3D *pb = mesh->mBitangents = new aiVector3D[mesh->mNumFaces * 5];

        aiVector3D *puv = mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumFaces * 5];

        unsigned int remaining[4] = { num[i][0], num[i][1], num[i][2], num[i][3] };
        unsigned int n = 0;
        for (unsigned int m = 0; m < 1000; ++m) {
            unsigned int idx = m % 4;
            while (true) {
                if (!remaining[idx]) {
                    if (4 == ++idx) {
                        idx = 0;
                    }
                    continue;
                }
                break;
            }
            faces->mNumIndices = idx + 1;
            if (4 == faces->mNumIndices) {
                if (five) ++faces->mNumIndices;
                five = !five;
            }
            faces->mIndices = new unsigned int[faces->mNumIndices];
            for (unsigned int q = 0; q < faces->mNumIndices; ++q, ++n) {
                faces->mIndices[q] = n;
                float f = (float)remaining[idx];

                // (the values need to be unique - otherwise all degenerates would be removed)
                *pv++ = aiVector3D(f, f + 1.f, f + q);
                *pn++ = aiVector3D(f, f + 1.f, f + q);
                *pt++ = aiVector3D(f, f + 1.f, f + q);
                *pb++ = aiVector3D(f, f + 1.f, f + q);
                *puv++ = aiVector3D(f, f + 1.f, f + q);
            }
            ++faces;
            --remaining[idx];
        }
        mesh->mNumVertices = n;
    }

    mScene->mRootNode = new aiNode();
    mScene->mRootNode->mNumChildren = 5;
    mScene->mRootNode->mChildren = new aiNode *[5];
    for (unsigned int i = 0; i < 5; ++i) {
        aiNode *node = mScene->mRootNode->mChildren[i] = new aiNode();
        node->mNumMeshes = 2;
        node->mMeshes = new unsigned int[2];
        node->mMeshes[0] = (i << 1u);
        node->mMeshes[1] = (i << 1u) + 1;
    }
}

// ------------------------------------------------------------------------------------------------
void SortByPTypeProcessTest::TearDown() {
    delete mProcess1;
    delete mScene;
}

// ------------------------------------------------------------------------------------------------
TEST_F(SortByPTypeProcessTest, SortByPTypeStep) {
    ScenePreprocessor s(mScene);
    s.ProcessScene();
    for (unsigned int m = 0; m < 10; ++m)
        EXPECT_EQ(result[m], mScene->mMeshes[m]->mPrimitiveTypes);

    mProcess1->Execute(mScene);

    unsigned int idx = 0;
    for (unsigned int m = 0, real = 0; m < 10; ++m) {
        for (unsigned int n = 0; n < 4; ++n) {
            idx = num[m][n];
            if (idx) {
                EXPECT_TRUE(real < mScene->mNumMeshes);

                aiMesh *mesh = mScene->mMeshes[real];

                EXPECT_TRUE(nullptr != mesh);
                EXPECT_EQ(AI_PRIMITIVE_TYPE_FOR_N_INDICES(n + 1), mesh->mPrimitiveTypes);
                EXPECT_TRUE(nullptr != mesh->mVertices);
                EXPECT_TRUE(nullptr != mesh->mNormals);
                EXPECT_TRUE(nullptr != mesh->mTangents);
                EXPECT_TRUE(nullptr != mesh->mBitangents);
                EXPECT_TRUE(nullptr != mesh->mTextureCoords[0]);

                EXPECT_TRUE(mesh->mNumFaces == idx);
                for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                    aiFace &face = mesh->mFaces[f];
                    EXPECT_TRUE(face.mNumIndices == (n + 1) || (3 == n && face.mNumIndices > 3));
                }
                ++real;
            }
        }
    }
}

TEST_F(SortByPTypeProcessTest, issue389327770Test) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/fuzzer_data/clusterfuzz-testcase-minimized-assimp_fuzzer-4751812606885888", aiProcessPreset_TargetRealtime_Fast);
    EXPECT_NE(nullptr, scene);
}

// ------------------------------------------------------------------------------------------------
// A mesh may store UV and color channels out of order (e.g. channel 0 missing,
// channel 1 present). Splitting by primitive type must copy every populated
// channel, not stop at the first empty one.
TEST_F(SortByPTypeProcessTest, copiesSparseAttributeChannels) {
    aiScene *scene = new aiScene();
    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh *[1]();
    scene->mRootNode = new aiNode();
    scene->mRootNode->mNumMeshes = 1;
    scene->mRootNode->mMeshes = new unsigned int[1]{ 0 };

    aiMesh *mesh = scene->mMeshes[0] = new aiMesh();
    mesh->mNumVertices = 6;
    mesh->mVertices = new aiVector3D[6];
    mesh->mTextureCoords[1] = new aiVector3D[6]; // sparse: channel 0 is absent
    mesh->mColors[2] = new aiColor4D[6]; // sparse: channels 0 and 1 are absent
    mesh->mNumUVComponents[1] = 3;

    for (unsigned int i = 0; i < 6; ++i) {
        mesh->mVertices[i] = aiVector3D(static_cast<ai_real>(i), 0, 0);
        mesh->mTextureCoords[1][i] = aiVector3D(static_cast<ai_real>(i) + 10, 0, 0);
        mesh->mColors[2][i] = aiColor4D(static_cast<ai_real>(i) + 20, 0, 0, 1);
    }

    // one triangle and one line, so the mesh is split into two
    mesh->mNumFaces = 2;
    mesh->mFaces = new aiFace[2];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3]{ 0, 1, 2 };
    mesh->mFaces[1].mNumIndices = 2;
    mesh->mFaces[1].mIndices = new unsigned int[2]{ 3, 4 };
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE | aiPrimitiveType_LINE;

    mProcess1->Execute(scene);

    ASSERT_EQ(2u, scene->mNumMeshes);
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh *out = scene->mMeshes[m];
        ASSERT_NE(nullptr, out);
        EXPECT_NE(nullptr, out->mTextureCoords[1]);
        EXPECT_NE(nullptr, out->mColors[2]);
        EXPECT_EQ(3u, out->mNumUVComponents[1]);

        for (unsigned int i = 0; i < out->mNumVertices; ++i) {
            const ai_real p = out->mVertices[i].x;
            EXPECT_EQ(aiVector3D(p + 10, 0, 0), out->mTextureCoords[1][i]);
            EXPECT_EQ(aiColor4D(p + 20, 0, 0, 1), out->mColors[2][i]);
        }
    }

    delete scene;
}
