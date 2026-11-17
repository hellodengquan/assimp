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

#ifndef ASSIMP_BUILD_NO_OBJ_IMPORTER

#include "ObjFileImporter.h"
#include "ObjFileData.h"
#include "ObjFileParser.h"
#include <assimp/DefaultIOSystem.h>
#include <assimp/IOStreamBuffer.h>
#include <assimp/ai_assert.h>
#include <assimp/importerdesc.h>
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/Importer.hpp>
#include <assimp/ObjMaterial.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>

static constexpr aiImporterDesc desc = {
    "Wavefront Object Importer",
    "",
    "",
    "surfaces not supported",
    aiImporterFlags_SupportTextFlavour,
    0,
    0,
    0,
    0,
    "obj"
};

static constexpr unsigned int ObjMinSize = 16u;

namespace Assimp {

namespace {

// ------------------------------------------------------------------------------------------------
//  Exact bit pattern of a position, used to look up the direction of valid geometry at the same
//  location. Comparing bit patterns avoids any epsilon question: the fallback only needs to find
//  positions that were written from the very same input values.
struct PositionKey {
    // Number of 32-bit words needed to hold a position (works for float and double ai_real).
    static constexpr size_t WordCount = (3 * sizeof(ai_real) + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    std::array<uint32_t, WordCount> mWords{};

    static PositionKey From(const aiVector3D &v) {
        PositionKey key;
        ::memcpy(key.mWords.data(), &v, sizeof(ai_real) * 3);
        return key;
    }

    bool operator==(const PositionKey &other) const {
        return mWords == other.mWords;
    }
};

struct PositionKeyHash {
    size_t operator()(const PositionKey &key) const {
        // Mix all words; the exact quality does not matter, only the distribution.
        size_t h = 0;
        for (uint32_t word : key.mWords) {
            h = h * 1000003u ^ static_cast<size_t>(word);
        }
        return h;
    }
};

// ------------------------------------------------------------------------------------------------
//  Computes an area-weighted face normal using Newell's method. This also gives a sensible
//  normal for non-planar polygons and returns a zero vector for degenerate faces.
aiVector3D ComputeFaceNormal(const ObjFile::Face *face, const std::vector<aiVector3D> &positions) {
    const size_t numCorners = face->mVertices.size();
    if (numCorners < 3) {
        return aiVector3D();
    }

    aiVector3D normal;
    for (size_t i = 0; i < numCorners; ++i) {
        const unsigned int current = face->mVertices[i];
        const unsigned int next = face->mVertices[(i + 1) % numCorners];
        if (current >= positions.size() || next >= positions.size()) {
            continue;
        }

        const aiVector3D &a = positions[current];
        const aiVector3D &b = positions[next];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }

    return normal;
}

// ------------------------------------------------------------------------------------------------
//  Generates normals for all face corners without an explicit normal, honouring the smoothing
//  groups of the OBJ file. The computation is model-wide on purpose: a face keeps its smoothing
//  relation to neighbouring faces even if the model is split into several meshes (for example
//  because several materials are used).
void GenerateSmoothNormals(ObjFile::Model &model) {
    std::vector<ObjFile::Face *> faces;
    std::vector<ObjFile::Mesh *> faceMeshes;
    size_t faceCount = 0;
    for (ObjFile::Mesh *mesh : model.mMeshes) {
        if (mesh != nullptr) {
            faceCount += mesh->mFaces.size();
        }
    }
    faces.reserve(faceCount);
    faceMeshes.reserve(faceCount);

    // Only polygon and triangle faces carry normals; lines and points do not.
    auto isSurface = [](const ObjFile::Face *face) {
        return face->mPrimitiveType == aiPrimitiveType_POLYGON ||
               face->mPrimitiveType == aiPrimitiveType_TRIANGLE;
    };

    // A corner needs a generated normal if the file carries smoothing information, or if it
    // mixes explicit normals with missing ones. Files that provide neither must keep their
    // historical behaviour (no normals at all).
    bool hasMissingNormal = false;
    bool hasExplicitNormal = false;
    for (ObjFile::Mesh *mesh : model.mMeshes) {
        if (mesh == nullptr) {
            continue;
        }
        for (ObjFile::Face *face : mesh->mFaces) {
            if (face == nullptr || !isSurface(face)) {
                continue;
            }
            faces.push_back(face);
            faceMeshes.push_back(mesh);

            if (face->mNormals.size() != face->mVertices.size()) {
                // A malformed face dropped some normal slots; treat all of them as missing.
                hasMissingNormal = true;
                continue;
            }
            for (unsigned int normal : face->mNormals) {
                if (normal == ObjFile::Face::InvalidIndex) {
                    hasMissingNormal = true;
                } else {
                    hasExplicitNormal = true;
                }
            }
        }
    }

    if (faces.empty() || !hasMissingNormal || (!model.mHasSmoothingStatement && !hasExplicitNormal)) {
        return;
    }

    std::vector<aiVector3D> faceNormals(faces.size());
    for (size_t i = 0; i < faces.size(); ++i) {
        faceNormals[i] = ComputeFaceNormal(faces[i], model.mVertices);
    }

    // Scale for rejecting zero-area faces. Newell's method returns twice the face area.
    aiVector3D boundsMin(std::numeric_limits<ai_real>::max(), std::numeric_limits<ai_real>::max(), std::numeric_limits<ai_real>::max());
    aiVector3D boundsMax(std::numeric_limits<ai_real>::lowest(), std::numeric_limits<ai_real>::lowest(), std::numeric_limits<ai_real>::lowest());
    for (const aiVector3D &v : model.mVertices) {
        boundsMin.x = std::min(boundsMin.x, v.x);
        boundsMin.y = std::min(boundsMin.y, v.y);
        boundsMin.z = std::min(boundsMin.z, v.z);
        boundsMax.x = std::max(boundsMax.x, v.x);
        boundsMax.y = std::max(boundsMax.y, v.y);
        boundsMax.z = std::max(boundsMax.z, v.z);
    }
    const ai_real degenerateEpsilon = (boundsMax - boundsMin).Length() * ai_real(1e-6);
    const ai_real degenerateSquareEpsilon = degenerateEpsilon * degenerateEpsilon;

    // Accumulate the normals of all faces that share a vertex and belong to the same smoothing
    // group. Vertices are matched by index, not by position, so surfaces that merely coincide in
    // space without sharing vertices do not influence each other. Degenerate faces are skipped so
    // that they cannot pollute the normals of their neighbours.
    std::unordered_map<uint64_t, aiVector3D> accumulated;
    std::unordered_map<unsigned int, aiVector3D> vertexFallback;
    std::unordered_map<PositionKey, aiVector3D, PositionKeyHash> positionFallback;
    accumulated.reserve(faces.size() * 3);
    vertexFallback.reserve(model.mVertices.size());
    positionFallback.reserve(model.mVertices.size());
    aiVector3D modelNormal;
    bool hasValidFace = false;
    for (size_t i = 0; i < faces.size(); ++i) {
        const ObjFile::Face *face = faces[i];
        if (faceNormals[i].SquareLength() <= degenerateSquareEpsilon) {
            continue;
        }
        hasValidFace = true;
        modelNormal += faceNormals[i];
        for (unsigned int vertex : face->mVertices) {
            // Directions of valid geometry, used as a last resort for degenerate faces.
            vertexFallback[vertex] += faceNormals[i];
            if (vertex < model.mVertices.size()) {
                positionFallback[PositionKey::From(model.mVertices[vertex])] += faceNormals[i];
            }
            if (face->mSmoothGroup == 0) {
                continue;
            }
            const uint64_t key = (static_cast<uint64_t>(vertex) << 32) | static_cast<uint64_t>(face->mSmoothGroup);
            accumulated[key] += faceNormals[i];
        }
    }
    modelNormal.NormalizeSafe();

    if (!hasValidFace) {
        // Every surface face is degenerate, so there is no meaningful direction at all. Leave
        // the mesh without generated normals rather than emitting an arbitrary one.
        return;
    }

    // Distribute the normals to the face corners. Explicit normals are left untouched, so only
    // corners that lack one receive generated data.
    for (size_t i = 0; i < faces.size(); ++i) {
        ObjFile::Face *face = faces[i];
        const bool hasExplicitNormals = face->mNormals.size() == face->mVertices.size();
        const bool faceIsDegenerate = faceNormals[i].SquareLength() <= degenerateSquareEpsilon;

        aiVector3D flatNormal = faceNormals[i];
        flatNormal.NormalizeSafe();

        bool generated = false;
        face->mSmoothNormals.assign(face->mVertices.size(), aiVector3D());
        for (size_t corner = 0; corner < face->mVertices.size(); ++corner) {
            if (hasExplicitNormals && face->mNormals[corner] != ObjFile::Face::InvalidIndex) {
                // An explicit normal always wins and is never overwritten.
                continue;
            }

            const unsigned int vertex = face->mVertices[corner];
            aiVector3D normal;
            if (face->mSmoothGroup != 0) {
                const uint64_t key = (static_cast<uint64_t>(vertex) << 32) |
                                     static_cast<uint64_t>(face->mSmoothGroup);
                auto it = accumulated.find(key);
                if (it != accumulated.end()) {
                    normal = it->second;
                }
            }

            if (normal.SquareLength() == 0) {
                if (!faceIsDegenerate) {
                    // Smoothing disabled, or the group has no usable contribution: use the face
                    // normal, which also keeps cross-group boundaries sharp.
                    normal = flatNormal;
                } else {
                    // A degenerate face has no normal of its own. Reuse the direction of valid
                    // geometry at the same vertex or position instead of emitting a zero vector.
                    auto it = vertexFallback.find(vertex);
                    if (it != vertexFallback.end()) {
                        normal = it->second;
                    } else if (vertex < model.mVertices.size()) {
                        auto posIt = positionFallback.find(PositionKey::From(model.mVertices[vertex]));
                        if (posIt != positionFallback.end()) {
                            normal = posIt->second;
                        }
                    }
                    if (normal.SquareLength() == 0) {
                        // Nothing touches this vertex: use the average direction of the valid
                        // geometry so the array stays free of zero-length normals.
                        normal = modelNormal;
                    }
                }
            }

            normal.NormalizeSafe();
            face->mSmoothNormals[corner] = normal;
            generated = true;
        }

        if (generated) {
            ObjFile::Mesh *mesh = faceMeshes[i];
            if (mesh != nullptr) {
                mesh->mHasSmoothNormals = true;
            }
        }
    }
}

} // namespace

using namespace std;

// ------------------------------------------------------------------------------------------------
//  Default constructor
ObjFileImporter::ObjFileImporter() : mAbsPath(std::string(1, DefaultIOSystem().getOsSeparator())) {
    // empty
}

// ------------------------------------------------------------------------------------------------
//  Destructor.
ObjFileImporter::~ObjFileImporter() {
    delete mRootObject;
}

// ------------------------------------------------------------------------------------------------
//  Returns true if file is an obj file.
bool ObjFileImporter::CanRead(const std::string &pFile, IOSystem *pIOHandler, bool /*checkSig*/) const {
    static const char *tokens[] = { "mtllib", "usemtl", "v ", "vt ", "vn ", "o ", "g ", "s ", "f " };
    return BaseImporter::SearchFileHeaderForToken(pIOHandler, pFile, tokens, AI_COUNT_OF(tokens), 200, false, true);
}

// ------------------------------------------------------------------------------------------------
const aiImporterDesc *ObjFileImporter::GetInfo() const {
    return &desc;
}

// ------------------------------------------------------------------------------------------------
//  Obj-file import implementation
void ObjFileImporter::InternReadFile(const std::string &file, aiScene *pScene, IOSystem *pIOHandler) {
    if (mRootObject != nullptr) {
        delete mRootObject;
        mRootObject = nullptr;
    }

    // Read file into memory
    static constexpr char mode[] = "rb";
    auto streamCloser = [&](IOStream *pStream) {
        pIOHandler->Close(pStream);
    };
    std::unique_ptr<IOStream, decltype(streamCloser)> fileStream(pIOHandler->Open(file, mode), streamCloser);
    if (!fileStream) {
        throw DeadlyImportError("Failed to open file ", file, ".");
    }

    // Get the file-size and validate it, throwing an exception when fails
    const size_t fileSize = fileStream->FileSize();
    if (fileSize < ObjMinSize) {
        throw DeadlyImportError("OBJ-file is too small.");
    }

    IOStreamBuffer<char> streamedBuffer;
    streamedBuffer.open(fileStream.get());

    // Allocate buffer and read file into it
    //TextFileToBuffer( fileStream.get(),m_Buffer);

    // Get the model name
    std::string modelName, folderName;
    std::string::size_type pos = file.find_last_of("\\/");
    if (pos != std::string::npos) {
        modelName = file.substr(pos + 1, file.size() - pos - 1);
        folderName = file.substr(0, pos);
        if (!folderName.empty()) {
            pIOHandler->PushDirectory(folderName);
        }
    } else {
        modelName = file;
    }

    // parse the file into a temporary representation
    ObjFileParser parser(streamedBuffer, modelName, pIOHandler, m_progress, file);

    // Fill in normals for all corners without an explicit one, honouring the smoothing
    // groups of the file.
    GenerateSmoothNormals(*parser.GetModel());

    // And create the proper return structures out of it
    CreateDataFromImport(parser.GetModel(), pScene);

    streamedBuffer.close();

    // Clean up allocated storage for the next import
    mBuffer.clear();

    // Pop directory stack
    if (pIOHandler->StackSize() > 0) {
        pIOHandler->PopDirectory();
    }
}

// ------------------------------------------------------------------------------------------------
//  Create the data from parsed obj-file
void ObjFileImporter::CreateDataFromImport(const ObjFile::Model *pModel, aiScene *pScene) {
    if (pModel == nullptr) {
        return;
    }

    // Create the root node of the scene
    pScene->mRootNode = new aiNode;
    if (!pModel->mModelName.empty()) {
        // Set the name of the scene
        pScene->mRootNode->mName.Set(pModel->mModelName);
    } else {
        // This is a fatal error, so break down the application
        ai_assert(false);
    }

    if (!pModel->mObjects.empty()) {
        unsigned int meshCount = 0;
        unsigned int childCount = 0;

        for (auto object : pModel->mObjects) {
            if (object) {
                ++childCount;
                meshCount += (unsigned int)object->mMeshes.size();
            }
        }

        // Allocate space for the child nodes on the root node
        pScene->mRootNode->mChildren = new aiNode *[childCount];

        // Create nodes for the whole scene
        std::vector<std::unique_ptr<aiMesh>> MeshArray;
        MeshArray.reserve(meshCount);
        for (size_t index = 0; index < pModel->mObjects.size(); ++index) {
            createNodes(pModel, pModel->mObjects[index], pScene->mRootNode, pScene, MeshArray);
        }

        ai_assert(pScene->mRootNode->mNumChildren == childCount);

        // Create mesh pointer buffer for this scene
        if (pScene->mNumMeshes > 0) {
            pScene->mMeshes = new aiMesh *[MeshArray.size()];
            for (size_t index = 0; index < MeshArray.size(); ++index) {
                pScene->mMeshes[index] = MeshArray[index].release();
            }
        }

        // Create all materials
        createMaterials(pModel, pScene);
    } else {
        if (pModel->mVertices.empty()) {
            return;
        }

        std::unique_ptr<aiMesh> mesh(new aiMesh);
        mesh->mPrimitiveTypes = aiPrimitiveType_POINT;
        unsigned int n = (unsigned int)pModel->mVertices.size();
        mesh->mNumVertices = n;

        mesh->mVertices = new aiVector3D[n];
        memcpy(mesh->mVertices, pModel->mVertices.data(), n * sizeof(aiVector3D));

        if (!pModel->mNormals.empty()) {
            mesh->mNormals = new aiVector3D[n];
            if (pModel->mNormals.size() < n) {
                throw DeadlyImportError("OBJ: vertex normal index out of range");
            }
            memcpy(mesh->mNormals, pModel->mNormals.data(), n * sizeof(aiVector3D));
        }

        if (!pModel->mVertexColors.empty()) {
            mesh->mColors[0] = new aiColor4D[mesh->mNumVertices];
            for (unsigned int i = 0; i < n; ++i) {
                if (i < pModel->mVertexColors.size()) {
                    const aiVector3D &color = pModel->mVertexColors[i];
                    mesh->mColors[0][i] = aiColor4D(color.x, color.y, color.z, 1.0);
                } else {
                    throw DeadlyImportError("OBJ: vertex color index out of range");
                }
            }
        }

        pScene->mRootNode->mNumMeshes = 1;
        pScene->mRootNode->mMeshes = new unsigned int[1];
        pScene->mRootNode->mMeshes[0] = 0;
        pScene->mMeshes = new aiMesh *[1];
        pScene->mNumMeshes = 1;
        pScene->mMeshes[0] = mesh.release();
    }
}

// ------------------------------------------------------------------------------------------------
//  Creates all nodes of the model
aiNode *ObjFileImporter::createNodes(const ObjFile::Model *pModel, const ObjFile::Object *pObject,
        aiNode *pParent, aiScene *pScene,  std::vector<std::unique_ptr<aiMesh>> &MeshArray) {
    if (nullptr == pObject || pModel == nullptr) {
        return nullptr;
    }

    // Store older mesh size to be able to computes mesh offsets for new mesh instances
    const size_t oldMeshSize = MeshArray.size();
    auto *pNode = new aiNode;

    pNode->mName = pObject->mObjName;

    // If we have a parent node, store it
    ai_assert(nullptr != pParent);
    appendChildToParentNode(pParent, pNode);

    for (size_t i = 0; i < pObject->mMeshes.size(); ++i) {
        unsigned int meshId = pObject->mMeshes[i];
        std::unique_ptr<aiMesh> pMesh = createTopology(pModel, pObject, meshId);
        if (pMesh != nullptr) {
            if (pMesh->mNumFaces > 0) {
                MeshArray.push_back(std::move(pMesh));
            }
        }
    }

    // Create all nodes from the sub-objects stored in the current object
    if (!pObject->mSubObjects.empty()) {
        size_t numChilds = pObject->mSubObjects.size();
        pNode->mNumChildren = static_cast<unsigned int>(numChilds);
        pNode->mChildren = new aiNode *[numChilds];
        pNode->mNumMeshes = 1;
        pNode->mMeshes = new unsigned int[1];
    }

    // Set mesh instances into scene- and node-instances
    const size_t meshSizeDiff = MeshArray.size() - oldMeshSize;
    if (meshSizeDiff > 0) {
        pNode->mMeshes = new unsigned int[meshSizeDiff];
        pNode->mNumMeshes = static_cast<unsigned int>(meshSizeDiff);
        size_t index = 0;
        for (size_t i = oldMeshSize; i < MeshArray.size(); ++i) {
            pNode->mMeshes[index] = pScene->mNumMeshes;
            pScene->mNumMeshes++;
            ++index;
        }
    }

    return pNode;
}

// ------------------------------------------------------------------------------------------------
//  Create topology data
std::unique_ptr<aiMesh> ObjFileImporter::createTopology(const ObjFile::Model *pModel, const
        ObjFile::Object *pData, unsigned int meshIndex) {
    if (nullptr == pData || pModel == nullptr) {
        return nullptr;
    }

    // Create faces
    ObjFile::Mesh *pObjMesh = pModel->mMeshes[meshIndex];
    if (pObjMesh == nullptr) {
        return nullptr;
    }

    if (pObjMesh->mFaces.empty()) {
        return nullptr;
    }

    std::unique_ptr<aiMesh> pMesh(new aiMesh);
    if (!pObjMesh->mName.empty()) {
        pMesh->mName.Set(pObjMesh->mName);
    }

    for (size_t index = 0; index < pObjMesh->mFaces.size(); index++) {
        const ObjFile::Face *inp = pObjMesh->mFaces[index];
        if (inp == nullptr) {
            continue;
        }

        if (inp->mPrimitiveType == aiPrimitiveType_LINE) {
            pMesh->mNumFaces += static_cast<unsigned int>(inp->mVertices.size() - 1);
            pMesh->mPrimitiveTypes |= aiPrimitiveType_LINE;
        } else if (inp->mPrimitiveType == aiPrimitiveType_POINT) {
            pMesh->mNumFaces += static_cast<unsigned int>(inp->mVertices.size());
            pMesh->mPrimitiveTypes |= aiPrimitiveType_POINT;
        } else {
            ++pMesh->mNumFaces;
            if (inp->mVertices.size() > 3) {
                pMesh->mPrimitiveTypes |= aiPrimitiveType_POLYGON;
            } else {
                pMesh->mPrimitiveTypes |= aiPrimitiveType_TRIANGLE;
            }
        }
    }

    unsigned int uiIdxCount = 0u;
    if (pMesh->mNumFaces > 0) {
        pMesh->mFaces = new aiFace[pMesh->mNumFaces];
        if (pObjMesh->mMaterialIndex != ObjFile::Mesh::NoMaterial) {
            pMesh->mMaterialIndex = pObjMesh->mMaterialIndex;
        }

        unsigned int outIndex = 0u;

        // Copy all data from all stored meshes
        for (auto &face : pObjMesh->mFaces) {
            const ObjFile::Face *inp = face;
            if (inp->mPrimitiveType == aiPrimitiveType_LINE) {
                for (size_t i = 0; i < inp->mVertices.size() - 1; ++i) {
                    aiFace &f = pMesh->mFaces[outIndex++];
                    uiIdxCount += f.mNumIndices = 2;
                    f.mIndices = new unsigned int[2];
                }
                continue;
            }

            if (inp->mPrimitiveType == aiPrimitiveType_POINT) {
                for (size_t i = 0; i < inp->mVertices.size(); ++i) {
                    aiFace &f = pMesh->mFaces[outIndex++];
                    uiIdxCount += f.mNumIndices = 1;
                    f.mIndices = new unsigned int[1];
                }
                continue;
            }

            aiFace *pFace = &pMesh->mFaces[outIndex++];
            const unsigned int uiNumIndices = static_cast<unsigned int>(face->mVertices.size());
            uiIdxCount += pFace->mNumIndices = static_cast<unsigned int>(uiNumIndices);
            if (pFace->mNumIndices > 0) {
                pFace->mIndices = new unsigned int[uiNumIndices];
            }
        }
    }

    // Create mesh vertices
    createVertexArray(pModel, pData, meshIndex, pMesh.get(), uiIdxCount);

    return pMesh;
}

// ------------------------------------------------------------------------------------------------
//  Creates a vertex array
void ObjFileImporter::createVertexArray(const ObjFile::Model *model, const ObjFile::Object *currentObject,
        unsigned int meshIndex, aiMesh *mesh, unsigned int numIndices) {
    if (currentObject == nullptr || model == nullptr || mesh == nullptr) {
        return;
    }

    // Break if no faces are stored in the object
    if (currentObject->mMeshes.empty()) {
        return;
    }

    // Get the current mesh
    ObjFile::Mesh *objMesh = model->mMeshes[meshIndex];
    if (nullptr == objMesh || objMesh->mNumIndices < 1) {
        return;
    }

    // Copy vertices of this mesh instance
    mesh->mNumVertices = numIndices;
    if (mesh->mNumVertices == 0) {
        throw DeadlyImportError("OBJ: no vertices");
    }

    if (mesh->mNumVertices > AI_MAX_VERTICES) {
        throw DeadlyImportError("OBJ: Too many vertices");
    }

    mesh->mVertices = new aiVector3D[mesh->mNumVertices];

    // Allocate buffer for normal vectors. Generated normals may exist even when the file
    // contains no explicit `vn` records at all.
    if (objMesh->mHasNormals || objMesh->mHasSmoothNormals)
        mesh->mNormals = new aiVector3D[mesh->mNumVertices];

    // Allocate buffer for vertex-color vectors
    if (!model->mVertexColors.empty())
        mesh->mColors[0] = new aiColor4D[mesh->mNumVertices];

    // Allocate buffer for texture coordinates
    if (!model->mTextureCoord.empty() && objMesh->mUVCoordinates[0]) {
        mesh->mNumUVComponents[0] = model->mTextureCoordDim;
        mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumVertices];
    }

    // Copy vertices, normals and textures into aiMesh instance
    bool normalsok = true, uvok = true;
    unsigned int newIndex = 0, outIndex = 0;
    for (auto sourceFace : objMesh->mFaces) {
        // Copy all index arrays
        for (size_t vertexIndex = 0, outVertexIndex = 0; vertexIndex < sourceFace->mVertices.size(); vertexIndex++) {
            const unsigned int vertex = sourceFace->mVertices[vertexIndex];
            if (vertex >= model->mVertices.size()) {
                throw DeadlyImportError("OBJ: vertex index out of range");
            }

            if (mesh->mNumVertices <= newIndex) {
                throw DeadlyImportError("OBJ: bad vertex index");
            }

            mesh->mVertices[newIndex] = model->mVertices[vertex];

            // Explicit normals from the file keep their direction; where none is present the
            // value generated from the smoothing groups (or the per-face fallback) is used.
            aiVector3D normal;
            bool hasValue = false;
            if (sourceFace->mNormals.size() == sourceFace->mVertices.size() &&
                    sourceFace->mNormals[vertexIndex] != ObjFile::Face::InvalidIndex) {
                const unsigned int normalIndex = sourceFace->mNormals[vertexIndex];
                if (!model->mNormals.empty() && normalIndex < model->mNormals.size()) {
                    normal = model->mNormals[normalIndex];
                    hasValue = true;
                } else {
                    normalsok = false;
                }
            }

            if (!hasValue && vertexIndex < sourceFace->mSmoothNormals.size()) {
                normal = sourceFace->mSmoothNormals[vertexIndex];
                hasValue = true;
            }

            if (mesh->mNormals != nullptr && normalsok) {
                mesh->mNormals[newIndex] = hasValue ? normal : aiVector3D();
            }

            // Copy all vertex colors
            if (vertex < model->mVertexColors.size()) {
                const aiVector3D &color = model->mVertexColors[vertex];
                mesh->mColors[0][newIndex] = aiColor4D(color.x, color.y, color.z, 1.0);
            }

            // Copy all texture coordinates
            if (uvok && !model->mTextureCoord.empty() && vertexIndex < sourceFace->mTextureCoords.size()) {
                const unsigned int tex = sourceFace->mTextureCoords[vertexIndex];
                if (tex >= model->mTextureCoord.size()) {
                    uvok = false;
                } else {
                    const aiVector3D &coord3d = model->mTextureCoord[tex];
                    mesh->mTextureCoords[0][newIndex] = aiVector3D(coord3d.x, coord3d.y, coord3d.z);
                }
            }

            // Get destination face
            aiFace *pDestFace = &mesh->mFaces[outIndex];

            const bool last = (vertexIndex == sourceFace->mVertices.size() - 1);
            if (sourceFace->mPrimitiveType != aiPrimitiveType_LINE || !last) {
                pDestFace->mIndices[outVertexIndex] = newIndex;
                outVertexIndex++;
            }

            if (sourceFace->mPrimitiveType == aiPrimitiveType_POINT) {
                outIndex++;
                outVertexIndex = 0;
            } else if (sourceFace->mPrimitiveType == aiPrimitiveType_LINE) {
                outVertexIndex = 0;

                if (!last)
                    outIndex++;

                if (vertexIndex) {
                    if (!last) {
                        if (mesh->mNumVertices <= newIndex + 1) {
                            throw DeadlyImportError("OBJ: bad vertex index");
                        }

                        mesh->mVertices[newIndex + 1] = mesh->mVertices[newIndex];
                        if (mesh->mNormals) {
                            mesh->mNormals[newIndex + 1] = mesh->mNormals[newIndex];
                        }
                        if (!model->mTextureCoord.empty()) {
                            for (size_t i = 0; i < mesh->GetNumUVChannels(); i++) {
                                mesh->mTextureCoords[i][newIndex + 1] = mesh->mTextureCoords[i][newIndex];
                            }
                        }
                        ++newIndex;
                    }

                    pDestFace[-1].mIndices[1] = newIndex;
                }
            } else if (last) {
                outIndex++;
            }
            ++newIndex;
        }
    }

    if (!normalsok) {
        delete[] mesh->mNormals;
        mesh->mNormals = nullptr;
    }

    if (!uvok) {
        delete[] mesh->mTextureCoords[0];
        mesh->mTextureCoords[0] = nullptr;
    }
}

// ------------------------------------------------------------------------------------------------
//  Counts all stored meshes
void ObjFileImporter::countObjects(const std::vector<ObjFile::Object *> &rObjects, int &iNumMeshes) {
    iNumMeshes = 0;
    if (rObjects.empty())
        return;

    iNumMeshes += static_cast<unsigned int>(rObjects.size());
    for (auto object : rObjects) {
        if (!object->mSubObjects.empty()) {
            countObjects(object->mSubObjects, iNumMeshes);
        }
    }
}

// ------------------------------------------------------------------------------------------------
//   Add clamp mode property to material if necessary
void ObjFileImporter::addTextureMappingModeProperty(aiMaterial *mat, aiTextureType type, int clampMode, int index) {
    if (nullptr == mat) {
        return;
    }

    mat->AddProperty<int>(&clampMode, 1, AI_MATKEY_MAPPINGMODE_U(type, index));
    mat->AddProperty<int>(&clampMode, 1, AI_MATKEY_MAPPINGMODE_V(type, index));
}

// ------------------------------------------------------------------------------------------------
//  Creates the material
void ObjFileImporter::createMaterials(const ObjFile::Model *pModel, aiScene *pScene) {
    if (nullptr == pScene) {
        return;
    }

    const unsigned int numMaterials = (unsigned int)pModel->mMaterialLib.size();
    pScene->mNumMaterials = 0;
    if (pModel->mMaterialLib.empty()) {
        ASSIMP_LOG_DEBUG("OBJ: no materials specified");
        return;
    }

    pScene->mMaterials = new aiMaterial *[numMaterials];
    for (unsigned int matIndex = 0; matIndex < numMaterials; matIndex++) {
        // Store material name
        std::unordered_map<std::string, ObjFile::Material *>::const_iterator it;
        it = pModel->mMaterialMap.find(pModel->mMaterialLib[matIndex]);

        // No material found, use the default material
        if (pModel->mMaterialMap.end() == it) {
            continue;
        }

        aiMaterial *mat = new aiMaterial;
        ObjFile::Material *pCurrentMaterial = it->second;
        mat->AddProperty(&pCurrentMaterial->MaterialName, AI_MATKEY_NAME);

        // convert illumination model
        int sm = 0;
        switch (pCurrentMaterial->illumination_model) {
        case 0:
            sm = aiShadingMode_NoShading;
            break;
        case 1:
            sm = aiShadingMode_Gouraud;
            break;
        case 2:
            sm = aiShadingMode_Phong;
            break;
        default:
            sm = aiShadingMode_Gouraud;
            ASSIMP_LOG_ERROR("OBJ: unexpected illumination model (0-2 recognized)");
        }

        mat->AddProperty<int>(&sm, 1, AI_MATKEY_SHADING_MODEL);

        // Preserve the original illum value
        mat->AddProperty<int>(&pCurrentMaterial->illumination_model, 1, AI_MATKEY_OBJ_ILLUM);

        // Adding material colors
        mat->AddProperty(&pCurrentMaterial->ambient, 1, AI_MATKEY_COLOR_AMBIENT);
        mat->AddProperty(&pCurrentMaterial->diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
        mat->AddProperty(&pCurrentMaterial->specular, 1, AI_MATKEY_COLOR_SPECULAR);
        mat->AddProperty(&pCurrentMaterial->emissive, 1, AI_MATKEY_COLOR_EMISSIVE);
        mat->AddProperty(&pCurrentMaterial->shineness, 1, AI_MATKEY_SHININESS);
        mat->AddProperty(&pCurrentMaterial->alpha, 1, AI_MATKEY_OPACITY);
        mat->AddProperty(&pCurrentMaterial->transparent, 1, AI_MATKEY_COLOR_TRANSPARENT);
        if (pCurrentMaterial->roughness)
            mat->AddProperty(&pCurrentMaterial->roughness.Get(), 1, AI_MATKEY_ROUGHNESS_FACTOR);
        if (pCurrentMaterial->metallic)
            mat->AddProperty(&pCurrentMaterial->metallic.Get(), 1, AI_MATKEY_METALLIC_FACTOR);
        if (pCurrentMaterial->sheen)
            mat->AddProperty(&pCurrentMaterial->sheen.Get(), 1, AI_MATKEY_SHEEN_COLOR_FACTOR);
        if (pCurrentMaterial->clearcoat_thickness)
            mat->AddProperty(&pCurrentMaterial->clearcoat_thickness.Get(), 1, AI_MATKEY_CLEARCOAT_FACTOR);
        if (pCurrentMaterial->clearcoat_roughness)
            mat->AddProperty(&pCurrentMaterial->clearcoat_roughness.Get(), 1, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR);
        mat->AddProperty(&pCurrentMaterial->anisotropy, 1, AI_MATKEY_ANISOTROPY_FACTOR);

        // Adding refraction index
        mat->AddProperty(&pCurrentMaterial->ior, 1, AI_MATKEY_REFRACTI);

        // Adding textures
        const int uvwIndex = 0;

        if (0 != pCurrentMaterial->texture.length) {
            mat->AddProperty(&pCurrentMaterial->texture, AI_MATKEY_TEXTURE_DIFFUSE(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_DIFFUSE(0));
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureDiffuseType]) {
                addTextureMappingModeProperty(mat, aiTextureType_DIFFUSE);
            }
        }

        if (0 != pCurrentMaterial->textureAmbient.length) {
            mat->AddProperty(&pCurrentMaterial->textureAmbient, AI_MATKEY_TEXTURE_AMBIENT(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_AMBIENT(0));
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureAmbientType]) {
                addTextureMappingModeProperty(mat, aiTextureType_AMBIENT);
            }
        }

        if (0 != pCurrentMaterial->textureEmissive.length) {
            mat->AddProperty(&pCurrentMaterial->textureEmissive, AI_MATKEY_TEXTURE_EMISSIVE(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_EMISSIVE(0));
        }

        if (0 != pCurrentMaterial->textureSpecular.length) {
            mat->AddProperty(&pCurrentMaterial->textureSpecular, AI_MATKEY_TEXTURE_SPECULAR(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_SPECULAR(0));
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureSpecularType]) {
                addTextureMappingModeProperty(mat, aiTextureType_SPECULAR);
            }
        }

        if (0 != pCurrentMaterial->textureBump.length) {
            mat->AddProperty(&pCurrentMaterial->textureBump, AI_MATKEY_TEXTURE_HEIGHT(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_HEIGHT(0));
            if (pCurrentMaterial->bump_multiplier != 1.0) {
                mat->AddProperty(&pCurrentMaterial->bump_multiplier, 1, AI_MATKEY_OBJ_BUMPMULT_HEIGHT(0));
            }
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureBumpType]) {
                addTextureMappingModeProperty(mat, aiTextureType_HEIGHT);
            }
        }

        if (0 != pCurrentMaterial->textureNormal.length) {
            mat->AddProperty(&pCurrentMaterial->textureNormal, AI_MATKEY_TEXTURE_NORMALS(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_NORMALS(0));
            if (pCurrentMaterial->bump_multiplier != 1.0) {
                mat->AddProperty(&pCurrentMaterial->bump_multiplier, 1, AI_MATKEY_OBJ_BUMPMULT_NORMALS(0));
            }
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureNormalType]) {
                addTextureMappingModeProperty(mat, aiTextureType_NORMALS);
            }
        }

        if (0 != pCurrentMaterial->textureReflection[0].length) {
            ObjFile::Material::TextureType type = 0 != pCurrentMaterial->textureReflection[1].length ?
                                                          ObjFile::Material::TextureReflectionCubeTopType :
                                                          ObjFile::Material::TextureReflectionSphereType;

            unsigned count = type == ObjFile::Material::TextureReflectionSphereType ? 1 : 6;
            for (unsigned i = 0; i < count; i++) {
                mat->AddProperty(&pCurrentMaterial->textureReflection[i], AI_MATKEY_TEXTURE_REFLECTION(i));
                mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_REFLECTION(i));

                if (pCurrentMaterial->clamp[type])
                    addTextureMappingModeProperty(mat, aiTextureType_REFLECTION, 1, i);
            }
        }

        if (0 != pCurrentMaterial->textureDisp.length) {
            mat->AddProperty(&pCurrentMaterial->textureDisp, AI_MATKEY_TEXTURE_DISPLACEMENT(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_DISPLACEMENT(0));
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureDispType]) {
                addTextureMappingModeProperty(mat, aiTextureType_DISPLACEMENT);
            }
        }

        if (0 != pCurrentMaterial->textureOpacity.length) {
            mat->AddProperty(&pCurrentMaterial->textureOpacity, AI_MATKEY_TEXTURE_OPACITY(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_OPACITY(0));
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureOpacityType]) {
                addTextureMappingModeProperty(mat, aiTextureType_OPACITY);
            }
        }

        if (0 != pCurrentMaterial->textureSpecularity.length) {
            mat->AddProperty(&pCurrentMaterial->textureSpecularity, AI_MATKEY_TEXTURE_SHININESS(0));
            mat->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_SHININESS(0));
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureSpecularityType]) {
                addTextureMappingModeProperty(mat, aiTextureType_SHININESS);
            }
        }

        if (0 != pCurrentMaterial->textureRoughness.length) {
            mat->AddProperty(&pCurrentMaterial->textureRoughness, _AI_MATKEY_TEXTURE_BASE, aiTextureType_DIFFUSE_ROUGHNESS, 0);
            mat->AddProperty(&uvwIndex, 1, _AI_MATKEY_UVWSRC_BASE, aiTextureType_DIFFUSE_ROUGHNESS, 0 );
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureRoughnessType]) {
                addTextureMappingModeProperty(mat, aiTextureType_DIFFUSE_ROUGHNESS);
            }
        }

        if (0 != pCurrentMaterial->textureMetallic.length) {
            mat->AddProperty(&pCurrentMaterial->textureMetallic, _AI_MATKEY_TEXTURE_BASE, aiTextureType_METALNESS, 0);
            mat->AddProperty(&uvwIndex, 1, _AI_MATKEY_UVWSRC_BASE, aiTextureType_METALNESS, 0 );
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureMetallicType]) {
                addTextureMappingModeProperty(mat, aiTextureType_METALNESS);
            }
        }

        if (0 != pCurrentMaterial->textureSheen.length) {
            mat->AddProperty(&pCurrentMaterial->textureSheen, _AI_MATKEY_TEXTURE_BASE, aiTextureType_SHEEN, 0);
            mat->AddProperty(&uvwIndex, 1, _AI_MATKEY_UVWSRC_BASE, aiTextureType_SHEEN, 0 );
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureSheenType]) {
                addTextureMappingModeProperty(mat, aiTextureType_SHEEN);
            }
        }

        if (0 != pCurrentMaterial->textureRMA.length) {
            // NOTE: glTF importer places Rough/Metal/AO texture in Unknown so doing the same here for consistency.
            mat->AddProperty(&pCurrentMaterial->textureRMA, _AI_MATKEY_TEXTURE_BASE, aiTextureType_UNKNOWN, 0);
            mat->AddProperty(&uvwIndex, 1, _AI_MATKEY_UVWSRC_BASE, aiTextureType_UNKNOWN, 0 );
            if (pCurrentMaterial->clamp[ObjFile::Material::TextureRMAType]) {
                addTextureMappingModeProperty(mat, aiTextureType_UNKNOWN);
            }
        }

        // Store material property info in material array in scene
        pScene->mMaterials[pScene->mNumMaterials] = mat;
        pScene->mNumMaterials++;
    }

    // Test number of created materials.
    ai_assert(pScene->mNumMaterials == numMaterials);
}

// ------------------------------------------------------------------------------------------------
//  Appends this node to the parent node
void ObjFileImporter::appendChildToParentNode(aiNode *pParent, aiNode *pChild) {
    // Checking preconditions
    if (pParent == nullptr || pChild == nullptr) {
        ai_assert(nullptr != pParent);
        ai_assert(nullptr != pChild);
        return;
    }

    // Assign parent to child
    pChild->mParent = pParent;

    // Copy node instances into parent node
    pParent->mNumChildren++;
    pParent->mChildren[pParent->mNumChildren - 1] = pChild;
}

// ------------------------------------------------------------------------------------------------

} // Namespace Assimp

#endif // !! ASSIMP_BUILD_NO_OBJ_IMPORTER
