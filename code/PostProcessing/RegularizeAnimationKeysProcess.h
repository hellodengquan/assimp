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

/** @file RegularizeAnimationKeysProcess.h
 *  @brief Optional post-processing step that regularizes animation key
 *         frames: semantically equivalent redundant keys at the same
 *         point in time are merged and the resulting time series are
 *         made strictly increasing. The step is opt-in and rejects
 *         inputs whose authoring intent cannot be determined.
 */
#pragma once
#ifndef AI_REGULARIZEANIMATIONKEYSPROCESS_H_INC
#define AI_REGULARIZEANIMATIONKEYSPROCESS_H_INC

#include "Common/BaseProcess.h"

#include <assimp/anim.h>

struct aiAnimation;
struct aiMeshAnim;
struct aiMeshMorphAnim;
struct aiNodeAnim;
struct aiScene;

namespace Assimp {

// ---------------------------------------------------------------------------
/** The RegularizeAnimationKeys post-processing step.
 *
 *  The step is disabled by default and activated through the
 *  #AI_CONFIG_PP_AKR_ENABLE import property. When enabled it processes
 *  every animation of the scene:
 *
 *  @li Keys sharing the same time stamp are merged if they carry
 *      semantically equivalent values. Rotation quaternions are treated
 *      as equivalent when they are equal or when one is the negation of
 *      the other, because q and -q describe the same orientation.
 *  @li Out-of-order keys are sorted chronologically; this is lossless.
 *  @li The regularized time series of every channel is strictly
 *      increasing in time. The animation duration (first and last key
 *      time of every series) and all sampled poses are preserved.
 *
 *  The operation is transactional per animation: all channels of an
 *  animation are regularized on temporary storage first. If any channel
 *  contains data whose intent is ambiguous - such as conflicting values
 *  at the same time stamp - the whole animation is rejected, the
 *  original data is left untouched and a DeadlyImportError is thrown so
 *  the caller receives an explicit failure.
 */
class ASSIMP_API RegularizeAnimationKeysProcess : public BaseProcess {
public:
    RegularizeAnimationKeysProcess();
    ~RegularizeAnimationKeysProcess() override = default;

    /// The step is opt-in, it is activated through AI_CONFIG_PP_AKR_ENABLE.
    bool IsActive(unsigned int pFlags) const override;

    /// Read the configuration properties of the owning Importer.
    void SetupProperties(const Importer *pImp) override;

    /// Regularize all animations of the given scene.
    void Execute(aiScene *pScene) override;

    /// Regularize a single animation, throwing on ambiguous input.
    void RegularizeAnimation(aiAnimation *anim);

private:
    // Regularize one node animation channel (position/rotation/scaling).
    // Returns false and fills errorOut when the channel must be rejected.
    static bool RegularizeNodeAnim(const aiNodeAnim *src, aiNodeAnim *dst,
            ai_real vectorEpsilon, ai_real quaternionEpsilon,
            std::string &errorOut);

    // Regularize a mesh (vertex track) animation channel.
    static bool RegularizeMeshAnim(const aiMeshAnim *src, aiMeshAnim *dst,
            std::string &errorOut);

    // Regularize a morph animation channel.
    static bool RegularizeMeshMorphAnim(const aiMeshMorphAnim *src,
            aiMeshMorphAnim *dst, std::string &errorOut);

    // Generic per-series worker used by the typed specializations above.
    template <typename T>
    static bool RegularizeKeySeries(const T *srcKeys, unsigned int numSrc,
            T *&outKeys, unsigned int &outNum,
            bool (*valuesEquivalent)(const T &, const T &, ai_real),
            ai_real epsilon, std::string &errorOut);

private:
    bool mEnabled;
    ai_real mVectorEpsilon;
    ai_real mQuaternionEpsilon;
};

} // end of namespace Assimp

#endif // AI_REGULARIZEANIMATIONKEYSPROCESS_H_INC
