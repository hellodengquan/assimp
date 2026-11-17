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

#include <assimp/anim.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "PostProcessing/GenFaceNormalsProcess.h"
#include "PostProcessing/GenVertexNormalsProcess.h"

using namespace ::std;
using namespace ::Assimp;

class GenNormalsTest : public ::testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();

protected:
    aiMesh *pcMesh;
    GenVertexNormalsProcess *piProcess;
};

// ------------------------------------------------------------------------------------------------
void GenNormalsTest::SetUp() {
    piProcess = new GenVertexNormalsProcess();
    pcMesh = new aiMesh();
    pcMesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    pcMesh->mNumFaces = 1;
    pcMesh->mFaces = new aiFace[1];
    pcMesh->mFaces[0].mIndices = new unsigned int[pcMesh->mFaces[0].mNumIndices = 3];
    pcMesh->mFaces[0].mIndices[0] = 0;
    pcMesh->mFaces[0].mIndices[1] = 1;
    pcMesh->mFaces[0].mIndices[2] = 1;
    pcMesh->mNumVertices = 3;
    pcMesh->mVertices = new aiVector3D[3];
    pcMesh->mVertices[0] = aiVector3D(0.0f, 1.0f, 6.0f);
    pcMesh->mVertices[1] = aiVector3D(2.0f, 3.0f, 1.0f);
    pcMesh->mVertices[2] = aiVector3D(3.0f, 2.0f, 4.0f);
}

// ------------------------------------------------------------------------------------------------
void GenNormalsTest::TearDown() {
    delete this->pcMesh;
    delete this->piProcess;
}

// ------------------------------------------------------------------------------------------------
TEST_F(GenNormalsTest, testSimpleTriangle) {
    piProcess->GenMeshVertexNormals(pcMesh, 0);
    EXPECT_TRUE(pcMesh->mNormals != nullptr);
}

// ------------------------------------------------------------------------------------------------
// Face normal generation duplicates every vertex that is shared by two faces.
// The duplicates must keep the UVs, colors, bone weights and morph targets of
// the vertex they were split from.
TEST(GenFaceNormalsTest, vertexSplittingRemapsAllAttributes) {
    GenFaceNormalsProcess process;
    ASSERT_TRUE(process.IsActive(aiProcess_GenNormals));

    aiMesh *mesh = new aiMesh();
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

    // Two triangles sharing vertex 0. Vertex 0 gets duplicated by the step.
    mesh->mNumVertices = 4;
    mesh->mVertices = new aiVector3D[4];
    mesh->mTextureCoords[0] = new aiVector3D[4];
    mesh->mTextureCoords[1] = new aiVector3D[4];
    mesh->mColors[0] = new aiColor4D[4];
    mesh->mNumUVComponents[0] = 2;
    mesh->mNumUVComponents[1] = 3;

    for (unsigned int i = 0; i < 4; ++i) {
        const ai_real v = static_cast<ai_real>(i);
        mesh->mVertices[i] = aiVector3D(v, 0, 0);
        mesh->mTextureCoords[0][i] = aiVector3D(v + 10, 0, 0);
        mesh->mTextureCoords[1][i] = aiVector3D(v + 20, 0, 0);
        mesh->mColors[0][i] = aiColor4D(v + 30, 0, 0, 1);
    }

    mesh->mNumBones = 1;
    mesh->mBones = new aiBone *[1];
    aiBone *bone = mesh->mBones[0] = new aiBone();
    bone->mWeights = new aiVertexWeight[2];
    bone->mNumWeights = 2;
    bone->mWeights[0] = aiVertexWeight(0, 0.6f);
    bone->mWeights[1] = aiVertexWeight(3, 0.4f);

    mesh->mNumAnimMeshes = 1;
    mesh->mAnimMeshes = new aiAnimMesh *[1];
    aiAnimMesh *anim = mesh->mAnimMeshes[0] = new aiAnimMesh();
    anim->mNumVertices = 4;
    anim->mVertices = new aiVector3D[4];
    anim->mTextureCoords[1] = new aiVector3D[4];
    for (unsigned int i = 0; i < 4; ++i) {
        anim->mVertices[i] = aiVector3D(static_cast<ai_real>(i) + 100, 0, 0);
        anim->mTextureCoords[1][i] = aiVector3D(static_cast<ai_real>(i) + 200, 0, 0);
    }

    mesh->mNumFaces = 2;
    mesh->mFaces = new aiFace[2];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3]{ 0, 1, 2 };
    mesh->mFaces[1].mNumIndices = 3;
    mesh->mFaces[1].mIndices = new unsigned int[3]{ 0, 2, 3 };

    aiScene scene;
    scene.mNumMeshes = 1;
    scene.mMeshes = new aiMesh *[1];
    scene.mMeshes[0] = mesh;

    process.Execute(&scene);

    // vertex 0 and vertex 2 are each referenced by both faces, so one copy of
    // each was appended
    ASSERT_EQ(6u, mesh->mNumVertices);
    EXPECT_EQ(6u, mesh->mAnimMeshes[0]->mNumVertices);

    // every duplicated vertex must carry the data of its source vertex. Face 0
    // keeps the original vertex 0, face 1 uses the copy at index 4.
    const unsigned int indices[2][3] = {
        { mesh->mFaces[0].mIndices[0], mesh->mFaces[0].mIndices[1], mesh->mFaces[0].mIndices[2] },
        { mesh->mFaces[1].mIndices[0], mesh->mFaces[1].mIndices[1], mesh->mFaces[1].mIndices[2] }
    };
    EXPECT_EQ(0u, indices[0][0]);
    EXPECT_EQ(4u, indices[1][0]);

    for (unsigned int f = 0; f < 2; ++f) {
        const unsigned int a = indices[f][0];
        EXPECT_EQ(aiVector3D(0, 0, 0), mesh->mVertices[a]);
        EXPECT_EQ(aiVector3D(10, 0, 0), mesh->mTextureCoords[0][a]);
        EXPECT_EQ(aiVector3D(20, 0, 0), mesh->mTextureCoords[1][a]);
        EXPECT_EQ(aiColor4D(30, 0, 0, 1), mesh->mColors[0][a]);
        EXPECT_EQ(aiVector3D(100, 0, 0), mesh->mAnimMeshes[0]->mVertices[a]);
        EXPECT_EQ(aiVector3D(200, 0, 0), mesh->mAnimMeshes[0]->mTextureCoords[1][a]);
    }

    // the bone weight of vertex 0 must now be split across both copies, and
    // every weight has to point at a valid vertex
    ASSERT_EQ(1u, mesh->mNumBones);
    float weightSum = 0.f;
    for (unsigned int w = 0; w < mesh->mBones[0]->mNumWeights; ++w) {
        const aiVertexWeight &vw = mesh->mBones[0]->mWeights[w];
        EXPECT_LT(vw.mVertexId, mesh->mNumVertices);
        if (vw.mVertexId == 0 || vw.mVertexId == 4) {
            EXPECT_FLOAT_EQ(0.6f, vw.mWeight);
        }
        weightSum += vw.mWeight;
    }
    EXPECT_FLOAT_EQ(1.6f, weightSum);
    EXPECT_EQ(nullptr, mesh->mTextureCoords[2]);

    // aiScene owns the mesh and the mesh array now.
}

// ------------------------------------------------------------------------------------------------
// With aiProcess_ForceGenNormals the existing normals are freed first, then
// line and point meshes are skipped. The freed pointer must not stay in the
// mesh, otherwise aiMesh deletes it a second time.
TEST(GenFaceNormalsTest, forceOnLineMeshDoesNotLeaveDanglingNormals) {
    GenFaceNormalsProcess process;
    ASSERT_TRUE(process.IsActive(aiProcess_GenNormals | aiProcess_ForceGenNormals));

    aiScene scene;
    scene.mNumMeshes = 1;
    scene.mMeshes = new aiMesh *[1];
    aiMesh *mesh = scene.mMeshes[0] = new aiMesh();
    mesh->mPrimitiveTypes = aiPrimitiveType_LINE;
    mesh->mNumVertices = 2;
    mesh->mVertices = new aiVector3D[2];
    mesh->mNormals = new aiVector3D[2];
    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 2;
    mesh->mFaces[0].mIndices = new unsigned int[2]{ 0, 1 };

    process.Execute(&scene);

    EXPECT_EQ(nullptr, mesh->mNormals);

    // aiScene owns the mesh and the mesh array now.
}
