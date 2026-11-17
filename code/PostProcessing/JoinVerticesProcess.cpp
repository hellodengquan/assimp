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

/** @file Implementation of the post processing step to join identical vertices
 * for all imported meshes
 */

#ifndef ASSIMP_BUILD_NO_JOINVERTICES_PROCESS

#include "JoinVerticesProcess.h"
#include "ProcessHelper.h"
#include <assimp/Vertex.h>
#include <assimp/TinyFormatter.h>

#include <stdio.h>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <map>

using namespace Assimp;

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool JoinVerticesProcess::IsActive( unsigned int pFlags) const {
    return (pFlags & aiProcess_JoinIdenticalVertices) != 0;
}
// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void JoinVerticesProcess::Execute( aiScene* pScene) {
    ASSIMP_LOG_DEBUG("JoinVerticesProcess begin");

    // get the total number of vertices BEFORE the step is executed
    int iNumOldVertices = 0;
    if (!DefaultLogger::isNullLogger()) {
        for( unsigned int a = 0; a < pScene->mNumMeshes; a++)   {
            iNumOldVertices +=  pScene->mMeshes[a]->mNumVertices;
        }
    }

    // execute the step
    int iNumVertices = 0;
    for( unsigned int a = 0; a < pScene->mNumMeshes; a++) {
        iNumVertices += ProcessMesh( pScene->mMeshes[a],a);
    }

    pScene->mFlags |= AI_SCENE_FLAGS_NON_VERBOSE_FORMAT;

    // if logging is active, print detailed statistics
    if (!DefaultLogger::isNullLogger()) {
        if (iNumOldVertices == iNumVertices) {
            ASSIMP_LOG_DEBUG("JoinVerticesProcess finished ");
            return;
        }

        // Show statistics
        ASSIMP_LOG_INFO("JoinVerticesProcess finished | Verts in: ", iNumOldVertices,
            " out: ", iNumVertices, " | ~",
            ((iNumOldVertices - iNumVertices) / (float)iNumOldVertices) * 100.f );
    }
}

namespace {

struct CompareVerticesAlmostEqual {
    bool operator () (const Vertex & a, const Vertex & b) const {
        static const float epsilon = 1e-5f;
        static const float squareEpsilon = epsilon * epsilon;

        if ((a.position - b.position).SquareLength() > squareEpsilon) {
            return false;
        }

        // We just test the other attributes even if they're not present in the mesh.
        // In this case they're initialized to 0 so the comparison succeeds.
        // By this method the non-present attributes are effectively ignored in the comparison.

        if ((a.normal - b.normal).SquareLength() > squareEpsilon) {
            return false;
        }

        if ((a.tangent - b.tangent).SquareLength() > squareEpsilon) {
            return false;
        }

        if ((a.bitangent - b.bitangent).SquareLength() > squareEpsilon) {
            return false;
        }

        for (uint32_t i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; i ++) {
            if ((a.texcoords[i] - b.texcoords[i]).SquareLength() > squareEpsilon) {
                return false;
            }
        }

        for (uint32_t i = 0; i < AI_MAX_NUMBER_OF_COLOR_SETS; i ++) {
            if (GetColorDifference(a.colors[i], b.colors[i]) > squareEpsilon) {
                return false;
            }
        }

        // If reached this point, they are ~equal
        return true;
    }
};

struct HashVertex {
    inline void hash_combine(std::size_t& seed, const ai_real& v) const {
        std::hash<ai_real> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    size_t operator () (const Vertex & v) const {
        size_t hash = 0;

        hash_combine(hash, v.position.x);
        hash_combine(hash, v.position.y);
        hash_combine(hash, v.position.z);

        return hash;
    }
};

} // namespace

// ------------------------------------------------------------------------------------------------

static constexpr size_t JOINED_VERTICES_MARK = 0x80000000u;

// now start the JoinVerticesProcess
int JoinVerticesProcess::ProcessMesh( aiMesh* pMesh, unsigned int meshIndex) {
    static_assert( AI_MAX_NUMBER_OF_COLOR_SETS    == 8, "AI_MAX_NUMBER_OF_COLOR_SETS    == 8");
	static_assert( AI_MAX_NUMBER_OF_TEXTURECOORDS == 8, "AI_MAX_NUMBER_OF_TEXTURECOORDS == 8");

    // Return early if we don't have any positions
    if (!pMesh->HasPositions() || !pMesh->HasFaces()) {
        return 0;
    }

    // We should care only about used vertices, not all of them
    // (this can happen due to original file vertices buffer being used by
    // multiple meshes)
    std::vector<bool> usedVertexIndicesMask;
    usedVertexIndicesMask.resize(pMesh->mNumVertices, false);
    for (unsigned int a = 0; a < pMesh->mNumFaces; a++) {
        aiFace& face = pMesh->mFaces[a];
        for (unsigned int b = 0; b < face.mNumIndices; b++) {
            usedVertexIndicesMask[face.mIndices[b]] = true;
        }
    }

    // We'll never have more vertices afterwards.
    // This is also the new-index -> source-index mapping handed to the remap.
    std::vector<unsigned int> uniqueVertices;

    // For each vertex the index of the vertex it was replaced by.
    // Since the maximal number of vertices is 2^31-1, the most significand bit can be used to mark
    //  whether a new vertex was created for the index (true) or if it was replaced by an existing
    //  unique vertex (false). This saves an additional std::vector<bool> and greatly enhances
    //  branching performance.
    static_assert(AI_MAX_VERTICES == 0x7fffffff, "AI_MAX_VERTICES == 0x7fffffff");
    std::vector<unsigned int> replaceIndex( pMesh->mNumVertices, 0xffffffff);

    // a map that maps a vertex to its new index
    std::unordered_map<Vertex, int, HashVertex, CompareVerticesAlmostEqual> vertex2Index = {};
    // we can not end up with more vertices than we started with
    // Now check each vertex if it brings something new to the table
    int newIndex = 0;
    for( unsigned int a = 0; a < pMesh->mNumVertices; a++)  {
        // if the vertex is unused Do nothing
        if (!usedVertexIndicesMask[a]) {
            continue;
        }
        // collect the vertex data
        Vertex v(pMesh,a);
        // is the vertex already in the map?
        auto it = vertex2Index.find(v);
        // if the vertex is not in the map then it is a new vertex add it.
        if (it == vertex2Index.end()) {
            // this is a new vertex give it a new index
            vertex2Index.emplace(v, newIndex);
            // keep track of its index and increment 1
            replaceIndex[a] = newIndex++;
            // add the vertex to the unique vertices
            uniqueVertices.push_back(a);
        } else{
            // if the vertex is already there just find the replace index that is appropriate to it
			// mark it with JOINED_VERTICES_MARK
            replaceIndex[a] = it->second | JOINED_VERTICES_MARK;
        }
    }

    if (!DefaultLogger::isNullLogger() && DefaultLogger::get()->getLogSeverity() == Logger::VERBOSE)    {
        ASSIMP_LOG_VERBOSE_DEBUG(
            "Mesh ",meshIndex,
            " (",
            (pMesh->mName.length ? pMesh->mName.data : "unnamed"),
            ") | Verts in: ",pMesh->mNumVertices,
            " out: ",
            uniqueVertices.size(),
            " | ~",
            ((pMesh->mNumVertices - uniqueVertices.size()) / (float)pMesh->mNumVertices) * 100.f,
            "%"
        );
    }

    // A vertex that survives the join keeps its own bone weights; the weights
    // of a vertex that was merged into an existing one are dropped (they would
    // otherwise be counted twice). The same mapping is applied to every present
    // vertex stream, to all morph targets and to the bone weights, so they stay
    // aligned with the new indices.
    const MeshVertexRemap remap(pMesh->mNumVertices, uniqueVertices);
    remap.ApplyInPlace(pMesh);

    // adjust the indices in all faces
    for( unsigned int a = 0; a < pMesh->mNumFaces; a++) {
        aiFace& face = pMesh->mFaces[a];
        for( unsigned int b = 0; b < face.mNumIndices; b++) {
            face.mIndices[b] = replaceIndex[face.mIndices[b]] & ~JOINED_VERTICES_MARK;
        }
    }

    return pMesh->mNumVertices;
}

#endif // !! ASSIMP_BUILD_NO_JOINVERTICES_PROCESS
