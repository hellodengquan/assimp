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

#include "PostProcessing/SplitLargeMeshes.h"
#include <assimp/scene.h>

using namespace std;
using namespace Assimp;

class SplitLargeMeshesTest : public ::testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();

protected:
    SplitLargeMeshesProcess_Triangle *piProcessTriangle;
    SplitLargeMeshesProcess_Vertex *piProcessVertex;
};

// ------------------------------------------------------------------------------------------------
void SplitLargeMeshesTest::SetUp() {
    // construct the processes
    this->piProcessTriangle = new SplitLargeMeshesProcess_Triangle();
    this->piProcessVertex = new SplitLargeMeshesProcess_Vertex();

    this->piProcessTriangle->SetLimit(1000);
    this->piProcessVertex->SetLimit(1000);
}

// ------------------------------------------------------------------------------------------------
void SplitLargeMeshesTest::TearDown() {
    delete this->piProcessTriangle;
    delete this->piProcessVertex;
}

// ------------------------------------------------------------------------------------------------
TEST_F(SplitLargeMeshesTest, testVertexSplit) {
    std::vector<std::pair<aiMesh *, unsigned int>> avOut;

    aiMesh *pcMesh1 = new aiMesh();
    pcMesh1->mNumVertices = 2100; // quersumme: 3
    pcMesh1->mVertices = new aiVector3D[pcMesh1->mNumVertices];
    pcMesh1->mNormals = new aiVector3D[pcMesh1->mNumVertices];

    pcMesh1->mNumFaces = pcMesh1->mNumVertices / 3;
    pcMesh1->mFaces = new aiFace[pcMesh1->mNumFaces];

    unsigned int qq = 0;
    for (unsigned int i = 0; i < pcMesh1->mNumFaces; ++i) {
        aiFace &face = pcMesh1->mFaces[i];
        face.mNumIndices = 3;
        face.mIndices = new unsigned int[3];
        face.mIndices[0] = qq++;
        face.mIndices[1] = qq++;
        face.mIndices[2] = qq++;
    }

    int iOldFaceNum = (int)pcMesh1->mNumFaces;
    piProcessVertex->SplitMesh(0, pcMesh1, avOut);

    for (std::vector<std::pair<aiMesh *, unsigned int>>::const_iterator
                    iter = avOut.begin(),
                    end = avOut.end();
            iter != end; ++iter) {
        aiMesh *mesh = (*iter).first;
        EXPECT_LT(mesh->mNumVertices, 1000U);
        EXPECT_TRUE(nullptr != mesh->mNormals);
        EXPECT_TRUE(nullptr != mesh->mVertices);

        iOldFaceNum -= mesh->mNumFaces;
        delete mesh;
    }
    EXPECT_EQ(0, iOldFaceNum);
}

// ------------------------------------------------------------------------------------------------
TEST_F(SplitLargeMeshesTest, testTriangleSplit) {
    std::vector<std::pair<aiMesh *, unsigned int>> avOut;

    // generate many, many faces with randomized indices for
    // the second mesh
    aiMesh *pcMesh2 = new aiMesh();
    pcMesh2->mNumVertices = 3000;
    pcMesh2->mVertices = new aiVector3D[pcMesh2->mNumVertices];
    pcMesh2->mNormals = new aiVector3D[pcMesh2->mNumVertices];

    pcMesh2->mNumFaces = 10000;
    pcMesh2->mFaces = new aiFace[pcMesh2->mNumFaces];

    for (unsigned int i = 0; i < pcMesh2->mNumFaces; ++i) {
        aiFace &face = pcMesh2->mFaces[i];
        face.mNumIndices = 3;
        face.mIndices = new unsigned int[3];
        face.mIndices[0] = (unsigned int)((rand() / (float)RAND_MAX) * pcMesh2->mNumVertices);
        face.mIndices[1] = (unsigned int)((rand() / (float)RAND_MAX) * pcMesh2->mNumVertices);
        face.mIndices[2] = (unsigned int)((rand() / (float)RAND_MAX) * pcMesh2->mNumVertices);
    }

    // the number of faces shouldn't change
    int iOldFaceNum = (int)pcMesh2->mNumFaces;
    piProcessTriangle->SplitMesh(0, pcMesh2, avOut);

    for (std::vector<std::pair<aiMesh *, unsigned int>>::const_iterator
                    iter = avOut.begin(),
                    end = avOut.end();
            iter != end; ++iter) {
        aiMesh *mesh = (*iter).first;
        EXPECT_LT(mesh->mNumFaces, 1000U);
        EXPECT_TRUE(nullptr != mesh->mNormals);
        EXPECT_TRUE(nullptr != mesh->mVertices);

        iOldFaceNum -= mesh->mNumFaces;
        delete mesh;
    }
    EXPECT_EQ(0, iOldFaceNum);
}

// ------------------------------------------------------------------------------------------------
// Splitting must keep bones and morph targets aligned with the new vertices in
// both the triangle-limited and the vertex-limited variants.
namespace {

aiMesh *makeSplitSourceMesh() {
    const unsigned int kVertices = 4;
    aiMesh *mesh = new aiMesh();
    mesh->mNumVertices = kVertices;
    mesh->mVertices = new aiVector3D[kVertices];
    mesh->mTextureCoords[0] = new aiVector3D[kVertices];
    mesh->mTextureCoords[2] = new aiVector3D[kVertices];
    mesh->mColors[0] = new aiColor4D[kVertices];
    mesh->mNumUVComponents[2] = 3;
    mesh->mMethod = aiMorphingMethod_MORPH_RELATIVE;

    for (unsigned int i = 0; i < kVertices; ++i) {
        const ai_real v = static_cast<ai_real>(i);
        mesh->mVertices[i] = aiVector3D(v, 0, 0);
        mesh->mTextureCoords[0][i] = aiVector3D(v + 10, 0, 0);
        mesh->mTextureCoords[2][i] = aiVector3D(v + 20, 0, 0);
        mesh->mColors[0][i] = aiColor4D(v + 30, 0, 0, 1);
    }

    mesh->mNumBones = 1;
    mesh->mBones = new aiBone *[1];
    aiBone *bone = mesh->mBones[0] = new aiBone();
    bone->mWeights = new aiVertexWeight[kVertices];
    bone->mNumWeights = kVertices;
    for (unsigned int i = 0; i < kVertices; ++i) {
        bone->mWeights[i] = aiVertexWeight(i, 0.25f);
    }

    mesh->mNumAnimMeshes = 1;
    mesh->mAnimMeshes = new aiAnimMesh *[1];
    aiAnimMesh *anim = mesh->mAnimMeshes[0] = new aiAnimMesh();
    anim->mNumVertices = kVertices;
    anim->mVertices = new aiVector3D[kVertices];
    anim->mTextureCoords[2] = new aiVector3D[kVertices];
    for (unsigned int i = 0; i < kVertices; ++i) {
        anim->mVertices[i] = aiVector3D(static_cast<ai_real>(i) + 100, 0, 0);
        anim->mTextureCoords[2][i] = aiVector3D(static_cast<ai_real>(i) + 200, 0, 0);
    }

    mesh->mNumFaces = 4;
    mesh->mFaces = new aiFace[4];
    static const unsigned int kFaceIndices[4][3] = {
        { 0, 1, 2 }, { 2, 1, 3 }, { 0, 2, 1 }, { 1, 2, 3 }
    };
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        mesh->mFaces[f].mNumIndices = 3;
        mesh->mFaces[f].mIndices = new unsigned int[3];
        for (unsigned int v = 0; v < 3; ++v) {
            mesh->mFaces[f].mIndices[v] = kFaceIndices[f][v];
        }
    }

    return mesh;
}

// ------------------------------------------------------------------------------------------------
// Every index of a split mesh must address position, UV, color, morph and bone
// data that all belong to the same source vertex.
void verifySplitMeshIsConsistent(const aiMesh *mesh) {
    ASSERT_NE(nullptr, mesh->mVertices);
    ASSERT_NE(nullptr, mesh->mTextureCoords[0]);
    ASSERT_NE(nullptr, mesh->mTextureCoords[2]);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const ai_real v = mesh->mVertices[i].x;
        EXPECT_EQ(aiVector3D(v + 10, 0, 0), mesh->mTextureCoords[0][i]);
        EXPECT_EQ(aiVector3D(v + 20, 0, 0), mesh->mTextureCoords[2][i]);
        EXPECT_EQ(aiColor4D(v + 30, 0, 0, 1), mesh->mColors[0][i]);
    }
    EXPECT_EQ(nullptr, mesh->mTextureCoords[1]);

    ASSERT_EQ(1u, mesh->mNumAnimMeshes);
    ASSERT_EQ(mesh->mNumVertices, mesh->mAnimMeshes[0]->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const ai_real v = mesh->mVertices[i].x;
        EXPECT_EQ(aiVector3D(v + 100, 0, 0), mesh->mAnimMeshes[0]->mVertices[i]);
        EXPECT_EQ(aiVector3D(v + 200, 0, 0), mesh->mAnimMeshes[0]->mTextureCoords[2][i]);
    }

    ASSERT_EQ(1u, mesh->mNumBones);
    for (unsigned int w = 0; w < mesh->mBones[0]->mNumWeights; ++w) {
        const aiVertexWeight &vw = mesh->mBones[0]->mWeights[w];
        ASSERT_LT(vw.mVertexId, mesh->mNumVertices);
        EXPECT_FLOAT_EQ(0.25f, vw.mWeight);
    }

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        for (unsigned int v = 0; v < mesh->mFaces[f].mNumIndices; ++v) {
            EXPECT_LT(mesh->mFaces[f].mIndices[v], mesh->mNumVertices);
        }
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------
TEST_F(SplitLargeMeshesTest, triangleSplitKeepsAttributesAligned) {
    std::vector<std::pair<aiMesh *, unsigned int>> avOut;
    piProcessTriangle->SetLimit(2);

    aiMesh *mesh = makeSplitSourceMesh();
    piProcessTriangle->SplitMesh(0, mesh, avOut);

    EXPECT_GT(avOut.size(), 1u);
    for (const auto &entry : avOut) {
        verifySplitMeshIsConsistent(entry.first);
        delete entry.first;
    }
}

// ------------------------------------------------------------------------------------------------
TEST_F(SplitLargeMeshesTest, vertexSplitKeepsAttributesAligned) {
    std::vector<std::pair<aiMesh *, unsigned int>> avOut;
    piProcessVertex->SetLimit(3);

    aiMesh *mesh = makeSplitSourceMesh();
    piProcessVertex->SplitMesh(0, mesh, avOut);

    EXPECT_GT(avOut.size(), 1u);
    for (const auto &entry : avOut) {
        verifySplitMeshIsConsistent(entry.first);
        delete entry.first;
    }
}
