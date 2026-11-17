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

/// @file ProcessHelper.cpp
/** Implement shared utility functions for postprocessing steps */

#include "ProcessHelper.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace Assimp {

namespace {

// -------------------------------------------------------------------------------
// Rebuild a per-vertex array in place using a new-index -> source-index mapping.
// Entries whose source index is out of range are filled with a default value.
template <typename T>
void RemapArrayInPlace(T *&data, unsigned int sourceCount,
        const std::vector<unsigned int> &newToOld, const std::vector<bool> &newToOldValid) {
    if (data == nullptr) {
        return;
    }

    std::unique_ptr<T[]> old(data);
    T *remapped = new T[newToOld.size()];
    for (size_t i = 0; i < newToOld.size(); ++i) {
        if (newToOldValid[i] && newToOld[i] < sourceCount) {
            remapped[i] = old[newToOld[i]];
        } else {
            remapped[i] = T();
        }
    }
    data = remapped;
}

// -------------------------------------------------------------------------------
// Create a remapped copy of a per-vertex array. A missing source stream stays
// missing.
template <typename T>
void CopyRemappedArray(T *&dest, const T *src, unsigned int sourceCount,
        const std::vector<unsigned int> &newToOld, const std::vector<bool> &newToOldValid) {
    dest = nullptr;
    if (src == nullptr) {
        return;
    }

    T *remapped = new T[newToOld.size()];
    for (size_t i = 0; i < newToOld.size(); ++i) {
        if (newToOldValid[i] && newToOld[i] < sourceCount) {
            remapped[i] = src[newToOld[i]];
        } else {
            remapped[i] = T();
        }
    }
    dest = remapped;
}

// -------------------------------------------------------------------------------
// Expand the weights of a single bone so that every copy of a source vertex
// created by the remap receives the influence. The relative order of the source
// weights is preserved, and the copies of one source vertex are emitted in
// ascending new-index order.
void ExpandBoneWeights(const aiBone *src, std::vector<aiVertexWeight> &out,
        unsigned int oldVertexCount,
        const std::vector<unsigned int> &oldToNewStart,
        const std::vector<unsigned int> &oldToNewIndices) {
    out.clear();
    if (src == nullptr || src->mWeights == nullptr) {
        return;
    }

    out.reserve(src->mNumWeights);
    for (unsigned int w = 0; w < src->mNumWeights; ++w) {
        const unsigned int oldId = src->mWeights[w].mVertexId;
        if (oldId >= oldVertexCount) {
            ASSIMP_LOG_WARN("MeshVertexRemap: bone weight references vertex ", oldId,
                    " but the mesh only has ", oldVertexCount, " vertices; weight dropped.");
            continue;
        }
        for (unsigned int k = oldToNewStart[oldId]; k < oldToNewStart[oldId + 1]; ++k) {
            out.emplace_back(oldToNewIndices[k], src->mWeights[w].mWeight);
        }
    }
}

} // namespace

// -------------------------------------------------------------------------------
MeshVertexRemap::MeshVertexRemap(unsigned int oldVertexCount, std::vector<unsigned int> newToOld) :
        mOldVertexCount(oldVertexCount),
        mNewToOld(std::move(newToOld)) {
    mNewToOldValid.resize(mNewToOld.size());
    mIsIdentity = mNewToOld.size() == mOldVertexCount;
    for (size_t i = 0; i < mNewToOld.size(); ++i) {
        const bool valid = mNewToOld[i] < mOldVertexCount;
        mNewToOldValid[i] = valid;
        if (!valid || mNewToOld[i] != i) {
            mIsIdentity = false;
        }
    }

    // Build an old-index -> new-indices list as CSR so that bone weights can
    // be expanded without scanning the whole mapping for every weight.
    mOldToNewStart.assign(static_cast<size_t>(mOldVertexCount) + 1, 0);
    size_t validCount = 0;
    for (size_t i = 0; i < mNewToOld.size(); ++i) {
        if (mNewToOldValid[i]) {
            ++mOldToNewStart[mNewToOld[i] + 1];
            ++validCount;
        }
    }
    for (unsigned int i = 0; i < mOldVertexCount; ++i) {
        mOldToNewStart[i + 1] += mOldToNewStart[i];
    }

    mOldToNewIndices.resize(validCount);
    std::vector<unsigned int> cursor(mOldToNewStart.begin(), mOldToNewStart.end() - 1);
    for (size_t i = 0; i < mNewToOld.size(); ++i) {
        if (mNewToOldValid[i]) {
            mOldToNewIndices[cursor[mNewToOld[i]]++] = static_cast<unsigned int>(i);
        }
    }
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::ApplyInPlace(aiMesh *mesh) const {
    if (mesh == nullptr || mIsIdentity) {
        return;
    }
    if (mesh->mNumVertices != mOldVertexCount) {
        ASSIMP_LOG_ERROR("MeshVertexRemap: mesh has ", mesh->mNumVertices,
                " vertices but the remap was built for ", mOldVertexCount, "; skipping remap.");
        return;
    }

    RemapVertexStreamsInPlace(mesh);

    for (unsigned int i = 0; i < mesh->mNumAnimMeshes; ++i) {
        RemapAnimMeshInPlace(mesh->mAnimMeshes[i]);
    }

    RemapBonesInPlace(mesh);
    mesh->mNumVertices = NewVertexCount();
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::RemapVertexStreamsInPlace(aiMesh *mesh) const {
    RemapArrayInPlace(mesh->mVertices, mOldVertexCount, mNewToOld, mNewToOldValid);
    RemapArrayInPlace(mesh->mNormals, mOldVertexCount, mNewToOld, mNewToOldValid);
    RemapArrayInPlace(mesh->mTangents, mOldVertexCount, mNewToOld, mNewToOldValid);
    RemapArrayInPlace(mesh->mBitangents, mOldVertexCount, mNewToOld, mNewToOldValid);

    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i) {
        RemapArrayInPlace(mesh->mColors[i], mOldVertexCount, mNewToOld, mNewToOldValid);
    }
    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
        RemapArrayInPlace(mesh->mTextureCoords[i], mOldVertexCount, mNewToOld, mNewToOldValid);
    }
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::RemapAnimMeshInPlace(aiAnimMesh *animMesh) const {
    if (animMesh == nullptr) {
        return;
    }
    if (animMesh->mNumVertices != mOldVertexCount) {
        // A morph target must have exactly one entry per host vertex. Anything
        // else cannot be mapped without guessing; leave it untouched instead of
        // indexing out of bounds.
        ASSIMP_LOG_WARN("MeshVertexRemap: morph target \"", animMesh->mName.C_Str(),
                "\" has ", animMesh->mNumVertices, " vertices but the mesh has ",
                mOldVertexCount, "; morph target left untouched.");
        return;
    }

    const unsigned int count = animMesh->mNumVertices;
    RemapArrayInPlace(animMesh->mVertices, count, mNewToOld, mNewToOldValid);
    RemapArrayInPlace(animMesh->mNormals, count, mNewToOld, mNewToOldValid);
    RemapArrayInPlace(animMesh->mTangents, count, mNewToOld, mNewToOldValid);
    RemapArrayInPlace(animMesh->mBitangents, count, mNewToOld, mNewToOldValid);

    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i) {
        RemapArrayInPlace(animMesh->mColors[i], count, mNewToOld, mNewToOldValid);
    }
    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
        RemapArrayInPlace(animMesh->mTextureCoords[i], count, mNewToOld, mNewToOldValid);
    }

    animMesh->mNumVertices = NewVertexCount();
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::RemapBonesInPlace(aiMesh *mesh) const {
    if (mesh->mBones == nullptr) {
        return;
    }

    std::vector<aiVertexWeight> remappedWeights;
    for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
        aiBone *bone = mesh->mBones[i];
        if (bone == nullptr) {
            continue;
        }

        ExpandBoneWeights(bone, remappedWeights, mOldVertexCount, mOldToNewStart, mOldToNewIndices);

        delete[] bone->mWeights;
        bone->mWeights = nullptr;
        bone->mNumWeights = static_cast<unsigned int>(remappedWeights.size());
        if (!remappedWeights.empty()) {
            bone->mWeights = new aiVertexWeight[remappedWeights.size()];
            std::copy(remappedWeights.begin(), remappedWeights.end(), bone->mWeights);
        }
    }
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::CopyTo(aiMesh *dest, const aiMesh *src, bool copyBones) const {
    if (dest == nullptr || src == nullptr) {
        return;
    }
    if (src->mNumVertices != mOldVertexCount) {
        ASSIMP_LOG_ERROR("MeshVertexRemap: source mesh has ", src->mNumVertices,
                " vertices but the remap was built for ", mOldVertexCount, "; copying skipped.");
        return;
    }

    CopyVertexStreams(dest, src);
    if (copyBones) {
        CopyBones(dest, src);
    } else {
        dest->mNumBones = 0;
        dest->mBones = nullptr;
    }
    CopyAnimMeshes(dest, src);
    dest->mMethod = src->mMethod;
    dest->mNumVertices = NewVertexCount();

    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
        dest->mNumUVComponents[i] = src->mNumUVComponents[i];
    }

    dest->mTextureCoordsNames = nullptr;
    if (src->mTextureCoordsNames != nullptr) {
        dest->mTextureCoordsNames = new aiString *[AI_MAX_NUMBER_OF_TEXTURECOORDS]();
        for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
            if (src->mTextureCoordsNames[i] != nullptr) {
                dest->mTextureCoordsNames[i] = new aiString(*src->mTextureCoordsNames[i]);
            }
        }
    }
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::CopyAnimMeshesTo(aiMesh *dest, const aiMesh *src) const {
    if (dest == nullptr || src == nullptr) {
        return;
    }
    if (src->mNumVertices != mOldVertexCount) {
        ASSIMP_LOG_ERROR("MeshVertexRemap: source mesh has ", src->mNumVertices,
                " vertices but the remap was built for ", mOldVertexCount, "; morph targets skipped.");
        return;
    }
    CopyAnimMeshes(dest, src);
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::CopyVertexStreams(aiMesh *dest, const aiMesh *src) const {
    CopyRemappedArray(dest->mVertices, src->mVertices, mOldVertexCount, mNewToOld, mNewToOldValid);
    CopyRemappedArray(dest->mNormals, src->mNormals, mOldVertexCount, mNewToOld, mNewToOldValid);
    CopyRemappedArray(dest->mTangents, src->mTangents, mOldVertexCount, mNewToOld, mNewToOldValid);
    CopyRemappedArray(dest->mBitangents, src->mBitangents, mOldVertexCount, mNewToOld, mNewToOldValid);

    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i) {
        CopyRemappedArray(dest->mColors[i], src->mColors[i], mOldVertexCount, mNewToOld, mNewToOldValid);
    }
    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
        CopyRemappedArray(dest->mTextureCoords[i], src->mTextureCoords[i], mOldVertexCount, mNewToOld, mNewToOldValid);
    }
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::CopyBones(aiMesh *dest, const aiMesh *src) const {
    dest->mNumBones = 0;
    dest->mBones = nullptr;
    if (src->mBones == nullptr || src->mNumBones == 0) {
        return;
    }

    std::vector<aiBone *> bones;
    bones.reserve(src->mNumBones);

    std::vector<aiVertexWeight> remappedWeights;
    for (unsigned int i = 0; i < src->mNumBones; ++i) {
        const aiBone *srcBone = src->mBones[i];
        if (srcBone == nullptr) {
            continue;
        }

        ExpandBoneWeights(srcBone, remappedWeights, mOldVertexCount, mOldToNewStart, mOldToNewIndices);
        if (remappedWeights.empty()) {
            // The bone has no influence on any vertex of the result.
            continue;
        }

        aiBone *dstBone = new aiBone();
        dstBone->mName = srcBone->mName;
        dstBone->mOffsetMatrix = srcBone->mOffsetMatrix;
        dstBone->mNumWeights = static_cast<unsigned int>(remappedWeights.size());
        dstBone->mWeights = new aiVertexWeight[dstBone->mNumWeights];
        std::copy(remappedWeights.begin(), remappedWeights.end(), dstBone->mWeights);
        bones.push_back(dstBone);
    }

    if (bones.empty()) {
        return;
    }

    dest->mNumBones = static_cast<unsigned int>(bones.size());
    dest->mBones = new aiBone *[dest->mNumBones];
    std::copy(bones.begin(), bones.end(), dest->mBones);
}

// -------------------------------------------------------------------------------
void MeshVertexRemap::CopyAnimMeshes(aiMesh *dest, const aiMesh *src) const {
    dest->mNumAnimMeshes = 0;
    dest->mAnimMeshes = nullptr;
    if (src->mAnimMeshes == nullptr || src->mNumAnimMeshes == 0) {
        return;
    }

    aiAnimMesh **out = new aiAnimMesh *[src->mNumAnimMeshes]();
    unsigned int outCount = 0;
    for (unsigned int i = 0; i < src->mNumAnimMeshes; ++i) {
        const aiAnimMesh *srcAnim = src->mAnimMeshes[i];
        if (srcAnim == nullptr) {
            continue;
        }

        aiAnimMesh *dstAnim = new aiAnimMesh();
        dstAnim->mName = srcAnim->mName;
        dstAnim->mWeight = srcAnim->mWeight;

        if (srcAnim->mNumVertices == mOldVertexCount) {
            const unsigned int count = srcAnim->mNumVertices;
            CopyRemappedArray(dstAnim->mVertices, srcAnim->mVertices, count, mNewToOld, mNewToOldValid);
            CopyRemappedArray(dstAnim->mNormals, srcAnim->mNormals, count, mNewToOld, mNewToOldValid);
            CopyRemappedArray(dstAnim->mTangents, srcAnim->mTangents, count, mNewToOld, mNewToOldValid);
            CopyRemappedArray(dstAnim->mBitangents, srcAnim->mBitangents, count, mNewToOld, mNewToOldValid);
            for (unsigned int c = 0; c < AI_MAX_NUMBER_OF_COLOR_SETS; ++c) {
                CopyRemappedArray(dstAnim->mColors[c], srcAnim->mColors[c], count, mNewToOld, mNewToOldValid);
            }
            for (unsigned int c = 0; c < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++c) {
                CopyRemappedArray(dstAnim->mTextureCoords[c], srcAnim->mTextureCoords[c], count, mNewToOld, mNewToOldValid);
            }
            dstAnim->mNumVertices = NewVertexCount();
        } else {
            // Mismatched morph target: keep the descriptor but no data rather
            // than reading out of bounds.
            ASSIMP_LOG_WARN("MeshVertexRemap: morph target \"", srcAnim->mName.C_Str(),
                    "\" has ", srcAnim->mNumVertices, " vertices but the mesh has ",
                    mOldVertexCount, "; morph target data dropped.");
            dstAnim->mNumVertices = 0;
        }

        out[outCount++] = dstAnim;
    }

    if (outCount == 0) {
        delete[] out;
        return;
    }

    dest->mNumAnimMeshes = outCount;
    dest->mAnimMeshes = out;
}

// -------------------------------------------------------------------------------
void ConvertListToStrings(const std::string &in, std::list<std::string> &out) {
    const char *s = in.c_str();
    const char *end = in.c_str() + in.size();
    while (*s) {
        SkipSpacesAndLineEnd(&s, end);
        if (*s == '\'') {
            const char *base = ++s;
            while (*s != '\'') {
                ++s;
                if (*s == '\0') {
                    ASSIMP_LOG_ERROR("ConvertListToString: String list is ill-formatted");
                    return;
                }
            }
            out.emplace_back(base, (size_t)(s - base));
            ++s;
        } else {
            out.push_back(GetNextToken(s, end));
        }
    }
}

// -------------------------------------------------------------------------------
void FindAABBTransformed(const aiMesh *mesh, aiVector3D &min, aiVector3D &max,
        const aiMatrix4x4 &m) {
    min = aiVector3D(ai_real(10e10), ai_real(10e10), ai_real(10e10));
    max = aiVector3D(ai_real(-10e10), ai_real(-10e10), ai_real(-10e10));
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D v = m * mesh->mVertices[i];
        min = std::min(v, min);
        max = std::max(v, max);
    }
}

// -------------------------------------------------------------------------------
void FindMeshCenter(aiMesh *mesh, aiVector3D &out, aiVector3D &min, aiVector3D &max) {
    ArrayBounds(mesh->mVertices, mesh->mNumVertices, min, max);
    out = min + (max - min) * (ai_real)0.5;
}

// -------------------------------------------------------------------------------
void FindSceneCenter(aiScene *scene, aiVector3D &out, aiVector3D &min, aiVector3D &max) {
    if (nullptr == scene) {
        return;
    }

    if (0 == scene->mNumMeshes) {
        return;
    }
    FindMeshCenter(scene->mMeshes[0], out, min, max);
    for (unsigned int i = 1; i < scene->mNumMeshes; ++i) {
        aiVector3D tout, tmin, tmax;
        FindMeshCenter(scene->mMeshes[i], tout, tmin, tmax);
        if (min[0] > tmin[0]) min[0] = tmin[0];
        if (min[1] > tmin[1]) min[1] = tmin[1];
        if (min[2] > tmin[2]) min[2] = tmin[2];
        if (max[0] < tmax[0]) max[0] = tmax[0];
        if (max[1] < tmax[1]) max[1] = tmax[1];
        if (max[2] < tmax[2]) max[2] = tmax[2];
    }
    out = min + (max - min) * (ai_real)0.5;
}

// -------------------------------------------------------------------------------
void FindMeshCenterTransformed(aiMesh *mesh, aiVector3D &out, aiVector3D &min,
        aiVector3D &max, const aiMatrix4x4 &m) {
    FindAABBTransformed(mesh, min, max, m);
    out = min + (max - min) * (ai_real)0.5;
}

// -------------------------------------------------------------------------------
void FindMeshCenter(aiMesh *mesh, aiVector3D &out) {
    aiVector3D min, max;
    FindMeshCenter(mesh, out, min, max);
}

// -------------------------------------------------------------------------------
void FindMeshCenterTransformed(aiMesh *mesh, aiVector3D &out,
        const aiMatrix4x4 &m) {
    aiVector3D min, max;
    FindMeshCenterTransformed(mesh, out, min, max, m);
}

// -------------------------------------------------------------------------------
ai_real ComputePositionEpsilon(const aiMesh *pMesh) {
    const ai_real epsilon = ai_real(1e-4);

    // calculate the position bounds so we have a reliable epsilon to check position differences against
    aiVector3D minVec, maxVec;
    ArrayBounds(pMesh->mVertices, pMesh->mNumVertices, minVec, maxVec);
    return (maxVec - minVec).Length() * epsilon;
}

// -------------------------------------------------------------------------------
ai_real ComputePositionEpsilon(const aiMesh *const *pMeshes, size_t num) {
    ai_assert(nullptr != pMeshes);

    const ai_real epsilon = ai_real(1e-4);

    // calculate the position bounds so we have a reliable epsilon to check position differences against
    aiVector3D minVec, maxVec, mi, ma;
    MinMaxChooser<aiVector3D>()(minVec, maxVec);

    for (size_t a = 0; a < num; ++a) {
        const aiMesh *pMesh = pMeshes[a];
        ArrayBounds(pMesh->mVertices, pMesh->mNumVertices, mi, ma);

        minVec = std::min(minVec, mi);
        maxVec = std::max(maxVec, ma);
    }
    return (maxVec - minVec).Length() * epsilon;
}

// -------------------------------------------------------------------------------
unsigned int GetMeshVFormatUnique(const aiMesh *pcMesh) {
    ai_assert(nullptr != pcMesh);

    // FIX: the hash may never be 0. Otherwise a comparison against
    // nullptr could be successful
    unsigned int iRet = 1;

    // normals
    if (pcMesh->HasNormals()) iRet |= 0x2;
    // tangents and bitangents
    if (pcMesh->HasTangentsAndBitangents()) iRet |= 0x4;

    static_assert(8 >= AI_MAX_NUMBER_OF_COLOR_SETS, "static_assert(8 >= AI_MAX_NUMBER_OF_COLOR_SETS)");
    static_assert(8 >= AI_MAX_NUMBER_OF_TEXTURECOORDS, "static_assert(8 >= AI_MAX_NUMBER_OF_TEXTURECOORDS)");

    // texture coordinates
    unsigned int p = 0;
    while (pcMesh->HasTextureCoords(p)) {
        iRet |= (0x100 << p);
        if (3 == pcMesh->mNumUVComponents[p])
            iRet |= (0x10000 << p);

        ++p;
    }
    // vertex colors
    p = 0;
    while (pcMesh->HasVertexColors(p))
        iRet |= (0x1000000 << p++);
    return iRet;
}

// -------------------------------------------------------------------------------
VertexWeightTable *ComputeVertexBoneWeightTable(const aiMesh *pMesh) {
    if (!pMesh || !pMesh->mNumVertices || !pMesh->mNumBones) {
        return nullptr;
    }

    VertexWeightTable *avPerVertexWeights = new VertexWeightTable[pMesh->mNumVertices];
    for (unsigned int i = 0; i < pMesh->mNumBones; ++i) {

        aiBone *bone = pMesh->mBones[i];
        for (unsigned int a = 0; a < bone->mNumWeights; ++a) {
            const aiVertexWeight &weight = bone->mWeights[a];
            avPerVertexWeights[weight.mVertexId].emplace_back(i, weight.mWeight);
        }
    }
    return avPerVertexWeights;
}

// -------------------------------------------------------------------------------
const char *MappingTypeToString(aiTextureMapping in) {
    switch (in) {
    case aiTextureMapping_UV:
        return "UV";
    case aiTextureMapping_BOX:
        return "Box";
    case aiTextureMapping_SPHERE:
        return "Sphere";
    case aiTextureMapping_CYLINDER:
        return "Cylinder";
    case aiTextureMapping_PLANE:
        return "Plane";
    case aiTextureMapping_OTHER:
        return "Other";
    default:
        break;
    }

    ai_assert(false);
    return "BUG";
}

// -------------------------------------------------------------------------------
aiMesh *MakeSubmesh(const aiMesh *pMesh, const std::vector<unsigned int> &subMeshFaces, unsigned int subFlags) {
    aiMesh *oMesh = new aiMesh();
    std::vector<unsigned int> vMap(pMesh->mNumVertices, UINT_MAX);

    const size_t numSubFaces = subMeshFaces.size();

    // Build the new-index -> source-index mapping used by MeshVertexRemap.
    std::vector<unsigned int> newToOld;

    for (size_t i = 0; i < numSubFaces; i++) {
        const aiFace &f = pMesh->mFaces[subMeshFaces[i]];

        for (unsigned int j = 0; j < f.mNumIndices; j++) {
            if (vMap[f.mIndices[j]] == UINT_MAX) {
                vMap[f.mIndices[j]] = static_cast<unsigned int>(newToOld.size());
                newToOld.push_back(f.mIndices[j]);
            }
        }
    }

    const size_t numSubVerts = newToOld.size();

    oMesh->mName = pMesh->mName;

    oMesh->mMaterialIndex = pMesh->mMaterialIndex;
    oMesh->mPrimitiveTypes = pMesh->mPrimitiveTypes;

    // create all the arrays for this mesh if the old mesh contained them
    oMesh->mNumFaces = static_cast<unsigned int>(numSubFaces);

    // copy and remap all per-vertex data, morph targets and bone weights.
    // Positions used to be allocated unconditionally; keep doing so for callers
    // that rely on the array being present.
    const bool copyBones = (~subFlags & AI_SUBMESH_FLAGS_SANS_BONES) != 0;
    const MeshVertexRemap remap(pMesh->mNumVertices, newToOld);
    remap.CopyTo(oMesh, pMesh, copyBones);
    if (pMesh->mVertices == nullptr) {
        delete[] oMesh->mVertices;
        oMesh->mVertices = new aiVector3D[numSubVerts]();
    }

    // and copy over the data, generating faces with linear indices along the way
    oMesh->mFaces = new aiFace[numSubFaces];

    for (unsigned int a = 0; a < numSubFaces; ++a) {

        const aiFace &srcFace = pMesh->mFaces[subMeshFaces[a]];
        aiFace &dstFace = oMesh->mFaces[a];
        dstFace.mNumIndices = srcFace.mNumIndices;
        dstFace.mIndices = new unsigned int[dstFace.mNumIndices];

        // accumulate linearly all the vertices of the source face
        for (size_t b = 0; b < dstFace.mNumIndices; ++b) {
            dstFace.mIndices[b] = vMap[srcFace.mIndices[b]];
        }
    }

    return oMesh;
}

} // namespace Assimp
