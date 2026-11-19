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

/** @file  MakeLeftHandedProcess.h
 *  @brief Defines a bunch of post-processing steps to handle
 *    coordinate system conversions.
 *
 *  - RH to LH (and vice versa, the operation is self-inverse)
 *  - UV origin upper-left to lower-left
 *  - face order cw to ccw
 */
#ifndef AI_CONVERTTOLHPROCESS_H_INC
#define AI_CONVERTTOLHPROCESS_H_INC

#include <assimp/types.h>

#include "Common/BaseProcess.h"

struct aiMesh;
struct aiNodeAnim;
struct aiNode;
struct aiMaterial;
struct aiCamera;
struct aiLight;
struct aiSkeleton;

namespace Assimp {

// -----------------------------------------------------------------------------------
/** @brief The MakeLeftHandedProcess converts all imported data to a left-handed
 *   coordinate system.
 *
 * The whole scene is mirrored across the local Z=0 plane, which is the standard
 * right-handed to left-handed conversion. Every piece of spatial data is handled
 * according to its meaning:
 *  - point-like quantities (positions, translations, camera look-at targets, ...)
 *    are mirrored as points, i.e. their Z component is negated.
 *  - direction-like quantities (normals, tangents, light and camera directions, ...)
 *    are mirrored as free vectors, which is also a negation of their Z component.
 *  - rotation quaternions and transformation matrices are conjugated with the
 *    reflection, so the mirrored node/bone/animation transforms reproduce the
 *    mirrored geometry and point at the mirrored regions of the scene.
 *
 * The step is self-inverse: applying it twice restores the original scene, and its
 * result does not depend on the order in which it is combined with the
 * FlipWindingOrderProcess and FlipUVsProcess steps.
 *
 * @note RH-LH and LH-RH is the same, so this class can be used for both
 */
class ASSIMP_API MakeLeftHandedProcess final : public BaseProcess {
public:
    MakeLeftHandedProcess() = default;
    ~MakeLeftHandedProcess() override = default;

    // -------------------------------------------------------------------
    bool IsActive( unsigned int pFlags) const override;

    // -------------------------------------------------------------------
    void Execute( aiScene* pScene) override;

protected:
    // -------------------------------------------------------------------
    /** Recursively converts a node and all of its children.
     *  The node transformation is conjugated with the Z reflection.
     */
    void ProcessNode( aiNode* pNode, const aiMatrix4x4& pParentGlobalRotation);

    // -------------------------------------------------------------------
    /** Converts a single mesh to left handed coordinates.
     *  Positions, normals, tangents and bitangents of the base mesh and of
     *  every morph target (anim mesh) are mirrored across the local Z axis,
     *  together with the bone offset matrices and the bounding box.
     *  @param pMesh The mesh to convert.
     */
    void ProcessMesh( aiMesh* pMesh);

    // -------------------------------------------------------------------
    /** Converts a single material to left-handed coordinates
     * @param pMat Material to convert
     */
    void ProcessMaterial( aiMaterial* pMat);

    // -------------------------------------------------------------------
    /** Converts the given animation to LH coordinates.
     *  The rotation and translation keys are transformed, the scale keys
     *  work in local space and can therefore be left untouched.
     * @param pAnim The bone animation to transform
     */
    void ProcessAnimation( aiNodeAnim* pAnim);

    // -------------------------------------------------------------------
    /** Converts a single camera to left handed coordinates.
     *  Position, look-at target and up vector are mirrored across the local
     *  Z axis, so the converted camera looks at the mirrored target region.
     * @param pCam The camera to convert
     */
    void ProcessCamera( aiCamera* pCam);

    // -------------------------------------------------------------------
    /** Converts a single light to left handed coordinates.
     *  The light position is mirrored as a point and its direction and up
     *  vector as free vectors, so spot and directional lights keep
     *  illuminating the mirrored region of the scene.
     * @param pLight The light to convert
     */
    void ProcessLight( aiLight* pLight);

    // -------------------------------------------------------------------
    /** Converts a standalone skeleton to left handed coordinates.
     *  The offset and local matrices of every skeleton bone are conjugated
     *  with the Z reflection, matching the conversion of mesh bones and
     *  scene nodes.
     * @param pSkeleton The skeleton to convert
     */
    void ProcessSkeleton( aiSkeleton* pSkeleton);
};


// ---------------------------------------------------------------------------
/** Postprocessing step to flip the face order of the imported data
 */
class ASSIMP_API FlipWindingOrderProcess : public BaseProcess {
    friend class Importer;

public:
    /** Constructor to be privately used by Importer */
    FlipWindingOrderProcess() = default;

    /** Destructor, private as well */
    ~FlipWindingOrderProcess() override = default;

    // -------------------------------------------------------------------
    bool IsActive( unsigned int pFlags) const override;

    // -------------------------------------------------------------------
    void Execute( aiScene* pScene) override;

    /** Some other types of post-processing require winding order flips */
    static void ProcessMesh( aiMesh* pMesh);
};

// ---------------------------------------------------------------------------
/** Postprocessing step to flip the UV coordinate system of the import data
 */
class ASSIMP_API FlipUVsProcess final : public BaseProcess
{
    friend class Importer;

public:
    /** Constructor to be privately used by Importer */
    FlipUVsProcess();

    /** Destructor, private as well */
    ~FlipUVsProcess() override;

    // -------------------------------------------------------------------
    bool IsActive( unsigned int pFlags) const override;

    // -------------------------------------------------------------------
    void Execute( aiScene* pScene) override;

protected:
    void ProcessMesh( aiMesh* pMesh);
    void ProcessMaterial( aiMaterial* mat);
};

} // end of namespace Assimp

#endif // AI_CONVERTTOLHPROCESS_H_INC
