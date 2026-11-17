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

#include "AbstractImportExportBase.h"
#include "UnitTestPCH.h"

#include <assimp/commonMetaData.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>
#include <assimp/Importer.hpp>

using namespace Assimp;

class utFBXImporterExporter : public AbstractImportExportBase {
public:
    bool importerTest() override {
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/spider.fbx", aiProcess_ValidateDataStructure);
        return nullptr != scene;
    }
};

TEST_F(utFBXImporterExporter, importXFromFileTest) {
    EXPECT_TRUE(importerTest());
}

TEST_F(utFBXImporterExporter, importEmbeddedTextureNumericUV) {
    for (const char *variant : { "integer", "float", "double", "override" }) {
        SCOPED_TRACE(variant);
        const std::string path = std::string(ASSIMP_TEST_MODELS_DIR) + "/FBX/embedded_" + variant + "_uv.fbx";
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(path, aiProcess_ValidateDataStructure);
        ASSERT_NE(nullptr, scene) << importer.GetErrorString();
        ASSERT_EQ(1u, scene->mNumMeshes);
        ASSERT_EQ(1u, scene->mNumMaterials);
        ASSERT_EQ(1u, scene->mNumTextures);
        ASSERT_EQ(21u, scene->mTextures[0]->mWidth);
        EXPECT_EQ(0u, scene->mTextures[0]->mHeight);
        ASSERT_NE(nullptr, scene->mTextures[0]->pcData);
        const auto *pixel = reinterpret_cast<const unsigned char *>(scene->mTextures[0]->pcData);
        EXPECT_EQ(255u, pixel[20]);
        EXPECT_STREQ("tga", scene->mTextures[0]->achFormatHint);
        const aiMaterial *material = scene->mMaterials[0];
        aiString texture;
        ASSERT_EQ(AI_SUCCESS, material->GetTexture(aiTextureType_DIFFUSE, 0, &texture));
        EXPECT_EQ(scene->mTextures[0], scene->GetEmbeddedTexture(texture.C_Str()));
        aiUVTransform uv;
        ASSERT_EQ(AI_SUCCESS, material->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_DIFFUSE, 0), uv));
        const bool overridden = std::string(variant) == "override";
        EXPECT_FLOAT_EQ(overridden ? 6.f : -2.f, uv.mTranslation.x);
        EXPECT_FLOAT_EQ(overridden ? 7.f : 3.f, uv.mTranslation.y);
        EXPECT_FLOAT_EQ(overridden ? 8.f : 4.f, uv.mScaling.x);
        EXPECT_FLOAT_EQ(overridden ? 9.f : 5.f, uv.mScaling.y);
    }
}

TEST_F(utFBXImporterExporter, rejectNonNumericTextureUV) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/embedded_invalid_uv.fbx", 0);
    ASSERT_NE(nullptr, scene) << importer.GetErrorString();
    ASSERT_EQ(1u, scene->mNumMaterials);
    EXPECT_EQ(0u, scene->mNumTextures);
    EXPECT_EQ(0u, scene->mMaterials[0]->GetTextureCount(aiTextureType_DIFFUSE));
}

TEST_F(utFBXImporterExporter, importBareBoxWithoutColorsAndTextureCoords) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/box.fbx", aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene);
    EXPECT_EQ(scene->mNumMeshes, 1u);
    aiMesh *mesh = scene->mMeshes[0];
    EXPECT_EQ(mesh->mNumFaces, 12u);
    EXPECT_EQ(mesh->mNumVertices, 36u);
}

TEST_F(utFBXImporterExporter, importCubesWithNoNames) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/cubes_nonames.fbx", aiProcess_ValidateDataStructure);
    ASSERT_TRUE(scene);

    ASSERT_TRUE(scene->mRootNode);
    const auto root = scene->mRootNode;
    ASSERT_STREQ(root->mName.C_Str(), "RootNode");
    ASSERT_TRUE(root->mChildren);
    ASSERT_EQ(root->mNumChildren, 2u);
}

TEST_F(utFBXImporterExporter, importCubesWithUnicodeDuplicatedNames) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/cubes_with_names.fbx", aiProcess_ValidateDataStructure);
    ASSERT_TRUE(scene);

    ASSERT_TRUE(scene->mRootNode);
    const auto root = scene->mRootNode;
    ASSERT_STREQ(root->mName.C_Str(), "RootNode");
    ASSERT_TRUE(root->mChildren);
    ASSERT_EQ(root->mNumChildren, 2u);

    const auto child0 = root->mChildren[0];
    ASSERT_TRUE(child0);
    ASSERT_STREQ(child0->mName.C_Str(), "Cube2");
    ASSERT_TRUE(child0->mChildren);
    ASSERT_EQ(child0->mNumChildren, 1u);

    const auto child00 = child0->mChildren[0];
    ASSERT_TRUE(child00);
    ASSERT_STREQ(child00->mName.C_Str(), "\xd0\x9a\xd1\x83\xd0\xb1\x31");

    const auto child1 = root->mChildren[1];
    ASSERT_TRUE(child1);
    ASSERT_STREQ(child1->mName.C_Str(), "Cube3");
    ASSERT_TRUE(child1->mChildren);
    ASSERT_EQ(child1->mNumChildren, 1u);

    const auto child10 = child1->mChildren[0];
    ASSERT_TRUE(child10);
    ASSERT_STREQ(child10->mName.C_Str(), "\xd0\x9a\xd1\x83\xd0\xb1\x31");
}

TEST_F(utFBXImporterExporter, importCubesComplexTransform) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/cubes_with_mirroring_and_pivot.fbx", aiProcess_ValidateDataStructure);
    ASSERT_TRUE(scene);

    ASSERT_TRUE(scene->mRootNode);
    const auto root = scene->mRootNode;
    ASSERT_STREQ(root->mName.C_Str(), "RootNode");
    ASSERT_TRUE(root->mChildren);
    ASSERT_EQ(root->mNumChildren, 2u);

    const auto child0 = root->mChildren[0];
    ASSERT_TRUE(child0);
    ASSERT_STREQ(child0->mName.C_Str(), "Cube2");
    ASSERT_TRUE(child0->mChildren);
    ASSERT_EQ(child0->mNumChildren, 1u);

    const auto child00 = child0->mChildren[0];
    ASSERT_TRUE(child00);
    ASSERT_STREQ(child00->mName.C_Str(), "Cube1");

    const auto child1 = root->mChildren[1];
    ASSERT_TRUE(child1);
    ASSERT_STREQ(child1->mName.C_Str(), "Cube3");

    auto parent = child1;
    const size_t chain_length = 8u;
    const char *chainStr[chain_length] = {
        "Cube1_$AssimpFbx$_Translation",
        "Cube1_$AssimpFbx$_RotationPivot",
        "Cube1_$AssimpFbx$_RotationPivotInverse",
        "Cube1_$AssimpFbx$_ScalingOffset",
        "Cube1_$AssimpFbx$_ScalingPivot",
        "Cube1_$AssimpFbx$_Scaling",
        "Cube1_$AssimpFbx$_ScalingPivotInverse",
        "Cube1"
    };
    for (size_t i = 0; i < chain_length; ++i) {
        ASSERT_TRUE(parent->mChildren);
        ASSERT_EQ(parent->mNumChildren, 1u);
        auto node = parent->mChildren[0];
        ASSERT_TRUE(node);
        ASSERT_STREQ(node->mName.C_Str(), chainStr[i]);
        parent = node;
    }
    ASSERT_EQ(0u, parent->mNumChildren) << "Leaf node";
}

TEST_F(utFBXImporterExporter, importCloseToIdentityTransforms) {
    Assimp::Importer importer;
    // This was asserting in FBXConverter.cpp because the transforms appeared to be the identity by one test, but not by another.
    // This asset should now load successfully.
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/close_to_identity_transforms.fbx", aiProcess_ValidateDataStructure);
    ASSERT_TRUE(scene);
}

TEST_F(utFBXImporterExporter, importPhongMaterial) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/phong_cube.fbx", aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene);
    EXPECT_EQ(1u, scene->mNumMaterials);
    const aiMaterial *mat = scene->mMaterials[0];
    EXPECT_NE(nullptr, mat);
    float f;
    aiColor3D c;

    // phong_cube.fbx has all properties defined
    EXPECT_EQ(mat->Get(AI_MATKEY_COLOR_DIFFUSE, c), aiReturn_SUCCESS);
    EXPECT_EQ(c, aiColor3D(0.5, 0.25, 0.25));
    EXPECT_EQ(mat->Get(AI_MATKEY_COLOR_SPECULAR, c), aiReturn_SUCCESS);
    EXPECT_EQ(c, aiColor3D(0.25, 0.25, 0.5));
    EXPECT_EQ(mat->Get(AI_MATKEY_SHININESS_STRENGTH, f), aiReturn_SUCCESS);
    EXPECT_EQ(f, 0.5f);
    EXPECT_EQ(mat->Get(AI_MATKEY_SHININESS, f), aiReturn_SUCCESS);
    EXPECT_EQ(f, 10.0f);
    EXPECT_EQ(mat->Get(AI_MATKEY_COLOR_AMBIENT, c), aiReturn_SUCCESS);
    EXPECT_EQ(c, aiColor3D(0.125, 0.25, 0.25));
    EXPECT_EQ(mat->Get(AI_MATKEY_COLOR_EMISSIVE, c), aiReturn_SUCCESS);
    EXPECT_EQ(c, aiColor3D(0.25, 0.125, 0.25));
    EXPECT_EQ(mat->Get(AI_MATKEY_COLOR_TRANSPARENT, c), aiReturn_SUCCESS);
    EXPECT_EQ(c, aiColor3D(0.75, 0.5, 0.25));
    EXPECT_EQ(mat->Get(AI_MATKEY_OPACITY, f), aiReturn_SUCCESS);
    EXPECT_EQ(f, 0.5f);
}

TEST_F(utFBXImporterExporter, importUnitScaleFactor) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/global_settings.fbx", aiProcess_ValidateDataStructure);

    EXPECT_NE(nullptr, scene);
    EXPECT_NE(nullptr, scene->mMetaData);

    float factor(0.0f);
    scene->mMetaData->Get("UnitScaleFactor", factor);
    EXPECT_EQ(500.0f, factor);

    scene->mMetaData->Set("UnitScaleFactor", factor * 2.0f);
    scene->mMetaData->Get("UnitScaleFactor", factor);
    EXPECT_EQ(1000.0f, factor);
}

TEST_F(utFBXImporterExporter, importEmbeddedAsciiTest) {
    // see https://github.com/assimp/assimp/issues/1957
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/embedded_ascii/box.FBX", aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene);

    EXPECT_EQ(1u, scene->mNumMaterials);
    aiMaterial *mat = scene->mMaterials[0];
    ASSERT_NE(nullptr, mat);

    aiString path;
    aiTextureMapMode modes[2];
    EXPECT_EQ(aiReturn_SUCCESS, mat->GetTexture(aiTextureType_DIFFUSE, 0, &path, nullptr, nullptr, nullptr, nullptr, modes));
    ASSERT_STREQ(path.C_Str(), "..\\..\\..\\Desktop\\uv_test.png");

    ASSERT_EQ(1u, scene->mNumTextures);
    ASSERT_TRUE(scene->mTextures[0]->pcData);
    ASSERT_EQ(439176u, scene->mTextures[0]->mWidth) << "FBX ASCII base64 compression splits data by 512Kb, it should be two parts for this texture";
}

TEST_F(utFBXImporterExporter, importEmbeddedFragmentedAsciiTest) {
    // see https://github.com/assimp/assimp/issues/1957
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/embedded_ascii/box_embedded_texture_fragmented.fbx", aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene);

    EXPECT_EQ(1u, scene->mNumMaterials);
    aiMaterial *mat = scene->mMaterials[0];
    ASSERT_NE(nullptr, mat);

    aiString path;
    aiTextureMapMode modes[2];
    ASSERT_EQ(aiReturn_SUCCESS, mat->GetTexture(aiTextureType_DIFFUSE, 0, &path, nullptr, nullptr, nullptr, nullptr, modes));
    ASSERT_STREQ(path.C_Str(), "paper.png");

    ASSERT_EQ(1u, scene->mNumTextures);
    ASSERT_TRUE(scene->mTextures[0]->pcData);
    ASSERT_EQ(968029u, scene->mTextures[0]->mWidth) << "FBX ASCII base64 compression splits data by 512Kb, it should be two parts for this texture";
}

TEST_F(utFBXImporterExporter, fbxTokenizeTestTest) {
    //Assimp::Importer importer;
    //const aiScene* scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/transparentTest2.fbx", aiProcess_ValidateDataStructure);
    //EXPECT_NE(nullptr, scene);
}

TEST_F(utFBXImporterExporter, importOrphantEmbeddedTextureTest) {
    // see https://github.com/assimp/assimp/issues/1957
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/box_orphant_embedded_texture.fbx", aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene);

    EXPECT_EQ(1u, scene->mNumMaterials);
    aiMaterial *mat = scene->mMaterials[0];
    ASSERT_NE(nullptr, mat);

    aiString path;
    aiTextureMapMode modes[2];
    ASSERT_EQ(aiReturn_SUCCESS, mat->GetTexture(aiTextureType_DIFFUSE, 0, &path, nullptr, nullptr, nullptr, nullptr, modes));
    ASSERT_STREQ(path.C_Str(), "..\\Primitives\\GridGrey.tga");

    ASSERT_EQ(1u, scene->mNumTextures);
    ASSERT_TRUE(scene->mTextures[0]->pcData);
    ASSERT_EQ(9026u, scene->mTextures[0]->mWidth) << "FBX ASCII base64 compression used for a texture.";
}

TEST_F(utFBXImporterExporter, sceneMetadata) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/global_settings.fbx",
            aiProcess_ValidateDataStructure);
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->mMetaData, nullptr);
    {
        ASSERT_TRUE(scene->mMetaData->HasKey(AI_METADATA_SOURCE_FORMAT));
        aiString format;
        ASSERT_TRUE(scene->mMetaData->Get(AI_METADATA_SOURCE_FORMAT, format));
        ASSERT_EQ(strcmp(format.C_Str(), "Autodesk FBX Importer"), 0);
    }
    {
        ASSERT_TRUE(scene->mMetaData->HasKey(AI_METADATA_SOURCE_FORMAT_VERSION));
        aiString version;
        ASSERT_TRUE(scene->mMetaData->Get(AI_METADATA_SOURCE_FORMAT_VERSION, version));
        ASSERT_EQ(strcmp(version.C_Str(), "7400"), 0);
    }
    {
        ASSERT_TRUE(scene->mMetaData->HasKey(AI_METADATA_SOURCE_GENERATOR));
        aiString generator;
        ASSERT_TRUE(scene->mMetaData->Get(AI_METADATA_SOURCE_GENERATOR, generator));
        ASSERT_EQ(strncmp(generator.C_Str(), "Blender", 7), 0);
    }
}

TEST_F(utFBXImporterExporter, importCustomAxes) {
    // see https://github.com/assimp/assimp/issues/5494
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/embedded_ascii/box.FBX", aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene);

    // The ASCII box has customised the Up and Forward axes, verify that the RootNode transform has applied it
    ASSERT_FALSE(scene->mRootNode->mTransformation.IsIdentity()) << "Did not apply the custom axis transform";

    aiVector3D upVec{ 0, 0, 1 }; // Up is +Z
    aiVector3D forwardVec{ 0, -1, 0 }; // Forward is -Y
    aiVector3D rightVec{ 1, 0, 0 }; // Right is +X
    aiMatrix4x4 mat(rightVec.x, rightVec.y, rightVec.z, 0.0f,
            upVec.x, upVec.y, upVec.z, 0.0f,
            forwardVec.x, forwardVec.y, forwardVec.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);

    EXPECT_EQ(mat, scene->mRootNode->mTransformation);
}

TEST_F(utFBXImporterExporter, importIgnoreCustomAxes) {
    // see https://github.com/assimp/assimp/issues/5494
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/embedded_ascii/box.FBX", aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene);

    // Verify that the RootNode transform has NOT applied the custom axes
    EXPECT_TRUE(scene->mRootNode->mTransformation.IsIdentity());
}

TEST_F(utFBXImporterExporter, importCubesWithOutOfRangeFloat) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/cubes_with_outofrange_float.fbx", aiProcess_ValidateDataStructure);
    ASSERT_NE(nullptr, scene);
    ASSERT_TRUE(scene->mRootNode);
}

TEST_F(utFBXImporterExporter, importMaxPbrMaterialsMetalRoughness) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/maxPbrMaterial_metalRough.fbx", aiProcess_ValidateDataStructure);
    ASSERT_NE(nullptr, scene);
    ASSERT_TRUE(scene->mRootNode);

    ASSERT_EQ(scene->mNumMaterials, 1u);
    const aiMaterial* mat = scene->mMaterials[0];
    aiString texture;
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_BASE_COLOR, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\albedo.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_METALNESS, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\metalness.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_EMISSION_COLOR, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\emission.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_NORMAL_CAMERA, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\normal.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE_ROUGHNESS, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\roughness.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_AMBIENT_OCCLUSION, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\occlusion.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_OPACITY, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\opacity.png"));

    // The material contains values for standard properties (e.g. SpecularColor), where 3ds Max has presumably
    // used formulas to map the Pbr values into the standard material model. However, the pbr values themselves
    // are available in the material as untyped "raw" properties. We check that these are correctly parsed:

    aiColor4D baseColor;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|basecolor", aiTextureType_NONE, 0, baseColor), aiReturn_SUCCESS);
    EXPECT_EQ(baseColor, aiColor4D(0, 1, 1, 1));

    float metalness;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|metalness", aiTextureType_NONE, 0, metalness), aiReturn_SUCCESS);
    EXPECT_EQ(metalness, 0.25f);

    float roughness;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|roughness", aiTextureType_NONE, 0, roughness), aiReturn_SUCCESS);
    EXPECT_EQ(roughness, 0.5f);

    int useGlossiness;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|useGlossiness", aiTextureType_NONE, 0, useGlossiness), aiReturn_SUCCESS);
    EXPECT_EQ(useGlossiness, 2); // 1 = Roughness map is glossiness, 2 = Roughness map is roughness.

    float bumpMapAmt; // Presumably amount.
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|bump_map_amt", aiTextureType_NONE, 0, bumpMapAmt), aiReturn_SUCCESS);
    EXPECT_EQ(bumpMapAmt, 0.75f);

    aiColor4D emitColor;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|emit_color", aiTextureType_NONE, 0, emitColor), aiReturn_SUCCESS);
    EXPECT_EQ(emitColor, aiColor4D(1, 1, 0, 1));
}

TEST_F(utFBXImporterExporter, importMaxPbrMaterialsSpecularGloss) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/maxPbrMaterial_specGloss.fbx", aiProcess_ValidateDataStructure);
    ASSERT_NE(nullptr, scene);
    ASSERT_TRUE(scene->mRootNode);

    ASSERT_EQ(scene->mNumMaterials, 1u);
    const aiMaterial* mat = scene->mMaterials[0];
    aiString texture;
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_BASE_COLOR, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\albedo.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_SPECULAR, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\specular.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_EMISSION_COLOR, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\emission.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_NORMAL_CAMERA, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\normal.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_SHININESS, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\glossiness.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_AMBIENT_OCCLUSION, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\occlusion.png"));
    ASSERT_EQ(mat->Get(AI_MATKEY_TEXTURE(aiTextureType_OPACITY, 0), texture), AI_SUCCESS);
    EXPECT_EQ(texture, aiString("Textures\\opacity.png"));

    // The material contains values for standard properties (e.g. SpecularColor), where 3ds Max has presumably
    // used formulas to map the Pbr values into the standard material model. However, the pbr values themselves
    // are available in the material as untyped "raw" properties. We check that these are correctly parsed:

    aiColor4D baseColor;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|basecolor", aiTextureType_NONE, 0, baseColor), aiReturn_SUCCESS);
    EXPECT_EQ(baseColor, aiColor4D(0, 1, 1, 1));

    aiColor4D specular;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|Specular", aiTextureType_NONE, 0, specular), aiReturn_SUCCESS);
    EXPECT_EQ(specular, aiColor4D(1, 1, 0, 1));

    float glossiness;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|glossiness", aiTextureType_NONE, 0, glossiness), aiReturn_SUCCESS);
    EXPECT_EQ(glossiness, 0.33f);

    int useGlossiness;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|useGlossiness", aiTextureType_NONE, 0, useGlossiness), aiReturn_SUCCESS);
    EXPECT_EQ(useGlossiness, 1); // 1 = Glossiness map is glossiness, 2 = Glossiness map is roughness.

    float bumpMapAmt; // Presumably amount.
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|bump_map_amt", aiTextureType_NONE, 0, bumpMapAmt), aiReturn_SUCCESS);
    EXPECT_EQ(bumpMapAmt, 0.66f);

    aiColor4D emitColor;
    ASSERT_EQ(mat->Get("$raw.3dsMax|main|emit_color", aiTextureType_NONE, 0, emitColor), aiReturn_SUCCESS);
    EXPECT_EQ(emitColor, aiColor4D(1, 0, 1, 1));
}

TEST_F(utFBXImporterExporter, importSkeletonTest) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(ASSIMP_TEST_MODELS_DIR "/FBX/animation_with_skeleton.fbx", aiProcess_ValidateDataStructure);
    ASSERT_NE(nullptr, scene);
    ASSERT_TRUE(scene->mRootNode);
}

namespace {

std::string fbxTriangleWithLastIndex(const char *lastIndex) {
    return std::string(
            "; FBX 7.4.0 project file\n"
            "FBXHeaderExtension:  {\n"
            "\tFBXHeaderVersion: 1003\n"
            "\tFBXVersion: 7400\n"
            "}\n"
            "Objects:  {\n"
            "\tGeometry: 10, \"Geometry::\", \"Mesh\" {\n"
            "\t\tVertices: *9 {\n"
            "\t\t\ta: 0,0,0,1,0,0,0,1,0\n"
            "\t\t}\n"
            "\t\tPolygonVertexIndex: *3 {\n"
            "\t\t\ta: 0,1,") +
           lastIndex +
           "\n"
           "\t\t}\n"
           "\t}\n"
           "\tModel: 20, \"Model::triangle\", \"Mesh\" {\n"
           "\t}\n"
           "}\n"
           "Connections:  {\n"
           "\tC: \"OO\",10,20\n"
           "\tC: \"OO\",20,0\n"
           "}\n";
}

} // namespace

TEST_F(utFBXImporterExporter, importPolygonVertexIndexIntMin) {
    // The last index of a polygon is stored as -(vertex + 1); decoding INT_MIN
    // must not overflow, and it is out of range for the mesh.
    Assimp::Importer importer;
    const std::string valid = fbxTriangleWithLastIndex("-3");
    const aiScene *scene = importer.ReadFileFromMemory(valid.data(), valid.size(), aiProcess_ValidateDataStructure, "fbx");
    ASSERT_NE(nullptr, scene);
    ASSERT_EQ(1u, scene->mNumMeshes);
    EXPECT_EQ(1u, scene->mMeshes[0]->mNumFaces);

    // The geometry is rejected as out of range, so the triangle mesh is not imported.
    const std::string intMin = fbxTriangleWithLastIndex("-2147483648");
    scene = importer.ReadFileFromMemory(intMin.data(), intMin.size(), aiProcess_ValidateDataStructure, "fbx");
    ASSERT_NE(nullptr, scene);
    EXPECT_EQ(0u, scene->mNumMeshes);
}

namespace {

/// <summary>
/// Imports one of the small hand-written FBX fixtures with the pivot setting
/// under test and requires the result to pass the data structure validation.
/// </summary>
const aiScene *importFbxFixture(Assimp::Importer &importer, const char *fileName, bool preservePivots) {
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, preservePivots);
    const std::string path = std::string(ASSIMP_TEST_MODELS_DIR) + "/FBX/" + fileName;
    const aiScene *scene = importer.ReadFile(path, aiProcess_ValidateDataStructure);
    EXPECT_NE(nullptr, scene) << importer.GetErrorString();
    return scene;
}

/// <summary>
/// Builds the world matrix of a node, i.e. the product of all local
/// transformations from the root node down to (and including) this node.
/// </summary>
aiMatrix4x4 worldMatrix(const aiNode *node) {
    aiMatrix4x4 result;
    for (; node != nullptr; node = node->mParent) {
        result = node->mTransformation * result;
    }
    return result;
}

/// <summary>
/// Finds the node that references the mesh whose name matches meshName.
/// Geometry names and node names are independent in FBX, so the fixtures name
/// both identically to make the lookup unambiguous.
/// </summary>
const aiNode *findMeshNode(const aiNode *node, const aiScene *scene, const char *meshName) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        if (mesh != nullptr && mesh->mName == aiString(meshName)) {
            return node;
        }
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        if (const aiNode *found = findMeshNode(node->mChildren[i], scene, meshName)) {
            return found;
        }
    }
    return nullptr;
}

/// <summary>
/// Transforms the vertices of the named mesh into world space. The vertices of
/// the fixtures are all distinct, so the helper also verifies the count.
/// </summary>
std::vector<aiVector3D> worldVertices(const aiScene *scene, const char *meshName) {
    const aiNode *node = findMeshNode(scene->mRootNode, scene, meshName);
    EXPECT_NE(nullptr, node) << "no node references mesh " << meshName;
    if (node == nullptr) {
        return {};
    }

    const aiMatrix4x4 world = worldMatrix(node);
    std::vector<aiVector3D> result;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        if (mesh->mName != aiString(meshName)) {
            continue;
        }
        result.reserve(mesh->mNumVertices);
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            result.push_back(world * mesh->mVertices[v]);
        }
    }
    return result;
}

/// <summary>
/// Requires two world-space vertex lists to match within a small tolerance.
/// </summary>
void expectVerticesNear(const std::vector<aiVector3D> &actual, const std::vector<aiVector3D> &expected, const char *what) {
    ASSERT_EQ(expected.size(), actual.size()) << what;
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_NEAR(expected[i].x, actual[i].x, 1e-5f) << what << " vertex " << i << " x";
        EXPECT_NEAR(expected[i].y, actual[i].y, 1e-5f) << what << " vertex " << i << " y";
        EXPECT_NEAR(expected[i].z, actual[i].z, 1e-5f) << what << " vertex " << i << " z";
    }
}

/// <summary>
/// Requires two world matrices to match within a small tolerance.
/// </summary>
void expectMatrixNear(const aiMatrix4x4 &actual, const aiMatrix4x4 &expected, const char *what) {
    for (unsigned int r = 0; r < 4; ++r) {
        for (unsigned int c = 0; c < 4; ++c) {
            EXPECT_NEAR(expected[r][c], actual[r][c], 1e-5f) << what << " [" << r << "][" << c << "]";
        }
    }
}

/// <summary>
/// Returns true if any node below (and including) node has the given name.
/// </summary>
bool containsNode(const aiNode *node, const char *name) {
    if (node->mName == aiString(name)) {
        return true;
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        if (containsNode(node->mChildren[i], name)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_F(utFBXImporterExporter, pivotPreservationEquivalentWorldVertices) {
    // pivot_transforms.fbx has three independent branches:
    //   RotationPivotNode : translation (1,0,0), rotate 90 deg around Z about the
    //                       pivot (2,0,0) -> a point (x,y,0) maps to (3-y, x-2, 0).
    //                       The child keeps its own local translation (0,0,1).
    //   ScalingPivotNode  : scale 2 about the pivot (1,0,0) -> (x,y,0) maps to
    //                       (2x-1, 2y, 0).
    //   PreRotationNode   : pre-rotation of 90 deg about Z followed by a 90 deg
    //                       local rotation -> both rotations add up to 180 deg.
    // Collapsing the pivots into the node transform and keeping them as extra
    // nodes must produce the same world-space geometry.
    Assimp::Importer preserving;
    const aiScene *withPivots = importFbxFixture(preserving, "pivot_transforms.fbx", true);
    ASSERT_NE(nullptr, withPivots);

    Assimp::Importer collapsing;
    const aiScene *withoutPivots = importFbxFixture(collapsing, "pivot_transforms.fbx", false);
    ASSERT_NE(nullptr, withoutPivots);

    const char *meshes[] = { "RotationPivotGeometry", "RotationPivotChildGeometry", "ScalingPivotGeometry", "PreRotationGeometry" };
    for (const char *mesh : meshes) {
        SCOPED_TRACE(mesh);
        expectVerticesNear(worldVertices(withoutPivots, mesh), worldVertices(withPivots, mesh), "pivot setting must not move geometry");
    }

    // Spot-check the analytically derived positions so the test does not only
    // compare the two code paths against each other.
    const std::vector<aiVector3D> rotated = worldVertices(withPivots, "RotationPivotGeometry");
    ASSERT_EQ(4u, rotated.size());
    EXPECT_NEAR(3.f, rotated[0].x, 1e-5f);
    EXPECT_NEAR(-2.f, rotated[0].y, 1e-5f);
    EXPECT_NEAR(3.f, rotated[1].x, 1e-5f);
    EXPECT_NEAR(-1.f, rotated[1].y, 1e-5f);

    // The pivot only affects the node that declares it: the child keeps the
    // parent rotation but adds its own unmodified local translation.
    const std::vector<aiVector3D> child = worldVertices(withPivots, "RotationPivotChildGeometry");
    ASSERT_EQ(4u, child.size());
    EXPECT_NEAR(-2.f, child[0].y, 1e-5f);
    EXPECT_NEAR(1.f, child[0].z, 1e-5f);
    EXPECT_NEAR(1.f, child[3].z, 1e-5f);

    // Scaling around the pivot at (1,0,0) keeps that point fixed.
    const std::vector<aiVector3D> scaled = worldVertices(withPivots, "ScalingPivotGeometry");
    ASSERT_EQ(4u, scaled.size());
    EXPECT_NEAR(-1.f, scaled[0].x, 1e-5f);
    EXPECT_NEAR(0.f, scaled[0].y, 1e-5f);
    EXPECT_NEAR(1.f, scaled[1].x, 1e-5f);
    EXPECT_NEAR(2.f, scaled[2].y, 1e-5f);

    // PreRotation (0,0,90) is applied before the local rotation (0,0,90); both
    // rotate around the same axis, so the quad ends up rotated 180 degrees.
    const std::vector<aiVector3D> preRotated = worldVertices(withPivots, "PreRotationGeometry");
    ASSERT_EQ(4u, preRotated.size());
    EXPECT_NEAR(0.f, preRotated[0].x, 1e-5f);
    EXPECT_NEAR(0.f, preRotated[0].y, 1e-5f);
    EXPECT_NEAR(-1.f, preRotated[1].x, 1e-5f);
    EXPECT_NEAR(0.f, preRotated[1].y, 1e-5f);
    EXPECT_NEAR(-1.f, preRotated[2].x, 1e-5f);
    EXPECT_NEAR(-1.f, preRotated[2].y, 1e-5f);
    EXPECT_NEAR(0.f, preRotated[3].x, 1e-5f);
    EXPECT_NEAR(-1.f, preRotated[3].y, 1e-5f);
}

TEST_F(utFBXImporterExporter, pivotTransformNodeChainBoundary) {
    // Preserving pivots represents them with generated nodes named
    // <model>_$AssimpFbx$_<component>; collapsing them folds the same arithmetic
    // into the model node itself. The world transform of the model node and the
    // local transform of its child must be identical either way.
    Assimp::Importer preserving;
    const aiScene *withPivots = importFbxFixture(preserving, "pivot_transforms.fbx", true);
    ASSERT_NE(nullptr, withPivots);

    Assimp::Importer collapsing;
    const aiScene *withoutPivots = importFbxFixture(collapsing, "pivot_transforms.fbx", false);
    ASSERT_NE(nullptr, withoutPivots);

    // The generated pivots only exist in the preserving import.
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "RotationPivotNode_$AssimpFbx$_RotationPivot"));
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "RotationPivotNode_$AssimpFbx$_RotationPivotInverse"));
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "ScalingPivotNode_$AssimpFbx$_ScalingPivot"));
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "ScalingPivotNode_$AssimpFbx$_ScalingPivotInverse"));
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "PreRotationNode_$AssimpFbx$_PreRotation"));

    EXPECT_FALSE(containsNode(withoutPivots->mRootNode, "RotationPivotNode_$AssimpFbx$_RotationPivot"));
    EXPECT_FALSE(containsNode(withoutPivots->mRootNode, "ScalingPivotNode_$AssimpFbx$_ScalingPivot"));
    EXPECT_FALSE(containsNode(withoutPivots->mRootNode, "PreRotationNode_$AssimpFbx$_PreRotation"));

    const aiNode *withChain = withPivots->mRootNode->FindNode("RotationPivotNode");
    ASSERT_NE(nullptr, withChain);
    // With the chain, the model node itself is untransformed; its parents carry
    // the declared translation and the pivot rotation.
    EXPECT_TRUE(withChain->mTransformation.IsIdentity(1e-6f));

    const aiNode *collapsed = withoutPivots->mRootNode->FindNode("RotationPivotNode");
    ASSERT_NE(nullptr, collapsed);
    EXPECT_FALSE(collapsed->mTransformation.IsIdentity(1e-6f));

    // Both representations describe the same placement in world space.
    expectMatrixNear(worldMatrix(collapsed), worldMatrix(withChain), "rotation pivot world matrix");

    // The child is attached below the chain in one mode and directly to the
    // model node in the other, but its own local transform never changes.
    ASSERT_EQ(1u, withChain->mNumChildren);
    ASSERT_EQ(1u, collapsed->mNumChildren);
    const aiNode *chainChild = withChain->mChildren[0];
    const aiNode *collapsedChild = collapsed->mChildren[0];
    ASSERT_STREQ("RotationPivotChild", chainChild->mName.C_Str());
    ASSERT_STREQ("RotationPivotChild", collapsedChild->mName.C_Str());
    expectMatrixNear(collapsedChild->mTransformation, chainChild->mTransformation, "child local transform");

    // That transform is the plain translation (0,0,1) declared in the file; the
    // parent pivot reaches the child only through the hierarchy.
    EXPECT_NEAR(0.f, collapsedChild->mTransformation.a4, 1e-6f);
    EXPECT_NEAR(0.f, collapsedChild->mTransformation.b4, 1e-6f);
    EXPECT_NEAR(1.f, collapsedChild->mTransformation.c4, 1e-6f);
}

TEST_F(utFBXImporterExporter, geometricOffsetStaysOnOwningNode) {
    // geometric_offset.fbx has GeoParent (translation 1 on X, geometric
    // translation 10 on X, geometric scale 2) with the child GeoChild, plus an
    // unrelated GeoLeaf that has its own geometric translation (0,5,0).
    Assimp::Importer preserving;
    const aiScene *withPivots = importFbxFixture(preserving, "geometric_offset.fbx", true);
    ASSERT_NE(nullptr, withPivots);

    Assimp::Importer collapsing;
    const aiScene *withoutPivots = importFbxFixture(collapsing, "geometric_offset.fbx", false);
    ASSERT_NE(nullptr, withoutPivots);

    // The geometry of the owning node is independent of the import setting.
    expectVerticesNear(worldVertices(withPivots, "GeoParentGeometry"), worldVertices(withoutPivots, "GeoParentGeometry"), "owning node");
    expectVerticesNear(worldVertices(withPivots, "GeoLeafGeometry"), worldVertices(withoutPivots, "GeoLeafGeometry"), "leaf node");

    // GeoParent is translated by (1,0,0); its quad spans [0,1]x[0,1], is offset
    // by the geometric translation (10,0,0) and then scaled by 2.
    const std::vector<aiVector3D> parent = worldVertices(withPivots, "GeoParentGeometry");
    ASSERT_EQ(4u, parent.size());
    EXPECT_NEAR(11.f, parent[0].x, 1e-5f);
    EXPECT_NEAR(13.f, parent[1].x, 1e-5f);
    EXPECT_NEAR(2.f, parent[2].y, 1e-5f);

    // GeoLeaf has no children and applies only its own geometric translation.
    const std::vector<aiVector3D> leaf = worldVertices(withPivots, "GeoLeafGeometry");
    ASSERT_EQ(4u, leaf.size());
    EXPECT_NEAR(4.f, leaf[0].x, 1e-5f);
    EXPECT_NEAR(5.f, leaf[0].y, 1e-5f);

    // Preserving the geometric transform means adding generated nodes around the
    // model node, including the inverses that undo it for the children.
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "GeoParent_$AssimpFbx$_GeometricTranslation"));
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "GeoParent_$AssimpFbx$_GeometricTranslationInverse"));
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "GeoParent_$AssimpFbx$_GeometricScaling"));
    EXPECT_TRUE(containsNode(withPivots->mRootNode, "GeoParent_$AssimpFbx$_GeometricScalingInverse"));

    EXPECT_FALSE(containsNode(withoutPivots->mRootNode, "GeoParent_$AssimpFbx$_GeometricTranslation"));
    EXPECT_FALSE(containsNode(withoutPivots->mRootNode, "GeoParent_$AssimpFbx$_GeometricScaling"));

    // The mesh stays attached to the plain model node, not to a generated node,
    // so consumers looking up a node by name keep working in both modes.
    const aiNode *preservedParent = withPivots->mRootNode->FindNode("GeoParent");
    ASSERT_NE(nullptr, preservedParent);
    EXPECT_EQ(1u, preservedParent->mNumMeshes);
    const aiNode *collapsedParent = withoutPivots->mRootNode->FindNode("GeoParent");
    ASSERT_NE(nullptr, collapsedParent);
    EXPECT_EQ(1u, collapsedParent->mNumMeshes);
}

TEST_F(utFBXImporterExporter, geometricOffsetChildInheritanceBoundary) {
    // For a child of a node that declares a geometric transform the two import
    // settings disagree. Preserving the transform inserts the inverse nodes
    // between the parent and its children, honouring the FBX rule that geometric
    // transforms affect the node's own geometry only. Collapsing it folds the
    // transform into the parent's node matrix, so the child inherits it too.
    //
    // The collapsed result contradicts the FBX specification. It is pinned down
    // here so the boundary is explicit: a future fix will fail this test on
    // purpose rather than silently moving every such child.
    Assimp::Importer preserving;
    const aiScene *withPivots = importFbxFixture(preserving, "geometric_offset.fbx", true);
    ASSERT_NE(nullptr, withPivots);

    Assimp::Importer collapsing;
    const aiScene *withoutPivots = importFbxFixture(collapsing, "geometric_offset.fbx", false);
    ASSERT_NE(nullptr, withoutPivots);

    const std::vector<aiVector3D> preserved = worldVertices(withPivots, "GeoChildGeometry");
    const std::vector<aiVector3D> collapsed = worldVertices(withoutPivots, "GeoChildGeometry");
    ASSERT_EQ(4u, preserved.size());
    ASSERT_EQ(4u, collapsed.size());

    // Preserved: the child only inherits the node translation and lands at
    // (1,0,1)..(2,1,1); it stays a unit quad.
    EXPECT_NEAR(1.f, preserved[0].x, 1e-5f);
    EXPECT_NEAR(1.f, preserved[0].z, 1e-5f);
    EXPECT_NEAR(2.f, preserved[2].x, 1e-5f);
    EXPECT_NEAR(1.f, preserved[2].y, 1e-5f);

    // Collapsed: the child picks up the parent's geometric translation and
    // scale, landing at x in [11,13], y in [0,2] and z = 2.
    EXPECT_NEAR(11.f, collapsed[0].x, 1e-5f);
    EXPECT_NEAR(2.f, collapsed[0].z, 1e-5f);
    EXPECT_NEAR(13.f, collapsed[2].x, 1e-5f);
    EXPECT_NEAR(2.f, collapsed[2].y, 1e-5f);

    // The inverse nodes are exactly what keeps the child clean when the
    // transform is preserved; the collapsed import has no such nodes, so the
    // child becomes a direct child of the transformed node.
    const aiNode *childWithChain = withPivots->mRootNode->FindNode("GeoChild");
    ASSERT_NE(nullptr, childWithChain);
    ASSERT_NE(nullptr, childWithChain->mParent);
    EXPECT_STREQ("GeoParent_$AssimpFbx$_GeometricTranslationInverse", childWithChain->mParent->mName.C_Str());

    const aiNode *collapsedParent = withoutPivots->mRootNode->FindNode("GeoParent");
    ASSERT_NE(nullptr, collapsedParent);
    ASSERT_EQ(1u, collapsedParent->mNumChildren);
    EXPECT_STREQ("GeoChild", collapsedParent->mChildren[0]->mName.C_Str());
}

TEST_F(utFBXImporterExporter, mirroringWithPivotsBoundaries) {
    // mirroring.fbx mirrors its child with a negative X scale while also using
    // rotation and scaling pivots and a rotation offset. The negative
    // determinant is what flips the winding of the geometry, so it has to
    // survive both import settings.
    Assimp::Importer preserving;
    const aiScene *withPivots = importFbxFixture(preserving, "mirroring.fbx", true);
    ASSERT_NE(nullptr, withPivots);

    Assimp::Importer collapsing;
    const aiScene *withoutPivots = importFbxFixture(collapsing, "mirroring.fbx", false);
    ASSERT_NE(nullptr, withoutPivots);

    expectVerticesNear(worldVertices(withPivots, "MirrorParentGeometry"), worldVertices(withoutPivots, "MirrorParentGeometry"), "mirrored parent");
    expectVerticesNear(worldVertices(withPivots, "MirrorChildGeometry"), worldVertices(withoutPivots, "MirrorChildGeometry"), "mirrored child");

    // The parent's quad lands at x in [-1,0] and y in [1,2]: mirrored in X, then
    // shifted by the rotation offset (0,1,0).
    const std::vector<aiVector3D> parent = worldVertices(withPivots, "MirrorParentGeometry");
    ASSERT_EQ(4u, parent.size());
    EXPECT_NEAR(0.f, parent[0].x, 1e-5f);
    EXPECT_NEAR(1.f, parent[0].y, 1e-5f);
    EXPECT_NEAR(-1.f, parent[1].x, 1e-5f);
    EXPECT_NEAR(2.f, parent[3].y, 1e-5f);

    // The child keeps its own translation (0.5,0,1); the mirror turns it into
    // x = -0.5 and -1.5, the pivots leave it otherwise untouched.
    const std::vector<aiVector3D> child = worldVertices(withPivots, "MirrorChildGeometry");
    ASSERT_EQ(4u, child.size());
    EXPECT_NEAR(-0.5f, child[0].x, 1e-5f);
    EXPECT_NEAR(1.f, child[0].y, 1e-5f);
    EXPECT_NEAR(1.f, child[0].z, 1e-5f);
    EXPECT_NEAR(-1.5f, child[1].x, 1e-5f);

    // A negative determinant on the world matrix of both nodes proves the
    // mirroring was not flattened away by the pivot handling.
    const aiNode *mirrorNode = withoutPivots->mRootNode->FindNode("MirrorParent");
    ASSERT_NE(nullptr, mirrorNode);
    EXPECT_LT(worldMatrix(mirrorNode).Determinant(), 0.f);
    const aiNode *mirrorChild = withoutPivots->mRootNode->FindNode("MirrorChild");
    ASSERT_NE(nullptr, mirrorChild);
    EXPECT_LT(worldMatrix(mirrorChild).Determinant(), 0.f);

    // The scaling pivot is represented by generated nodes only when preserved.
    EXPECT_EQ(nullptr, withoutPivots->mRootNode->FindNode("MirrorParent_$AssimpFbx$_ScalingPivot"));
    EXPECT_NE(nullptr, withPivots->mRootNode->FindNode("MirrorParent_$AssimpFbx$_ScalingPivot"));
}

TEST_F(utFBXImporterExporter, inheritTypeDeclarationsStayConsistent) {
    // inherit_type.fbx contains three branches whose transforms are identical
    // and which only differ in how the FBX InheritType is declared: omitted
    // (default), RrSs (0) and RSrs (1). The three branches must produce the
    // same geometry in both import modes, and each keeps its own child.
    for (bool preservePivots : { true, false }) {
        SCOPED_TRACE(preservePivots ? "preserve pivots" : "collapse pivots");
        Assimp::Importer importer;
        const aiScene *scene = importFbxFixture(importer, "inherit_type.fbx", preservePivots);
        ASSERT_NE(nullptr, scene);
        ASSERT_EQ(6u, scene->mNumMeshes);

        const std::vector<aiVector3D> reference = worldVertices(scene, "InheritDefault");
        ASSERT_EQ(4u, reference.size());
        expectVerticesNear(worldVertices(scene, "InheritRrSs"), reference, "RrSs declaration");
        expectVerticesNear(worldVertices(scene, "InheritRSrs"), reference, "RSrs declaration");

        const std::vector<aiVector3D> childReference = worldVertices(scene, "InheritDefaultChild");
        ASSERT_EQ(4u, childReference.size());
        expectVerticesNear(worldVertices(scene, "InheritRrSsChild"), childReference, "RrSs child");
        expectVerticesNear(worldVertices(scene, "InheritRSrsChild"), childReference, "RSrs child");

        // The parent scales X by 2 and rotates 90 degrees around Y; the child is
        // one unit along its local Z, which the parent maps onto world X.
        EXPECT_NEAR(1.f, reference[0].x, 1e-5f);
        EXPECT_NEAR(2.f, childReference[0].x, 1e-5f);

        // Each parent node keeps exactly one direct child model node.
        const aiNode *parent = scene->mRootNode->FindNode("InheritDefault");
        ASSERT_NE(nullptr, parent);
        ASSERT_EQ(1u, parent->mNumChildren);
        EXPECT_STREQ("InheritDefaultChild", parent->mChildren[0]->mName.C_Str());
    }
}

TEST_F(utFBXImporterExporter, inheritTypeIsReportedInNodeMetadata) {
    // The InheritType is surfaced as node metadata. An omitted declaration means
    // the FBX default applies and therefore produces no metadata entry, while an
    // explicit declaration is exposed verbatim.
    Assimp::Importer importer;
    const aiScene *scene = importFbxFixture(importer, "inherit_type.fbx", true);
    ASSERT_NE(nullptr, scene);

    const aiNode *defaultNode = scene->mRootNode->FindNode("InheritDefault");
    ASSERT_NE(nullptr, defaultNode);
    ASSERT_NE(nullptr, defaultNode->mMetaData);
    EXPECT_FALSE(defaultNode->mMetaData->HasKey("InheritType"));

    const aiNode *rrssNode = scene->mRootNode->FindNode("InheritRrSs");
    ASSERT_NE(nullptr, rrssNode);
    ASSERT_NE(nullptr, rrssNode->mMetaData);
    int32_t rrssType = -1;
    ASSERT_TRUE(rrssNode->mMetaData->Get(std::string("InheritType"), rrssType));
    EXPECT_EQ(0, rrssType);

    const aiNode *rsrsNode = scene->mRootNode->FindNode("InheritRSrs");
    ASSERT_NE(nullptr, rsrsNode);
    ASSERT_NE(nullptr, rsrsNode->mMetaData);
    int32_t rsrsType = -1;
    ASSERT_TRUE(rsrsNode->mMetaData->Get(std::string("InheritType"), rsrsType));
    EXPECT_EQ(1, rsrsType);
}
