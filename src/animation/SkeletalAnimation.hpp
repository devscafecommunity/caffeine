#pragma once

#include "animation/AnimationComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/ComponentQuery.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "math/Math.hpp"
#include "containers/FixedString.hpp"
#include "containers/HashMap.hpp"

#include <vector>
#include <cmath>
#include <functional>

namespace Caffeine::Animation {

using namespace Caffeine;

// ============================================================================
// @brief Bone — nó da hierarquia esquelética.
// ============================================================================
struct Bone {
    FixedString<32> name;
    i32             parentIndex   = -1;
    Mat4            bindPoseInverse;
    Mat4            localTransform;
};

// ============================================================================
// @brief Esqueleto — hierarquia de bones.
// ============================================================================
struct Skeleton {
    std::vector<Bone> bones;

    u32 boneCount() const { return static_cast<u32>(bones.size()); }

    i32 findBone(const char* name) const {
        for (u32 i = 0; i < boneCount(); ++i) {
            if (bones[i].name == name) return static_cast<i32>(i);
        }
        return -1;
    }
};

// ============================================================================
// @brief Keyframe de animação esquelética.
// ============================================================================
struct SkeletalKeyframe {
    f32  time  = 0.0f;
    Vec3 position { 0.0f, 0.0f, 0.0f };
    Quat rotation { Quat::identity() };
    Vec3 scale    { 1.0f, 1.0f, 1.0f };
};

// ============================================================================
// @brief Clipe de animação esquelética.
// ============================================================================
struct SkeletalClip {
    FixedString<32>  name;
    f32              duration = 0.0f;
    f32              fps      = 30.0f;
    bool             loop     = true;

    /// Canal por bone: bone_index -> lista de keyframes.
    HashMap<u32, std::vector<SkeletalKeyframe>> channels;

    /// Amostra o clipe no tempo `time` (segundos) e preenche `boneTransforms`
    /// com as matrizes ósseas finais (world * inverseBindPose).
    /// `boneTransforms` deve ter tamanho >= skeleton.boneCount().
    void sampleAt(f32 time, const Skeleton& skeleton, std::vector<Mat4>& boneTransforms) const;
};

// ============================================================================
// @brief Blend tree — combina múltiplas animações com pesos.
// ============================================================================
struct BlendNode {
    enum class Type : u8 {
        Clip,
        Blend1D,
        Blend2D
    };

    Type type = Type::Clip;
    const SkeletalClip* clip = nullptr;
    std::vector<std::pair<f32, BlendNode*>> children;
    f32 parameter = 0.0f;
};

// ============================================================================
// @brief Estado de animação esquelética (análogo a AnimationState da Fase 4).
// ============================================================================
struct SkeletalAnimationState {
    FixedString<32>                  name;
    const SkeletalClip*              clip  = nullptr;
    f32                              speed = 1.0f;
    std::vector<AnimationTransition> transitions;
};

// ============================================================================
// @brief Componente de animação esquelética de uma entidade.
// ============================================================================
struct SkeletalAnimator {
    const Skeleton*         skeleton     = nullptr;
    std::vector<Mat4>       boneMatrices;

    // State machine
    HashMap<FixedString<32>, SkeletalAnimationState> states;
    FixedString<32>  currentState;
    f32              timeInState = 0.0f;
    f32              blendWeight = 1.0f;
    bool             paused      = false;

    // Blend tree (opcional)
    BlendNode* blendTree = nullptr;
};

// ============================================================================
// @brief Sistema de animação esquelética.
// ============================================================================
class SkeletalAnimationSystem : public ECS::ISystem {
public:
    void onUpdate(ECS::World& world, f32 dt) override;

    // Controle de playback
    void play(ECS::World& world, ECS::Entity e, const char* stateName);
    void pause(ECS::World& world, ECS::Entity e);
    void resume(ECS::World& world, ECS::Entity e);
    void setSpeed(ECS::World& world, ECS::Entity e, f32 speed);
    bool isPlaying(ECS::World& world, ECS::Entity e, const char* stateName) const;

private:
    static void evaluateTransitions(SkeletalAnimator& anim);
    static void sampleBlendTree(const BlendNode* node, f32 time,
                                 const Skeleton& skeleton,
                                 std::vector<Mat4>& outResult,
                                 f32 weight = 1.0f);
    static void accumulateBlend(std::vector<Mat4>& accum, const std::vector<Mat4>& sample, f32 weight);
};

// ============================================================================
// Implementação inline de SkeletalClip::sampleAt
// ============================================================================

namespace Detail {

inline Vec3 lerpVec3(const Vec3& a, const Vec3& b, f32 t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

inline Mat4 buildTRS(const Vec3& pos, const Quat& rot, const Vec3& scale) {
    Mat4 t = Mat4::translation(pos.x, pos.y, pos.z);
    Mat4 r = rot.toMatrix();
    Mat4 s = Mat4::scale(scale.x, scale.y, scale.z);
    return t * r * s;
}

} // namespace Detail

inline void SkeletalClip::sampleAt(f32 time, const Skeleton& skeleton, std::vector<Mat4>& boneTransforms) const {
    u32 boneCount = skeleton.boneCount();
    if (boneTransforms.size() < boneCount) {
        boneTransforms.resize(boneCount);
    }

    // Wrap/clamp time
    f32 t = time;
    if (duration > 0.0f) {
        if (loop) {
            t = std::fmod(time, duration);
            if (t < 0.0f) t += duration;
        } else {
            t = (time < 0.0f) ? 0.0f : (time > duration ? duration : time);
        }
    }

    // ── Passo 1: amostrar keyframes e montar matrizes locais ──
    std::vector<Mat4> localMatrices(boneCount, Mat4::identity());

    for (u32 boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
        const std::vector<SkeletalKeyframe>* keys = channels.get(boneIdx);
        if (!keys || keys->empty()) continue;

        const auto& kf = *keys;

        // Antes do primeiro keyframe
        if (t <= kf[0].time) {
            localMatrices[boneIdx] = Detail::buildTRS(kf[0].position, kf[0].rotation, kf[0].scale);
            continue;
        }

        // Após o último keyframe
        if (t >= kf.back().time) {
            localMatrices[boneIdx] = Detail::buildTRS(kf.back().position, kf.back().rotation, kf.back().scale);
            continue;
        }

        // Encontrar keyframes de interpolação
        usize i = 0;
        for (; i + 1 < kf.size(); ++i) {
            if (kf[i + 1].time >= t) break;
        }

        const SkeletalKeyframe& k0 = kf[i];
        const SkeletalKeyframe& k1 = kf[i + 1];
        f32 segDur = k1.time - k0.time;
        f32 frac = (segDur > 0.0f) ? (t - k0.time) / segDur : 0.0f;

        Vec3 pos = Detail::lerpVec3(k0.position, k1.position, frac);
        Quat rot = Quat::slerp(k0.rotation, k1.rotation, frac);
        Vec3 s   = Detail::lerpVec3(k0.scale, k1.scale, frac);

        localMatrices[boneIdx] = Detail::buildTRS(pos, rot, s);
    }

    // ── Passo 2: calcular matrizes mundo ──
    // Os bones são armazenados em ordem topológica (pais antes dos filhos).
    std::vector<Mat4> worldMatrices(boneCount);
    for (u32 boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
        const Bone& bone = skeleton.bones[boneIdx];
        if (bone.parentIndex >= 0) {
            worldMatrices[boneIdx] = worldMatrices[static_cast<u32>(bone.parentIndex)] * localMatrices[boneIdx];
        } else {
            worldMatrices[boneIdx] = localMatrices[boneIdx];
        }
    }

    // ── Passo 3: multiplicar por inverseBindPose para skinning ──
    for (u32 boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
        boneTransforms[boneIdx] = worldMatrices[boneIdx] * skeleton.bones[boneIdx].bindPoseInverse;
    }
}

// ============================================================================
// Implementação inline de SkeletalAnimationSystem
// ============================================================================

inline void SkeletalAnimationSystem::onUpdate(ECS::World& world, f32 dt) {
    ECS::ComponentQuery q;
    q.with<SkeletalAnimator>();

    world.forEach<SkeletalAnimator>(q,
        [dt](ECS::Entity entity, SkeletalAnimator& anim) {
            (void)entity;
            if (anim.paused) return;
            if (!anim.skeleton || anim.skeleton->bones.empty()) return;

            u32 boneCount = anim.skeleton->boneCount();
            if (anim.boneMatrices.size() < boneCount) {
                anim.boneMatrices.resize(boneCount, Mat4::identity());
            }

            // Estado inicial: bind pose
            std::vector<Mat4> blendedResult(boneCount);
            for (u32 i = 0; i < boneCount; ++i) {
                blendedResult[i] = anim.skeleton->bones[i].bindPoseInverse;
            }

            f32 totalWeight = 0.0f;

            // ── Blend tree ──
            if (anim.blendTree) {
                sampleBlendTree(anim.blendTree, anim.timeInState,
                                *anim.skeleton, blendedResult, 1.0f);
                totalWeight = 1.0f;
            }

            // ── State machine ──
            const SkeletalAnimationState* state = anim.states.get(anim.currentState);
            if (state && state->clip) {
                evaluateTransitions(anim);

                // Re-obter state após possível transição
                state = anim.states.get(anim.currentState);
                if (!state || !state->clip) return;

                f32 effectiveSpeed = state->speed;
                anim.timeInState += dt * effectiveSpeed;

                const SkeletalClip* clip = state->clip;

                // Amostrar clipe
                std::vector<Mat4> clipMatrices(boneCount, Mat4::identity());
                clip->sampleAt(anim.timeInState, *anim.skeleton, clipMatrices);

                if (totalWeight > 0.0f) {
                    // Blend entre acumulado (blend tree) e state machine
                    f32 invW = 1.0f - anim.blendWeight;
                    for (u32 i = 0; i < boneCount; ++i) {
                        for (u32 k = 0; k < 16; ++k) {
                            anim.boneMatrices[i].data()[k] =
                                blendedResult[i].data()[k] * invW +
                                clipMatrices[i].data()[k] * anim.blendWeight;
                        }
                    }
                } else {
                    anim.boneMatrices = clipMatrices;
                }
            } else if (totalWeight > 0.0f) {
                anim.boneMatrices = blendedResult;
            }

            // Verificar fim de clipe não-looping
            if (state && state->clip && !state->clip->loop) {
                if (state->clip->duration > 0.0f && anim.timeInState >= state->clip->duration) {
                    anim.timeInState = state->clip->duration;
                }
            }
        });
}

inline void SkeletalAnimationSystem::play(ECS::World& world, ECS::Entity e, const char* stateName) {
    SkeletalAnimator* anim = world.get<SkeletalAnimator>(e);
    if (!anim) return;
    anim->currentState = stateName;
    anim->timeInState  = 0.0f;
    anim->paused       = false;
}

inline void SkeletalAnimationSystem::pause(ECS::World& world, ECS::Entity e) {
    SkeletalAnimator* anim = world.get<SkeletalAnimator>(e);
    if (anim) anim->paused = true;
}

inline void SkeletalAnimationSystem::resume(ECS::World& world, ECS::Entity e) {
    SkeletalAnimator* anim = world.get<SkeletalAnimator>(e);
    if (anim) anim->paused = false;
}

inline void SkeletalAnimationSystem::setSpeed(ECS::World& world, ECS::Entity e, f32 speed) {
    SkeletalAnimator* anim = world.get<SkeletalAnimator>(e);
    if (anim) anim->blendWeight = speed;
}

inline bool SkeletalAnimationSystem::isPlaying(ECS::World& world, ECS::Entity e, const char* stateName) const {
    const SkeletalAnimator* anim = world.get<SkeletalAnimator>(e);
    if (!anim || anim->paused) return false;
    return anim->currentState == FixedString<32>(stateName);
}

inline void SkeletalAnimationSystem::evaluateTransitions(SkeletalAnimator& anim) {
    const SkeletalAnimationState* state = anim.states.get(anim.currentState);
    if (!state) return;

    for (const auto& t : state->transitions) {
        if (!t.legacyCondition) continue;
        if (t.hasExitTime && state->clip) {
            if (anim.timeInState < state->clip->duration) continue;
        }
        if (t.legacyCondition()) {
            anim.currentState = t.toState;
            anim.timeInState  = 0.0f;
            return;
        }
    }
}

inline void SkeletalAnimationSystem::sampleBlendTree(
    const BlendNode* node, f32 time,
    const Skeleton& skeleton,
    std::vector<Mat4>& outResult,
    f32 weight)
{
    if (!node) return;

    switch (node->type) {
    case BlendNode::Type::Clip:
        if (node->clip) {
            std::vector<Mat4> sample(skeleton.boneCount(), Mat4::identity());
            node->clip->sampleAt(time, skeleton, sample);
            accumulateBlend(outResult, sample, weight);
        }
        break;

    case BlendNode::Type::Blend1D: {
        if (node->children.empty()) break;
        // Encontrar os dois children que cercam o parâmetro
        f32 param = node->parameter;

        // Se param <= primeiro child ou children só tem 1
        if (param <= node->children[0].first || node->children.size() == 1) {
            sampleBlendTree(node->children[0].second, time, skeleton, outResult, weight);
            break;
        }

        // Se param >= último child
        if (param >= node->children.back().first) {
            sampleBlendTree(node->children.back().second, time, skeleton, outResult, weight);
            break;
        }

        // Interpolar entre dois children
        for (usize i = 0; i + 1 < node->children.size(); ++i) {
            f32 a = node->children[i].first;
            f32 b = node->children[i + 1].first;
            if (param >= a && param <= b) {
                f32 t = (b > a) ? (param - a) / (b - a) : 0.0f;
                // Amostrar ambos e interpolar
                std::vector<Mat4> sampleA(skeleton.boneCount(), Mat4::identity());
                std::vector<Mat4> sampleB(skeleton.boneCount(), Mat4::identity());
                sampleBlendTree(node->children[i].second, time, skeleton, sampleA, 1.0f);
                sampleBlendTree(node->children[i + 1].second, time, skeleton, sampleB, 1.0f);
                for (u32 j = 0; j < skeleton.boneCount(); ++j) {
                    // Interpolar as translações
                    Vec3 posA(sampleA[j].data()[0], sampleA[j].data()[1], sampleA[j].data()[2]);
                    Vec3 posB(sampleB[j].data()[0], sampleB[j].data()[1], sampleB[j].data()[2]);
                    Vec3 pos = Detail::lerpVec3(posA, posB, t);
                    (void)pos;
                    // As bone matrices completas usam lerp para simplificar
                    Mat4 blended;
                    for (u32 k = 0; k < 16; ++k) {
                        blended.data()[k] = sampleA[j].data()[k] + (sampleB[j].data()[k] - sampleA[j].data()[k]) * t;
                    }
                    // Acumular com peso
                    for (u32 k = 0; k < 16; ++k) {
                        outResult[j].data()[k] += blended.data()[k] * weight;
                    }
                }
                return;
            }
        }
        break;
    }

    case BlendNode::Type::Blend2D:
        // Blend2D: simplificado — amostra primeiro child
        if (!node->children.empty()) {
            sampleBlendTree(node->children[0].second, time, skeleton, outResult, weight);
        }
        break;
    }
}

inline void SkeletalAnimationSystem::accumulateBlend(
    std::vector<Mat4>& accum,
    const std::vector<Mat4>& sample,
    f32 weight)
{
    u32 count = static_cast<u32>(accum.size() < sample.size() ? accum.size() : sample.size());
    for (u32 i = 0; i < count; ++i) {
        for (u32 k = 0; k < 16; ++k) {
            accum[i].data()[k] += sample[i].data()[k] * weight;
        }
    }
}

} // namespace Caffeine::Animation
