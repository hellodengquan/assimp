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
/** @file Implementation of the post processing step "MakeVerboseFormat"
*/

#include "MakeVerboseFormat.h"
#include "ProcessHelper.h"
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>

using namespace Assimp;

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void MakeVerboseFormatProcess::Execute(aiScene *pScene) {
    if (pScene == nullptr) {
        ASSIMP_LOG_ERROR("Nullptr to scene, aborted.");
        return;
    }
    ASSIMP_LOG_DEBUG("MakeVerboseFormatProcess begin");

    bool bHas = false;
    for (unsigned int a = 0; a < pScene->mNumMeshes; a++) {
        if (MakeVerboseFormat(pScene->mMeshes[a]))
            bHas = true;
    }
    if (bHas) {
        ASSIMP_LOG_INFO("MakeVerboseFormatProcess finished. There was much work to do ...");
    } else {
        ASSIMP_LOG_DEBUG("MakeVerboseFormatProcess. There was nothing to do.");
    }

    pScene->mFlags &= ~AI_SCENE_FLAGS_NON_VERBOSE_FORMAT;
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
bool MakeVerboseFormatProcess::MakeVerboseFormat(aiMesh *pcMesh) {
    ai_assert(nullptr != pcMesh);

    const unsigned int iOldNumVertices = pcMesh->mNumVertices;
    unsigned int iNumVerts = 0;
    for (unsigned int a = 0; a < pcMesh->mNumFaces; ++a) {
        iNumVerts += pcMesh->mFaces[a].mNumIndices;
    }

    // Build the new-index -> source-index mapping: every face index gets its
    // own vertex. Renumbering the faces and remapping all vertex streams, the
    // morph targets and the bone weights is then done by MeshVertexRemap so the
    // attribute data cannot drift out of sync with the positions.
    std::vector<unsigned int> newToOld;
    newToOld.reserve(iNumVerts);
    for (unsigned int a = 0; a < pcMesh->mNumFaces; ++a) {
        aiFace *pcFace = &pcMesh->mFaces[a];
        for (unsigned int q = 0; q < pcFace->mNumIndices; ++q) {
            newToOld.push_back(pcFace->mIndices[q]);
            pcFace->mIndices[q] = static_cast<unsigned int>(newToOld.size() - 1);
        }
    }

    const MeshVertexRemap remap(iOldNumVertices, newToOld);
    remap.ApplyInPlace(pcMesh);

    return (pcMesh->mNumVertices != iOldNumVertices);
}

// ------------------------------------------------------------------------------------------------
bool IsMeshInVerboseFormat(const aiMesh *mesh) {
    // avoid slow vector<bool> specialization
    std::vector<unsigned int> seen(mesh->mNumVertices, 0);
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace &f = mesh->mFaces[i];
        for (unsigned int j = 0; j < f.mNumIndices; ++j) {
            if (++seen[f.mIndices[j]] == 2) {
                // found a duplicate index
                return false;
            }
        }
    }

    return true;
}

// ------------------------------------------------------------------------------------------------
bool MakeVerboseFormatProcess::IsVerboseFormat(const aiScene *pScene) {
    for (unsigned int i = 0; i < pScene->mNumMeshes; ++i) {
        if (!IsMeshInVerboseFormat(pScene->mMeshes[i])) {
            return false;
        }
    }

    return true;
}
