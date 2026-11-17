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

 ///  @file Implementation of the SplitLargeMeshes postprocessing step

// internal headers of the post-processing framework
#include "SplitLargeMeshes.h"
#include "ProcessHelper.h"

using namespace Assimp;

// ------------------------------------------------------------------------------------------------
SplitLargeMeshesProcess_Triangle::SplitLargeMeshesProcess_Triangle() {
    LIMIT = AI_SLM_DEFAULT_MAX_TRIANGLES;
}

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool SplitLargeMeshesProcess_Triangle::IsActive( unsigned int pFlags) const {
    return (pFlags & aiProcess_SplitLargeMeshes) != 0;
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void SplitLargeMeshesProcess_Triangle::Execute( aiScene* pScene) {
    if (0xffffffff == this->LIMIT || nullptr == pScene ) {
        return;
    }

    ASSIMP_LOG_DEBUG("SplitLargeMeshesProcess_Triangle begin");
    std::vector<std::pair<aiMesh*, unsigned int> > avList;

    for( unsigned int a = 0; a < pScene->mNumMeshes; ++a) {
        this->SplitMesh(a, pScene->mMeshes[a],avList);
    }

    if (avList.size() == pScene->mNumMeshes) {
        ASSIMP_LOG_DEBUG("SplitLargeMeshesProcess_Triangle finished. There was nothing to do");
    }

    // it seems something has been split. rebuild the mesh list
    delete[] pScene->mMeshes;
    pScene->mNumMeshes = (unsigned int)avList.size();
    pScene->mMeshes = new aiMesh*[avList.size()];

    for (unsigned int i = 0; i < avList.size();++i) {
        pScene->mMeshes[i] = avList[i].first;
    }

    // now we need to update all nodes
    this->UpdateNode(pScene->mRootNode,avList);
    ASSIMP_LOG_INFO("SplitLargeMeshesProcess_Triangle finished. Meshes have been split");
}

// ------------------------------------------------------------------------------------------------
// Setup properties
void SplitLargeMeshesProcess_Triangle::SetupProperties( const Importer* pImp) {
    // get the current value of the split property
    this->LIMIT = pImp->GetPropertyInteger(AI_CONFIG_PP_SLM_TRIANGLE_LIMIT,AI_SLM_DEFAULT_MAX_TRIANGLES);
}

// ------------------------------------------------------------------------------------------------
// Update a node after some meshes have been split
void SplitLargeMeshesProcess_Triangle::UpdateNode(aiNode* pcNode, const std::vector<std::pair<aiMesh*, unsigned int> >& avList) {
    if (pcNode == nullptr) {
        ASSIMP_LOG_WARN("UpdateNode skipped, nullptr detected.");
        return;
    }

    // for every index in out list build a new entry
    std::vector<unsigned int> aiEntries;
    aiEntries.reserve(pcNode->mNumMeshes + 1);
    for (unsigned int i = 0; i < pcNode->mNumMeshes;++i) {
        for (unsigned int a = 0; a < avList.size();++a) {
            if (avList[a].second == pcNode->mMeshes[i]) {
                aiEntries.push_back(a);
            }
        }
    }

    // now build the new list
    delete[] pcNode->mMeshes;
    pcNode->mNumMeshes = (unsigned int)aiEntries.size();
    pcNode->mMeshes = new unsigned int[pcNode->mNumMeshes];

    for (unsigned int b = 0; b < pcNode->mNumMeshes;++b) {
        pcNode->mMeshes[b] = aiEntries[b];
    }

    // recursively update all other nodes
    for (unsigned int i = 0; i < pcNode->mNumChildren;++i) {
        UpdateNode ( pcNode->mChildren[i], avList );
    }
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void SplitLargeMeshesProcess_Triangle::SplitMesh(
        unsigned int a,
        aiMesh* pMesh,
        std::vector<std::pair<aiMesh*, unsigned int> >& avList) {
    if (pMesh->mNumFaces > SplitLargeMeshesProcess_Triangle::LIMIT) {
        ASSIMP_LOG_INFO("Mesh exceeds the triangle limit. It will be split ...");

        // we need to split this mesh into sub meshes
        // determine the size of a submesh
        const unsigned int iSubMeshes = (pMesh->mNumFaces / LIMIT) + 1;

        const unsigned int iOutFaceNum = pMesh->mNumFaces / iSubMeshes;

        // now generate all submeshes
        for (unsigned int i = 0; i < iSubMeshes;++i) {
            unsigned int numOutFaces = iOutFaceNum;
            if (i == iSubMeshes-1) {
                numOutFaces += (pMesh->mNumFaces - iOutFaceNum * iSubMeshes);
            }

            const unsigned int iBase = iOutFaceNum * i;

            // Every index of the output mesh gets its own vertex; the source
            // index of each one is recorded so all vertex streams, the morph
            // targets and the bone weights can be remapped consistently.
            std::vector<unsigned int> newToOld;
            for (unsigned int p = 0; p < numOutFaces; ++p) {
                const aiFace &srcFace = pMesh->mFaces[iBase + p];
                for (unsigned int v = 0; v < srcFace.mNumIndices; ++v) {
                    newToOld.push_back(srcFace.mIndices[v]);
                }
            }

            aiMesh *pcMesh = new aiMesh;
            pcMesh->mNumFaces = numOutFaces;
            pcMesh->mMaterialIndex = pMesh->mMaterialIndex;

            // the name carries the adjacency information between the meshes
            pcMesh->mName = pMesh->mName;

            // copy the per-vertex streams, morph targets and bone weights
            const MeshVertexRemap remap(pMesh->mNumVertices, newToOld);
            remap.CopyTo(pcMesh, pMesh);

            // copy the list of faces, renumbering the indices sequentially
            pcMesh->mFaces = new aiFace[numOutFaces];
            unsigned int iIndexOut = 0;
            for (unsigned int p = 0; p < numOutFaces; ++p) {
                const aiFace &srcFace = pMesh->mFaces[iBase + p];
                aiFace &dstFace = pcMesh->mFaces[p];
                dstFace.mNumIndices = srcFace.mNumIndices;
                dstFace.mIndices = new unsigned int[dstFace.mNumIndices];
                for (unsigned int v = 0; v < dstFace.mNumIndices; ++v) {
                    dstFace.mIndices[v] = iIndexOut++;
                }

                // need to update the output primitive types
                switch (dstFace.mNumIndices) {
                case 1:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_POINT;
                    break;
                case 2:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_LINE;
                    break;
                case 3:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_TRIANGLE;
                    break;
                default:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_POLYGON;
                }
            }

            // add the newly created mesh to the list
            avList.emplace_back(pcMesh,a);
        }

        // now delete the old mesh data
        delete pMesh;
    } else {
        avList.emplace_back(pMesh,a);
    }
}

// ------------------------------------------------------------------------------------------------
SplitLargeMeshesProcess_Vertex::SplitLargeMeshesProcess_Vertex() {
    LIMIT = AI_SLM_DEFAULT_MAX_VERTICES;
}

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool SplitLargeMeshesProcess_Vertex::IsActive( unsigned int pFlags) const {
    return (pFlags & aiProcess_SplitLargeMeshes) != 0;
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void SplitLargeMeshesProcess_Vertex::Execute( aiScene* pScene) {
    if (0xffffffff == this->LIMIT || nullptr == pScene ) {
        return;
    }

    ASSIMP_LOG_DEBUG("SplitLargeMeshesProcess_Vertex begin");

    std::vector<std::pair<aiMesh*, unsigned int> > avList;

    //Check for point cloud first,
    //Do not process point cloud, splitMesh works only with faces data
    for (unsigned int a = 0; a < pScene->mNumMeshes; a++) {
        if ( pScene->mMeshes[a]->mPrimitiveTypes == aiPrimitiveType_POINT ) {
            return;
        }
    }

    for( unsigned int a = 0; a < pScene->mNumMeshes; ++a ) {
        this->SplitMesh(a, pScene->mMeshes[a], avList);
    }

    if (avList.size() != pScene->mNumMeshes) {
        // it seems something has been split. rebuild the mesh list
        delete[] pScene->mMeshes;
        pScene->mNumMeshes = (unsigned int)avList.size();
        pScene->mMeshes = new aiMesh*[avList.size()];

        for (unsigned int i = 0; i < avList.size();++i) {
            pScene->mMeshes[i] = avList[i].first;
        }

        // now we need to update all nodes
        SplitLargeMeshesProcess_Triangle::UpdateNode(pScene->mRootNode,avList);
        ASSIMP_LOG_INFO("SplitLargeMeshesProcess_Vertex finished. Meshes have been split");
    } else {
        ASSIMP_LOG_DEBUG("SplitLargeMeshesProcess_Vertex finished. There was nothing to do");
    }
}

// ------------------------------------------------------------------------------------------------
// Setup properties
void SplitLargeMeshesProcess_Vertex::SetupProperties( const Importer* pImp) {
    this->LIMIT = pImp->GetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT,AI_SLM_DEFAULT_MAX_VERTICES);
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void SplitLargeMeshesProcess_Vertex::SplitMesh(
        unsigned int a,
        aiMesh* pMesh,
        std::vector<std::pair<aiMesh*, unsigned int> >& avList) {
    if (pMesh->mNumVertices > SplitLargeMeshesProcess_Vertex::LIMIT) {
        // we need to split this mesh into sub meshes
        // determine the estimated size of a submesh
        // (this could be too large. Max waste is a single digit percentage)
        const unsigned int iSubMeshes = (pMesh->mNumVertices / SplitLargeMeshesProcess_Vertex::LIMIT) + 1;

        // create a std::vector<unsigned int> to indicate which vertices
        // have already been copied
        std::vector<unsigned int> avWasCopied;
        avWasCopied.resize(pMesh->mNumVertices,0xFFFFFFFF);

        // try to find a good estimate for the number of output faces
        // per mesh. Add 12.5% as buffer
        unsigned int iEstimatedSize = pMesh->mNumFaces / iSubMeshes;
        iEstimatedSize += iEstimatedSize >> 3;

        // now generate all submeshes
        unsigned int iBase( 0 );
        while (true) {
            const unsigned int iOutVertexNum = SplitLargeMeshesProcess_Vertex::LIMIT;
            aiMesh* pcMesh          = new aiMesh;
            pcMesh->mNumVertices    = 0;
            pcMesh->mMaterialIndex  = pMesh->mMaterialIndex;

            // the name carries the adjacency information between the meshes
            pcMesh->mName = pMesh->mName;

            // clear the temporary helper array
            if (iBase) {
                // we can't use memset here we unsigned int needn' be 32 bits
                for (auto &elem : avWasCopied) {
                    elem = 0xffffffff;
                }
            }

            // new vertex index -> source vertex index for this submesh
            std::vector<unsigned int> newToOld;
            newToOld.reserve(iOutVertexNum);

            // output vectors
            std::vector<aiFace> vFaces;
            vFaces.reserve(iEstimatedSize);

            // (we will also need to copy the array of indices)
            while (iBase < pMesh->mNumFaces) {
                // allocate a new array
                const unsigned int iNumIndices = pMesh->mFaces[iBase].mNumIndices;

                // doesn't catch degenerates but is quite fast
                unsigned int iNeed = 0;
                for (unsigned int v = 0; v < iNumIndices;++v) {
                    unsigned int iIndex = pMesh->mFaces[iBase].mIndices[v];

                    // check whether we do already have this vertex
                    if (0xFFFFFFFF == avWasCopied[iIndex]) {
                        iNeed++;
                    }
                }
                if (pcMesh->mNumVertices + iNeed > iOutVertexNum) {
                    // don't use this face
                    break;
                }

                vFaces.emplace_back();
                aiFace& rFace = vFaces.back();

                // setup face type and number of indices
                rFace.mNumIndices = iNumIndices;
                rFace.mIndices = new unsigned int[iNumIndices];

                // need to update the output primitive types
                switch (rFace.mNumIndices) {
                case 1:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_POINT;
                    break;
                case 2:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_LINE;
                    break;
                case 3:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_TRIANGLE;
                    break;
                default:
                    pcMesh->mPrimitiveTypes |= aiPrimitiveType_POLYGON;
                }

                // and copy the contents of the old array, offset by current base
                for (unsigned int v = 0; v < iNumIndices;++v) {
                    unsigned int iIndex = pMesh->mFaces[iBase].mIndices[v];

                    // check whether we do already have this vertex
                    if (0xFFFFFFFF != avWasCopied[iIndex]) {
                        rFace.mIndices[v] = avWasCopied[iIndex];
                        continue;
                    }

                    avWasCopied[iIndex] = pcMesh->mNumVertices;
                    newToOld.push_back(iIndex);
                    rFace.mIndices[v] = pcMesh->mNumVertices++;
                }
                ++iBase;
                if(pcMesh->mNumVertices == iOutVertexNum) {
                    // break here. The face is only added if it was complete
                    break;
                }
            }

            // copy the per-vertex streams, all morph targets and the bone
            // weights that belong to the vertices of this submesh
            const MeshVertexRemap remap(pMesh->mNumVertices, newToOld);
            remap.CopyTo(pcMesh, pMesh);

            // copy the face list to the mesh
            pcMesh->mFaces = new aiFace[vFaces.size()];
            pcMesh->mNumFaces = (unsigned int)vFaces.size();

            for (unsigned int p = 0; p < pcMesh->mNumFaces;++p) {
                pcMesh->mFaces[p] = vFaces[p];
            }

            // add the newly created mesh to the list
            avList.emplace_back(pcMesh,a);

            if (iBase == pMesh->mNumFaces) {
                // have all faces ... finish the outer loop, too
                break;
            }
        }

        // now delete the old mesh data
        delete pMesh;
        return;
    }
    avList.emplace_back(pMesh,a);
}
