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
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include "PostProcessing/ProcessHelper.h"

using namespace Assimp;

namespace {

// ------------------------------------------------------------------------------------------------
// Build a mesh with three source vertices. Every vertex stream is given a
// value derived from the vertex index so a stale mapping is easy to spot.
aiMesh *makeSourceMesh() {
    const unsigned int kVertices = 3;

    aiMesh *mesh = new aiMesh();
    mesh->mNumVertices = kVertices;
    mesh->mVertices = new aiVector3D[kVertices];
    mesh->mNormals = new aiVector3D[kVertices];
    mesh->mTangents = new aiVector3D[kVertices];
    mesh->mBitangents = new aiVector3D[kVertices];
    mesh->mTextureCoords[0] = new aiVector3D[kVertices];
    mesh->mTextureCoords[2] = new aiVector3D[kVertices]; // sparse second channel
    mesh->mColors[0] = new aiColor4D[kVertices];
    mesh->mColors[1] = new aiColor4D[kVertices]; // sparse second set
    mesh->mNumUVComponents[0] = 2;
    mesh->mNumUVComponents[2] = 3;
    mesh->mMethod = aiMorphingMethod_MORPH_NORMALIZED;

    for (unsigned int i = 0; i < kVertices; ++i) {
        const ai_real v = static_cast<ai_real>(i);
        mesh->mVertices[i] = aiVector3D(v, v + 10, v + 20);
        mesh->mNormals[i] = aiVector3D(v + 30, v + 40, v + 50);
        mesh->mTangents[i] = aiVector3D(v + 60, v + 70, v + 80);
        mesh->mBitangents[i] = aiVector3D(v + 90, v + 100, v + 110);
        mesh->mTextureCoords[0][i] = aiVector3D(v + 120, v + 130, 0);
        mesh->mTextureCoords[2][i] = aiVector3D(v + 140, v + 150, v + 160);
        mesh->mColors[0][i] = aiColor4D(v + 170, v + 180, v + 190, v + 200);
        mesh->mColors[1][i] = aiColor4D(v + 210, v + 220, v + 230, v + 240);
    }

    // two bones: bone 0 influences vertex 0, bone 1 influences vertices 1 and 2
    mesh->mNumBones = 2;
    mesh->mBones = new aiBone *[2];

    aiBone *bone0 = mesh->mBones[0] = new aiBone();
    bone0->mName.Set("bone0");
    bone0->mNumWeights = 1;
    bone0->mWeights = new aiVertexWeight[1];
    bone0->mWeights[0] = aiVertexWeight(0, 0.75f);

    aiBone *bone1 = mesh->mBones[1] = new aiBone();
    bone1->mName.Set("bone1");
    bone1->mNumWeights = 2;
    bone1->mWeights = new aiVertexWeight[2];
    bone1->mWeights[0] = aiVertexWeight(1, 0.5f);
    bone1->mWeights[1] = aiVertexWeight(2, 0.25f);

    // one morph target with the same streams as the host mesh
    mesh->mNumAnimMeshes = 1;
    mesh->mAnimMeshes = new aiAnimMesh *[1];
    aiAnimMesh *anim = mesh->mAnimMeshes[0] = new aiAnimMesh();
    anim->mName.Set("target");
    anim->mNumVertices = kVertices;
    anim->mVertices = new aiVector3D[kVertices];
    anim->mNormals = new aiVector3D[kVertices];
    anim->mTextureCoords[2] = new aiVector3D[kVertices];
    anim->mColors[1] = new aiColor4D[kVertices];
    for (unsigned int i = 0; i < kVertices; ++i) {
        const ai_real v = static_cast<ai_real>(i);
        anim->mVertices[i] = aiVector3D(v + 1000, 0, 0);
        anim->mNormals[i] = aiVector3D(v + 2000, 0, 0);
        anim->mTextureCoords[2][i] = aiVector3D(v + 3000, 0, 0);
        anim->mColors[1][i] = aiColor4D(v + 4000, 0, 0, 0);
    }

    // a single triangle referencing all three vertices
    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3];
    mesh->mFaces[0].mIndices[0] = 0;
    mesh->mFaces[0].mIndices[1] = 1;
    mesh->mFaces[0].mIndices[2] = 2;

    return mesh;
}

// ------------------------------------------------------------------------------------------------
// Collect the weights of a bone into a lookup table so tests can assert on
// both ordering and values.
std::vector<ai_real> weightsByVertex(const aiBone *bone, unsigned int vertexCount) {
    std::vector<ai_real> weights(vertexCount, 0.f);
    for (unsigned int i = 0; i < bone->mNumWeights; ++i) {
        weights[bone->mWeights[i].mVertexId] = bone->mWeights[i].mWeight;
    }
    return weights;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// A vertex-duplicating mapping (0, 1, 2, 1, 1) must leave every attribute of
// the copies consistent: positions, all vertex streams, morph targets and bone
// weights.
TEST(utMeshVertexRemap, DuplicateVerticesRemapsAllAttributes) {
    aiMesh *mesh = makeSourceMesh();

    // new -> old
    std::vector<unsigned int> newToOld = { 0, 1, 2, 1, 1 };
    const MeshVertexRemap remap(mesh->mNumVertices, newToOld);
    remap.ApplyInPlace(mesh);

    ASSERT_EQ(5u, mesh->mNumVertices);

    // vertex streams
    const ai_real expectedOld[] = { 0, 1, 2, 1, 1 };
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const ai_real v = expectedOld[i];
        EXPECT_EQ(aiVector3D(v, v + 10, v + 20), mesh->mVertices[i]);
        EXPECT_EQ(aiVector3D(v + 30, v + 40, v + 50), mesh->mNormals[i]);
        EXPECT_EQ(aiVector3D(v + 60, v + 70, v + 80), mesh->mTangents[i]);
        EXPECT_EQ(aiVector3D(v + 90, v + 100, v + 110), mesh->mBitangents[i]);
        EXPECT_EQ(aiVector3D(v + 120, v + 130, 0), mesh->mTextureCoords[0][i]);
        EXPECT_EQ(aiVector3D(v + 140, v + 150, v + 160), mesh->mTextureCoords[2][i]);
        EXPECT_EQ(aiColor4D(v + 170, v + 180, v + 190, v + 200), mesh->mColors[0][i]);
        EXPECT_EQ(aiColor4D(v + 210, v + 220, v + 230, v + 240), mesh->mColors[1][i]);
    }

    // channels that were absent must not spring into existence
    EXPECT_EQ(nullptr, mesh->mTextureCoords[1]);
    EXPECT_EQ(nullptr, mesh->mColors[2]);

    // morph target follows the same mapping, including the sparse channels
    ASSERT_EQ(1u, mesh->mNumAnimMeshes);
    const aiAnimMesh *anim = mesh->mAnimMeshes[0];
    EXPECT_EQ(5u, anim->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const ai_real v = expectedOld[i];
        EXPECT_EQ(aiVector3D(v + 1000, 0, 0), anim->mVertices[i]);
        EXPECT_EQ(aiVector3D(v + 2000, 0, 0), anim->mNormals[i]);
        EXPECT_EQ(aiVector3D(v + 3000, 0, 0), anim->mTextureCoords[2][i]);
        EXPECT_EQ(aiColor4D(v + 4000, 0, 0, 0), anim->mColors[1][i]);
    }
    EXPECT_EQ(nullptr, anim->mTextureCoords[0]);

    // bone weights: every copy of vertex 1 receives bone1's influence
    ASSERT_EQ(2u, mesh->mNumBones);
    const std::vector<ai_real> bone0 = weightsByVertex(mesh->mBones[0], mesh->mNumVertices);
    const std::vector<ai_real> bone1 = weightsByVertex(mesh->mBones[1], mesh->mNumVertices);
    EXPECT_FLOAT_EQ(0.75f, bone0[0]);
    EXPECT_FLOAT_EQ(0.f, bone0[1]);
    EXPECT_FLOAT_EQ(0.5f, bone1[1]);
    EXPECT_FLOAT_EQ(0.5f, bone1[3]);
    EXPECT_FLOAT_EQ(0.5f, bone1[4]);
    EXPECT_FLOAT_EQ(0.25f, bone1[2]);

    // no weight may reference a vertex outside the result
    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
        for (unsigned int w = 0; w < mesh->mBones[b]->mNumWeights; ++w) {
            EXPECT_LT(mesh->mBones[b]->mWeights[w].mVertexId, mesh->mNumVertices);
        }
    }

    delete mesh;
}

// ------------------------------------------------------------------------------------------------
// A deduplicating mapping (0, 2) must drop the weights of vertices that were
// merged away instead of summing them onto the surviving vertex.
TEST(utMeshVertexRemap, CollapsedVerticesDoNotAccumulateWeights) {
    aiMesh *mesh = makeSourceMesh();

    std::vector<unsigned int> newToOld = { 0, 2 };
    const MeshVertexRemap remap(mesh->mNumVertices, newToOld);
    remap.ApplyInPlace(mesh);

    ASSERT_EQ(2u, mesh->mNumVertices);
    EXPECT_EQ(aiVector3D(0, 10, 20), mesh->mVertices[0]);
    EXPECT_EQ(aiVector3D(2, 12, 22), mesh->mVertices[1]);

    const std::vector<ai_real> bone0 = weightsByVertex(mesh->mBones[0], mesh->mNumVertices);
    const std::vector<ai_real> bone1 = weightsByVertex(mesh->mBones[1], mesh->mNumVertices);
    EXPECT_FLOAT_EQ(0.75f, bone0[0]);
    EXPECT_FLOAT_EQ(0.f, bone0[1]);
    EXPECT_FLOAT_EQ(0.f, bone1[0]);
    EXPECT_FLOAT_EQ(0.25f, bone1[1]);

    delete mesh;
}

// ------------------------------------------------------------------------------------------------
// CopyTo must produce an independent mesh: every stream is remapped, missing
// streams stay missing and the source is untouched.
TEST(utMeshVertexRemap, CopyToProducesAlignedIndependentMesh) {
    aiMesh *source = makeSourceMesh();

    std::vector<unsigned int> newToOld = { 2, 2, 0, 1 };
    const MeshVertexRemap remap(source->mNumVertices, newToOld);

    aiMesh *dest = new aiMesh();
    remap.CopyTo(dest, source);

    ASSERT_EQ(4u, dest->mNumVertices);
    EXPECT_EQ(aiMorphingMethod_MORPH_NORMALIZED, dest->mMethod);
    EXPECT_EQ(2u, dest->mNumUVComponents[0]);
    EXPECT_EQ(3u, dest->mNumUVComponents[2]);

    const ai_real expectedOld[] = { 2, 2, 0, 1 };
    for (unsigned int i = 0; i < dest->mNumVertices; ++i) {
        const ai_real v = expectedOld[i];
        EXPECT_EQ(aiVector3D(v, v + 10, v + 20), dest->mVertices[i]);
        EXPECT_EQ(aiVector3D(v + 30, v + 40, v + 50), dest->mNormals[i]);
        EXPECT_EQ(aiVector3D(v + 120, v + 130, 0), dest->mTextureCoords[0][i]);
        EXPECT_EQ(aiVector3D(v + 140, v + 150, v + 160), dest->mTextureCoords[2][i]);
        EXPECT_EQ(aiColor4D(v + 210, v + 220, v + 230, v + 240), dest->mColors[1][i]);
    }
    EXPECT_EQ(nullptr, dest->mTextureCoords[1]);

    ASSERT_EQ(1u, dest->mNumAnimMeshes);
    ASSERT_EQ(4u, dest->mAnimMeshes[0]->mNumVertices);
    EXPECT_EQ(aiVector3D(2 + 1000, 0, 0), dest->mAnimMeshes[0]->mVertices[0]);
    EXPECT_EQ(aiVector3D(2 + 1000, 0, 0), dest->mAnimMeshes[0]->mVertices[1]);
    EXPECT_EQ(aiVector3D(0 + 1000, 0, 0), dest->mAnimMeshes[0]->mVertices[2]);

    ASSERT_EQ(2u, dest->mNumBones);
    const std::vector<ai_real> bone1 = weightsByVertex(dest->mBones[1], dest->mNumVertices);
    EXPECT_FLOAT_EQ(0.25f, bone1[0]);
    EXPECT_FLOAT_EQ(0.25f, bone1[1]);
    EXPECT_FLOAT_EQ(0.5f, bone1[3]);

    // the source mesh must still be intact
    EXPECT_EQ(3u, source->mNumVertices);
    EXPECT_EQ(aiVector3D(1, 11, 21), source->mVertices[1]);
    EXPECT_EQ(2u, source->mBones[1]->mNumWeights);

    delete dest;
    delete source;
}

// ------------------------------------------------------------------------------------------------
// A morph target with a vertex count that does not match the mesh cannot be
// remapped; it must be copied without data instead of reading out of bounds.
TEST(utMeshVertexRemap, MismatchedMorphTargetIsNotIndexedOutOfBounds) {
    aiMesh *source = makeSourceMesh();
    source->mAnimMeshes[0]->mNumVertices = 1;
    delete[] source->mAnimMeshes[0]->mVertices;
    source->mAnimMeshes[0]->mVertices = new aiVector3D[1];

    std::vector<unsigned int> newToOld = { 2, 0 };
    const MeshVertexRemap remap(source->mNumVertices, newToOld);

    aiMesh *dest = new aiMesh();
    remap.CopyTo(dest, source);

    ASSERT_EQ(2u, dest->mNumVertices);
    EXPECT_EQ(aiVector3D(2, 12, 22), dest->mVertices[0]);
    EXPECT_EQ(aiVector3D(0, 10, 20), dest->mVertices[1]);

    // The mismatched target is kept as an empty descriptor.
    ASSERT_EQ(1u, dest->mNumAnimMeshes);
    EXPECT_EQ(0u, dest->mAnimMeshes[0]->mNumVertices);
    EXPECT_EQ(nullptr, dest->mAnimMeshes[0]->mVertices);

    delete dest;
    delete source;
}

// ------------------------------------------------------------------------------------------------
// The identity mapping must not change or reallocate anything.
TEST(utMeshVertexRemap, IdentityMappingIsNoOp) {
    aiMesh *mesh = makeSourceMesh();
    aiVector3D *positions = mesh->mVertices;
    aiVertexWeight *weights = mesh->mBones[0]->mWeights;

    std::vector<unsigned int> newToOld = { 0, 1, 2 };
    const MeshVertexRemap remap(mesh->mNumVertices, newToOld);
    ASSERT_TRUE(remap.IsIdentity());
    remap.ApplyInPlace(mesh);

    EXPECT_EQ(positions, mesh->mVertices);
    EXPECT_EQ(weights, mesh->mBones[0]->mWeights);
    EXPECT_EQ(3u, mesh->mNumVertices);

    delete mesh;
}

// ------------------------------------------------------------------------------------------------
// MakeSubmesh is used by the debone step. Besides positions, UVs, colors and
// bones it must also carry the morph targets over to the submesh.
TEST(utMeshVertexRemap, MakeSubmeshCarriesMorphTargets) {
    aiMesh *source = makeSourceMesh();
    std::vector<unsigned int> subFaces = { 0 };

    aiMesh *sub = MakeSubmesh(source, subFaces, 0);

    ASSERT_EQ(3u, sub->mNumVertices);
    ASSERT_EQ(1u, sub->mNumAnimMeshes);
    const aiAnimMesh *anim = sub->mAnimMeshes[0];
    ASSERT_EQ(3u, anim->mNumVertices);
    for (unsigned int i = 0; i < sub->mNumVertices; ++i) {
        const ai_real p = sub->mVertices[i].x;
        EXPECT_EQ(aiVector3D(p + 1000, 0, 0), anim->mVertices[i]);
        EXPECT_EQ(aiVector3D(p + 3000, 0, 0), anim->mTextureCoords[2][i]);
        EXPECT_EQ(aiColor4D(p + 4000, 0, 0, 0), anim->mColors[1][i]);
    }

    // bones are copied when not suppressed
    ASSERT_EQ(2u, sub->mNumBones);
    for (unsigned int b = 0; b < sub->mNumBones; ++b) {
        for (unsigned int w = 0; w < sub->mBones[b]->mNumWeights; ++w) {
            EXPECT_LT(sub->mBones[b]->mWeights[w].mVertexId, sub->mNumVertices);
        }
    }

    delete sub;
    delete source;
}
