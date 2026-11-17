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
#include <assimp/scene.h>

#include "PostProcessing/MakeVerboseFormat.h"

using namespace Assimp;

// ------------------------------------------------------------------------------------------------
// Converting a mesh to the verbose format gives every face index its own
// vertex. The vertex streams, the morph targets and the bone weights have to
// follow that renumbering.
TEST(utMakeVerboseFormat, remapsAllVertexAttributes) {
    aiMesh *mesh = new aiMesh();
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

    mesh->mNumVertices = 3;
    mesh->mVertices = new aiVector3D[3];
    mesh->mNormals = new aiVector3D[3];
    mesh->mTextureCoords[0] = new aiVector3D[3];
    mesh->mTextureCoords[2] = new aiVector3D[3];
    mesh->mColors[0] = new aiColor4D[3];
    mesh->mNumUVComponents[2] = 3;
    mesh->mMethod = aiMorphingMethod_VERTEX_BLEND;

    for (unsigned int i = 0; i < 3; ++i) {
        const ai_real v = static_cast<ai_real>(i);
        mesh->mVertices[i] = aiVector3D(v, 0, 0);
        mesh->mNormals[i] = aiVector3D(v + 10, 0, 0);
        mesh->mTextureCoords[0][i] = aiVector3D(v + 20, 0, 0);
        mesh->mTextureCoords[2][i] = aiVector3D(v + 30, 0, 0);
        mesh->mColors[0][i] = aiColor4D(v + 40, 0, 0, 1);
    }

    mesh->mNumBones = 1;
    mesh->mBones = new aiBone *[1];
    aiBone *bone = mesh->mBones[0] = new aiBone();
    bone->mWeights = new aiVertexWeight[3];
    bone->mNumWeights = 3;
    for (unsigned int i = 0; i < 3; ++i) {
        bone->mWeights[i] = aiVertexWeight(i, 0.25f);
    }

    mesh->mNumAnimMeshes = 1;
    mesh->mAnimMeshes = new aiAnimMesh *[1];
    aiAnimMesh *anim = mesh->mAnimMeshes[0] = new aiAnimMesh();
    anim->mNumVertices = 3;
    anim->mVertices = new aiVector3D[3];
    anim->mTextureCoords[2] = new aiVector3D[3];
    for (unsigned int i = 0; i < 3; ++i) {
        anim->mVertices[i] = aiVector3D(static_cast<ai_real>(i) + 100, 0, 0);
        anim->mTextureCoords[2][i] = aiVector3D(static_cast<ai_real>(i) + 200, 0, 0);
    }

    // two triangles sharing vertices 0 and 1
    mesh->mNumFaces = 2;
    mesh->mFaces = new aiFace[2];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3]{ 0, 1, 2 };
    mesh->mFaces[1].mNumIndices = 3;
    mesh->mFaces[1].mIndices = new unsigned int[3]{ 2, 1, 0 };

    aiScene scene;
    scene.mNumMeshes = 1;
    scene.mMeshes = new aiMesh *[1];
    scene.mMeshes[0] = mesh;

    MakeVerboseFormatProcess process;
    process.Execute(&scene);

    ASSERT_EQ(6u, mesh->mNumVertices);
    EXPECT_EQ(aiMorphingMethod_VERTEX_BLEND, mesh->mMethod);
    EXPECT_EQ(3u, mesh->mNumUVComponents[2]);
    EXPECT_EQ(nullptr, mesh->mTextureCoords[1]);

    // Every face index is unique and the value streams agree with the position.
    std::vector<bool> seen(mesh->mNumVertices, false);
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        for (unsigned int v = 0; v < mesh->mFaces[f].mNumIndices; ++v) {
            const unsigned int idx = mesh->mFaces[f].mIndices[v];
            ASSERT_LT(idx, mesh->mNumVertices);
            EXPECT_FALSE(seen[idx]);
            seen[idx] = true;

            const ai_real p = mesh->mVertices[idx].x;
            EXPECT_EQ(aiVector3D(p + 10, 0, 0), mesh->mNormals[idx]);
            EXPECT_EQ(aiVector3D(p + 20, 0, 0), mesh->mTextureCoords[0][idx]);
            EXPECT_EQ(aiVector3D(p + 30, 0, 0), mesh->mTextureCoords[2][idx]);
            EXPECT_EQ(aiColor4D(p + 40, 0, 0, 1), mesh->mColors[0][idx]);

            ASSERT_EQ(6u, mesh->mAnimMeshes[0]->mNumVertices);
            EXPECT_EQ(aiVector3D(p + 100, 0, 0), mesh->mAnimMeshes[0]->mVertices[idx]);
            EXPECT_EQ(aiVector3D(p + 200, 0, 0), mesh->mAnimMeshes[0]->mTextureCoords[2][idx]);
        }
    }

    // each source vertex is referenced twice, so each of them gets two weights
    ASSERT_EQ(1u, mesh->mNumBones);
    EXPECT_EQ(6u, mesh->mBones[0]->mNumWeights);
    std::vector<unsigned int> weightCount(mesh->mNumVertices, 0);
    for (unsigned int w = 0; w < mesh->mBones[0]->mNumWeights; ++w) {
        const aiVertexWeight &vw = mesh->mBones[0]->mWeights[w];
        ASSERT_LT(vw.mVertexId, mesh->mNumVertices);
        EXPECT_FLOAT_EQ(0.25f, vw.mWeight);
        ++weightCount[vw.mVertexId];
    }
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        EXPECT_EQ(1u, weightCount[i]);
    }

    // aiScene owns the mesh and the mesh array.
}
