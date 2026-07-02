# Chapter 43: Inverse Kinematics

## What You'll Learn
- Why inverse kinematics is essential for grounding characters in the world
- The difference between forward kinematics (what animation clips do) and inverse kinematics (what procedural correction does)
- Two-bone IK: the law of cosines solver for arms and legs, with full geometric derivation
- Foot placement IK: raycasting to find ground, adjusting pelvis height, planting feet on uneven terrain
- Hand IK: making the off-hand grip a weapon's foregrip
- Look-at / head tracking: rotating neck and head bones toward a target with angle constraints
- CCD (Cyclic Coordinate Descent): a general-purpose solver for chains longer than two bones
- IK as a post-process: where it fits in the animation pipeline and how to blend it smoothly
- ECS components and systems for all IK types

---

## The Problem

In Chapters 33 and 42, we built a full animation pipeline. Clips provide keyframed poses, layers let us blend multiple clips across different bone groups, and the result is a composited skeleton pose each frame. This is **forward kinematics** (FK): the animation sets each bone's local rotation, and the end positions of limbs are whatever falls out of the math.

The problem is that FK knows nothing about the world. It does not know where the ground is, where the player's weapon is, or where an enemy should be looking. The result: feet float above ramps or sink into stairs, the off-hand hovers near (but not on) the weapon's foregrip, and enemies stare blankly forward while the player walks right past them.

```
FORWARD KINEMATICS — BONES SET, END POSITION IS A RESULT

  Hip rotates 30°            Knee rotates 45°           Foot ends up...
       │                          │                      wherever it
       ○                          ○────○                 ends up.
      ╱                                 ╲
     ╱                                   ●  ← foot position
                                              is a CONSEQUENCE
                                              of bone rotations.

   FK: set rotations → foot lands somewhere
```

```
INVERSE KINEMATICS — TARGET SET, BONE ROTATIONS ARE A RESULT

  Target: ground hit point         IK solver figures out
       at (2.0, -0.3, 5.0)        hip and knee rotations
                                   to reach the target.
       ○
      ╱ ╲                           ○
     ╱   ╲                         ╱ ╲
    ╱     ● ← TARGET              ╱   ╲
                                  ╱     ● ← foot ON target

   IK: set target → solver computes rotations
```

**Forward kinematics** is what animation clips do: you specify bone rotations and the end position is a consequence. **Inverse kinematics** reverses this: you specify a target position and the solver computes the bone rotations needed to reach it.

Without IK, the animation system is oblivious to the environment:

```
WITHOUT IK — FEET IGNORE THE WORLD

  Walk animation on flat ground:        Same animation on a slope:

       ○                                     ○
      / \                                   / \
     /   \                                 /   \
    ●     ●                               ●     ●
  ══════════                            ╱╱╱╱╱╱╱╱╱╱╱╱
  Ground                              Slope
  ✓ Feet on ground.                   ✗ Left foot floats.
                                      ✗ Right foot sinks.
```

IK fixes this by adjusting the final pose after the animation system has done its work. It is a **post-process** that bridges the gap between the animated skeleton and the physical world.

---

## Two-Bone IK Solver

Two-bone IK is the workhorse of character IK. It handles any chain of two bones reaching a target: upper leg + lower leg reaching a foot position, or upper arm + forearm reaching a hand position. It is fast (closed-form, no iteration), exact, and covers the most common cases.

### The Setup

Given three joint positions and a target:

```
TWO-BONE IK — THE INPUTS

     A ─────────── B ─────────── C           T
   (root)        (mid)        (end)       (target)

   A = shoulder or hip          (known position)
   B = elbow or knee            (known position)
   C = hand or foot             (current end effector position)
   T = target                   (where we want C to be)

   len_ab = length of bone A→B  (upper arm or upper leg)
   len_bc = length of bone B→C  (forearm or lower leg)
```

We also need a **pole vector** — a point in space that tells the solver which direction the mid joint (elbow or knee) should bend toward. Without a pole vector, the solver has infinite valid solutions (the elbow could point in any direction around the axis from shoulder to hand).

### The Geometry

The problem reduces to a triangle. After IK, joints A, B, and T form a triangle with known side lengths. We know `len_ab` (side A→B), `len_bc` (side B→T, because C must land on T), and `len_at` (the distance from A to T). The law of cosines gives us the angle at each vertex.

```
THE IK TRIANGLE

            B (mid joint)
           ╱ ╲
   len_ab ╱   ╲ len_bc
         ╱ α   ╲
        ╱       ╲
       A ─────── T
         len_at
       (root)    (target)

   We know all three side lengths.
   Law of cosines gives us angle α at B.

   cos(α) = (len_ab² + len_bc² - len_at²) / (2 · len_ab · len_bc)
```

### Step-by-Step Derivation

**Step 1: Compute distances and clamp.**

```
len_ab = |B - A|     (length of the upper bone)
len_bc = |C - B|     (length of the lower bone)
len_at = |T - A|     (distance from root to target)

If len_at > len_ab + len_bc:
    Target is out of reach. Fully extend the chain toward T.
If len_at < |len_ab - len_bc|:
    Target is too close. Fold the chain as tight as possible.
Otherwise:
    The triangle is valid. Solve with law of cosines.
```

**Step 2: Find the angle at the mid joint using the law of cosines.**

```
cos(angle_B) = (len_ab² + len_bc² - len_at²) / (2 · len_ab · len_bc)

This gives us how much the knee/elbow bends.
```

**Step 3: Find the angle at the root joint.**

```
cos(angle_A) = (len_ab² + len_at² - len_bc²) / (2 · len_ab · len_at)

This tells us the angle between the upper bone and the line from A to T.
```

**Step 4: Construct rotations.**

The root joint (A) must rotate so that bone A→B points in a direction that, combined with the bend at B, places C exactly at T. The mid joint (B) must rotate to achieve the computed bend angle. Both rotations use the pole vector to determine which plane the triangle lies in.

```
POLE VECTOR — RESOLVING AMBIGUITY

                 Without pole vector:         With pole vector:
                 B could be anywhere           B bends toward P
                 on this circle.               (the pole target).

  Top view:        B?                              B
                 ╱    ╲                           ╱ ╲
               ╱   A────T                       A────T
                 ╲    ╱
                   B?                         P ← pole vector
                                                  (e.g., "knee bends forward")
```

### Implementation

```cpp
// In src/engine/animation/ik_solver.h

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

struct TwoBoneIKResult {
    glm::quat rootRotation;    // New local rotation for the root bone (hip/shoulder)
    glm::quat midRotation;     // New local rotation for the mid bone (knee/elbow)
    bool reached;              // True if the target was within reach
};

// Compute a quaternion that rotates vector 'from' to vector 'to'.
// Both vectors must be normalized.
inline glm::quat rotationBetweenVectors(const glm::vec3& from, const glm::vec3& to) {
    float dot = glm::dot(from, to);

    // Vectors are nearly identical — no rotation needed
    if (dot > 0.99999f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // identity
    }

    // Vectors are nearly opposite — rotate 180° around any perpendicular axis
    if (dot < -0.99999f) {
        glm::vec3 perp = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);
        if (glm::length(perp) < 0.001f) {
            perp = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), from);
        }
        perp = glm::normalize(perp);
        return glm::angleAxis(glm::pi<float>(), perp);
    }

    glm::vec3 axis = glm::cross(from, to);
    float s = std::sqrt((1.0f + dot) * 2.0f);
    float invS = 1.0f / s;

    return glm::quat(s * 0.5f, axis.x * invS, axis.y * invS, axis.z * invS);
}

// Solve two-bone IK in model space.
// Positions a, b, c are the current model-space positions of the root, mid, and end joints.
// target is the desired model-space position of the end effector.
// poleVector is a model-space point that the mid joint should bend toward.
//
// Returns rotations that should be APPLIED to (multiplied with) the current
// model-space rotations of the root and mid joints.
TwoBoneIKResult solveTwoBoneIK(
    const glm::vec3& a,           // Root joint position (hip/shoulder)
    const glm::vec3& b,           // Mid joint position (knee/elbow)
    const glm::vec3& c,           // End effector position (ankle/wrist)
    const glm::vec3& target,      // Desired end effector position
    const glm::vec3& poleVector   // Pole target (controls bend direction)
) {
    TwoBoneIKResult result;
    result.reached = true;

    float len_ab = glm::length(b - a);
    float len_bc = glm::length(c - b);
    float len_at = glm::length(target - a);

    // Prevent division by zero
    constexpr float EPSILON = 0.0001f;
    if (len_ab < EPSILON || len_bc < EPSILON || len_at < EPSILON) {
        result.rootRotation = glm::quat(1, 0, 0, 0);
        result.midRotation  = glm::quat(1, 0, 0, 0);
        result.reached = false;
        return result;
    }

    // Clamp target distance to reachable range
    float maxReach = len_ab + len_bc - EPSILON;
    float minReach = std::abs(len_ab - len_bc) + EPSILON;

    if (len_at > maxReach) {
        len_at = maxReach;
        result.reached = false;
    }
    if (len_at < minReach) {
        len_at = minReach;
        result.reached = false;
    }

    // --- Step 1: Compute the angle at the mid joint (B) using law of cosines ---
    // cos(angle_B) = (ab² + bc² - at²) / (2 · ab · bc)
    float cosAngleB = (len_ab * len_ab + len_bc * len_bc - len_at * len_at)
                    / (2.0f * len_ab * len_bc);
    cosAngleB = glm::clamp(cosAngleB, -1.0f, 1.0f);

    // The current angle at B
    glm::vec3 ba = glm::normalize(a - b);
    glm::vec3 bc = glm::normalize(c - b);
    float cosCurrentAngleB = glm::dot(ba, bc);
    cosCurrentAngleB = glm::clamp(cosCurrentAngleB, -1.0f, 1.0f);

    // Rotation to adjust the mid joint from its current bend to the desired bend
    float angleDiff = std::acos(cosAngleB) - std::acos(cosCurrentAngleB);
    glm::vec3 midAxis = glm::cross(ba, bc);
    if (glm::length(midAxis) < EPSILON) {
        // Bones are collinear — pick an axis from the pole vector
        midAxis = glm::normalize(glm::cross(ba, glm::normalize(poleVector - b)));
    }
    midAxis = glm::normalize(midAxis);
    result.midRotation = glm::angleAxis(angleDiff, midAxis);

    // --- Step 2: Rotate the root joint so the chain points toward the target ---

    // After adjusting the mid joint, recompute where C would end up.
    // Apply the mid rotation to the vector from B to C:
    glm::vec3 newC = b + glm::mat3_cast(result.midRotation) * (c - b);

    // Now rotate the entire chain around A so that newC lands on the target.
    glm::vec3 toCurrentEnd = glm::normalize(newC - a);
    glm::vec3 toTarget     = glm::normalize(target - a);
    glm::quat aimRotation  = rotationBetweenVectors(toCurrentEnd, toTarget);

    // --- Step 3: Apply pole vector constraint ---
    // After the aim rotation, the mid joint is somewhere on a circle around the
    // A→T axis. We twist around that axis until B is as close as possible to
    // the pole vector.

    glm::vec3 newB = a + aimRotation * (b - a);
    glm::vec3 atAxis = glm::normalize(target - a);

    // Project both the current mid position and the pole vector onto the plane
    // perpendicular to the A→T axis, then twist to align them.
    glm::vec3 bOnPlane    = newB - a - glm::dot(newB - a, atAxis) * atAxis;
    glm::vec3 poleOnPlane = poleVector - a - glm::dot(poleVector - a, atAxis) * atAxis;

    if (glm::length(bOnPlane) > EPSILON && glm::length(poleOnPlane) > EPSILON) {
        bOnPlane    = glm::normalize(bOnPlane);
        poleOnPlane = glm::normalize(poleOnPlane);
        glm::quat twistRotation = rotationBetweenVectors(bOnPlane, poleOnPlane);
        aimRotation = twistRotation * aimRotation;
    }

    result.rootRotation = aimRotation;

    return result;
}
```

### Edge Cases

The solver handles three edge cases:

1. **Target too far** (`len_at > len_ab + len_bc`): The chain cannot reach. We clamp `len_at` to the maximum reach and fully extend the limb toward the target. The `reached` flag is set to false so calling code can detect this.

2. **Target too close** (`len_at < |len_ab - len_bc|`): The chain cannot fold tight enough. We clamp to the minimum distance. This happens when the target is inside the character's body.

3. **Collinear bones**: When the chain is perfectly straight, the cross product used to find the bend axis is zero. We fall back to an axis derived from the pole vector.

---

## Foot Placement IK

Foot placement is the primary use case for IK in a Quake-style FPS. Every humanoid character — the player, enemies, NPCs — walks on terrain that is rarely perfectly flat. Ramps, stairs, debris, and uneven ground all require the feet to adjust.

### The Algorithm

```
FOOT PLACEMENT — STEP BY STEP

  1. For each foot, cast a ray      2. Compute IK target         3. Lower pelvis so
     downward from the hip.            from ground hit.              lowest foot reaches.

       ○ Hip                             ○ Hip                        ○ Hip (lowered)
       │                                 │                           ╱ ╲
       │  ↓ ray                          │                          ╱   ╲
       │                                 │                         ╱     ╲
       │                                 ● target = hit +         ●       ●
  ╱╱╱╱╱●╱╱╱╱╱╱                            foot offset       ╱╱╱╱╱╱╱╱╱╱╱╱╱╱
    ground hit                       ╱╱╱╱╱╱╱╱╱╱╱╱╱           Feet on ground.
                                      ground

  4. Apply two-bone IK to            5. Rotate foot bone to
     each leg chain.                    match ground normal.

       ○                                   ○
      ╱ ╲                                 ╱ ╲
     ╱   ╲   ← IK adjusts              ╱   ╲
    ╱     ╲     hip and knee           ╱     ╲
   ●       ●  ← feet reach         ──●       ●──  ← toe aligned
╱╱╱╱╱╱╱╱╱╱╱╱╱╱  targets            ╱╱╱╱╱╱╱╱╱╱╱╱╱  with slope
```

### The FootIK Component

```cpp
// In src/engine/animation/foot_ik.h

#pragma once

#include <glm/glm.hpp>

struct FootIK {
    bool enabled = true;

    // Bone indices in the skeleton
    int pelvisBone   = -1;

    int leftHipBone  = -1;
    int leftKneeBone = -1;
    int leftFootBone = -1;

    int rightHipBone  = -1;
    int rightKneeBone = -1;
    int rightFootBone = -1;

    // Tuning parameters
    float footOffset       = 0.05f;   // Height above ground to place the ankle
    float raycastDistance   = 1.5f;    // How far below the hip to cast
    float pelvisSpeed      = 8.0f;    // How fast the pelvis adjusts (lerp speed)
    float footSpeed        = 12.0f;   // How fast feet blend to IK targets

    // Runtime state (updated each frame by the system)
    float currentPelvisOffset = 0.0f;
    glm::vec3 leftFootTarget  = glm::vec3(0.0f);
    glm::vec3 rightFootTarget = glm::vec3(0.0f);
    glm::vec3 leftGroundNormal  = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 rightGroundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    float ikWeight = 1.0f;            // 0 while jumping, 1 while grounded
};
```

### Foot Placement System

```cpp
// In src/engine/systems/foot_placement_system.h

#pragma once

#include "engine/animation/foot_ik.h"
#include "engine/animation/ik_solver.h"
#include "engine/animation/skeleton.h"
#include "engine/components/animator.h"
#include "engine/components/transform.h"
#include "engine/physics/raycast.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

// Extract the model-space position of a bone from its model-space matrix.
inline glm::vec3 bonePosition(const glm::mat4& boneModelMatrix) {
    return glm::vec3(boneModelMatrix[3]);
}

// Convert a model-space bone matrix to world space using the entity transform.
inline glm::vec3 boneWorldPos(const glm::mat4& boneModelMatrix,
                              const glm::mat4& entityTransform) {
    return glm::vec3(entityTransform * boneModelMatrix * glm::vec4(0, 0, 0, 1));
}

void footPlacementSystem(entt::registry& registry, float deltaTime) {
    auto view = registry.view<Transform, Animator, FootIK>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& animator  = view.get<Animator>(entity);
        auto& footIK    = view.get<FootIK>(entity);

        if (!footIK.enabled) continue;

        const Skeleton& skeleton = *animator.skeleton;
        glm::mat4 entityMatrix = transform.getMatrix();

        // ---- Step 1: Raycast from each hip downward to find ground ----

        glm::vec3 leftHipWorld  = boneWorldPos(
            animator.modelSpaceMatrices[footIK.leftHipBone], entityMatrix);
        glm::vec3 rightHipWorld = boneWorldPos(
            animator.modelSpaceMatrices[footIK.rightHipBone], entityMatrix);

        RaycastHit leftHit, rightHit;
        bool leftGrounded  = raycast(
            leftHipWorld, glm::vec3(0, -1, 0), footIK.raycastDistance, leftHit);
        bool rightGrounded = raycast(
            rightHipWorld, glm::vec3(0, -1, 0), footIK.raycastDistance, rightHit);

        // ---- Step 2: Compute IK targets from ground hits ----

        if (leftGrounded) {
            // Target is the ground hit point, raised by the foot offset
            glm::vec3 targetWorld = leftHit.point + glm::vec3(0, footIK.footOffset, 0);
            // Convert back to model space
            glm::mat4 invEntity = glm::inverse(entityMatrix);
            footIK.leftFootTarget  = glm::vec3(invEntity * glm::vec4(targetWorld, 1.0f));
            footIK.leftGroundNormal = leftHit.normal;
        }

        if (rightGrounded) {
            glm::vec3 targetWorld = rightHit.point + glm::vec3(0, footIK.footOffset, 0);
            glm::mat4 invEntity = glm::inverse(entityMatrix);
            footIK.rightFootTarget = glm::vec3(invEntity * glm::vec4(targetWorld, 1.0f));
            footIK.rightGroundNormal = rightHit.normal;
        }

        // ---- Step 3: Adjust pelvis height ----
        // Lower the pelvis so the lowest foot can reach the ground.
        // Compute how far each foot needs to move down from its animated position.

        glm::vec3 leftFootAnimated  = bonePosition(
            animator.modelSpaceMatrices[footIK.leftFootBone]);
        glm::vec3 rightFootAnimated = bonePosition(
            animator.modelSpaceMatrices[footIK.rightFootBone]);

        float leftDelta  = leftGrounded
            ? (footIK.leftFootTarget.y  - leftFootAnimated.y)  : 0.0f;
        float rightDelta = rightGrounded
            ? (footIK.rightFootTarget.y - rightFootAnimated.y) : 0.0f;

        // The pelvis must drop by the most negative delta
        // (the foot that needs to reach the farthest down)
        float targetPelvisOffset = std::min(leftDelta, rightDelta);
        targetPelvisOffset = std::min(targetPelvisOffset, 0.0f);  // Only lower, never raise

        // Smooth the pelvis adjustment to avoid popping
        footIK.currentPelvisOffset = glm::mix(
            footIK.currentPelvisOffset,
            targetPelvisOffset,
            1.0f - std::exp(-footIK.pelvisSpeed * deltaTime)
        );

        // Apply pelvis offset to the pelvis bone's model-space matrix
        animator.modelSpaceMatrices[footIK.pelvisBone][3][1] +=
            footIK.currentPelvisOffset * footIK.ikWeight;

        // Recompute child bone positions after pelvis adjustment
        // (forward pass from pelvis through the hierarchy)
        for (size_t i = footIK.pelvisBone + 1;
             i < skeleton.bones.size(); i++) {
            int parent = skeleton.bones[i].parentIndex;
            if (parent >= 0) {
                animator.modelSpaceMatrices[i] =
                    animator.modelSpaceMatrices[parent]
                    * animator.localPoseMatrices[i];
            }
        }

        // ---- Step 4: Apply two-bone IK to each leg ----

        auto applyLegIK = [&](int hipIdx, int kneeIdx, int footIdx,
                              const glm::vec3& footTarget,
                              const glm::vec3& groundNormal,
                              bool grounded) {
            if (!grounded) return;

            glm::vec3 hipPos  = bonePosition(animator.modelSpaceMatrices[hipIdx]);
            glm::vec3 kneePos = bonePosition(animator.modelSpaceMatrices[kneeIdx]);
            glm::vec3 footPos = bonePosition(animator.modelSpaceMatrices[footIdx]);

            // Pole vector: knee bends forward (in the character's forward direction)
            glm::vec3 forward = glm::vec3(entityMatrix[2]);
            glm::vec3 poleTarget = kneePos + glm::normalize(forward) * 0.5f;

            // Blend the foot target based on IK weight
            glm::vec3 blendedTarget = glm::mix(footPos, footTarget, footIK.ikWeight);

            TwoBoneIKResult ikResult = solveTwoBoneIK(
                hipPos, kneePos, footPos, blendedTarget, poleTarget
            );

            // Apply root (hip) rotation
            glm::mat4& hipMatrix = animator.modelSpaceMatrices[hipIdx];
            hipMatrix = glm::mat4_cast(ikResult.rootRotation) * hipMatrix;

            // Recompute knee position after hip rotation
            animator.modelSpaceMatrices[kneeIdx] =
                hipMatrix * animator.localPoseMatrices[kneeIdx];

            // Apply mid (knee) rotation
            glm::mat4& kneeMatrix = animator.modelSpaceMatrices[kneeIdx];
            kneeMatrix = glm::mat4_cast(ikResult.midRotation) * kneeMatrix;

            // Recompute foot position after knee rotation
            animator.modelSpaceMatrices[footIdx] =
                kneeMatrix * animator.localPoseMatrices[footIdx];

            // ---- Step 5: Align foot to ground normal ----
            glm::vec3 currentUp = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::quat footAlignment = rotationBetweenVectors(currentUp, groundNormal);
            // Blend the alignment
            footAlignment = glm::slerp(
                glm::quat(1, 0, 0, 0), footAlignment, footIK.ikWeight);
            animator.modelSpaceMatrices[footIdx] =
                glm::mat4_cast(footAlignment) * animator.modelSpaceMatrices[footIdx];
        };

        applyLegIK(footIK.leftHipBone, footIK.leftKneeBone, footIK.leftFootBone,
                    footIK.leftFootTarget, footIK.leftGroundNormal, leftGrounded);

        applyLegIK(footIK.rightHipBone, footIK.rightKneeBone, footIK.rightFootBone,
                    footIK.rightFootTarget, footIK.rightGroundNormal, rightGrounded);
    }
}
```

### Setting Up Foot IK

When creating a character entity, look up the bone indices from the skeleton and assign them to the FootIK component:

```cpp
void setupFootIK(entt::registry& registry, entt::entity entity,
                 const Skeleton& skeleton) {
    auto& footIK = registry.emplace<FootIK>(entity);

    footIK.pelvisBone   = skeleton.findBone("Hips");
    footIK.leftHipBone  = skeleton.findBone("LeftUpLeg");
    footIK.leftKneeBone = skeleton.findBone("LeftLeg");
    footIK.leftFootBone = skeleton.findBone("LeftFoot");

    footIK.rightHipBone  = skeleton.findBone("RightUpLeg");
    footIK.rightKneeBone = skeleton.findBone("RightLeg");
    footIK.rightFootBone = skeleton.findBone("RightFoot");

    footIK.footOffset     = 0.05f;
    footIK.raycastDistance = 1.5f;
}
```

### Blending IK In and Out

The `ikWeight` field on FootIK controls how much IK affects the pose. This is critical for smooth transitions:

```cpp
// In the character state system, adjust IK weight based on state.
// Grounded: full IK. Jumping/falling: no IK.

void updateFootIKWeights(entt::registry& registry, float deltaTime) {
    auto view = registry.view<FootIK, CharacterState>();

    for (auto entity : view) {
        auto& footIK = view.get<FootIK>(entity);
        auto& state  = view.get<CharacterState>(entity);

        float targetWeight = (state.grounded) ? 1.0f : 0.0f;

        // Smooth transition over ~0.2 seconds
        float blendSpeed = 5.0f;
        footIK.ikWeight = glm::mix(
            footIK.ikWeight,
            targetWeight,
            1.0f - std::exp(-blendSpeed * deltaTime)
        );
    }
}
```

---

## Hand IK / Weapon Grip

In a Quake-style FPS, the player holds weapons with two hands. The dominant hand (right) is positioned by the animation, but the off-hand (left) needs to grip the weapon's foregrip regardless of what animation is playing. Without IK, switching between reload, idle, and sprint animations would require every animation to place the left hand in exactly the right spot — a nightmare for animators.

### The Approach

1. The weapon model has an **attachment point** — a predefined position on the foregrip where the left hand should go.
2. Each frame, transform that attachment point into model space to get the IK target.
3. Apply two-bone IK to the left arm chain: shoulder → elbow → wrist.

### Hand IK Component

```cpp
// In src/engine/animation/hand_ik.h

#pragma once

#include <glm/glm.hpp>

struct HandIK {
    bool enabled = true;

    // Left arm chain
    int shoulderBone = -1;
    int elbowBone    = -1;
    int wristBone    = -1;

    // Target: set each frame by the weapon system
    glm::vec3 targetPosition = glm::vec3(0.0f);

    // Pole vector: controls elbow direction
    // Typically a point behind and below the shoulder
    glm::vec3 poleVector = glm::vec3(0.0f);

    float weight = 1.0f;
};
```

### Hand IK System

```cpp
// In src/engine/systems/hand_ik_system.h

#pragma once

#include "engine/animation/hand_ik.h"
#include "engine/animation/ik_solver.h"
#include "engine/components/animator.h"
#include <entt/entt.hpp>

void handIKSystem(entt::registry& registry) {
    auto view = registry.view<Animator, HandIK>();

    for (auto entity : view) {
        auto& animator = view.get<Animator>(entity);
        auto& handIK   = view.get<HandIK>(entity);

        if (!handIK.enabled || handIK.weight < 0.001f) continue;

        glm::vec3 shoulderPos = bonePosition(
            animator.modelSpaceMatrices[handIK.shoulderBone]);
        glm::vec3 elbowPos    = bonePosition(
            animator.modelSpaceMatrices[handIK.elbowBone]);
        glm::vec3 wristPos    = bonePosition(
            animator.modelSpaceMatrices[handIK.wristBone]);

        // Blend target based on weight
        glm::vec3 target = glm::mix(wristPos, handIK.targetPosition, handIK.weight);

        TwoBoneIKResult result = solveTwoBoneIK(
            shoulderPos, elbowPos, wristPos, target, handIK.poleVector
        );

        // Apply shoulder rotation
        glm::quat blendedRoot = glm::slerp(
            glm::quat(1, 0, 0, 0), result.rootRotation, handIK.weight);
        animator.modelSpaceMatrices[handIK.shoulderBone] =
            glm::mat4_cast(blendedRoot)
            * animator.modelSpaceMatrices[handIK.shoulderBone];

        // Recompute elbow
        animator.modelSpaceMatrices[handIK.elbowBone] =
            animator.modelSpaceMatrices[handIK.shoulderBone]
            * animator.localPoseMatrices[handIK.elbowBone];

        // Apply elbow rotation
        glm::quat blendedMid = glm::slerp(
            glm::quat(1, 0, 0, 0), result.midRotation, handIK.weight);
        animator.modelSpaceMatrices[handIK.elbowBone] =
            glm::mat4_cast(blendedMid)
            * animator.modelSpaceMatrices[handIK.elbowBone];

        // Recompute wrist
        animator.modelSpaceMatrices[handIK.wristBone] =
            animator.modelSpaceMatrices[handIK.elbowBone]
            * animator.localPoseMatrices[handIK.wristBone];
    }
}
```

### Setting the Target From a Weapon

Each frame, the weapon system computes the foregrip position and writes it into the HandIK component:

```cpp
void updateWeaponGripTarget(entt::registry& registry) {
    auto view = registry.view<Animator, HandIK, WeaponAttachment>();

    for (auto entity : view) {
        auto& animator  = view.get<Animator>(entity);
        auto& handIK    = view.get<HandIK>(entity);
        auto& weapon    = view.get<WeaponAttachment>(entity);

        // The weapon is attached to the right hand bone.
        // The foregrip offset is defined in the weapon's local space.
        glm::mat4 rightHandMatrix =
            animator.modelSpaceMatrices[weapon.rightHandBone];
        glm::vec3 foregripWorld = glm::vec3(
            rightHandMatrix * glm::vec4(weapon.foregripOffset, 1.0f));

        handIK.targetPosition = foregripWorld;

        // Pole vector: behind and below the shoulder, so the elbow
        // points outward and slightly down (natural arm position)
        glm::vec3 shoulderPos = bonePosition(
            animator.modelSpaceMatrices[handIK.shoulderBone]);
        handIK.poleVector = shoulderPos + glm::vec3(-0.3f, -0.2f, 0.0f);
    }
}
```

---

## Look-At / Head Tracking

Head tracking is simpler than full IK. Instead of solving a multi-bone chain for a position, we rotate one or two bones so the character looks toward a target. This is used for enemies tracking the player, NPCs looking at whoever is speaking, or the player character's head following the camera.

### Constraints

An unconstrained look-at would let the head spin 180 degrees backward — anatomically impossible and deeply unsettling. We constrain the rotation to a realistic range:

```
HEAD ROTATION CONSTRAINTS

  Top view (yaw):                Side view (pitch):

       -70°    0°   +70°              +30° (look up)
         ╲     │     ╱                  │
          ╲    │    ╱                   │
           ╲   │   ╱                   ○── 0° (straight)
            ╲  │  ╱                    │
             ╲ │ ╱                     │
              ○                      -30° (look down)
           (neck)
```

### LookAtIK Component

```cpp
// In src/engine/animation/look_at_ik.h

#pragma once

#include <glm/glm.hpp>

struct LookAtIK {
    bool enabled = true;

    // Bone indices
    int headBone = -1;
    int neckBone = -1;     // Optional (-1 to skip). Distributes rotation.

    // Target: set each frame by AI or camera system
    glm::vec3 targetPosition = glm::vec3(0.0f);

    // Constraints (radians)
    float maxYaw   = glm::radians(70.0f);
    float maxPitch = glm::radians(30.0f);

    // How much rotation the neck absorbs (0.0 = all on head, 0.5 = split evenly)
    float neckContribution = 0.4f;

    float weight = 1.0f;
};
```

### Look-At System

```cpp
// In src/engine/systems/look_at_system.h

#pragma once

#include "engine/animation/look_at_ik.h"
#include "engine/animation/ik_solver.h"
#include "engine/components/animator.h"
#include "engine/components/transform.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

// Decompose a rotation into yaw (around Y) and pitch (around X),
// then clamp both to the given limits.
glm::quat clampLookRotation(const glm::quat& rotation,
                             float maxYaw, float maxPitch) {
    // Convert to Euler angles (pitch, yaw, roll)
    glm::vec3 euler = glm::eulerAngles(rotation);

    float pitch = glm::clamp(euler.x, -maxPitch, maxPitch);
    float yaw   = glm::clamp(euler.y, -maxYaw,   maxYaw);
    // Zero out roll — heads don't tilt sideways when tracking
    float roll  = 0.0f;

    return glm::quat(glm::vec3(pitch, yaw, roll));
}

void lookAtSystem(entt::registry& registry) {
    auto view = registry.view<Transform, Animator, LookAtIK>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& animator  = view.get<Animator>(entity);
        auto& lookAt    = view.get<LookAtIK>(entity);

        if (!lookAt.enabled || lookAt.weight < 0.001f) continue;
        if (lookAt.headBone < 0) continue;

        glm::mat4 entityMatrix = transform.getMatrix();

        // Get the head's current model-space position and forward direction
        glm::mat4 headMatrix = animator.modelSpaceMatrices[lookAt.headBone];
        glm::vec3 headPos    = bonePosition(headMatrix);

        // Convert target to model space
        glm::mat4 invEntity = glm::inverse(entityMatrix);
        glm::vec3 targetModel = glm::vec3(
            invEntity * glm::vec4(lookAt.targetPosition, 1.0f));

        // Direction from head to target in model space
        glm::vec3 toTarget = glm::normalize(targetModel - headPos);

        // Current head forward direction (local Z axis of the head bone)
        glm::vec3 headForward = glm::normalize(glm::vec3(headMatrix[2]));

        // Compute the rotation from current forward to target direction
        glm::quat lookRotation = rotationBetweenVectors(headForward, toTarget);

        // Clamp to anatomical limits
        lookRotation = clampLookRotation(
            lookRotation, lookAt.maxYaw, lookAt.maxPitch);

        // Blend by weight
        lookRotation = glm::slerp(
            glm::quat(1, 0, 0, 0), lookRotation, lookAt.weight);

        // Distribute rotation between neck and head
        if (lookAt.neckBone >= 0 && lookAt.neckContribution > 0.0f) {
            glm::quat neckRotation = glm::slerp(
                glm::quat(1, 0, 0, 0), lookRotation, lookAt.neckContribution);
            glm::quat headOnly = glm::slerp(
                glm::quat(1, 0, 0, 0), lookRotation,
                1.0f - lookAt.neckContribution);

            // Apply neck rotation
            animator.modelSpaceMatrices[lookAt.neckBone] =
                glm::mat4_cast(neckRotation)
                * animator.modelSpaceMatrices[lookAt.neckBone];

            // Recompute head after neck change
            animator.modelSpaceMatrices[lookAt.headBone] =
                animator.modelSpaceMatrices[lookAt.neckBone]
                * animator.localPoseMatrices[lookAt.headBone];

            // Apply remaining rotation to head
            animator.modelSpaceMatrices[lookAt.headBone] =
                glm::mat4_cast(headOnly)
                * animator.modelSpaceMatrices[lookAt.headBone];
        } else {
            // All rotation on the head
            animator.modelSpaceMatrices[lookAt.headBone] =
                glm::mat4_cast(lookRotation)
                * animator.modelSpaceMatrices[lookAt.headBone];
        }
    }
}
```

### Setting the Look-At Target

For enemies, the AI system sets the target to the player's position. For the player, it follows the camera direction:

```cpp
// In the AI system:
void updateEnemyLookAt(entt::registry& registry) {
    auto view = registry.view<LookAtIK, EnemyAI>();

    for (auto entity : view) {
        auto& lookAt = view.get<LookAtIK>(entity);
        auto& ai     = view.get<EnemyAI>(entity);

        if (ai.canSeePlayer) {
            lookAt.targetPosition = ai.lastKnownPlayerPosition;
            lookAt.weight = 1.0f;
        } else {
            lookAt.weight = 0.0f;  // Stop tracking, return to animation pose
        }
    }
}
```

---

## CCD (Cyclic Coordinate Descent) — General Purpose IK

Two-bone IK is fast and exact, but it only works for chains of exactly two bones. For longer chains — a spine curving to dodge, a tentacle reaching for prey, a tail following a path — we need a general-purpose solver. CCD is the simplest iterative IK algorithm that handles chains of any length.

### The Algorithm

CCD works backward from the end effector to the root. For each bone in the chain, it computes a rotation that points the end effector closer to the target. After one pass, the end effector is closer but probably not there yet. Repeat for multiple iterations until convergence.

```
CCD — ONE ITERATION

  Chain: A → B → C → D → E (end effector)
  Target: T

  Step 1: Rotate D so E points toward T
         A───B───C───D        A───B───C───D
                      ╲                    ╲
                       E                    E → closer to T

  Step 2: Rotate C so E points toward T
         A───B───C             A───B───C
                  ╲                     ╲
                   D                     D
                    ╲                     ╲
                     E                     E → even closer

  Step 3: Rotate B so E points toward T
         A───B                 A───B
              ╲                     ╲
               C                     C───D
                ╲                         ╲
                 D───E                     E → almost there

  Step 4: Rotate A so E points toward T
         (same pattern)

  After one full iteration: E is closer to T.
  Repeat 10-20 iterations until |E - T| < threshold.
```

### Implementation

```cpp
// In src/engine/animation/ik_solver.h (continued)

struct CCDResult {
    std::vector<glm::quat> rotations;  // One rotation per bone in the chain
    bool converged;
    int iterationsUsed;
};

// Solve IK for a chain of arbitrary length using Cyclic Coordinate Descent.
//
// bonePositions: model-space positions of each joint, from root to end effector.
//                Must have at least 3 entries (2 bones + end effector).
// target:        desired model-space position of the end effector.
// maxIterations: how many full passes to run (10-20 is typical).
// tolerance:     stop early if end effector is within this distance of target.
//
// Returns a rotation for each bone (except the last, which is the end effector).
CCDResult solveCCD(
    const std::vector<glm::vec3>& bonePositions,
    const glm::vec3& target,
    int maxIterations = 15,
    float tolerance   = 0.001f
) {
    CCDResult result;
    result.converged = false;
    result.iterationsUsed = 0;

    int numBones = static_cast<int>(bonePositions.size());
    if (numBones < 3) return result;

    // Working copy of positions — we update these as we rotate
    std::vector<glm::vec3> positions = bonePositions;

    // Accumulated rotation for each joint (starts as identity)
    result.rotations.resize(numBones - 1, glm::quat(1, 0, 0, 0));

    for (int iter = 0; iter < maxIterations; iter++) {
        result.iterationsUsed = iter + 1;

        // Check convergence: is the end effector close enough?
        glm::vec3 endEffector = positions.back();
        if (glm::length(endEffector - target) < tolerance) {
            result.converged = true;
            break;
        }

        // Iterate backward from the bone just before the end effector to the root
        for (int i = numBones - 2; i >= 0; i--) {
            glm::vec3 bonePos = positions[i];
            glm::vec3 endPos  = positions.back();

            // Direction from this bone to the current end effector
            glm::vec3 toEnd = endPos - bonePos;
            // Direction from this bone to the target
            glm::vec3 toTarget = target - bonePos;

            float lenToEnd    = glm::length(toEnd);
            float lenToTarget = glm::length(toTarget);

            if (lenToEnd < 0.0001f || lenToTarget < 0.0001f) continue;

            toEnd    = toEnd / lenToEnd;
            toTarget = toTarget / lenToTarget;

            // Compute the rotation that brings toEnd toward toTarget
            glm::quat rotation = rotationBetweenVectors(toEnd, toTarget);

            // Accumulate the rotation
            result.rotations[i] = rotation * result.rotations[i];

            // Apply this rotation to all positions downstream of this bone
            for (int j = i + 1; j < numBones; j++) {
                positions[j] = bonePos + rotation * (positions[j] - bonePos);
            }
        }
    }

    return result;
}
```

### When to Use CCD vs Two-Bone

```
SOLVER COMPARISON

Feature              Two-Bone IK           CCD
─────────────────────────────────────────────────────────
Chain length         Exactly 2 bones       Any length
Solution             Closed-form (exact)   Iterative (approximate)
Speed                Very fast (O(1))      Moderate (O(n × iterations))
Pole vector          Built-in              Not built-in (needs extra work)
Use cases            Arms, legs            Spines, tails, tentacles, ropes

Rule of thumb:
  - Arms and legs → always use two-bone IK
  - Everything else → use CCD
```

---

## IK as a Post-Process

IK must run after the animation system has produced its final pose but before the bone matrices are uploaded to the GPU. This is because IK corrects the animated pose — it needs the animation result as input.

### Pipeline Order

```
ANIMATION PIPELINE — SYSTEM EXECUTION ORDER

  ┌───────────────────────┐
  │  1. animationSystem   │  Evaluate clips, apply layers (Ch 33, 42)
  │     Produces local    │  Output: localPoseMatrices[], modelSpaceMatrices[]
  │     and model-space   │
  │     bone matrices.    │
  └──────────┬────────────┘
             │
             ▼
  ┌───────────────────────┐
  │  2. IK Systems        │  Post-process the model-space matrices
  │     footPlacement     │  Adjusts bones to match world targets
  │     handIK            │
  │     lookAt            │
  └──────────┬────────────┘
             │
             ▼
  ┌───────────────────────┐
  │  3. finalBoneUpload   │  Multiply modelSpaceMatrices by offsetMatrices
  │     Computes final    │  Upload to GPU uniform / SSBO
  │     skinning matrices │
  │     for the shader.   │
  └───────────────────────┘
```

### System Registration

```cpp
// In your game loop or system scheduler:

void updateSystems(entt::registry& registry, float dt) {
    // --- Animation evaluation ---
    animationSystem(registry, dt);           // Ch 33 + Ch 42

    // --- IK post-processing ---
    updateFootIKWeights(registry, dt);       // Blend weights based on state
    footPlacementSystem(registry, dt);       // Foot IK (raycasts + two-bone)
    handIKSystem(registry);                  // Weapon grip IK
    updateEnemyLookAt(registry);             // Set look-at targets
    lookAtSystem(registry);                  // Head tracking

    // --- Upload to GPU ---
    uploadBoneMatrices(registry);            // Compute final matrices, send to shader
}
```

### IK Blending

Every IK component has a `weight` field that controls how much the IK result influences the final pose. This is critical for smooth transitions:

```cpp
// Weight = 0.0: pure animation pose (IK has no effect)
// Weight = 0.5: halfway between animation pose and IK solution
// Weight = 1.0: full IK correction

// The blend is applied inside each IK system:
glm::vec3 blendedTarget = glm::mix(animatedPosition, ikTarget, weight);
glm::quat blendedRotation = glm::slerp(animatedRotation, ikRotation, weight);
```

Practical weight management examples:

- **Foot IK**: Weight = 1.0 while grounded, lerp to 0.0 when jumping, lerp back to 1.0 on landing.
- **Hand IK**: Weight = 1.0 during normal gameplay, 0.0 during reload animation (the reload clip places the hand precisely).
- **Look-At**: Weight = 1.0 when the enemy sees the player, lerp to 0.0 when the player leaves line of sight.

---

## IK Components Summary

Here are all the IK-related components introduced in this chapter, collected in one place:

```cpp
// In src/engine/animation/ik_components.h

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

// Identifies the solver type for general-purpose IK targets
enum class IKSolverType {
    TwoBone,
    CCD
};

// General-purpose IK target — for any bone chain
struct IKTarget {
    bool enabled = true;

    glm::vec3 targetPosition = glm::vec3(0.0f);
    glm::vec3 poleVector     = glm::vec3(0.0f);

    // Bone chain: indices into the skeleton
    int rootBone = -1;     // First bone in the chain (shoulder, hip)
    int midBone  = -1;     // Middle bone (elbow, knee) — only used by TwoBone
    int endBone  = -1;     // End effector (wrist, ankle)

    // For CCD: full chain from root to end
    std::vector<int> chainBones;

    IKSolverType solverType = IKSolverType::TwoBone;
    float weight = 1.0f;
};

// Foot placement IK — specialized for bipedal characters
struct FootIK {
    bool enabled = true;

    int pelvisBone = -1;

    int leftHipBone   = -1;
    int leftKneeBone  = -1;
    int leftFootBone  = -1;

    int rightHipBone  = -1;
    int rightKneeBone = -1;
    int rightFootBone = -1;

    float footOffset     = 0.05f;
    float raycastDistance = 1.5f;
    float pelvisSpeed    = 8.0f;
    float footSpeed      = 12.0f;

    float currentPelvisOffset = 0.0f;
    glm::vec3 leftFootTarget    = glm::vec3(0.0f);
    glm::vec3 rightFootTarget   = glm::vec3(0.0f);
    glm::vec3 leftGroundNormal  = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 rightGroundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    float ikWeight = 1.0f;
};

// Look-at / head tracking IK
struct LookAtIK {
    bool enabled = true;

    int headBone = -1;
    int neckBone = -1;

    glm::vec3 targetPosition = glm::vec3(0.0f);

    float maxYaw   = glm::radians(70.0f);
    float maxPitch = glm::radians(30.0f);

    float neckContribution = 0.4f;
    float weight = 1.0f;
};
```

These follow the ECS rule: components hold data, no behaviour. The behaviour lives entirely in the free-function systems (`footPlacementSystem`, `handIKSystem`, `lookAtSystem`).

---

## Practical Integration

### Foot IK + Walk Animation

The most visible result of this chapter: enemies walking on uneven terrain with feet that track the ground.

```cpp
// Setup: done once when the enemy is spawned
void spawnEnemy(entt::registry& registry, const glm::vec3& position) {
    auto entity = registry.create();

    // Transform, mesh, skeleton, animator (from previous chapters)
    registry.emplace<Transform>(entity, position);
    auto& animator = registry.emplace<Animator>(entity);
    animator.skeleton = &enemySkeleton;
    // ... load animation clips, set up layers ...

    // Foot IK
    setupFootIK(registry, entity, enemySkeleton);

    // Look-at IK (enemies track the player)
    auto& lookAt = registry.emplace<LookAtIK>(entity);
    lookAt.headBone = enemySkeleton.findBone("Head");
    lookAt.neckBone = enemySkeleton.findBone("Neck");
}
```

### Weapon Grip

The off-hand grips the weapon's foregrip while any upper-body animation plays:

```cpp
void setupPlayerIK(entt::registry& registry, entt::entity player,
                   const Skeleton& skeleton) {
    // Hand IK for the left hand gripping the weapon
    auto& handIK = registry.emplace<HandIK>(entity);
    handIK.shoulderBone = skeleton.findBone("LeftShoulder");
    handIK.elbowBone    = skeleton.findBone("LeftArm");
    handIK.wristBone    = skeleton.findBone("LeftHand");
}
```

### Combined With Animation Layers (Chapter 42)

IK runs after all animation layers have been evaluated. This means it post-processes the final composited result. The layered system from Chapter 42 handles blending walk + shoot. IK then adjusts the feet to match the ground and the off-hand to match the weapon grip. They are independent stages in the same pipeline.

```
FULL PIPELINE EXAMPLE — ENEMY WALKING ON A SLOPE WHILE SHOOTING

  Layer 0: "walk" (full body)       → legs and arms in walk cycle
  Layer 1: "shoot" (upper body)     → arms and torso overridden with shoot pose

  animationSystem composites layers → final model-space pose

  footPlacementSystem               → adjusts legs to match slope
  lookAtSystem                      → head turns toward player

  uploadBoneMatrices                → sends to GPU for skinning
```

---

## C++ Concepts

### Law of Cosines for Geometric IK

The law of cosines is the foundation of two-bone IK. Given a triangle with sides a, b, c, the angle C opposite side c is:

```
cos(C) = (a² + b² - c²) / (2ab)
```

In our IK solver, the three sides are the two bone lengths and the distance to the target. This gives a closed-form (non-iterative) solution for the exact angle at the mid joint. The result is always mathematically correct — no iterations, no convergence issues, no tolerance thresholds.

The clamp to [-1, 1] before calling `acos` is essential. Floating-point imprecision can produce values like 1.0000001, and `acos` of anything outside [-1, 1] returns NaN. Always clamp before `acos`:

```cpp
// WRONG — can produce NaN from floating-point error:
float angle = std::acos(dotProduct);

// CORRECT — safe from NaN:
float angle = std::acos(glm::clamp(dotProduct, -1.0f, 1.0f));
```

### Quaternion From-To Rotation

The `rotationBetweenVectors` utility computes the shortest-arc quaternion that rotates one direction to another. This is the building block for all IK solvers in this chapter. The math works by finding the axis (cross product) and angle (dot product) between the two vectors, then constructing a quaternion from them.

The edge cases matter:
- **Parallel vectors** (dot product near 1.0): no rotation needed. Return identity.
- **Opposite vectors** (dot product near -1.0): 180-degree rotation around any perpendicular axis. Finding that perpendicular axis requires a fallback cross product with an arbitrary vector.

```cpp
// The formula used in rotationBetweenVectors:
//
// axis  = normalize(cross(from, to))
// angle = acos(dot(from, to))
// quat  = angleAxis(angle, axis)
//
// The implementation in this chapter uses an equivalent but more numerically
// stable form that avoids the explicit acos call.
```

### Post-Processing Pipeline Pattern

The IK system demonstrates a common game engine pattern: a **post-processing pipeline** where each stage modifies the output of the previous one. The animation system produces a pose, then IK systems adjust it, then the result goes to the renderer.

This pattern appears throughout QEngine:
- **Animation**: clips → layers → IK → skinning (this chapter)
- **Rendering**: geometry → lighting → post-processing → display (Ch 28)
- **Physics**: broad phase → narrow phase → resolution (Ch 9-10)

The advantage is modularity. Each stage has a clear input and output. IK does not know about animation clips. The animation system does not know about terrain. Each system does one thing and passes the result to the next. This is exactly the ECS principle: systems are independent, stateless functions that operate on components.

---

## What's Next

Inverse kinematics gives our characters physical presence — feet plant on terrain, hands grip weapons, heads track targets. The animation system is now complete: clips provide the base motion (Ch 33), layers blend multiple actions on different body parts (Ch 42), and IK grounds the result in the physical world (this chapter).

In **Chapter 44: Physically-Based Rendering (PBR)**, we replace the Phong lighting model with a physically accurate shading pipeline. Instead of ambient/diffuse/specular approximations, materials will be defined by albedo, metalness, and roughness. The Cook-Torrance BRDF will produce realistic lighting that responds correctly to material properties — metals reflect their environment, rough surfaces scatter light broadly, and energy is conserved across all viewing angles. The visual quality of every surface in the engine takes a significant step forward.
