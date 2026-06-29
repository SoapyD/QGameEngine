# Chapter 42: Animation Layers & Partial Body Animation

## What You'll Learn
- Why a single animation clip per entity is not enough for believable characters
- Bone masks: selecting which bones a layer controls using bitsets
- Building masks automatically from a skeleton's bone hierarchy
- The AnimationLayer structure: clip, weight, blend mode, bone mask
- Evaluating multiple layers bottom-up with override and additive blending
- The math behind additive animation: delta rotations and delta positions
- Rewriting the animationSystem to process a stack of layers
- Practical examples: walk+shoot, reload+strafe, hit reactions, breathing
- Layer management: playing, fading, and removing layers at runtime
- Integration with AI, animation events, and ragdoll systems

---

## The Problem

In Chapter 33, we built a skeletal animation system. The `Animator` component plays one clip at a time, optionally cross-fading between two clips during transitions. This works for simple cases — an enemy idles, then walks, then attacks. Each state plays a single full-body animation.

But real characters do multiple things at once. A soldier runs forward while firing a rifle. The legs play a run cycle, the arms and torso play a firing animation. With the current system, this is impossible. We can play the run animation or the fire animation, but not both simultaneously on different parts of the body.

```
CURRENT SYSTEM — ONE CLIP AT A TIME

     ┌─────────┐           ┌─────────┐
     │  "run"  │    OR     │ "shoot" │
     │ (whole  │           │ (whole  │
     │  body)  │           │  body)  │
     └─────────┘           └─────────┘

     Legs move,            Arms shoot,
     arms swing.           legs frozen.

     Cannot do both at the same time.
```

```
LAYERED SYSTEM — MULTIPLE CLIPS ON DIFFERENT BONES

     ┌─────────────────────┐
     │  Layer 1: "shoot"   │  ← upper body only (spine, arms, head)
     │  (override, upper)  │
     ├─────────────────────┤
     │  Layer 0: "run"     │  ← full body base
     │  (full body)        │
     └─────────────────────┘

     Result: legs run, arms and torso shoot.
```

Here are some scenarios that require layered animation:

```
SCENARIO                         BASE LAYER        OVERLAY LAYER
─────────────────────────────────────────────────────────────────
Enemy runs and shoots            run (full body)    shoot (upper body, override)
Player reloads while strafing    strafe (full body) reload (arms, override)
Hit reaction while running       run (full body)    flinch_left (upper body, additive)
Breathing on idle                idle (full body)   breathe (chest, additive, low weight)
Head tracking a target           any (full body)    look_at (head/neck, override)
```

The solution is **animation layers**. Each layer controls a subset of bones and applies its animation on top of the layers beneath it. The base layer covers the full body. Overlay layers affect only the bones in their **bone mask**, leaving the rest of the skeleton untouched.

---

## Bone Masks

A bone mask tells a layer which bones it is allowed to modify. Bones outside the mask are left alone — they keep whatever pose the layers below assigned.

### BoneMask Type

We use `std::bitset` for the mask. It is fixed-size, cache-friendly, and supports fast bitwise operations. We define a maximum bone count that covers any skeleton we will encounter.

```cpp
// In src/engine/animation/bone_mask.h

#pragma once

#include "engine/animation/skeleton.h"
#include <bitset>
#include <string>

constexpr size_t MAX_BONES = 128;

using BoneMask = std::bitset<MAX_BONES>;
```

A set bit means "this layer controls this bone." A cleared bit means "leave this bone alone."

### Predefined Masks

For a humanoid skeleton, there are several common mask configurations. Each one is built by starting at a root bone and including all of its descendants.

```
FULL SKELETON — UPPER vs LOWER BODY MASK

                     Head        ─┐
                      │           │
                     Neck         │
                      │           │
               ┌──── Spine2 ────┐ │  UPPER BODY
               │      │         │ │  (Spine and above)
          L.Shoulder  Spine1 R.Shoulder │
               │      │         │ │
          L.Arm       │     R.Arm │
               │      │         │ │
          L.ForeArm  Spine  R.ForeArm │
               │      │         │ │
          L.Hand      │     R.Hand ─┘
                      │
                     Hips       ─┐
                    ┌─┴─┐        │
               L.UpLeg  R.UpLeg  │  LOWER BODY
                    │       │    │  (Hips and below)
               L.Leg    R.Leg   │
                    │       │    │
               L.Foot   R.Foot ─┘
```

### Building Masks From the Skeleton

Given a root bone name, we walk the skeleton and set the bit for that bone and every descendant. Because bones are stored parent-before-child, a single forward pass finds all descendants.

```cpp
// In src/engine/animation/bone_mask.h (continued)

// Build a mask that includes 'rootBoneName' and all of its descendants.
// This walks the skeleton array once. Because parents always appear before
// children (guaranteed by Ch 33), checking if a bone's parent is already
// in the mask is sufficient to find all descendants.
BoneMask buildBoneMask(const Skeleton& skeleton, const std::string& rootBoneName) {
    BoneMask mask;

    int rootIndex = skeleton.findBone(rootBoneName);
    if (rootIndex < 0) return mask;  // Bone not found — empty mask

    mask.set(rootIndex);

    // Forward pass: if a bone's parent is in the mask, include this bone too
    for (size_t i = rootIndex + 1; i < skeleton.bones.size(); i++) {
        int parent = skeleton.bones[i].parentIndex;
        if (parent >= 0 && mask.test(parent)) {
            mask.set(i);
        }
    }

    return mask;
}

// Build a full-body mask (all bones set)
BoneMask buildFullBodyMask(const Skeleton& skeleton) {
    BoneMask mask;
    for (size_t i = 0; i < skeleton.bones.size(); i++) {
        mask.set(i);
    }
    return mask;
}

// Combine two masks (useful for "arms + head" without torso)
BoneMask combineMasks(const BoneMask& a, const BoneMask& b) {
    return a | b;
}

// Invert a mask (useful for "everything except the head")
BoneMask invertMask(const BoneMask& mask, size_t boneCount) {
    BoneMask inverted;
    for (size_t i = 0; i < boneCount; i++) {
        if (!mask.test(i)) inverted.set(i);
    }
    return inverted;
}
```

### Creating Standard Masks

At initialisation time, after loading the skeleton, create the masks you will need. Note that `buildBoneMask("Hips")` would include everything (Spine is a child of Hips), so for a lower-body-only mask, invert the upper body mask instead:

```cpp
BoneMask upperBodyMask = buildBoneMask(skeleton, "Spine");
BoneMask lowerBodyMask = invertMask(upperBodyMask, skeleton.bones.size());
BoneMask armsMask      = combineMasks(buildBoneMask(skeleton, "LeftShoulder"),
                                       buildBoneMask(skeleton, "RightShoulder"));
BoneMask headMask      = buildBoneMask(skeleton, "Neck");
BoneMask fullBodyMask  = buildFullBodyMask(skeleton);
```

---

## Animation Layer Structure

Each layer represents a single animation playing on a subset of bones. It carries everything the evaluation system needs: which clip, how far through it, how strongly it applies, and which bones it affects.

### Blend Modes

```cpp
// In src/engine/animation/animation_layer.h

#pragma once

#include "engine/animation/bone_mask.h"
#include "engine/animation/animation_clip.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

enum class LayerBlendMode {
    Override,   // Replace the pose beneath with this layer's pose
    Additive    // Add this layer's delta on top of the pose beneath
};
```

### AnimationLayer Struct

```cpp
// In src/engine/animation/animation_layer.h (continued)

struct AnimationLayer {
    // What to play
    const AnimationClip* clip = nullptr;
    float currentTime = 0.0f;
    bool looping = true;
    float speed = 1.0f;

    // How to blend
    LayerBlendMode blendMode = LayerBlendMode::Override;
    float weight = 1.0f;           // 0.0 = no effect, 1.0 = full effect
    BoneMask mask;                 // Which bones this layer controls

    // Cross-fade within this layer (transitioning between clips on the same layer)
    const AnimationClip* previousClip = nullptr;
    float previousTime = 0.0f;
    float blendDuration = 0.0f;    // Total cross-fade time
    float blendTimer = 0.0f;       // Current cross-fade progress

    // Additive reference pose (only used when blendMode == Additive)
    const AnimationClip* referenceClip = nullptr;
    float referenceTime = 0.0f;    // Usually 0.0 (first frame of the reference)

    // Lifecycle
    bool active = true;            // Set to false to remove on next cleanup
    bool finished = false;         // Set to true when a non-looping clip ends
};
```

### Updated Animator Component

The Animator component no longer holds a single clip. It holds a fixed-size array of layers. Layer 0 is always the base layer (full body locomotion). Layers 1 through `MAX_LAYERS - 1` are overlay layers.

```cpp
// In src/engine/ecs/components/animator.h (updated from Ch 33)

#pragma once

#include "engine/animation/animation_layer.h"
#include "engine/animation/skeleton.h"
#include <glm/glm.hpp>
#include <array>
#include <vector>

constexpr int MAX_ANIM_LAYERS = 6;

struct Animator {
    // Layer stack: layer 0 = base, layers 1-5 = overlays
    std::array<AnimationLayer, MAX_ANIM_LAYERS> layers;
    int activeLayerCount = 1;  // How many layers are in use (always >= 1)

    // Output: final bone matrices (uploaded to GPU each frame)
    std::vector<glm::mat4> boneMatrices;

    // Reference to the skeleton (shared across all entities using the same model)
    const Skeleton* skeleton = nullptr;
};
```

```
LAYER STACK (3 of 6 slots in use)

    ┌───────────────────────────────────────────┐
    │ Layer 2: hit_flinch (additive, upper,     │  ← one-shot, weight fading
    │          weight=0.7)                      │     evaluated last (top)
    ├───────────────────────────────────────────┤
    │ Layer 1: shoot_rifle (override, upper,    │  ← override upper body
    │          weight=1.0)                      │
    ├───────────────────────────────────────────┤
    │ Layer 0: run_forward (full body,          │  ← base locomotion
    │          weight=1.0)                      │     evaluated first (bottom)
    └───────────────────────────────────────────┘
```

Layer 0 produces a full-body pose. Layer 1 overrides the upper body with the shooting animation. Layer 2 applies an additive flinch on top. The lower body is untouched by layers 1 and 2, so it keeps the run cycle from layer 0.

---

## Layer Evaluation

The core of the system: evaluating every layer and compositing the results into a final per-bone pose.

### BonePose

We need an intermediate representation for per-bone local transforms before they are converted to matrices.

```cpp
// In src/engine/animation/bone_pose.h

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct BonePose {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale    = glm::vec3(1.0f);
};
```

### Evaluating a Single Clip

Given a clip and a time, produce the local-space BonePose for each bone. This is the same logic from Chapter 33 extracted into a reusable function.

```cpp
// In src/engine/animation/clip_evaluator.h

#pragma once

#include "engine/animation/animation_clip.h"
#include "engine/animation/bone_pose.h"
#include <vector>

// Evaluate a clip at the given time. Writes one BonePose per bone.
// Bones not present in the clip receive the identity pose.
void evaluateClip(const AnimationClip& clip,
                  float time,
                  std::vector<BonePose>& outPoses) {
    for (size_t boneIndex = 0; boneIndex < outPoses.size(); boneIndex++) {
        const auto* channel = clip.findChannel(static_cast<int>(boneIndex));
        if (!channel) {
            // This bone has no keyframes in this clip — identity pose
            outPoses[boneIndex] = BonePose{};
            continue;
        }

        // Interpolate position keyframes
        outPoses[boneIndex].position = channel->interpolatePosition(time);

        // Interpolate rotation keyframes (quaternion slerp)
        outPoses[boneIndex].rotation = channel->interpolateRotation(time);

        // Interpolate scale keyframes
        outPoses[boneIndex].scale = channel->interpolateScale(time);
    }
}
```

### Blending Two Poses

For cross-fading within a layer, we blend between two evaluated poses:

```cpp
// In src/engine/animation/clip_evaluator.h (continued)

BonePose blendPoses(const BonePose& a, const BonePose& b, float t) {
    BonePose result;
    result.position = glm::mix(a.position, b.position, t);
    result.rotation = glm::slerp(a.rotation, b.rotation, t);
    result.scale    = glm::mix(a.scale, b.scale, t);
    return result;
}
```

---

## Override vs Additive Blending

The two blend modes serve fundamentally different purposes.

### Override Mode

Override replaces the existing pose on masked bones, weighted by the layer weight. At weight 1.0, the layer fully takes over. At weight 0.5, the result is halfway between the base and the layer. **Use cases**: upper body shooting, arm reload, head look-at.

The math:

```cpp
// Override: blend between base pose and layer pose using weight
BonePose applyOverride(const BonePose& base, const BonePose& layer, float weight) {
    BonePose result;
    result.position = glm::mix(base.position, layer.position, weight);
    result.rotation = glm::slerp(base.rotation, layer.rotation, weight);
    result.scale    = glm::mix(base.scale, layer.scale, weight);
    return result;
}
```

### Additive Mode

Additive does not replace anything. It computes a **delta** (the difference between the additive clip and a reference pose) and applies that delta on top of whatever is already there. This means an additive animation works with any base animation.

**Use cases**: hit reactions (flinch left, flinch right), breathing, leaning, recoil. These are small adjustments that layer on top of whatever the character is doing.

```
Additive blending:

    Reference pose:     T-pose (neutral)
    Additive clip:      torso rotated 15 degrees left (flinch)
    Additive delta:     15 degrees left rotation

    Base layer pose:    running (torso slightly forward)
    + additive delta:   + 15 degrees left rotation
    Result:             running with torso flinched 15 degrees left

    Base layer pose:    idle (torso upright)
    + additive delta:   + 15 degrees left rotation
    Result:             idle with torso flinched 15 degrees left
```

The additive clip is authored as a full pose, but we only use the difference from a reference pose (typically frame 0 of the same clip, or the bind pose).

```cpp
// Compute the additive delta between a clip pose and a reference pose
BonePose computeAdditiveDelta(const BonePose& clipPose, const BonePose& refPose) {
    BonePose delta;
    // Position delta: how far the clip moved from the reference
    delta.position = clipPose.position - refPose.position;
    // Rotation delta: the rotation FROM reference TO clip
    // If ref is R and clip is C, then delta D satisfies: C = R * D, so D = inv(R) * C
    delta.rotation = glm::inverse(refPose.rotation) * clipPose.rotation;
    // Scale delta: ratio
    delta.scale = clipPose.scale / refPose.scale;
    return delta;
}

// Apply an additive delta on top of a base pose, scaled by weight
BonePose applyAdditive(const BonePose& base, const BonePose& delta, float weight) {
    BonePose result;
    // Position: add the weighted delta
    result.position = base.position + delta.position * weight;
    // Rotation: slerp the delta toward identity (no rotation) based on weight,
    // then multiply onto the base
    glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat weightedDelta = glm::slerp(identity, delta.rotation, weight);
    result.rotation = base.rotation * weightedDelta;
    // Scale: lerp the delta toward 1.0 (no scale change) based on weight
    glm::vec3 weightedScaleDelta = glm::mix(glm::vec3(1.0f), delta.scale, weight);
    result.scale = base.scale * weightedScaleDelta;
    return result;
}
```

---

## Multi-Layer animationSystem Update

With all the building blocks in place, we rewrite the `animationSystem` to process the full layer stack.

### Evaluation Pipeline

```
FOR EACH ENTITY WITH Animator:

  1. Start with bind pose (identity for all bones)
  2. Evaluate Layer 0 (base): clip → per-bone poses, apply to all bones
  3. For each overlay layer (1, 2, ...):
     - Evaluate clip → per-bone poses
     - For each bone in this layer's mask:
       Override → replace pose (weighted)
       Additive → add delta on top
  4. Convert final local poses → model-space matrices
     (parent chain multiplication, then apply offset matrix)
  5. Upload boneMatrices[] to the skinning shader
```

### Full Implementation

```cpp
// In src/engine/ecs/systems/animation_system.h

#pragma once

#include <entt/entt.hpp>

void animationSystem(entt::registry& registry, float dt);
```

```cpp
// In src/engine/ecs/systems/animation_system.cpp

#include "engine/ecs/systems/animation_system.h"
#include "engine/ecs/components/animator.h"
#include "engine/ecs/components/ragdoll.h"
#include "engine/animation/clip_evaluator.h"
#include "engine/animation/bone_pose.h"
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// Convert a BonePose to a 4x4 local transform matrix
glm::mat4 poseToMatrix(const BonePose& pose) {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), pose.position);
    glm::mat4 R = glm::mat4_cast(pose.rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), pose.scale);
    return T * R * S;
}

// Evaluate a single layer and return its per-bone poses.
// Handles cross-fading within the layer.
void evaluateLayer(const AnimationLayer& layer,
                   size_t boneCount,
                   std::vector<BonePose>& outPoses) {
    if (!layer.clip) return;

    evaluateClip(*layer.clip, layer.currentTime, outPoses);

    // If this layer is cross-fading between clips, blend
    if (layer.previousClip && layer.blendTimer < layer.blendDuration) {
        std::vector<BonePose> prevPoses(boneCount);
        evaluateClip(*layer.previousClip, layer.previousTime, prevPoses);

        float t = layer.blendDuration > 0.0f
                  ? layer.blendTimer / layer.blendDuration
                  : 1.0f;

        for (size_t i = 0; i < boneCount; i++) {
            outPoses[i] = blendPoses(prevPoses[i], outPoses[i], t);
        }
    }
}

void animationSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Animator>();

    for (auto [entity, animator] : view.each()) {
        // Skip entities driven by ragdoll physics (Ch 41)
        if (registry.all_of<Ragdoll>(entity)) {
            const auto& ragdoll = registry.get<Ragdoll>(entity);
            if (ragdoll.active) continue;
        }

        if (!animator.skeleton) continue;
        const Skeleton& skeleton = *animator.skeleton;
        size_t boneCount = skeleton.bones.size();

        // ─── Advance time on all active layers ───────────────────────
        for (int l = 0; l < animator.activeLayerCount; l++) {
            AnimationLayer& layer = animator.layers[l];
            if (!layer.active || !layer.clip) continue;

            layer.currentTime += dt * layer.speed;
            float dur = layer.clip->duration;
            if (layer.looping) {
                if (layer.currentTime >= dur)
                    layer.currentTime = fmod(layer.currentTime, dur);
            } else if (layer.currentTime >= dur) {
                layer.currentTime = dur;
                layer.finished = true;
            }

            // Advance cross-fade timer (blend between old and new clip)
            if (layer.previousClip) {
                layer.blendTimer += dt;
                if (layer.blendTimer >= layer.blendDuration) {
                    layer.previousClip = nullptr;
                    layer.blendTimer = 0.0f;
                } else {
                    layer.previousTime += dt * layer.speed;
                    float prevDur = layer.previousClip->duration;
                    if (layer.looping && layer.previousTime >= prevDur)
                        layer.previousTime = fmod(layer.previousTime, prevDur);
                }
            }
        }

        // ─── Evaluate layers and composite ───────────────────────────
        // Start with identity poses (bind pose)
        std::vector<BonePose> finalPoses(boneCount);

        for (int l = 0; l < animator.activeLayerCount; l++) {
            const AnimationLayer& layer = animator.layers[l];
            if (!layer.active || !layer.clip || layer.weight <= 0.0f) continue;

            // Evaluate this layer's clip into temporary poses
            std::vector<BonePose> layerPoses(boneCount);
            evaluateLayer(layer, boneCount, layerPoses);

            // Apply this layer to the final poses, bone by bone
            for (size_t b = 0; b < boneCount; b++) {
                if (!layer.mask.test(b)) continue;  // Bone not in this layer's mask

                if (layer.blendMode == LayerBlendMode::Override) {
                    // Override: replace the current pose with this layer's pose
                    finalPoses[b] = applyOverride(finalPoses[b], layerPoses[b],
                                                   layer.weight);
                }
                else if (layer.blendMode == LayerBlendMode::Additive) {
                    // Additive: compute delta from reference, apply on top
                    BonePose refPose;
                    if (layer.referenceClip) {
                        std::vector<BonePose> refPoses(boneCount);
                        evaluateClip(*layer.referenceClip, layer.referenceTime,
                                     refPoses);
                        refPose = refPoses[b];
                    }
                    // else refPose is identity (bind pose)

                    BonePose delta = computeAdditiveDelta(layerPoses[b], refPose);
                    finalPoses[b] = applyAdditive(finalPoses[b], delta, layer.weight);
                }
            }
        }

        // ─── Convert local poses to model-space bone matrices ────────
        // Walk the hierarchy. For each bone, its model-space transform is:
        //   modelTransform[i] = modelTransform[parent] * localTransform[i]
        // The final bone matrix includes the offset (inverse bind pose):
        //   boneMatrix[i] = modelTransform[i] * offsetMatrix[i]

        std::vector<glm::mat4> modelTransforms(boneCount, glm::mat4(1.0f));
        animator.boneMatrices.resize(boneCount);

        for (size_t i = 0; i < boneCount; i++) {
            glm::mat4 localMatrix = poseToMatrix(finalPoses[i]);

            int parent = skeleton.bones[i].parentIndex;
            if (parent >= 0) {
                modelTransforms[i] = modelTransforms[parent] * localMatrix;
            } else {
                modelTransforms[i] = localMatrix;
            }

            animator.boneMatrices[i] = modelTransforms[i]
                                      * skeleton.bones[i].offsetMatrix;
        }
    }
}
```

The system follows the same pattern as Chapter 33 but with two key changes. First, instead of evaluating a single clip, it evaluates every active layer and composites them according to blend mode and mask. Second, the additive path computes a delta from the reference pose before applying it.

---

## Practical Examples

### Enemy Walks and Shoots

The most common case. The AI system sets the base layer to a walk cycle and plays a shoot animation on the upper body.

```cpp
void aiCombatBehaviour(entt::registry& registry, entt::entity enemy,
                       const AnimationClip& walkClip,
                       const AnimationClip& shootClip,
                       const BoneMask& upperBodyMask,
                       const BoneMask& fullBodyMask) {
    auto& animator = registry.get<Animator>(enemy);

    // Base layer: walk cycle (full body)
    playOnLayer(animator, 0, &walkClip, LayerBlendMode::Override,
                fullBodyMask, true, 0.2f);

    // Layer 1: shoot (upper body override, one-shot)
    playOnLayer(animator, 1, &shootClip, LayerBlendMode::Override,
                upperBodyMask, false, 0.1f);
}
```

### Hit Reaction (Additive)

A hit reaction plays on top of any base animation. Because it is additive, it works whether the enemy is walking, idle, or attacking. The weight starts at 1.0 and fades to zero over 0.3 seconds using the `fadeOutLayer` function.

```cpp
void triggerHitReaction(Animator& animator,
                        const AnimationClip& flinchClip,
                        const BoneMask& upperBodyMask) {
    // Find an available layer slot (skip base layer 0)
    int slot = -1;
    for (int i = 1; i < MAX_ANIM_LAYERS; i++) {
        if (!animator.layers[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    AnimationLayer& layer = animator.layers[slot];
    layer.clip = &flinchClip;
    layer.currentTime = 0.0f;
    layer.blendMode = LayerBlendMode::Additive;
    layer.mask = upperBodyMask;
    layer.weight = 1.0f;
    layer.looping = false;
    layer.active = true;
    layer.finished = false;
    layer.referenceClip = &flinchClip;  // Delta computed from frame 0
    layer.referenceTime = 0.0f;

    animator.activeLayerCount = std::max(animator.activeLayerCount, slot + 1);
}
```

### Breathing and Reload

Breathing is a subtle additive loop at low weight (0.3) on chest bones. It makes idle characters look alive. Reload is an override on the arms mask, one-shot, cross-faded in over 0.15 seconds. Both use `playOnLayer` with the appropriate parameters:

```cpp
// Breathing: additive loop, low weight, chest bones only
void setupBreathingLayer(Animator& animator,
                         const AnimationClip& breatheClip,
                         const BoneMask& chestMask) {
    playOnLayer(animator, 2, &breatheClip, LayerBlendMode::Additive,
                chestMask, true, 0.0f);
    animator.layers[2].weight = 0.3f;
    animator.layers[2].referenceClip = &breatheClip;
    animator.layers[2].referenceTime = 0.0f;
}

// Reload: override on arms, one-shot
void startReload(Animator& animator,
                 const AnimationClip& reloadClip,
                 const BoneMask& armsMask) {
    playOnLayer(animator, 1, &reloadClip, LayerBlendMode::Override,
                armsMask, false, 0.15f);
}
```

---

## Layer Management

Layers need to be started, faded out, and cleaned up. We provide utility functions that game systems call.

### Playing an Animation on a Layer

```cpp
// In src/engine/animation/layer_manager.h

#pragma once

#include "engine/ecs/components/animator.h"

// Play a clip on the specified layer, optionally cross-fading from whatever
// is currently playing on that layer.
void playOnLayer(Animator& animator,
                 int layerIndex,
                 const AnimationClip* clip,
                 LayerBlendMode blendMode,
                 const BoneMask& mask,
                 bool looping = true,
                 float crossFadeDuration = 0.2f) {
    if (layerIndex < 0 || layerIndex >= MAX_ANIM_LAYERS) return;

    AnimationLayer& layer = animator.layers[layerIndex];

    // Set up cross-fade from current clip (if any)
    if (layer.clip && crossFadeDuration > 0.0f) {
        layer.previousClip = layer.clip;
        layer.previousTime = layer.currentTime;
        layer.blendDuration = crossFadeDuration;
        layer.blendTimer = 0.0f;
    }

    layer.clip = clip;
    layer.currentTime = 0.0f;
    layer.blendMode = blendMode;
    layer.mask = mask;
    layer.weight = 1.0f;
    layer.looping = looping;
    layer.active = true;
    layer.finished = false;

    // Ensure activeLayerCount includes this layer
    animator.activeLayerCount = std::max(animator.activeLayerCount, layerIndex + 1);
}
```

### Fading Out a Layer

When an overlay animation should stop, we don't cut it abruptly. We animate the weight from its current value down to zero over a duration. We store active fades in a per-entity `AnimationFades` component.

```cpp
// In src/engine/animation/layer_manager.h (continued)

struct LayerFade {
    int layerIndex = -1;
    float startWeight = 1.0f;
    float duration = 0.3f;
    float elapsed = 0.0f;
};

struct AnimationFades {
    std::vector<LayerFade> fades;
};

void fadeOutLayer(Animator& animator, AnimationFades& fadeData,
                  int layerIndex, float duration = 0.3f) {
    if (layerIndex < 0 || layerIndex >= MAX_ANIM_LAYERS) return;
    fadeData.fades.push_back({
        layerIndex, animator.layers[layerIndex].weight, duration, 0.0f
    });
}
```

### Layer Cleanup System

This system runs after the animation system. It ticks fade-outs, removes finished one-shot layers, and shrinks the active layer count.

```cpp
// In src/engine/ecs/systems/layer_cleanup_system.cpp

void layerCleanupSystem(entt::registry& registry, float dt) {
    // Tick fades
    for (auto [entity, animator, fadeData] :
         registry.view<Animator, AnimationFades>().each()) {
        for (int i = static_cast<int>(fadeData.fades.size()) - 1; i >= 0; i--) {
            LayerFade& fade = fadeData.fades[i];
            fade.elapsed += dt;
            float t = fade.elapsed / fade.duration;
            if (t >= 1.0f) {
                animator.layers[fade.layerIndex].weight = 0.0f;
                animator.layers[fade.layerIndex].active = false;
                animator.layers[fade.layerIndex].clip = nullptr;
                fadeData.fades.erase(fadeData.fades.begin() + i);
            } else {
                animator.layers[fade.layerIndex].weight =
                    fade.startWeight * (1.0f - t);
            }
        }
    }

    // Remove finished one-shot layers and shrink activeLayerCount
    for (auto [entity, animator] : registry.view<Animator>().each()) {
        for (int l = 1; l < animator.activeLayerCount; l++) {
            AnimationLayer& layer = animator.layers[l];
            if (layer.finished && !layer.looping) {
                layer.active = false;
                layer.clip = nullptr;
                layer.weight = 0.0f;
            }
        }
        while (animator.activeLayerCount > 1 &&
               !animator.layers[animator.activeLayerCount - 1].active) {
            animator.activeLayerCount--;
        }
    }
}
```

---

## Integration with Existing Systems

### AI System (Layer Selection)

The AI system from previous chapters now calls `playOnLayer` instead of setting a single clip. When the AI state is `Chasing`, it sets the base layer to the run clip. When the state is `Attacking`, it adds an upper body attack overlay on layer 1. The `playOnLayer` function handles cross-fading automatically.

### Animation Events (Chapter 40)

Animation events fire per-layer. Each layer has its own clip and its own timeline. The event detection logic from Chapter 40 runs independently for each active layer.

```cpp
// In the animation event detection, iterate layers instead of a single clip:

void animationEventSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Animator, AnimationEventQueue>();

    for (auto [entity, animator, eventQueue] : view.each()) {
        for (int l = 0; l < animator.activeLayerCount; l++) {
            const AnimationLayer& layer = animator.layers[l];
            if (!layer.active || !layer.clip) continue;

            float prevTime = layer.currentTime - dt * layer.speed;
            float currTime = layer.currentTime;

            // Detect events crossed between prevTime and currTime
            // (same logic as Ch 40, but per-layer)
            for (const auto& event : layer.clip->events) {
                bool crossed = false;
                if (prevTime <= currTime) {
                    crossed = (event.time > prevTime && event.time <= currTime);
                } else {
                    // Wrapped around (looping)
                    crossed = (event.time > prevTime || event.time <= currTime);
                }

                if (crossed) {
                    eventQueue.events.push_back(event);
                }
            }
        }
    }
}
```

### Ragdoll Transition (Chapter 41)

No code changes are needed. The ragdoll transition system reads `animator.boneMatrices[]`, which now reflects the composited result of all layers. The ragdoll starts from whatever combined pose the character was in at the moment of death.

---

## C++ Concepts

### std::bitset for Bone Masks

`std::bitset<N>` is a fixed-size sequence of `N` bits. We use it for bone masks because:

1. **Fixed size, no allocation**: `std::bitset<128>` is 16 bytes on the stack. No heap allocation, no vector overhead.
2. **Fast operations**: `test(i)`, `set(i)`, and bitwise operators (`|`, `&`, `~`) compile to single CPU instructions on most platforms.
3. **Bounded**: a skeleton never has more than `MAX_BONES` bones, so a fixed-size bitset is always sufficient.

```cpp
// std::bitset operations used in this chapter:

BoneMask mask;
mask.set(5);              // Set bit 5 (bone 5 is in the mask)
mask.test(5);             // Returns true (bone 5 is in the mask)
mask.reset(5);            // Clear bit 5

BoneMask a, b;
BoneMask combined = a | b;   // Union of two masks
BoneMask inverted = ~a;      // Invert all bits
```

The alternative would be `std::vector<bool>`, which is dynamically sized but has worse cache behaviour (it uses a specialised packed storage that can be slower to index) and requires heap allocation. Since we know the maximum bone count at compile time, `std::bitset` is the better choice.

### Quaternion Multiplication for Composing Rotations

Additive blending relies on quaternion multiplication to compose rotations. When we compute `base.rotation * weightedDelta`, we are applying the delta rotation in the base's local frame.

```cpp
// Quaternion multiplication is NOT commutative:
// q1 * q2 != q2 * q1
//
// base * delta = "first apply base, then apply delta in base's local frame"
// delta * base = "first apply delta, then apply base in delta's local frame"
//
// For additive animation, we want the delta to act in the bone's local frame,
// so we use: result = base * delta

glm::quat result = base.rotation * weightedDelta;
```

This is the correct order for skeletal animation because bones rotate in their parent's coordinate space. An additive flinch that tilts the spine 10 degrees to the left should tilt 10 degrees relative to wherever the spine currently is — not relative to world space.

### Layer Composition as a Design Pattern

The layer stack is an instance of the **composite** pattern. Each layer is a self-contained unit (clip, time, mask, blend mode) that modifies the pose independently. The same pattern appears in audio mixers (channels composited into final output) and render passes (geometry, lighting, post-process composited into a final frame). The advantage is modularity: adding a new animation behaviour (breathing, recoil, limp) means adding a layer, not changing the base animation or the evaluation code.

---

## What's Next

The layer system gives us control over which bones play which animations, but every bone still follows its clip exactly. In **Chapter 43: Inverse Kinematics**, we will add procedural bone adjustment. Given a target position (the ground under a foot, a ledge to grab, a point to aim at), IK solves for the bone chain that reaches that target. Feet will plant on uneven terrain instead of floating in the air, hands will reach for door handles, and weapons will aim precisely at the crosshair. IK runs as a final pass after layer evaluation, adjusting the composited pose to match the world.
