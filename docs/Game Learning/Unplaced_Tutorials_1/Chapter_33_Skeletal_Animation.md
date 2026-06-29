# Chapter 33: Skeletal Animation

## What You'll Learn
- Why skeletal animation is the standard approach for character movement
- How a bone hierarchy encodes a skeleton as a tree of transforms
- Skinning: how mesh vertices follow bones using weights
- Keyframed animation clips with position, rotation, and scale channels
- Computing final bone transforms by walking the hierarchy each frame
- Writing a vertex shader that deforms a mesh on the GPU
- An ECS-compliant Animator component and a stateless animationSystem
- Cross-fade blending for smooth transitions between animation clips
- Connecting animation playback to enemy AI states
- Loading animated models from glTF files

---

## Why Skeletal Animation?

Right now, QEngine's enemies are static meshes. They slide across the floor like chess pieces — no walk cycles, no attack windup, no death collapse. The world feels dead because the characters in it don't move like living things.

**Skeletal animation** solves this by hiding a skeleton inside the mesh. The skeleton is a tree of **bones** (also called joints). When you rotate a bone — say, the upper arm — every vertex near that bone moves with it. The shoulder pulls the elbow, the elbow pulls the wrist, the wrist pulls the fingers. One rotation cascades through the hierarchy, and the mesh deforms to match.

This is the same technique used by every 3D game from Quake III to modern AAA titles. The concept has three parts:

```
1. SKELETON        2. ANIMATION           3. SKINNING
   (bone tree)        (keyframed poses)      (vertex → bone mapping)

   Hips               time 0.0: idle         vertex 412:
   ├── Spine           time 0.3: leg forward    bone "LeftLeg"  weight 0.8
   │   └── Chest       time 0.6: leg back       bone "LeftFoot" weight 0.2
   ├── LeftLeg         ...
   └── RightLeg
```

The skeleton defines the structure. The animation tells each bone where to be at each moment in time. Skinning tells each vertex which bones it should follow, and by how much.

---

## Bone Hierarchy

A skeleton is a tree. Each bone has exactly one parent, except the root which has none. A bone's position in the world is determined by its own local transform combined with every ancestor's transform up to the root.

### Humanoid Skeleton

```
Root (Hips)
├── Spine
│   ├── Chest
│   │   ├── LeftShoulder → LeftArm → LeftHand
│   │   ├── RightShoulder → RightArm → RightHand
│   │   └── Neck → Head
│   └── ...
├── LeftUpLeg → LeftLeg → LeftFoot
└── RightUpLeg → RightLeg → RightFoot
```

When the Hips bone rotates, the entire body rotates. When the Chest rotates, only the upper body moves — the legs stay planted. This hierarchy is what makes animation intuitive: animators pose parent bones and the children follow naturally.

### Bone Data Structure

```cpp
// In src/engine/animation/skeleton.h

struct Bone {
    std::string name;
    int parentIndex;           // -1 for root
    glm::mat4 offsetMatrix;    // Inverse bind pose — transforms from model space to bone space
};

struct Skeleton {
    std::vector<Bone> bones;
    std::unordered_map<std::string, int> boneNameToIndex;

    int findBone(const std::string& name) const {
        auto it = boneNameToIndex.find(name);
        return (it != boneNameToIndex.end()) ? it->second : -1;
    }
};
```

The bones are stored in a flat array, ordered so that every parent appears before its children. This ordering is critical — it means we can compute world transforms in a single forward pass through the array, because by the time we reach any bone, its parent's world transform is already computed.

### The Offset Matrix

Each bone has an **offset matrix** (also called the inverse bind-pose matrix). This is the matrix that transforms a vertex from model space into that bone's local space *at the rest pose*. When the mesh is first modelled, the character stands in a T-pose or A-pose — the **bind pose**. The offset matrix is the inverse of where the bone was in that pose.

```
Model Space                      Bone Space
(T-pose vertex position)         (relative to the bone)

    ┌───────────┐                   ┌───────────┐
    │           │                   │           │
    │     *  ← vertex               │  *        │
    │           │    offset          │  ↑        │
    │     |     │   matrix           │  bone     │
    │     |     │  ───────→          │  origin   │
    │    / \    │                   │           │
    │   /   \   │                   └───────────┘
    └───────────┘

   "Where is the vertex           "Where is the vertex
    in the world?"                 relative to this bone?"
```

We need this because the animation system works in bone space. The final transform for each bone combines the animated world transform with the offset matrix: it first moves the vertex into bone space, then the animation moves it to its new world position.

---

## Skinning — How Vertices Follow Bones

Every vertex in a skinned mesh knows which bones influence it. Each vertex stores up to **four bone indices** and **four weights**. The weights always sum to 1.0 — they describe how much each bone pulls the vertex.

### Why Four?

Four bones per vertex is the industry standard. In practice, most vertices are influenced by one or two bones. Vertices near joints (elbows, knees) might use three or four. Four gives enough precision for smooth deformation without wasting memory or GPU bandwidth.

```
Vertex near the elbow:

    Bone: UpperArm   Weight: 0.6   ──┐
    Bone: LowerArm   Weight: 0.4   ──┤── sum = 1.0
    Bone: (unused)   Weight: 0.0   ──┤
    Bone: (unused)   Weight: 0.0   ──┘

    Final position = 0.6 * (UpperArm transform * vertex)
                   + 0.4 * (LowerArm transform * vertex)
```

When the arm bends, the elbow vertex smoothly follows both bones instead of snapping to one or the other. This is called **linear blend skinning** (LBS) and it's what virtually every real-time game uses.

### Extended Vertex Format

```cpp
// In src/engine/animation/skinned_vertex.h

struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::ivec4 boneIDs;    // Up to 4 bone indices
    glm::vec4 weights;      // Corresponding weights (sum to 1.0)
};
```

This extends the standard vertex format with two new attributes. The `boneIDs` are integers — indices into the skeleton's bone array. The `weights` are floats between 0.0 and 1.0.

### Vertex Attribute Setup

When setting up the VAO for a skinned mesh, we need to register these two extra attributes. Bone IDs use `glVertexAttribIPointer` (note the `I` — integer attributes), not `glVertexAttribPointer`.

```cpp
// In src/engine/animation/skinned_mesh.cpp

void setupSkinnedMeshVAO(GLuint VAO, GLuint VBO) {
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    // Layout matches SkinnedVertex struct layout:
    //   position  (vec3)  — location 0
    //   normal    (vec3)  — location 1
    //   texCoords (vec2)  — location 2
    //   boneIDs   (ivec4) — location 3
    //   weights   (vec4)  — location 4

    size_t stride = sizeof(SkinnedVertex);

    // Position — location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(SkinnedVertex, position));

    // Normal — location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(SkinnedVertex, normal));

    // Texture coordinates — location 2
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(SkinnedVertex, texCoords));

    // Bone IDs — location 3 (INTEGER attribute — use glVertexAttribIPointer)
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, stride,
                           (void*)offsetof(SkinnedVertex, boneIDs));

    // Weights — location 4
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(SkinnedVertex, weights));

    glBindVertexArray(0);
}
```

The critical detail: `glVertexAttribIPointer` for bone IDs. If you use the regular `glVertexAttribPointer`, OpenGL will convert the integers to floats and your shader will read garbage bone indices. This is one of the most common skeletal animation bugs.

---

## Animation Clips

An animation clip is a collection of keyframes for each bone over time. The "walk" clip might be 1 second long with 24 keyframes per bone. The "attack" clip might be 0.5 seconds with keyframes only on the arm and weapon bones.

### Keyframe Data

Each keyframe stores a timestamp and the bone's local transform decomposed into position, rotation, and scale. We store rotation as a **quaternion** rather than Euler angles — this is essential for smooth interpolation (covered in the C++ Concept section at the end).

```cpp
// In src/engine/animation/animation_clip.h

struct BoneKeyframe {
    float time;
    glm::vec3 position;
    glm::quat rotation;      // Quaternions for smooth rotation
    glm::vec3 scale;
};

struct BoneAnimation {
    int boneIndex;
    std::vector<BoneKeyframe> keyframes;
};

struct AnimationClip {
    std::string name;             // "walk", "attack", "death"
    float duration;               // Total length in seconds
    float ticksPerSecond;         // Playback speed (typically 24 or 30)
    std::vector<BoneAnimation> channels;  // One per animated bone
};
```

A clip doesn't have to animate every bone. If the "walk" clip only moves the legs and hips, the upper body bones stay in their bind pose. The `channels` vector only contains entries for bones that actually move.

### Interpolating Between Keyframes

At any given time `t`, each bone is between two keyframes. We find those two keyframes and interpolate:

```cpp
// In src/engine/animation/animation_clip.h

glm::mat4 interpolateKeyframes(const BoneAnimation& channel, float time) {
    // If only one keyframe, return it directly
    if (channel.keyframes.size() == 1) {
        const auto& kf = channel.keyframes[0];
        glm::mat4 T = glm::translate(glm::mat4(1.0f), kf.position);
        glm::mat4 R = glm::mat4_cast(kf.rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), kf.scale);
        return T * R * S;
    }

    // Find the two keyframes surrounding the current time
    size_t nextIndex = 0;
    for (size_t i = 0; i < channel.keyframes.size() - 1; i++) {
        if (time < channel.keyframes[i + 1].time) {
            nextIndex = i + 1;
            break;
        }
    }

    // Handle edge case: time is past the last keyframe
    if (nextIndex == 0) {
        nextIndex = channel.keyframes.size() - 1;
    }

    size_t prevIndex = nextIndex - 1;
    const auto& prev = channel.keyframes[prevIndex];
    const auto& next = channel.keyframes[nextIndex];

    // How far between the two keyframes (0.0 to 1.0)
    float range = next.time - prev.time;
    float t = (range > 0.0001f) ? (time - prev.time) / range : 0.0f;

    // Interpolate each component:
    //   Position — linear interpolation (glm::mix)
    //   Rotation — spherical linear interpolation (glm::slerp) — this is why we use quaternions
    //   Scale    — linear interpolation (glm::mix)
    glm::vec3 position = glm::mix(prev.position, next.position, t);
    glm::quat rotation = glm::slerp(prev.rotation, next.rotation, t);
    glm::vec3 scale    = glm::mix(prev.scale, next.scale, t);

    // Compose into a 4x4 matrix: Translation * Rotation * Scale
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

    return T * R * S;
}
```

The three interpolation methods match the nature of each component:

```
Position & Scale:  glm::mix  (linear interpolation — straight line between A and B)
Rotation:          glm::slerp (spherical interpolation — follows the shortest arc on a sphere)

Linear interpolation on rotations:        Spherical interpolation (slerp):
    A ─────────── B                            A
         ↑                                    ╱  ╲
   Cuts through the sphere.                  ╱    ╲ ← follows the surface
   Causes speed wobble.                    B ──────
                                            Constant angular speed.
                                            No wobble. No gimbal lock.
```

---

## Computing Final Bone Transforms

This is the core of the entire system. Every frame, for every animated entity, we need to produce an array of `mat4` — one per bone — that the vertex shader will use to deform the mesh.

### The Algorithm

For each bone in the skeleton (processed in order, root first):

1. **Interpolate keyframes** at the current time to get the bone's local transform
2. **Multiply by parent's world transform** to get the bone's world transform
3. **Apply the offset matrix** to convert from model space to bone-local space and back

```
Step 1: localTransform = interpolate(bone's keyframes, currentTime)

Step 2: worldTransform[bone] = worldTransform[parent] * localTransform
         (root bone has no parent, so worldTransform[root] = localTransform)

Step 3: finalTransform[bone] = worldTransform[bone] * bone.offsetMatrix
```

The offset matrix is the key piece people miss. Without it, vertices would be displaced by the bone's bind-pose position *plus* the animated position — they'd fly away from the mesh. The offset matrix cancels out the bind pose so that only the animation's delta is applied.

### Complete Computation Function

```cpp
// In src/engine/animation/animation_utils.h

#pragma once

#include "skeleton.h"
#include "animation_clip.h"
#include <glm/glm.hpp>
#include <vector>

// Compute final bone matrices for a single animation clip at a given time.
// Returns one mat4 per bone, ready to upload to the shader.
std::vector<glm::mat4> computeBoneTransforms(
    const Skeleton& skeleton,
    const AnimationClip& clip,
    float animationTime)
{
    size_t boneCount = skeleton.bones.size();

    // Intermediate: world-space transform for each bone
    std::vector<glm::mat4> worldTransforms(boneCount, glm::mat4(1.0f));

    // Final output: the transforms the shader uses
    std::vector<glm::mat4> finalTransforms(boneCount, glm::mat4(1.0f));

    // Build a lookup: bone index → channel index in the clip
    // (Not every bone is animated in every clip)
    std::unordered_map<int, int> boneToChannel;
    for (int i = 0; i < static_cast<int>(clip.channels.size()); i++) {
        boneToChannel[clip.channels[i].boneIndex] = i;
    }

    // Process bones in order (parent before child — guaranteed by skeleton layout)
    for (size_t i = 0; i < boneCount; i++) {
        const Bone& bone = skeleton.bones[i];

        // Step 1: Get this bone's local transform
        glm::mat4 localTransform(1.0f);  // Identity = bind pose (no animation)

        auto it = boneToChannel.find(static_cast<int>(i));
        if (it != boneToChannel.end()) {
            // This bone has animation data — interpolate keyframes
            localTransform = interpolateKeyframes(clip.channels[it->second],
                                                   animationTime);
        }

        // Step 2: Combine with parent to get world transform
        if (bone.parentIndex >= 0) {
            worldTransforms[i] = worldTransforms[bone.parentIndex] * localTransform;
        } else {
            // Root bone — local IS world
            worldTransforms[i] = localTransform;
        }

        // Step 3: Apply offset matrix
        finalTransforms[i] = worldTransforms[i] * bone.offsetMatrix;
    }

    return finalTransforms;
}
```

The result is an array of matrices that goes straight to the GPU. If the skeleton has 40 bones, we upload 40 `mat4` values as a uniform array.

---

## Skinning Vertex Shader

The GPU does the actual mesh deformation. For each vertex, the shader looks up the four bone transforms, multiplies the vertex position by each one, and blends the results using the weights.

```glsl
// In assets/shaders/skinned.vert

#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in ivec4 aBoneIDs;     // 4 bone indices (integers)
layout (location = 4) in vec4 aWeights;       // 4 bone weights (floats)

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

const int MAX_BONES = 64;
uniform mat4 boneTransforms[MAX_BONES];

void main() {
    // ── Skinning: blend bone transforms ─────────────────────────
    // Each vertex is influenced by up to 4 bones.
    // Final position = sum of (weight[i] * boneTransforms[boneID[i]] * position)

    mat4 skinMatrix = mat4(0.0);

    for (int i = 0; i < 4; i++) {
        if (aBoneIDs[i] >= 0 && aBoneIDs[i] < MAX_BONES) {
            skinMatrix += aWeights[i] * boneTransforms[aBoneIDs[i]];
        }
    }

    // If no bones influence this vertex (all weights zero), use identity
    // This happens for non-skinned parts of a mixed mesh
    if (aWeights.x + aWeights.y + aWeights.z + aWeights.w < 0.01) {
        skinMatrix = mat4(1.0);
    }

    // ── Apply skinning then model transform ─────────────────────
    vec4 skinnedPosition = skinMatrix * vec4(aPos, 1.0);
    FragPos = vec3(model * skinnedPosition);

    // Normal transform: use the upper 3x3 of (model * skinMatrix)
    // For correct lighting on deformed meshes
    mat3 normalMatrix = mat3(transpose(inverse(model * skinMatrix)));
    Normal = normalize(normalMatrix * aNormal);

    TexCoords = aTexCoords;

    gl_Position = projection * view * model * skinnedPosition;
}
```

```glsl
// In assets/shaders/skinned.frag

#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColour;

uniform sampler2D diffuseTexture;
uniform vec3 lightDir;
uniform vec3 lightColour;
uniform vec3 ambientColour;

void main() {
    // Basic directional lighting (same as non-skinned shader)
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, normalize(-lightDir)), 0.0);
    vec3 diffuse = diff * lightColour;

    vec4 texColour = texture(diffuseTexture, TexCoords);
    vec3 result = (ambientColour + diffuse) * texColour.rgb;

    FragColour = vec4(result, texColour.a);
}
```

The fragment shader is unchanged from a regular lit mesh. All the skeletal animation work happens in the vertex shader.

### Performance Note

64 bones means 64 `mat4` uniforms — that's 64 * 16 = 1024 floats. This is well within the OpenGL uniform limit. For characters with more than ~100 bones (rare in game models), you would switch to a **Shader Storage Buffer Object** (SSBO) or a texture buffer. For a dungeon crawler, 64 bones per enemy is more than enough.

---

## Animator Component

The `Animator` is a pure-data ECS component. It stores the current animation state, the timing, blending parameters, and the computed bone matrices. No methods, no behaviour — that all lives in the system.

```cpp
// In src/engine/ecs/components/animator.h

#pragma once

#include "engine/animation/animation_clip.h"
#include <glm/glm.hpp>
#include <vector>

struct Animator {
    // Current animation
    const AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    bool looping = true;
    float speed = 1.0f;

    // Blending (for transitions between clips)
    const AnimationClip* previousClip = nullptr;
    float previousTime = 0.0f;
    float blendTime = 0.0f;      // Total blend duration in seconds
    float blendTimer = 0.0f;     // Current blend progress (0 → blendTime)

    // Output: final bone matrices (uploaded to GPU each frame)
    std::vector<glm::mat4> boneMatrices;

    // Reference to the skeleton (shared across all entities using the same model)
    const Skeleton* skeleton = nullptr;
};
```

> **ECS note**: Components have no behaviour. The `Animator` stores raw data — which clip is playing, how far through it we are, and the computed matrices. The `animationSystem` function reads and writes this data every frame. The component never calls any functions on itself.

---

## The animationSystem

A stateless free function that advances animation time, computes bone transforms, and handles blending. It runs once per frame for every entity that has an `Animator` component.

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
#include "engine/animation/animation_utils.h"

void animationSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Animator>();

    for (auto [entity, animator] : view.each()) {
        if (!animator.currentClip || !animator.skeleton) continue;

        // ─── Advance animation time ────────────────────────────
        animator.currentTime += dt * animator.speed * animator.currentClip->ticksPerSecond;

        if (animator.looping) {
            // Wrap around when we reach the end
            if (animator.currentTime > animator.currentClip->duration) {
                animator.currentTime = fmod(animator.currentTime,
                                             animator.currentClip->duration);
            }
        } else {
            // Clamp to end — freeze on last frame
            if (animator.currentTime > animator.currentClip->duration) {
                animator.currentTime = animator.currentClip->duration;
            }
        }

        // ─── Compute bone transforms for current clip ──────────
        std::vector<glm::mat4> currentTransforms = computeBoneTransforms(
            *animator.skeleton,
            *animator.currentClip,
            animator.currentTime
        );

        // ─── Handle blending with previous clip ────────────────
        if (animator.previousClip && animator.blendTimer < animator.blendTime) {
            // Advance blend timer
            animator.blendTimer += dt;
            float blendFactor = glm::clamp(animator.blendTimer / animator.blendTime,
                                            0.0f, 1.0f);

            // Advance previous clip time as well (it keeps playing during the blend)
            animator.previousTime += dt * animator.speed
                                      * animator.previousClip->ticksPerSecond;
            if (animator.previousTime > animator.previousClip->duration) {
                animator.previousTime = fmod(animator.previousTime,
                                              animator.previousClip->duration);
            }

            // Compute bone transforms for the previous clip
            std::vector<glm::mat4> prevTransforms = computeBoneTransforms(
                *animator.skeleton,
                *animator.previousClip,
                animator.previousTime
            );

            // Blend: for each bone, lerp between previous and current transforms
            size_t boneCount = animator.skeleton->bones.size();
            animator.boneMatrices.resize(boneCount);
            for (size_t i = 0; i < boneCount; i++) {
                // Per-element interpolation of the 4x4 matrices
                // This is an approximation — true matrix blending would decompose
                // into TRS and interpolate each, but per-element lerp works well
                // for small angular differences (which is the case during short blends)
                for (int col = 0; col < 4; col++) {
                    animator.boneMatrices[i][col] = glm::mix(
                        prevTransforms[i][col],
                        currentTransforms[i][col],
                        blendFactor
                    );
                }
            }

            // Blend finished — stop tracking the previous clip
            if (animator.blendTimer >= animator.blendTime) {
                animator.previousClip = nullptr;
                animator.blendTimer = 0.0f;
            }
        } else {
            // No blending — just use the current transforms directly
            animator.boneMatrices = std::move(currentTransforms);
        }
    }
}
```

### Uploading Bone Matrices to the Shader

In the render system, after binding the skinned shader, upload the bone matrices:

```cpp
// In the render loop, for each skinned entity:

void renderSkinnedEntity(const Shader& shader, const Animator& animator) {
    for (size_t i = 0; i < animator.boneMatrices.size(); i++) {
        std::string uniform = "boneTransforms[" + std::to_string(i) + "]";
        shader.setMat4(uniform, animator.boneMatrices[i]);
    }
    // Then bind VAO and draw as normal
}
```

For better performance, you can upload the entire array at once with `glUniformMatrix4fv`:

```cpp
// Upload all bone matrices in one call
glUniformMatrix4fv(
    glGetUniformLocation(shader.getID(), "boneTransforms"),
    static_cast<GLsizei>(animator.boneMatrices.size()),
    GL_FALSE,
    glm::value_ptr(animator.boneMatrices[0])
);
```

---

## Animation Blending

When an enemy switches from "idle" to "walk", snapping instantly looks terrible. The arms teleport from one pose to another in a single frame. **Blending** cross-fades between two clips over a short duration (typically 0.15 to 0.3 seconds) so the transition looks smooth.

### The Timeline

```
Time ───────────────────────────────────────────────────────→

Clip:   ████ IDLE ████████│══ BLEND ══│████████ WALK █████████
                          │           │
                      playAnimation() │
                      called here     blend complete
                                      previousClip = nullptr

Blend factor:         0.0  ─────────→  1.0
Output:               100% idle        100% walk
                      0% walk          0% idle
```

During the blend window, both clips are evaluated. The final bone matrices are a weighted mix of both poses. As the blend factor goes from 0 to 1, the output shifts from the old clip to the new one.

### The playAnimation Function

This is a free function — not a method on the component. It sets up the blend by saving the current clip as the previous clip before switching to the new one.

```cpp
// In src/engine/animation/animation_utils.h

void playAnimation(Animator& animator, const AnimationClip* clip,
                   float blendDuration = 0.2f) {
    // Don't restart the same animation
    if (animator.currentClip == clip) return;

    // Save current state for blending
    if (animator.currentClip && blendDuration > 0.0f) {
        animator.previousClip = animator.currentClip;
        animator.previousTime = animator.currentTime;
        animator.blendTime = blendDuration;
        animator.blendTimer = 0.0f;
    }

    // Switch to new clip
    animator.currentClip = clip;
    animator.currentTime = 0.0f;
}
```

### Why Matrix Lerp Works for Short Blends

Mathematically, linearly interpolating two `mat4` matrices is not correct — it can introduce shearing and scaling artifacts. The proper approach decomposes each matrix into translation, rotation (quaternion), and scale, interpolates each component separately, then recomposes. However, for blend durations under ~0.3 seconds, the angular difference between consecutive poses is small enough that per-element lerp produces visually identical results. It's a practical trade-off: simpler code, no visible artifacts, and no decomposition overhead.

For longer crossfades (e.g. a 2-second blend between "combat stance" and "relaxed stance"), you would want to decompose and interpolate properly. That's an optimisation for later.

---

## Integration with AI

The AI system (Chapter 14) already gives enemies states like Idle, Chasing, Attacking, and Dead. We connect animation playback by calling `playAnimation` when the AI state changes.

```cpp
// In src/engine/ecs/systems/ai_system.cpp (additions)

// Animation clip references (loaded with the enemy model)
extern const AnimationClip* enemyIdleClip;
extern const AnimationClip* enemyWalkClip;
extern const AnimationClip* enemyAttackClip;
extern const AnimationClip* enemyDeathClip;

void aiSystem(entt::registry& registry, float dt) {
    auto view = registry.view<AIBrain, Animator, Transform, Health>();

    for (auto [entity, ai, animator, transform, health] : view.each()) {

        AIState previousState = ai.currentState;

        // ... existing AI logic (pathfinding, target selection, etc.) ...

        // Trigger animation when AI state changes
        if (ai.currentState != previousState) {
            switch (ai.currentState) {
                case AIState::Idle:
                    animator.looping = true;
                    animator.speed = 1.0f;
                    playAnimation(animator, enemyIdleClip, 0.2f);
                    break;

                case AIState::Chasing:
                    animator.looping = true;
                    animator.speed = 1.0f;
                    playAnimation(animator, enemyWalkClip, 0.2f);
                    break;

                case AIState::Attacking:
                    animator.looping = false;  // Play once
                    animator.speed = 1.0f;
                    playAnimation(animator, enemyAttackClip, 0.1f);
                    break;

                case AIState::Dead:
                    animator.looping = false;   // Play once, freeze on last frame
                    animator.speed = 1.0f;
                    playAnimation(animator, enemyDeathClip, 0.15f);
                    break;
            }
        }

        // Return to idle after a non-looping clip finishes
        if (!animator.looping && animator.currentClip &&
            animator.currentTime >= animator.currentClip->duration) {
            if (ai.currentState == AIState::Attacking) {
                ai.currentState = AIState::Idle;
                animator.looping = true;
                playAnimation(animator, enemyIdleClip, 0.2f);
            }
            // Dead entities stay on the last frame — no transition
        }
    }
}
```

The animation system and AI system remain completely independent. The AI system writes to the `Animator` component (choosing which clip to play), and the `animationSystem` reads it (computing bone transforms). Neither knows about the other's internals.

---

## Loading Animated Models

### Recommended Format: glTF

**glTF** (GL Transmission Format) is the recommended format for loading animated 3D models. It's a JSON-based format that stores everything in a single package: mesh geometry, skeleton hierarchy, animation clips, materials, and textures. It's widely supported by modelling tools (Blender exports it natively) and has become the standard interchange format for real-time 3D.

### What You Extract from a glTF File

```
glTF File
├── Meshes
│   └── Vertices (position, normal, UV, boneIDs, weights)
├── Skeleton
│   └── Nodes marked as joints → Bone hierarchy
│       ├── Parent-child relationships
│       └── Inverse bind matrices (offsetMatrix for each bone)
├── Animations
│   └── One or more clips
│       └── Channels (one per animated bone)
│           └── Keyframes (time, position, rotation, scale)
└── Materials / Textures
```

### Conceptual Loading Flow

```cpp
// Pseudocode — conceptual overview of loading a glTF model

struct AnimatedModel {
    Skeleton skeleton;
    std::vector<SkinnedVertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<AnimationClip> clips;
    GLuint VAO, VBO, EBO;
    GLuint textureID;
};

AnimatedModel loadAnimatedModel(const std::string& path) {
    AnimatedModel model;

    // 1. Parse the glTF JSON (use a library like tinygltf or cgltf)
    // tinygltf::Model gltfModel;
    // loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path);

    // 2. Extract skeleton
    //    - Find the root joint node
    //    - Recursively traverse children to build Bone array
    //    - Read inverse bind matrices from the skin accessor
    //    - Populate boneNameToIndex map

    // 3. Extract mesh vertices
    //    - Read position, normal, texCoord attributes
    //    - Read JOINTS_0 (bone IDs) and WEIGHTS_0 (bone weights) attributes
    //    - Pack into SkinnedVertex array

    // 4. Extract animations
    //    - For each animation in the file:
    //      - Create an AnimationClip
    //      - For each channel (bone):
    //        - Read sampler keyframes (time, position, rotation, scale)
    //        - Map node name → bone index
    //        - Store as BoneAnimation

    // 5. Upload to GPU
    //    - Create VAO/VBO/EBO
    //    - Call setupSkinnedMeshVAO()
    //    - Load textures

    return model;
}
```

A full glTF loader is substantial (tinygltf alone is a few thousand lines). For the purposes of this tutorial, the important thing is understanding what data you're extracting and how it maps to the structures we've defined. The loading code is plumbing — the animation system above is the actual engine work.

### Alternative: Simple Binary Format

If you want to avoid the complexity of glTF parsing during development, you can write a simple offline converter (a Python or C++ tool that reads glTF and writes a flat binary):

```
Header:
  boneCount (uint32)
  vertexCount (uint32)
  indexCount (uint32)
  clipCount (uint32)

Bones: [boneCount x { name(64 chars), parentIndex(int32), offsetMatrix(16 floats) }]
Vertices: [vertexCount x SkinnedVertex]
Indices: [indexCount x uint32]
Clips: [clipCount x { name(64 chars), duration(float), tps(float), channelCount(uint32),
         channels: [channelCount x { boneIndex(int32), keyframeCount(uint32),
                     keyframes: [keyframeCount x BoneKeyframe] }] }]
```

This loads in milliseconds with a single `fread` per section and has no third-party dependencies. Use whichever approach fits your project.

---

## Putting It All Together

Here is the full data flow from model loading to pixels on screen:

```
LOAD TIME:
  glTF file → parse → Skeleton, SkinnedVertex[], AnimationClip[]
                           │              │              │
                           ▼              ▼              ▼
                    stored once     uploaded to     stored once
                    (shared)       GPU (VAO/VBO)   (shared)

EACH FRAME:
  ┌────────────────────────────────────────────────────────────┐
  │  aiSystem()                                                 │
  │    reads AIBrain.currentState                               │
  │    calls playAnimation(animator, clip)                      │
  │    writes to Animator component                             │
  ├────────────────────────────────────────────────────────────┤
  │  animationSystem()                                          │
  │    reads Animator.currentClip, currentTime                  │
  │    advances time by dt                                      │
  │    calls computeBoneTransforms()                            │
  │    handles blending if previousClip active                  │
  │    writes Animator.boneMatrices                             │
  ├────────────────────────────────────────────────────────────┤
  │  renderSystem()                                             │
  │    reads Animator.boneMatrices                              │
  │    uploads boneTransforms[] uniform                         │
  │    binds skinned shader + VAO                               │
  │    draws mesh — GPU deforms vertices per-bone               │
  └────────────────────────────────────────────────────────────┘
```

Each system reads from and writes to components. No system stores state. No component calls functions. The data flows from AI decisions, through animation computation, to the GPU. This is ECS at its cleanest.

---

## C++ Concept: Quaternions and `glm::quat`

Throughout this chapter we've used `glm::quat` for bone rotations. Here's why, and how to use them in practice.

### The Problem with Euler Angles

Euler angles represent rotation as three values: pitch (X), yaw (Y), and roll (Z). They are intuitive — "rotate 45 degrees around Y" is easy to picture. But they have two critical problems for animation:

1. **Gimbal lock**: When two rotation axes align (e.g. pitch = 90 degrees), you lose a degree of freedom. The character's arm locks up and can't rotate in certain directions. This is a mathematical inevitability of representing 3D rotation as three sequential axis rotations.

2. **Interpolation artifacts**: Linearly interpolating between two sets of Euler angles does not follow the shortest rotation path. The result can spin the wrong way, take a longer path, or wobble.

### What Quaternions Solve

A quaternion represents a rotation as a single unit — there's no axis order, no gimbal lock, and `slerp` (spherical linear interpolation) always follows the shortest arc between two orientations at constant angular speed.

You do **not** need to understand the internal mathematics (4D complex numbers, Hamilton's equations). You only need the practical interface:

### Creating Quaternions

```cpp
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Identity rotation (no rotation)
glm::quat identity = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
// Note: glm::quat constructor order is (w, x, y, z)

// From axis + angle
glm::quat q = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
// 45 degrees around the Y axis

// From Euler angles (pitch, yaw, roll in radians)
glm::quat fromEuler = glm::quat(glm::vec3(
    glm::radians(10.0f),   // pitch (X)
    glm::radians(90.0f),   // yaw (Y)
    glm::radians(0.0f)     // roll (Z)
));
```

### Combining Rotations

```cpp
// Multiply two quaternions to combine their rotations
// Order matters: q1 * q2 applies q2 first, then q1
glm::quat combined = upperBodyRotation * armRotation;

// This is exactly what the bone hierarchy does:
// parentWorldRotation * childLocalRotation = childWorldRotation
```

### Interpolation (slerp)

```cpp
glm::quat a = /* keyframe at time 0.0 */;
glm::quat b = /* keyframe at time 1.0 */;
float t = 0.5f;  // halfway

// Spherical linear interpolation — shortest arc, constant speed
glm::quat result = glm::slerp(a, b, t);
```

This is why we use quaternions for animation. Every frame, we `slerp` between keyframe rotations. The result is always smooth, always takes the shortest path, and never suffers from gimbal lock.

### Converting Between Quaternion and Matrix

```cpp
// Quaternion → 4x4 rotation matrix (for building bone transforms)
glm::quat q = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
glm::mat4 rotationMatrix = glm::mat4_cast(q);

// 4x4 matrix → quaternion (for decomposing loaded transforms)
glm::mat4 someMatrix = /* loaded from file */;
glm::quat extracted = glm::quat_cast(someMatrix);
```

`glm::mat4_cast` is used in `interpolateKeyframes` to convert the interpolated quaternion into a matrix that can be composed with translation and scale. `glm::quat_cast` is useful when loading animation data that's stored as matrices rather than decomposed TRS.

### Summary Table

| Operation | Function | Notes |
|-----------|----------|-------|
| Create from axis+angle | `glm::angleAxis(angle, axis)` | Angle in radians |
| Combine rotations | `q1 * q2` | Applies q2 first, then q1 |
| Interpolate | `glm::slerp(a, b, t)` | Shortest arc, constant speed |
| To matrix | `glm::mat4_cast(q)` | For composing with T and S |
| From matrix | `glm::quat_cast(mat)` | For decomposing loaded data |
| Normalize | `glm::normalize(q)` | Keep unit length after many operations |

---

## What's Next

In **Chapter 34**, we'll build a level transition system — loading new maps, preserving player state across levels, transition screens, and streaming adjacent rooms. The dungeon crawler becomes a multi-level game.
