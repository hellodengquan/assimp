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

/** @file RegularizeAnimationKeysProcess.cpp
 *  @brief Implementation of the optional animation key-frame regularization.
 */

#include "PostProcessing/RegularizeAnimationKeysProcess.h"

#include <assimp/Exceptional.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/config.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace Assimp;

// ---------------------------------------------------------------------------
// Finite-time guard: NaN/Inf time stamps can neither be sorted nor merged
// deterministically, so a series containing one is rejected.
bool IsFiniteTime(double t) {
    return std::isfinite(t);
}

// ---------------------------------------------------------------------------
// Vector keys (position and scaling channels): two keys at the same time are
// redundant when the stored 3D vectors agree within the configured epsilon.
// Exact equality is used when the epsilon is zero.
bool VectorKeysEquivalent(const aiVectorKey &a, const aiVectorKey &b,
        ai_real epsilon) {
    if (epsilon == ai_real(0)) {
        return a.mValue == b.mValue;
    }
    return std::fabs(static_cast<ai_real>(a.mValue.x - b.mValue.x)) <= epsilon &&
           std::fabs(static_cast<ai_real>(a.mValue.y - b.mValue.y)) <= epsilon &&
           std::fabs(static_cast<ai_real>(a.mValue.z - b.mValue.z)) <= epsilon;
}

// ---------------------------------------------------------------------------
// Rotation keys: q and -q represent the exact same orientation, so the
// equivalence test checks both signs. This is deliberately separate from a
// genuine pose difference, where neither sign of one quaternion matches the
// other: such a pair at the same time stamp is an authoring conflict and must
// be rejected instead of being "normalized" silently.
bool QuatKeysEquivalent(const aiQuatKey &a, const aiQuatKey &b,
        ai_real epsilon) {
    const auto componentsMatch = [&](const aiQuaternion &x,
                                         const aiQuaternion &y) -> bool {
        if (epsilon == ai_real(0)) {
            return x == y;
        }
        return std::fabs(static_cast<ai_real>(x.w - y.w)) <= epsilon &&
               std::fabs(static_cast<ai_real>(x.x - y.x)) <= epsilon &&
               std::fabs(static_cast<ai_real>(x.y - y.y)) <= epsilon &&
               std::fabs(static_cast<ai_real>(x.z - y.z)) <= epsilon;
    };

    if (componentsMatch(a.mValue, b.mValue)) {
        return true;
    }
    const aiQuaternion negated(-b.mValue.w, -b.mValue.x, -b.mValue.y,
            -b.mValue.z);
    return componentsMatch(a.mValue, negated);
}

// ---------------------------------------------------------------------------
// Mesh keys reference a vertex frame by index; equality is exact.
bool MeshKeysEquivalent(const aiMeshKey &a, const aiMeshKey &b,
        ai_real /*epsilon*/) {
    return a.mValue == b.mValue;
}

// ---------------------------------------------------------------------------
// Morph keys describe the same pose at a given time when both the referenced
// blend shapes and their weights agree.
bool MorphKeysEquivalent(const aiMeshMorphKey &a, const aiMeshMorphKey &b,
        ai_real epsilon) {
    if (a.mNumValuesAndWeights != b.mNumValuesAndWeights) {
        return false;
    }
    for (unsigned int i = 0; i < a.mNumValuesAndWeights; ++i) {
        if (a.mValues[i] != b.mValues[i]) {
            return false;
        }
        if (epsilon == ai_real(0)) {
            if (a.mWeights[i] != b.mWeights[i]) {
                return false;
            }
        } else if (std::fabs(a.mWeights[i] - b.mWeights[i]) >
                           static_cast<double>(epsilon)) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// RAII wrapper for aiMeshMorphKey. The C-style struct owns heap arrays but
// only has a destructor, so its implicit copy/move operations are shallow and
// aliasing the arrays leads to double frees. All in-process handling of morph
// keys therefore goes through this wrapper, which performs deep copies and
// releases its payload exactly once.
class MorphKeyHolder {
public:
    MorphKeyHolder() : mKey() {}

    explicit MorphKeyHolder(const aiMeshMorphKey &src) : mKey() {
        AssignFrom(src);
    }

    MorphKeyHolder(const MorphKeyHolder &other) : mKey() {
        AssignFrom(other.mKey);
    }

    MorphKeyHolder(MorphKeyHolder &&other) noexcept : mKey(other.mKey) {
        other.mKey.mValues = nullptr;
        other.mKey.mWeights = nullptr;
        other.mKey.mNumValuesAndWeights = 0;
    }

    MorphKeyHolder &operator=(const MorphKeyHolder &other) {
        if (this != &other) {
            Release();
            AssignFrom(other.mKey);
        }
        return *this;
    }

    MorphKeyHolder &operator=(MorphKeyHolder &&other) noexcept {
        if (this != &other) {
            Release();
            mKey = other.mKey;
            other.mKey.mValues = nullptr;
            other.mKey.mWeights = nullptr;
            other.mKey.mNumValuesAndWeights = 0;
        }
        return *this;
    }

    ~MorphKeyHolder() {
        Release();
    }

    const aiMeshMorphKey &Get() const {
        return mKey;
    }
    aiMeshMorphKey &Get() {
        return mKey;
    }

private:
    void Release() {
        delete[] mKey.mValues;
        delete[] mKey.mWeights;
        mKey.mValues = nullptr;
        mKey.mWeights = nullptr;
        mKey.mNumValuesAndWeights = 0;
    }

    void AssignFrom(const aiMeshMorphKey &src) {
        mKey.mTime = src.mTime;
        mKey.mNumValuesAndWeights = src.mNumValuesAndWeights;
        mKey.mValues = nullptr;
        mKey.mWeights = nullptr;
        if (src.mNumValuesAndWeights > 0) {
            mKey.mValues = new unsigned int[src.mNumValuesAndWeights];
            mKey.mWeights = new double[src.mNumValuesAndWeights];
            std::copy(src.mValues, src.mValues + src.mNumValuesAndWeights,
                    mKey.mValues);
            std::copy(src.mWeights, src.mWeights + src.mNumValuesAndWeights,
                    mKey.mWeights);
        }
    }

    aiMeshMorphKey mKey;
};

} // namespace

namespace Assimp {

// ---------------------------------------------------------------------------
RegularizeAnimationKeysProcess::RegularizeAnimationKeysProcess() :
        mEnabled(false),
        mVectorEpsilon(ai_real(0)),
        mQuaternionEpsilon(ai_real(0)) {
    // empty
}

// ---------------------------------------------------------------------------
bool RegularizeAnimationKeysProcess::IsActive(unsigned int /*pFlags*/) const {
    // There is no free aiPostProcessSteps bit left, hence the property gate.
    return mEnabled;
}

// ---------------------------------------------------------------------------
void RegularizeAnimationKeysProcess::SetupProperties(const Importer *pImp) {
    ai_assert(nullptr != pImp);
    mEnabled = pImp->GetPropertyBool(AI_CONFIG_PP_AKR_ENABLE, false);
    mVectorEpsilon = pImp->GetPropertyFloat(AI_CONFIG_PP_AKR_VECTOR_EPSILON,
            ai_real(0));
    mQuaternionEpsilon = pImp->GetPropertyFloat(
            AI_CONFIG_PP_AKR_QUATERNION_EPSILON, ai_real(0));

    // Negative tolerances make no sense for an equivalence test.
    if (mVectorEpsilon < ai_real(0)) {
        mVectorEpsilon = ai_real(0);
    }
    if (mQuaternionEpsilon < ai_real(0)) {
        mQuaternionEpsilon = ai_real(0);
    }
}

// ---------------------------------------------------------------------------
// Generic regularization of one key series.
//
// Stage 1 (validate): every time stamp must be finite.
// Stage 2 (order): a stable chronological sort makes the series strictly
//                  ordered where it previously contained out-of-order keys.
//                  This never changes any pose, only the storage order.
// Stage 3 (merge): consecutive keys with identical time stamps are merged.
//                  Equivalent values collapse to a single key, conflicting
//                  values reject the series because the intended pose is
//                  ambiguous.
//
// The first and the last time stamp of the series are preserved, therefore
// the animation duration and every sampled pose remain unchanged.
template <typename T>
bool RegularizeAnimationKeysProcess::RegularizeKeySeries(const T *srcKeys,
        unsigned int numSrc, T *&outKeys, unsigned int &outNum,
        bool (*valuesEquivalent)(const T &, const T &, ai_real),
        ai_real epsilon, std::string &errorOut) {
    outKeys = nullptr;
    outNum = 0;

    if (numSrc == 0) {
        // An empty series is valid: nothing to regularize.
        return true;
    }

    std::vector<T> ordered(srcKeys, srcKeys + numSrc);

    for (const T &key : ordered) {
        if (!IsFiniteTime(key.mTime)) {
            errorOut = "non-finite key time";
            return false;
        }
    }

    // Stable sort keeps the relative order of equal-time keys intact, which
    // keeps later conflict diagnostics deterministic.
    std::stable_sort(ordered.begin(), ordered.end(),
            [](const T &a, const T &b) { return a.mTime < b.mTime; });

    std::vector<T> merged;
    merged.reserve(ordered.size());
    merged.push_back(ordered.front());

    for (unsigned int i = 1; i < ordered.size(); ++i) {
        T &last = merged.back();
        const T &candidate = ordered[i];

        if (candidate.mTime != last.mTime) {
            // Different time: a real step or interpolation boundary, always
            // retained regardless of how large the value jump is.
            merged.push_back(candidate);
            continue;
        }

        // Duplicate time stamp: merge only if the values express the same
        // pose. A conflict means the authoring intent cannot be recovered.
        if (!valuesEquivalent(last, candidate, epsilon)) {
            errorOut = "conflicting values at time " +
                       std::to_string(candidate.mTime);
            return false;
        }
        // The kept key already represents this pose; the duplicate is
        // semantically redundant and is dropped. Its interpolation mode is
        // ignored because both keys describe the identical pose at the
        // identical time.
    }

    outNum = static_cast<unsigned int>(merged.size());
    outKeys = new T[outNum];
    std::copy(merged.begin(), merged.end(), outKeys);
    return true;
}

// ---------------------------------------------------------------------------
// Morph specialization: aiMeshMorphKey owns heap arrays without value
// semantics, so the work is done on deep-copying MorphKeyHolder objects.
// Only the final result is materialized as a raw aiMeshMorphKey array, with
// ownership transferred out of the holders so the arrays are freed once.
template <>
bool RegularizeAnimationKeysProcess::RegularizeKeySeries<aiMeshMorphKey>(
        const aiMeshMorphKey *srcKeys, unsigned int numSrc,
        aiMeshMorphKey *&outKeys, unsigned int &outNum,
        bool (*valuesEquivalent)(const aiMeshMorphKey &,
                const aiMeshMorphKey &, ai_real),
        ai_real epsilon, std::string &errorOut) {
    outKeys = nullptr;
    outNum = 0;

    if (numSrc == 0) {
        return true;
    }

    for (unsigned int i = 0; i < numSrc; ++i) {
        if (!IsFiniteTime(srcKeys[i].mTime)) {
            errorOut = "non-finite key time";
            return false;
        }
    }

    std::vector<MorphKeyHolder> ordered;
    ordered.reserve(numSrc);
    for (unsigned int i = 0; i < numSrc; ++i) {
        ordered.emplace_back(srcKeys[i]);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
            [](const MorphKeyHolder &a, const MorphKeyHolder &b) {
                return a.Get().mTime < b.Get().mTime;
            });

    std::vector<MorphKeyHolder> merged;
    merged.reserve(ordered.size());
    merged.push_back(ordered.front());

    for (unsigned int i = 1; i < ordered.size(); ++i) {
        const aiMeshMorphKey &candidate = ordered[i].Get();
        const aiMeshMorphKey &last = merged.back().Get();

        if (candidate.mTime != last.mTime) {
            merged.push_back(ordered[i]);
            continue;
        }
        if (!valuesEquivalent(last, candidate, epsilon)) {
            errorOut = "conflicting morph values at time " +
                       std::to_string(candidate.mTime);
            return false;
        }
        // Equivalent duplicate: the pose at this time is already kept.
    }

    outNum = static_cast<unsigned int>(merged.size());
    outKeys = new aiMeshMorphKey[outNum];
    for (unsigned int i = 0; i < outNum; ++i) {
        // Take over the heap arrays; the holder is left empty and will not
        // free them when it is destroyed.
        outKeys[i] = merged[i].Get();
        merged[i].Get().mValues = nullptr;
        merged[i].Get().mWeights = nullptr;
        merged[i].Get().mNumValuesAndWeights = 0;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool RegularizeAnimationKeysProcess::RegularizeNodeAnim(
        const aiNodeAnim *src, aiNodeAnim *dst, ai_real vectorEpsilon,
        ai_real quaternionEpsilon, std::string &errorOut) {
    dst->mNodeName = src->mNodeName;
    dst->mPreState = src->mPreState;
    dst->mPostState = src->mPostState;

    if (!RegularizeKeySeries<aiVectorKey>(src->mPositionKeys,
                src->mNumPositionKeys, dst->mPositionKeys,
                dst->mNumPositionKeys, &VectorKeysEquivalent, vectorEpsilon,
                errorOut)) {
        errorOut = std::string("position channel: ") + errorOut;
        return false;
    }
    if (!RegularizeKeySeries<aiQuatKey>(src->mRotationKeys,
                src->mNumRotationKeys, dst->mRotationKeys,
                dst->mNumRotationKeys, &QuatKeysEquivalent,
                quaternionEpsilon, errorOut)) {
        errorOut = std::string("rotation channel: ") + errorOut;
        return false;
    }
    if (!RegularizeKeySeries<aiVectorKey>(src->mScalingKeys,
                src->mNumScalingKeys, dst->mScalingKeys,
                dst->mNumScalingKeys, &VectorKeysEquivalent, vectorEpsilon,
                errorOut)) {
        errorOut = std::string("scaling channel: ") + errorOut;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool RegularizeAnimationKeysProcess::RegularizeMeshAnim(const aiMeshAnim *src,
        aiMeshAnim *dst, std::string &errorOut) {
    dst->mName = src->mName;
    return RegularizeKeySeries<aiMeshKey>(src->mKeys, src->mNumKeys,
            dst->mKeys, dst->mNumKeys, &MeshKeysEquivalent, ai_real(0),
            errorOut);
}

// ---------------------------------------------------------------------------
bool RegularizeAnimationKeysProcess::RegularizeMeshMorphAnim(
        const aiMeshMorphAnim *src, aiMeshMorphAnim *dst,
        std::string &errorOut) {
    dst->mName = src->mName;
    return RegularizeKeySeries<aiMeshMorphKey>(src->mKeys, src->mNumKeys,
            dst->mKeys, dst->mNumKeys, &MorphKeysEquivalent, ai_real(0),
            errorOut);
}

// ---------------------------------------------------------------------------
void RegularizeAnimationKeysProcess::RegularizeAnimation(aiAnimation *anim) {
    ai_assert(nullptr != anim);

    // Build the complete regularized replacement first. Only when every
    // channel succeeds are the original key arrays replaced, so a rejected
    // channel leaves the whole animation untouched.
    std::vector<aiNodeAnim *> regularizedChannels;
    std::vector<aiMeshAnim *> regularizedMeshChannels;
    std::vector<aiMeshMorphAnim *> regularizedMorphChannels;
    regularizedChannels.reserve(anim->mNumChannels);
    regularizedMeshChannels.reserve(anim->mNumMeshChannels);
    regularizedMorphChannels.reserve(anim->mNumMorphMeshChannels);

    auto cleanup = [&]() {
        for (aiNodeAnim *channel : regularizedChannels) {
            delete channel;
        }
        for (aiMeshAnim *channel : regularizedMeshChannels) {
            delete channel;
        }
        for (aiMeshMorphAnim *channel : regularizedMorphChannels) {
            delete channel;
        }
    };

    std::string error;
    for (unsigned int i = 0; i < anim->mNumChannels; ++i) {
        const aiNodeAnim *src = anim->mChannels[i];
        if (src == nullptr) {
            error = "null node animation channel";
            cleanup();
            throw DeadlyImportError(
                    "RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" rejected: ", error.c_str());
        }

        auto *dst = new aiNodeAnim();
        if (!RegularizeNodeAnim(src, dst, mVectorEpsilon,
                    mQuaternionEpsilon, error)) {
            delete dst;
            cleanup();
            ASSIMP_LOG_ERROR("RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" node \"", src->mNodeName.C_Str(),
                    "\": ", error.c_str());
            throw DeadlyImportError(
                    "RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" rejected on node \"",
                    src->mNodeName.C_Str(), "\": ", error.c_str());
        }
        regularizedChannels.push_back(dst);
    }

    for (unsigned int i = 0; i < anim->mNumMeshChannels; ++i) {
        const aiMeshAnim *src = anim->mMeshChannels[i];
        if (src == nullptr) {
            error = "null mesh animation channel";
            cleanup();
            throw DeadlyImportError(
                    "RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" rejected: ", error.c_str());
        }

        auto *dst = new aiMeshAnim();
        if (!RegularizeMeshAnim(src, dst, error)) {
            delete dst;
            cleanup();
            ASSIMP_LOG_ERROR("RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" mesh \"", src->mName.C_Str(),
                    "\": ", error.c_str());
            throw DeadlyImportError(
                    "RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" rejected on mesh \"",
                    src->mName.C_Str(), "\": ", error.c_str());
        }
        regularizedMeshChannels.push_back(dst);
    }

    for (unsigned int i = 0; i < anim->mNumMorphMeshChannels; ++i) {
        const aiMeshMorphAnim *src = anim->mMorphMeshChannels[i];
        if (src == nullptr) {
            error = "null morph animation channel";
            cleanup();
            throw DeadlyImportError(
                    "RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" rejected: ", error.c_str());
        }

        auto *dst = new aiMeshMorphAnim();
        if (!RegularizeMeshMorphAnim(src, dst, error)) {
            delete dst;
            cleanup();
            ASSIMP_LOG_ERROR("RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" morph \"", src->mName.C_Str(),
                    "\": ", error.c_str());
            throw DeadlyImportError(
                    "RegularizeAnimationKeys: animation \"",
                    anim->mName.C_Str(), "\" rejected on morph \"",
                    src->mName.C_Str(), "\": ", error.c_str());
        }
        regularizedMorphChannels.push_back(dst);
    }

    // All channels passed: commit the result. No time stamp is ever shifted,
    // so relative timing between bones and channels stays identical and the
    // animation duration is preserved.
    for (unsigned int i = 0; i < anim->mNumChannels; ++i) {
        delete anim->mChannels[i];
        anim->mChannels[i] = regularizedChannels[i];
    }
    for (unsigned int i = 0; i < anim->mNumMeshChannels; ++i) {
        delete anim->mMeshChannels[i];
        anim->mMeshChannels[i] = regularizedMeshChannels[i];
    }
    for (unsigned int i = 0; i < anim->mNumMorphMeshChannels; ++i) {
        delete anim->mMorphMeshChannels[i];
        anim->mMorphMeshChannels[i] = regularizedMorphChannels[i];
    }
}

// ---------------------------------------------------------------------------
void RegularizeAnimationKeysProcess::Execute(aiScene *pScene) {
    if (pScene == nullptr || pScene->mAnimations == nullptr) {
        return;
    }

    for (unsigned int i = 0; i < pScene->mNumAnimations; ++i) {
        aiAnimation *anim = pScene->mAnimations[i];
        if (anim != nullptr) {
            RegularizeAnimation(anim);
        }
    }
}

} // end of namespace Assimp
