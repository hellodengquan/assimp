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

#include <assimp/scene.h>

#include "PostProcessing/JoinVerticesProcess.h"

using namespace std;
using namespace Assimp;

class utJoinVertices : public ::testing::Test {
public:
    utJoinVertices() :
            Test(), piProcess(nullptr), pcMesh(nullptr) {
        // empty
    }

protected:
    virtual void SetUp();
    virtual void TearDown();

protected:
    JoinVerticesProcess *piProcess;
    aiMesh *pcMesh;
};

// ------------------------------------------------------------------------------------------------
void utJoinVertices::SetUp() {
    // construct the process
    piProcess = new JoinVerticesProcess();

    // create a quite small mesh for testing purposes -
    // the mesh itself is *something* but it has redundant vertices
    pcMesh = new aiMesh();

    pcMesh->mNumVertices = 900;
    aiVector3D *&pv = pcMesh->mVertices = new aiVector3D[900];
    for (unsigned int i = 0; i < 3; ++i) {
        const unsigned int base = i * 300;
        for (unsigned int a = 0; a < 300; ++a) {
            pv[base + a].x = pv[base + a].y = pv[base + a].z = (float)a;
        }
    }

    // generate faces - each vertex is referenced once
    pcMesh->mNumFaces = 300;
    pcMesh->mFaces = new aiFace[300];
    for (unsigned int i = 0, p = 0; i < 300; ++i) {
        aiFace &face = pcMesh->mFaces[i];
        face.mIndices = new unsigned int[face.mNumIndices = 3];
        for (unsigned int a = 0; a < 3; ++a) {
            face.mIndices[a] = p++;
        }
    }

    // generate extra members - set them to zero to make sure they're identical
    pcMesh->mTextureCoords[0] = new aiVector3D[900];
    pcMesh->mBitangents = new aiVector3D[900];
    pcMesh->mNormals = new aiVector3D[900];
    pcMesh->mTangents = new aiVector3D[900];
    for (unsigned int i = 0; i < 900; ++i) {
        pcMesh->mTextureCoords[0][i] = aiVector3D(0.f);
        pcMesh->mNormals[i] = aiVector3D(0.f);
        pcMesh->mTangents[i] = aiVector3D(0.f);
        pcMesh->mBitangents[i] = aiVector3D(0.f);
    }
}

// ------------------------------------------------------------------------------------------------
void utJoinVertices::TearDown() {
    delete this->pcMesh;
    pcMesh = nullptr;
    delete this->piProcess;
    piProcess = nullptr;
}

// ------------------------------------------------------------------------------------------------
TEST_F(utJoinVertices, testProcess) {
    // execute the step on the given data
    piProcess->ProcessMesh(pcMesh, 0);

    // the number of faces shouldn't change
    ASSERT_EQ(300U, pcMesh->mNumFaces);
    ASSERT_EQ(300U, pcMesh->mNumVertices);

    ASSERT_TRUE(nullptr != pcMesh->mNormals);
    ASSERT_TRUE(nullptr != pcMesh->mTangents);
    ASSERT_TRUE(nullptr != pcMesh->mBitangents);
    ASSERT_TRUE(nullptr != pcMesh->mTextureCoords[0]);

    // the order doesn't care
    float fSum = 0.f;
    for (unsigned int i = 0; i < 300; ++i) {
        aiVector3D &v = pcMesh->mVertices[i];
        fSum += v.x + v.y + v.z;

        EXPECT_FALSE(pcMesh->mNormals[i].x);
        EXPECT_FALSE(pcMesh->mTangents[i].x);
        EXPECT_FALSE(pcMesh->mBitangents[i].x);
        EXPECT_FALSE(pcMesh->mTextureCoords[0][i].x);
    }
    EXPECT_EQ(150.f * 299.f * 3.f, fSum); // gaussian sum equation
}

// ------------------------------------------------------------------------------------------------
// Joining vertices removes duplicates from every vertex stream. Bone weights
// and morph targets must follow the surviving vertices instead of keeping
// stale indices.
TEST_F(utJoinVertices, joinsBonesAndMorphTargets) {
    aiMesh *mesh = new aiMesh();

    // vertices 0 and 2 are identical; vertex 1 is distinct
    mesh->mNumVertices = 3;
    mesh->mVertices = new aiVector3D[3];
    mesh->mVertices[0] = aiVector3D(0, 0, 0);
    mesh->mVertices[1] = aiVector3D(1, 0, 0);
    mesh->mVertices[2] = aiVector3D(0, 0, 0);

    mesh->mTextureCoords[0] = new aiVector3D[3];
    mesh->mTextureCoords[3] = new aiVector3D[3]; // sparse channel
    mesh->mColors[0] = new aiColor4D[3];
    for (unsigned int i = 0; i < 3; ++i) {
        mesh->mTextureCoords[0][i] = aiVector3D(0, 0, 0);
        mesh->mTextureCoords[3][i] = aiVector3D(static_cast<ai_real>(i), 0, 0);
        mesh->mColors[0][i] = aiColor4D(0, 0, 0, 1);
    }

    mesh->mNumBones = 1;
    mesh->mBones = new aiBone *[1];
    aiBone *bone = mesh->mBones[0] = new aiBone();
    bone->mWeights = new aiVertexWeight[3];
    bone->mNumWeights = 3;
    bone->mWeights[0] = aiVertexWeight(0, 0.5f);
    bone->mWeights[1] = aiVertexWeight(1, 0.25f);
    bone->mWeights[2] = aiVertexWeight(2, 0.25f);

    mesh->mNumAnimMeshes = 1;
    mesh->mAnimMeshes = new aiAnimMesh *[1];
    aiAnimMesh *anim = mesh->mAnimMeshes[0] = new aiAnimMesh();
    anim->mNumVertices = 3;
    anim->mVertices = new aiVector3D[3];
    anim->mTextureCoords[3] = new aiVector3D[3];
    for (unsigned int i = 0; i < 3; ++i) {
        anim->mVertices[i] = mesh->mVertices[i];
        anim->mTextureCoords[3][i] = mesh->mTextureCoords[3][i];
    }

    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3]{ 0, 1, 2 };

    piProcess->ProcessMesh(mesh, 0);

    // vertices 0 and 2 collapse
    ASSERT_EQ(2u, mesh->mNumVertices);
    EXPECT_EQ(2u, mesh->mAnimMeshes[0]->mNumVertices);

    // every face index must be in range and the surviving vertices must keep
    // their attributes
    for (unsigned int i = 0; i < mesh->mFaces[0].mNumIndices; ++i) {
        EXPECT_LT(mesh->mFaces[0].mIndices[i], mesh->mNumVertices);
    }
    EXPECT_EQ(aiVector3D(1, 0, 0), mesh->mVertices[1]);
    EXPECT_EQ(aiVector3D(0, 0, 0), mesh->mVertices[0]);
    EXPECT_EQ(aiVector3D(1, 0, 0), mesh->mTextureCoords[3][1]);
    EXPECT_EQ(nullptr, mesh->mTextureCoords[1]);
    EXPECT_EQ(nullptr, mesh->mTextureCoords[2]);

    // The bone must only reference surviving vertices. The duplicate copy's
    // weight must not be accumulated onto the survivor.
    ASSERT_EQ(1u, mesh->mNumBones);
    float sum = 0.f;
    for (unsigned int w = 0; w < mesh->mBones[0]->mNumWeights; ++w) {
        const aiVertexWeight &vw = mesh->mBones[0]->mWeights[w];
        EXPECT_LT(vw.mVertexId, mesh->mNumVertices);
        sum += vw.mWeight;
    }
    EXPECT_FLOAT_EQ(0.75f, sum);

    // morph target data must match the joined positions
    EXPECT_EQ(aiVector3D(1, 0, 0), mesh->mAnimMeshes[0]->mVertices[1]);
    EXPECT_EQ(aiVector3D(1, 0, 0), mesh->mAnimMeshes[0]->mTextureCoords[3][1]);
    EXPECT_EQ(nullptr, mesh->mAnimMeshes[0]->mTextureCoords[0]);

    delete mesh;
}
