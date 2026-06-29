# Chapter 41: Ragdoll Physics

## What You'll Learn
- Why ragdoll physics replaces canned death animations for dynamic, unrepeatable deaths
- Mapping a bone hierarchy to simplified physics rigid bodies (capsules, boxes, spheres)
- Joint constraints: hinge, cone-twist, and fixed joints with angle limits
- Defining ragdoll anatomy in a reusable data structure (RagdollDef)
- An ECS-compliant Ragdoll component that stores per-instance runtime state
- Seamlessly transitioning from animated skeleton to physics-driven ragdoll
- Simulating ragdoll physics: gravity, velocity integration, collision, and iterative constraint solving
- Overriding bone matrices so the skinning shader renders the ragdoll pose
- Settling detection, cleanup, and performance limits
- Death impulses: directional knockback, hit-location reactions, and gibbing thresholds
- Quaternion integration for angular velocity
- Iterative constraint solving (Gauss-Seidel style)

---

## The Problem

In Chapter 33, the AI system plays a death animation when an enemy's health reaches zero. Every enemy of the same type dies the same way — the same clip, the same pose, the same timing. Kill three zombies in a row and you see three identical collapses. It looks robotic.

Worse, the death animation ignores context. A shotgun blast to the chest and a pistol headshot produce the same animation. The death doesn't react to the direction or force of the killing blow. The player gets no visual feedback about where they hit or how hard.

**Ragdoll physics** solves both problems. When an enemy dies, the animation system stops and hands control of the skeleton to the physics engine. Each major bone becomes a rigid body. Gravity pulls the body down, joints constrain how far limbs can bend, and an impulse from the killing blow sends the body tumbling in the right direction.

```
CANNED DEATH ANIMATION                    RAGDOLL PHYSICS
─────────────────────────                  ─────────────────────
Same animation every time                  Unique pose every time
Ignores damage direction                   Reacts to killing blow direction
Plays to completion regardless             Responds to environment (stairs, ledges)
Clips through walls and floors             Collides with world geometry
Fixed duration                             Settles naturally under gravity
```

The transition must be seamless. At the moment of death, the ragdoll bodies start in exactly the positions the animated skeleton was in. The mesh continues to deform correctly because we feed the ragdoll body transforms back into the same bone matrix array the skinning shader already uses.

```
ANIMATION PIPELINE (alive)              RAGDOLL PIPELINE (dead)

AnimationClip                           RagdollPhysics
     │                                       │
     ▼                                       ▼
computeBoneTransforms()                 simulate bodies + constraints
     │                                       │
     ▼                                       ▼
Animator.boneMatrices[]  ──────────→   Animator.boneMatrices[]
     │                   (same output        │
     ▼                    format)            ▼
skinning shader                         skinning shader
     │                                       │
     ▼                                       ▼
deformed mesh on screen                 deformed mesh on screen
```

---

## Ragdoll Anatomy

A ragdoll is a simplified physics representation of a skeleton. Not every bone gets a rigid body — that would be wasteful and unstable. Instead, we group bones into major body parts and assign each one a simple collision shape.

### Body Part Mapping

```
SKELETON (40+ bones)              RAGDOLL (12 bodies)
────────────────────              ───────────────────
Hips                        →     Pelvis (box)
Spine, Spine1, Spine2       →     Torso (box)
Neck, Head                  →     Head (sphere)
LeftShoulder, LeftArm       →     Left Upper Arm (capsule)
LeftForeArm, LeftHand       →     Left Lower Arm (capsule)
RightShoulder, RightArm     →     Right Upper Arm (capsule)
RightForeArm, RightHand     →     Right Lower Arm (capsule)
LeftUpLeg                   →     Left Upper Leg (capsule)
LeftLeg, LeftFoot           →     Left Lower Leg (capsule)
RightUpLeg                  →     Right Upper Leg (capsule)
RightLeg, RightFoot         →     Right Lower Leg (capsule)
All finger bones            →     (grouped into hand body)
All toe bones               →     (grouped into foot body)
```

### ASCII Ragdoll Figure

```
                    ┌───┐
                    │ O │  ← Head (sphere, r=0.12)
                    └─┬─┘
              ╔═══════╬═══════╗
   L.UpperArm ║       ║       ║ R.UpperArm
   (capsule)  ║  ┌────╨────┐  ║ (capsule)
              ╠══╡  TORSO  ╞══╣
   L.LowerArm ║ │  (box)   │  ║ R.LowerArm
   (capsule)  ║  └────┬────┘  ║ (capsule)
              ╚═══╗   │   ╔═══╝
                  ║┌──┴──┐║
                  ║│PELVIS│║
                  ║└──┬──┘║
                  ║   │   ║
             ┌────╨─┐ ┌─╨────┐
             │L.Up  │ │R.Up  │
             │Leg   │ │Leg   │
             │(caps)│ │(caps)│
             └──┬───┘ └───┬──┘
             ┌──┴───┐ ┌───┴──┐
             │L.Low │ │R.Low │
             │Leg   │ │Leg   │
             │(caps)│ │(caps)│
             └──────┘ └──────┘
```

### Collision Shapes

We use three primitive shapes. These are cheap to collide and stable under simulation:

- **Sphere**: head. Defined by a radius. Simplest to collide.
- **Capsule**: limbs. A cylinder with hemispherical caps. Defined by radius and height. Rolls naturally along surfaces.
- **Box**: torso, pelvis. Defined by half-extents (width, height, depth). Provides flat surfaces for resting on floors.

### RagdollBody Struct

Each body in the ragdoll definition describes which bone it represents, its shape, dimensions, mass, and a local offset from the bone origin.

```cpp
// In src/engine/physics/ragdoll_def.h

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

enum class RagdollShape {
    Sphere,
    Capsule,
    Box
};

struct RagdollBody {
    int boneIndex;              // Which skeleton bone this body represents
    std::string boneName;       // For debugging and data authoring
    RagdollShape shape;
    glm::vec3 dimensions;       // Sphere: (radius, 0, 0)
                                // Capsule: (radius, halfHeight, 0)
                                // Box: (halfW, halfH, halfD)
    float mass;                 // In kilograms
    glm::vec3 localOffset;     // Offset from bone origin to body centre
};
```

The `localOffset` is important. A bone's origin is typically at the joint (the elbow, the shoulder), but the rigid body's centre of mass should be in the middle of the limb segment. The offset shifts the body centre to the correct position relative to the bone.

---

## Joint Constraints

Rigid bodies alone would fly apart. Joints connect adjacent bodies and limit their relative motion. Without constraints, the arm body would fall away from the torso. With constraints, the arm stays attached at the shoulder and can only rotate within a natural range.

### Joint Types

```
HINGE JOINT (1 DOF)                CONE-TWIST JOINT (3 DOF)
Elbows, Knees                      Shoulders, Hips

     ┌────┐                              ┌────┐
     │ A  │                              │ A  │
     └──┬─┘                              └──┬─┘
        │  ← pivot                          │  ← pivot
     ╔══╧══╗                             ╔══╧══╗
     ║     ║ ← rotates on               ║     ║ ← rotates in a cone
     ║     ║   one axis only             ║     ║   + twists around
     ╚══╤══╝                             ╚══╤══╝   its own axis
     ┌──┴─┐                              ┌──┴─┐
     │ B  │                              │ B  │
     └────┘                              └────┘

  min angle ←──→ max angle          cone angle: how far it
  (e.g. 0° to 150° for knee)       can tilt from parent axis
                                    twist limit: how far it
                                    can rotate around itself


FIXED JOINT (0 DOF)
Spine segments

     ┌────┐
     │ A  │
     └──┬─┘
        │  ← rigid connection
     ┌──┴─┐   (very small tolerance)
     │ B  │
     └────┘
```

### RagdollJoint Struct

```cpp
// In src/engine/physics/ragdoll_def.h (continued)

enum class RagdollJointType {
    Hinge,      // 1 DOF: min/max angle on one axis
    ConeTwist,  // 3 DOF: cone limit + twist limit
    Fixed       // 0 DOF: bodies locked together (small tolerance)
};

struct RagdollJoint {
    int bodyIndexA;             // Parent body index in the RagdollDef::bodies array
    int bodyIndexB;             // Child body index
    RagdollJointType type;

    glm::vec3 anchorA;          // Anchor point in body A's local space
    glm::vec3 anchorB;          // Anchor point in body B's local space
    glm::vec3 axis;             // Primary axis for hinge rotation (local to A)

    // Hinge limits (radians)
    float minAngle = 0.0f;
    float maxAngle = glm::radians(150.0f);

    // Cone-twist limits (radians)
    float coneAngle = glm::radians(45.0f);   // Max tilt from parent axis
    float twistAngle = glm::radians(30.0f);   // Max twist around own axis
};
```

The `anchorA` and `anchorB` fields define where the joint connects to each body, in that body's local coordinate space. For a shoulder joint, `anchorA` would be at the top-outer edge of the torso body, and `anchorB` would be at the top of the upper arm body.

---

## Ragdoll Definition Data

The `RagdollDef` holds the complete blueprint for a ragdoll. It is **shared immutable data** — like how `MeshRenderer` references a shared mesh, the `Ragdoll` component references a shared `RagdollDef`. One definition serves every instance of the same enemy type.

### RagdollDef Struct

```cpp
// In src/engine/physics/ragdoll_def.h (continued)

struct RagdollDef {
    std::vector<RagdollBody> bodies;
    std::vector<RagdollJoint> joints;
};
```

### Complete Humanoid Ragdoll Definition

This defines a 12-body, 11-joint humanoid ragdoll. The bone indices reference the skeleton from Chapter 33.

```cpp
// In src/engine/physics/humanoid_ragdoll.cpp

#include "engine/physics/ragdoll_def.h"
#include "engine/animation/skeleton.h"

RagdollDef createHumanoidRagdollDef(const Skeleton& skeleton) {
    RagdollDef def;

    // Helper to look up bone index by name
    auto bone = [&](const std::string& name) -> int {
        return skeleton.findBone(name);
    };

    // ─── Bodies (12 total) ──────────────────────────────────────────

    // 0: Pelvis
    def.bodies.push_back({
        bone("Hips"), "Hips", RagdollShape::Box,
        glm::vec3(0.15f, 0.10f, 0.10f),   // half-extents
        8.0f,                                // mass (kg)
        glm::vec3(0.0f, 0.0f, 0.0f)        // offset
    });

    // 1: Torso
    def.bodies.push_back({
        bone("Spine2"), "Spine2", RagdollShape::Box,
        glm::vec3(0.18f, 0.20f, 0.12f),
        12.0f,
        glm::vec3(0.0f, 0.0f, 0.0f)
    });

    // 2: Head
    def.bodies.push_back({
        bone("Head"), "Head", RagdollShape::Sphere,
        glm::vec3(0.12f, 0.0f, 0.0f),     // radius only
        4.0f,
        glm::vec3(0.0f, 0.08f, 0.0f)       // slightly above bone origin
    });

    // 3: Left Upper Arm
    def.bodies.push_back({
        bone("LeftArm"), "LeftArm", RagdollShape::Capsule,
        glm::vec3(0.05f, 0.13f, 0.0f),    // radius, halfHeight
        3.0f,
        glm::vec3(0.0f, -0.13f, 0.0f)
    });

    // 4: Left Lower Arm
    def.bodies.push_back({
        bone("LeftForeArm"), "LeftForeArm", RagdollShape::Capsule,
        glm::vec3(0.04f, 0.12f, 0.0f),
        2.0f,
        glm::vec3(0.0f, -0.12f, 0.0f)
    });

    // 5: Right Upper Arm
    def.bodies.push_back({
        bone("RightArm"), "RightArm", RagdollShape::Capsule,
        glm::vec3(0.05f, 0.13f, 0.0f),
        3.0f,
        glm::vec3(0.0f, -0.13f, 0.0f)
    });

    // 6: Right Lower Arm
    def.bodies.push_back({
        bone("RightForeArm"), "RightForeArm", RagdollShape::Capsule,
        glm::vec3(0.04f, 0.12f, 0.0f),
        2.0f,
        glm::vec3(0.0f, -0.12f, 0.0f)
    });

    // 7: Left Upper Leg
    def.bodies.push_back({
        bone("LeftUpLeg"), "LeftUpLeg", RagdollShape::Capsule,
        glm::vec3(0.06f, 0.20f, 0.0f),
        5.0f,
        glm::vec3(0.0f, -0.20f, 0.0f)
    });

    // 8: Left Lower Leg
    def.bodies.push_back({
        bone("LeftLeg"), "LeftLeg", RagdollShape::Capsule,
        glm::vec3(0.05f, 0.20f, 0.0f),
        4.0f,
        glm::vec3(0.0f, -0.20f, 0.0f)
    });

    // 9: Right Upper Leg
    def.bodies.push_back({
        bone("RightUpLeg"), "RightUpLeg", RagdollShape::Capsule,
        glm::vec3(0.06f, 0.20f, 0.0f),
        5.0f,
        glm::vec3(0.0f, -0.20f, 0.0f)
    });

    // 10: Right Lower Leg
    def.bodies.push_back({
        bone("RightLeg"), "RightLeg", RagdollShape::Capsule,
        glm::vec3(0.05f, 0.20f, 0.0f),
        4.0f,
        glm::vec3(0.0f, -0.20f, 0.0f)
    });

    // 11: Neck (small connector)
    def.bodies.push_back({
        bone("Neck"), "Neck", RagdollShape::Capsule,
        glm::vec3(0.04f, 0.05f, 0.0f),
        2.0f,
        glm::vec3(0.0f, 0.05f, 0.0f)
    });

    // ─── Joints (11 total) ──────────────────────────────────────────

    // Pelvis → Torso (fixed — spine doesn't bend much)
    def.joints.push_back({
        0, 1, RagdollJointType::Fixed,
        glm::vec3(0.0f, 0.10f, 0.0f),     // top of pelvis
        glm::vec3(0.0f, -0.20f, 0.0f),    // bottom of torso
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::radians(-10.0f), glm::radians(10.0f),
        glm::radians(10.0f), glm::radians(5.0f)
    });

    // Torso → Neck (fixed)
    def.joints.push_back({
        1, 11, RagdollJointType::Fixed,
        glm::vec3(0.0f, 0.20f, 0.0f),
        glm::vec3(0.0f, -0.05f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::radians(-10.0f), glm::radians(10.0f),
        glm::radians(15.0f), glm::radians(10.0f)
    });

    // Neck → Head (cone-twist — head can look around)
    def.joints.push_back({
        11, 2, RagdollJointType::ConeTwist,
        glm::vec3(0.0f, 0.05f, 0.0f),
        glm::vec3(0.0f, -0.04f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f, 0.0f,
        glm::radians(40.0f), glm::radians(30.0f)
    });

    // Torso → Left Upper Arm (cone-twist — shoulder)
    def.joints.push_back({
        1, 3, RagdollJointType::ConeTwist,
        glm::vec3(-0.18f, 0.18f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        0.0f, 0.0f,
        glm::radians(80.0f), glm::radians(45.0f)
    });

    // Left Upper Arm → Left Lower Arm (hinge — elbow)
    def.joints.push_back({
        3, 4, RagdollJointType::Hinge,
        glm::vec3(0.0f, -0.26f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),       // elbow bends on X axis
        glm::radians(0.0f), glm::radians(150.0f),
        0.0f, 0.0f
    });

    // Torso → Right Upper Arm (cone-twist — shoulder)
    def.joints.push_back({
        1, 5, RagdollJointType::ConeTwist,
        glm::vec3(0.18f, 0.18f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        0.0f, 0.0f,
        glm::radians(80.0f), glm::radians(45.0f)
    });

    // Right Upper Arm → Right Lower Arm (hinge — elbow)
    def.joints.push_back({
        5, 6, RagdollJointType::Hinge,
        glm::vec3(0.0f, -0.26f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::radians(0.0f), glm::radians(150.0f),
        0.0f, 0.0f
    });

    // Pelvis → Left Upper Leg (cone-twist — hip)
    def.joints.push_back({
        0, 7, RagdollJointType::ConeTwist,
        glm::vec3(-0.10f, -0.10f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        0.0f, 0.0f,
        glm::radians(60.0f), glm::radians(30.0f)
    });

    // Left Upper Leg → Left Lower Leg (hinge — knee)
    def.joints.push_back({
        7, 8, RagdollJointType::Hinge,
        glm::vec3(0.0f, -0.40f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::radians(0.0f), glm::radians(150.0f),
        0.0f, 0.0f
    });

    // Pelvis → Right Upper Leg (cone-twist — hip)
    def.joints.push_back({
        0, 9, RagdollJointType::ConeTwist,
        glm::vec3(0.10f, -0.10f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        0.0f, 0.0f,
        glm::radians(60.0f), glm::radians(30.0f)
    });

    // Right Upper Leg → Right Lower Leg (hinge — knee)
    def.joints.push_back({
        9, 10, RagdollJointType::Hinge,
        glm::vec3(0.0f, -0.40f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::radians(0.0f), glm::radians(150.0f),
        0.0f, 0.0f
    });

    return def;
}
```

### JSON Format

For data-driven authoring, ragdoll definitions can be stored in JSON alongside the model data:

```json
{
    "bodies": [
        {
            "bone": "Hips",
            "shape": "Box",
            "dimensions": [0.15, 0.10, 0.10],
            "mass": 8.0,
            "offset": [0.0, 0.0, 0.0]
        },
        {
            "bone": "Spine2",
            "shape": "Box",
            "dimensions": [0.18, 0.20, 0.12],
            "mass": 12.0,
            "offset": [0.0, 0.0, 0.0]
        }
    ],
    "joints": [
        {
            "bodyA": 0,
            "bodyB": 1,
            "type": "Fixed",
            "anchorA": [0.0, 0.10, 0.0],
            "anchorB": [0.0, -0.20, 0.0],
            "axis": [1.0, 0.0, 0.0]
        },
        {
            "bodyA": 1,
            "bodyB": 3,
            "type": "ConeTwist",
            "anchorA": [-0.18, 0.18, 0.0],
            "anchorB": [0.0, 0.0, 0.0],
            "axis": [0.0, -1.0, 0.0],
            "coneAngle": 80.0,
            "twistAngle": 45.0
        }
    ]
}
```

---

## The Ragdoll Component

The `Ragdoll` component stores the per-instance runtime state for an active ragdoll. It references a shared `RagdollDef` for the blueprint and holds the current position, rotation, and velocity of each body.

### RagdollBodyState

```cpp
// In src/engine/ecs/components/ragdoll.h

#pragma once

#include "engine/physics/ragdoll_def.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct RagdollBodyState {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 linearVelocity;
    glm::vec3 angularVelocity;
};
```

### Ragdoll Component

```cpp
// In src/engine/ecs/components/ragdoll.h (continued)

struct Ragdoll {
    const RagdollDef* def = nullptr;    // Shared blueprint (immutable)
    std::vector<RagdollBodyState> bodies;  // Per-instance state (mutable)
    bool active = false;                // True when physics is driving the skeleton
    bool settled = false;               // True when all velocities are below threshold
    float activeTime = 0.0f;            // How long the ragdoll has been active
};
```

This follows the same pattern as `Animator` referencing a shared `Skeleton`. The `RagdollDef` describes the shape and structure — it never changes at runtime. The `Ragdoll` component holds the dynamic state — positions, rotations, and velocities that change every frame.

### Attaching the Component

When creating an enemy entity, attach both the `Animator` and `Ragdoll` components. The ragdoll starts inactive:

```cpp
// When spawning an enemy:
auto enemy = registry.create();
registry.emplace<Transform>(enemy, spawnPosition);
registry.emplace<Health>(enemy, Health{ 100.0f, 100.0f });
registry.emplace<Animator>(enemy, /* ... from Ch 33 ... */);

// Ragdoll: pre-allocate body states but don't activate yet
Ragdoll ragdoll;
ragdoll.def = &humanoidRagdollDef;  // Shared across all enemies of this type
ragdoll.bodies.resize(humanoidRagdollDef.bodies.size());
ragdoll.active = false;
registry.emplace<Ragdoll>(enemy, std::move(ragdoll));
```

---

## Animation-to-Ragdoll Transition

This is the critical moment. When an enemy dies, we read the skeleton's current world-space pose from the `Animator`, copy those transforms into the `Ragdoll` body states, apply an impulse from the killing blow, and switch control from animation to physics.

### Transition Flow

```
ALIVE                              DEATH FRAME                       RAGDOLL ACTIVE
─────                              ───────────                       ──────────────

animationSystem()                  ragdollTransitionSystem()          ragdollPhysicsSystem()
computes bone matrices             detects Health <= 0                simulates bodies
     │                                  │                                  │
     ▼                                  ▼                                  ▼
bones at current pose              copies bone world transforms       updates positions
(e.g. mid-walk cycle)              into RagdollBodyState[]            via gravity + collision
                                   applies death impulse                   │
                                   sets ragdoll.active = true              ▼
                                   stops Animator playback            ragdollRenderSystem()
                                        │                            writes body transforms
                                        ▼                            back to bone matrices
                                   SEAMLESS — ragdoll starts              │
                                   exactly where animation left off       ▼
                                                                     skinning shader
                                                                     renders ragdoll pose
```

### ragdollTransitionSystem

```cpp
// In src/engine/ecs/systems/ragdoll_transition_system.h

#pragma once

#include <entt/entt.hpp>

void ragdollTransitionSystem(entt::registry& registry);
```

```cpp
// In src/engine/ecs/systems/ragdoll_transition_system.cpp

#include "engine/ecs/systems/ragdoll_transition_system.h"
#include "engine/ecs/components/ragdoll.h"
#include "engine/ecs/components/animator.h"
#include "engine/ecs/components/transform.h"
#include "engine/ecs/components/health.h"

void ragdollTransitionSystem(entt::registry& registry) {
    auto view = registry.view<Ragdoll, Animator, Transform, Health>();

    for (auto [entity, ragdoll, animator, transform, health] : view.each()) {
        // Only trigger on the frame health drops to zero
        if (health.current > 0.0f || ragdoll.active) continue;

        // ─── Read current bone world transforms from the Animator ────
        glm::mat4 modelMatrix = transform.getModelMatrix();

        for (size_t i = 0; i < ragdoll.def->bodies.size(); i++) {
            const RagdollBody& bodyDef = ragdoll.def->bodies[i];
            RagdollBodyState& state = ragdoll.bodies[i];

            if (bodyDef.boneIndex < 0 ||
                bodyDef.boneIndex >= static_cast<int>(animator.boneMatrices.size())) {
                // Bone not found — place at entity origin
                state.position = transform.position;
                state.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                state.linearVelocity = glm::vec3(0.0f);
                state.angularVelocity = glm::vec3(0.0f);
                continue;
            }

            // The bone matrix from the Animator includes the offset (inverse bind pose).
            // To get the bone's world position, we need to undo the offset.
            // boneMatrix = worldTransform * offsetMatrix
            // worldTransform = boneMatrix * inverse(offsetMatrix)
            const glm::mat4& boneMatrix = animator.boneMatrices[bodyDef.boneIndex];
            const glm::mat4& offsetMatrix = animator.skeleton->bones[bodyDef.boneIndex].offsetMatrix;
            glm::mat4 invOffset = glm::inverse(offsetMatrix);

            glm::mat4 boneWorldTransform = modelMatrix * boneMatrix * invOffset;

            // Extract position (4th column) and apply local offset
            glm::vec3 bonePos = glm::vec3(boneWorldTransform[3]);
            glm::quat boneRot = glm::quat_cast(boneWorldTransform);

            // Apply the body's local offset (rotated into world space)
            state.position = bonePos + boneRot * bodyDef.localOffset;
            state.rotation = boneRot;
            state.linearVelocity = glm::vec3(0.0f);
            state.angularVelocity = glm::vec3(0.0f);
        }

        // ─── Apply death impulse ─────────────────────────────────────
        // (covered in detail in the Death Impulse section below)
        if (health.lastDamageDirection != glm::vec3(0.0f)) {
            applyDeathImpulse(ragdoll, health.lastDamageDirection,
                              health.lastDamageAmount, health.lastHitBoneIndex);
        }

        // ─── Activate ragdoll, disable animation ─────────────────────
        ragdoll.active = true;
        ragdoll.settled = false;
        ragdoll.activeTime = 0.0f;

        // Stop the Animator — ragdoll now drives the bones
        animator.currentClip = nullptr;
        animator.previousClip = nullptr;
    }
}
```

The key insight: we extract world-space transforms from the Animator's bone matrices at the exact moment of death. The ragdoll bodies start in those positions. To the player, nothing visually changes on the frame of death — the mesh is in exactly the same pose. The next frame, physics takes over and the body begins to fall.

### Health Component Extension

To support directional death impulses, we add damage direction tracking to the `Health` component:

```cpp
// In src/engine/ecs/components/health.h (additions)

struct Health {
    float current = 100.0f;
    float max = 100.0f;

    // Tracking the last hit for ragdoll impulse
    glm::vec3 lastDamageDirection = glm::vec3(0.0f);  // Normalised direction of damage
    float lastDamageAmount = 0.0f;
    int lastHitBoneIndex = -1;  // Which bone was hit (-1 = centre mass)
};
```

---

## Ragdoll Physics Simulation

The `ragdollPhysicsSystem` runs every fixed timestep for each active ragdoll. It applies gravity, integrates velocities into positions, detects collisions with world geometry, and enforces joint constraints.

### System Overview

```
ragdollPhysicsSystem (per fixed timestep)
│
├── For each active ragdoll:
│   │
│   ├── 1. Apply gravity to each body
│   │
│   ├── 2. Integrate velocity → position
│   │      Integrate angular velocity → rotation
│   │
│   ├── 3. Collide each body with world geometry
│   │      (sphere/capsule/box vs level AABBs)
│   │
│   ├── 4. Solve joint constraints (iterative)
│   │      Repeat 4-8 times for stability
│   │
│   └── 5. Apply damping (slow everything down gradually)
│
└── Check settling condition
```

### The Physics System

```cpp
// In src/engine/ecs/systems/ragdoll_physics_system.h

#pragma once

#include <entt/entt.hpp>

void ragdollPhysicsSystem(entt::registry& registry, float dt);
```

```cpp
// In src/engine/ecs/systems/ragdoll_physics_system.cpp

#include "engine/ecs/systems/ragdoll_physics_system.h"
#include "engine/ecs/components/ragdoll.h"
#include "engine/ecs/components/transform.h"
#include "engine/physics/ragdoll_def.h"

constexpr float RAGDOLL_GRAVITY = 9.81f;
constexpr float LINEAR_DAMPING = 0.98f;
constexpr float ANGULAR_DAMPING = 0.95f;
constexpr int   CONSTRAINT_ITERATIONS = 6;
constexpr float SETTLE_VELOCITY_THRESHOLD = 0.05f;

// ─── Quaternion integration ──────────────────────────────────────
// Integrates angular velocity into a quaternion rotation.
// This is the correct way to update orientation — Euler angles
// would suffer from gimbal lock and order-dependent artifacts.
glm::quat integrateRotation(const glm::quat& rotation,
                             const glm::vec3& angularVelocity,
                             float dt) {
    // The derivative of a quaternion q under angular velocity w is:
    //   dq/dt = 0.5 * quat(0, w.x, w.y, w.z) * q
    glm::quat w(0.0f, angularVelocity.x, angularVelocity.y, angularVelocity.z);
    glm::quat dq = 0.5f * w * rotation;

    glm::quat result = rotation + dq * dt;
    return glm::normalize(result);  // Re-normalise to prevent drift
}

// ─── Sphere vs AABB collision ────────────────────────────────────
// Returns penetration depth and normal if overlapping, used for all
// ragdoll body shapes (we treat capsules as a set of sphere checks)
struct CollisionResult {
    bool hit = false;
    glm::vec3 normal;
    float penetration = 0.0f;
};

CollisionResult sphereVsAABB(const glm::vec3& centre, float radius,
                              const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
    CollisionResult result;

    // Find the closest point on the AABB to the sphere centre
    glm::vec3 closest;
    closest.x = glm::clamp(centre.x, aabbMin.x, aabbMax.x);
    closest.y = glm::clamp(centre.y, aabbMin.y, aabbMax.y);
    closest.z = glm::clamp(centre.z, aabbMin.z, aabbMax.z);

    glm::vec3 diff = centre - closest;
    float distSq = glm::dot(diff, diff);

    if (distSq < radius * radius) {
        result.hit = true;
        float dist = glm::sqrt(distSq);
        if (dist > 0.0001f) {
            result.normal = diff / dist;
        } else {
            result.normal = glm::vec3(0.0f, 1.0f, 0.0f);  // Default: push up
        }
        result.penetration = radius - dist;
    }

    return result;
}

// ─── Collide a single ragdoll body against world geometry ────────
void collideBodyWithWorld(RagdollBodyState& state,
                           const RagdollBody& bodyDef,
                           const std::vector<AABB>& worldAABBs) {
    // Determine effective collision radius based on shape
    float radius = 0.0f;
    switch (bodyDef.shape) {
        case RagdollShape::Sphere:
            radius = bodyDef.dimensions.x;
            break;
        case RagdollShape::Capsule:
            radius = bodyDef.dimensions.x;  // Capsule radius
            break;
        case RagdollShape::Box:
            // Approximate box as sphere for simplicity
            radius = glm::length(bodyDef.dimensions);
            break;
    }

    for (const auto& aabb : worldAABBs) {
        // For capsules, check both hemisphere centres
        if (bodyDef.shape == RagdollShape::Capsule) {
            float halfHeight = bodyDef.dimensions.y;
            glm::vec3 up = state.rotation * glm::vec3(0.0f, 1.0f, 0.0f);

            glm::vec3 topCentre = state.position + up * halfHeight;
            glm::vec3 bottomCentre = state.position - up * halfHeight;

            CollisionResult topHit = sphereVsAABB(topCentre, radius,
                                                    aabb.min, aabb.max);
            CollisionResult botHit = sphereVsAABB(bottomCentre, radius,
                                                    aabb.min, aabb.max);

            if (topHit.hit) {
                state.position += topHit.normal * topHit.penetration;
                float velAlongNormal = glm::dot(state.linearVelocity, topHit.normal);
                if (velAlongNormal < 0.0f) {
                    state.linearVelocity -= topHit.normal * velAlongNormal * 1.3f;
                }
            }
            if (botHit.hit) {
                state.position += botHit.normal * botHit.penetration;
                float velAlongNormal = glm::dot(state.linearVelocity, botHit.normal);
                if (velAlongNormal < 0.0f) {
                    state.linearVelocity -= botHit.normal * velAlongNormal * 1.3f;
                }
            }
        } else {
            CollisionResult hit = sphereVsAABB(state.position, radius,
                                                aabb.min, aabb.max);
            if (hit.hit) {
                state.position += hit.normal * hit.penetration;
                float velAlongNormal = glm::dot(state.linearVelocity, hit.normal);
                if (velAlongNormal < 0.0f) {
                    // Bounce with restitution (1.3 = slight energy loss: 1.0 + 0.3 friction)
                    state.linearVelocity -= hit.normal * velAlongNormal * 1.3f;
                }
            }
        }
    }
}

// ─── Joint constraint solver ─────────────────────────────────────
// Enforces that two connected bodies stay at the correct distance.
// Uses position-based correction: compute the error (how far the
// anchor points are from each other) and push both bodies to close
// the gap. This is a simplified Verlet-style approach.
void solveJointConstraint(RagdollBodyState& stateA,
                           RagdollBodyState& stateB,
                           const RagdollBody& bodyDefA,
                           const RagdollBody& bodyDefB,
                           const RagdollJoint& joint) {
    // Compute world-space anchor positions
    glm::vec3 worldAnchorA = stateA.position + stateA.rotation * joint.anchorA;
    glm::vec3 worldAnchorB = stateB.position + stateB.rotation * joint.anchorB;

    // Position error: the anchors should be at the same point
    glm::vec3 error = worldAnchorB - worldAnchorA;
    float errorLength = glm::length(error);

    if (errorLength < 0.0001f) return;  // Close enough

    glm::vec3 errorDir = error / errorLength;

    // Distribute correction based on inverse mass
    float invMassA = 1.0f / bodyDefA.mass;
    float invMassB = 1.0f / bodyDefB.mass;
    float totalInvMass = invMassA + invMassB;

    if (totalInvMass < 0.0001f) return;

    // Position correction — move both bodies toward each other
    float correctionA = invMassA / totalInvMass;
    float correctionB = invMassB / totalInvMass;

    stateA.position += errorDir * errorLength * correctionA;
    stateB.position -= errorDir * errorLength * correctionB;

    // Velocity correction — remove relative velocity along the error axis
    glm::vec3 relVel = stateB.linearVelocity - stateA.linearVelocity;
    float relVelAlongError = glm::dot(relVel, errorDir);

    if (relVelAlongError < 0.0f) {
        // Bodies are separating — apply corrective impulse
        glm::vec3 impulse = errorDir * relVelAlongError;
        stateA.linearVelocity -= impulse * correctionA;
        stateB.linearVelocity += impulse * correctionB;
    }

    // ─── Angular limits (simplified) ──────────────────────────────
    if (joint.type == RagdollJointType::Hinge) {
        // For hinges, project B's orientation onto the allowed range
        glm::vec3 axisA = stateA.rotation * joint.axis;
        glm::quat relativeRot = glm::inverse(stateA.rotation) * stateB.rotation;

        // Extract the angle around the hinge axis
        float angle = 2.0f * glm::atan(
            glm::dot(glm::vec3(relativeRot.x, relativeRot.y, relativeRot.z), joint.axis),
            relativeRot.w
        );

        // Clamp to limits
        float clampedAngle = glm::clamp(angle, joint.minAngle, joint.maxAngle);
        if (glm::abs(angle - clampedAngle) > 0.001f) {
            float correction = clampedAngle - angle;
            glm::quat fix = glm::angleAxis(correction * 0.5f, axisA);
            stateB.rotation = fix * stateB.rotation;
            stateB.rotation = glm::normalize(stateB.rotation);
        }
    }
    else if (joint.type == RagdollJointType::ConeTwist) {
        // Cone limit: restrict the angle between parent's axis and child's axis
        glm::vec3 parentAxis = stateA.rotation * joint.axis;
        glm::vec3 childAxis = stateB.rotation * joint.axis;

        float dotProduct = glm::dot(parentAxis, childAxis);
        float angleBetween = glm::acos(glm::clamp(dotProduct, -1.0f, 1.0f));

        if (angleBetween > joint.coneAngle) {
            // Push child axis back toward the cone boundary
            glm::vec3 rotAxis = glm::cross(childAxis, parentAxis);
            if (glm::length(rotAxis) > 0.0001f) {
                rotAxis = glm::normalize(rotAxis);
                float overshoot = angleBetween - joint.coneAngle;
                glm::quat fix = glm::angleAxis(overshoot * 0.5f, rotAxis);
                stateB.rotation = fix * stateB.rotation;
                stateB.rotation = glm::normalize(stateB.rotation);
            }
        }
    }
    // Fixed joints: same as above but with very tight cone (handled by small coneAngle)
}

// ─── Main ragdoll physics system ─────────────────────────────────
void ragdollPhysicsSystem(entt::registry& registry, float dt) {
    auto view = registry.view<Ragdoll, Transform>();

    // Gather world collision AABBs once per frame
    // (reuse the spatial hash from Ch 9, or a simpler list)
    std::vector<AABB> worldAABBs;
    // ... populate from level geometry ...

    for (auto [entity, ragdoll, transform] : view.each()) {
        if (!ragdoll.active || ragdoll.settled) continue;

        ragdoll.activeTime += dt;
        const RagdollDef& def = *ragdoll.def;

        // ─── Step 1: Apply gravity ────────────────────────────────
        for (size_t i = 0; i < ragdoll.bodies.size(); i++) {
            ragdoll.bodies[i].linearVelocity.y -= RAGDOLL_GRAVITY * dt;
        }

        // ─── Step 2: Integrate velocities ─────────────────────────
        for (size_t i = 0; i < ragdoll.bodies.size(); i++) {
            RagdollBodyState& state = ragdoll.bodies[i];

            // Linear: position += velocity * dt
            state.position += state.linearVelocity * dt;

            // Angular: quaternion integration (see C++ Concepts section)
            state.rotation = integrateRotation(state.rotation,
                                                state.angularVelocity, dt);
        }

        // ─── Step 3: World collision ──────────────────────────────
        for (size_t i = 0; i < ragdoll.bodies.size(); i++) {
            collideBodyWithWorld(ragdoll.bodies[i], def.bodies[i], worldAABBs);
        }

        // ─── Step 4: Joint constraint solving (iterative) ─────────
        for (int iter = 0; iter < CONSTRAINT_ITERATIONS; iter++) {
            for (const auto& joint : def.joints) {
                solveJointConstraint(
                    ragdoll.bodies[joint.bodyIndexA],
                    ragdoll.bodies[joint.bodyIndexB],
                    def.bodies[joint.bodyIndexA],
                    def.bodies[joint.bodyIndexB],
                    joint
                );
            }
        }

        // ─── Step 5: Apply damping ────────────────────────────────
        for (size_t i = 0; i < ragdoll.bodies.size(); i++) {
            ragdoll.bodies[i].linearVelocity *= LINEAR_DAMPING;
            ragdoll.bodies[i].angularVelocity *= ANGULAR_DAMPING;
        }

        // ─── Check settling ───────────────────────────────────────
        bool allSlow = true;
        for (const auto& body : ragdoll.bodies) {
            float speed = glm::length(body.linearVelocity)
                        + glm::length(body.angularVelocity);
            if (speed > SETTLE_VELOCITY_THRESHOLD) {
                allSlow = false;
                break;
            }
        }

        if (allSlow) {
            ragdoll.settled = true;
            // Zero out all velocities to prevent micro-drift
            for (auto& body : ragdoll.bodies) {
                body.linearVelocity = glm::vec3(0.0f);
                body.angularVelocity = glm::vec3(0.0f);
            }
        }
    }
}
```

### Why Multiple Constraint Iterations?

A single pass through the joints is not enough. Correcting one joint moves the bodies it connects, which violates the constraints of neighbouring joints. Each iteration reduces the total error. After 4-8 iterations, the error is small enough to be invisible.

```
Iteration 1:  Shoulder joint corrected → pushes arm → elbow joint violated
Iteration 2:  Elbow joint corrected → pushes forearm → shoulder slightly disturbed
Iteration 3:  Shoulder re-corrected → smaller push → elbow slightly disturbed
Iteration 4:  Elbow re-corrected → tiny push → almost converged
...
Iteration 6:  All joints within acceptable tolerance
```

This is **Gauss-Seidel iteration** — we solve each constraint sequentially, and each solution uses the most up-to-date positions from previous solutions in the same iteration. It converges faster than Jacobi iteration (which would use positions from the start of the iteration).

---

## Ragdoll-to-Bone Mapping (Rendering)

When the ragdoll is active, we need to feed the ragdoll body transforms back into the bone matrix array so the skinning shader can deform the mesh. The shader does not know or care whether the matrices came from animation keyframes or physics bodies — it just applies them.

### ragdollRenderSystem

```cpp
// In src/engine/ecs/systems/ragdoll_render_system.h

#pragma once

#include <entt/entt.hpp>

void ragdollRenderSystem(entt::registry& registry);
```

```cpp
// In src/engine/ecs/systems/ragdoll_render_system.cpp

#include "engine/ecs/systems/ragdoll_render_system.h"
#include "engine/ecs/components/ragdoll.h"
#include "engine/ecs/components/animator.h"
#include "engine/ecs/components/transform.h"

void ragdollRenderSystem(entt::registry& registry) {
    auto view = registry.view<Ragdoll, Animator, Transform>();

    for (auto [entity, ragdoll, animator, transform] : view.each()) {
        if (!ragdoll.active) continue;
        if (!animator.skeleton) continue;

        const Skeleton& skeleton = *animator.skeleton;
        const RagdollDef& def = *ragdoll.def;
        glm::mat4 modelMatrix = transform.getModelMatrix();
        glm::mat4 invModel = glm::inverse(modelMatrix);

        // Ensure bone matrices array is the right size
        animator.boneMatrices.resize(skeleton.bones.size(), glm::mat4(1.0f));

        // For each ragdoll body, compute the bone matrix that would place
        // the mesh vertices at the correct world position
        for (size_t i = 0; i < def.bodies.size(); i++) {
            const RagdollBody& bodyDef = def.bodies[i];
            const RagdollBodyState& state = ragdoll.bodies[i];

            if (bodyDef.boneIndex < 0 ||
                bodyDef.boneIndex >= static_cast<int>(skeleton.bones.size())) {
                continue;
            }

            // Build the body's world transform
            // Undo the local offset to get back to the bone origin
            glm::vec3 boneWorldPos = state.position
                                    - state.rotation * bodyDef.localOffset;

            glm::mat4 boneWorldTransform = glm::translate(glm::mat4(1.0f), boneWorldPos)
                                          * glm::mat4_cast(state.rotation);

            // The skinning shader expects:
            //   boneMatrix[i] = worldTransform[i] * offsetMatrix[i]
            //
            // But boneWorldTransform is in world space, and the shader applies
            // the model matrix on top. So we need to transform into model space:
            //   boneMatrix[i] = inverse(modelMatrix) * boneWorldTransform * offsetMatrix
            const glm::mat4& offsetMatrix = skeleton.bones[bodyDef.boneIndex].offsetMatrix;

            animator.boneMatrices[bodyDef.boneIndex] =
                invModel * boneWorldTransform * offsetMatrix;
        }
    }
}
```

The chain of transforms works like this:

```
SHADER DOES:    model * boneMatrix * vertexPosition

WE SET:         boneMatrix = invModel * boneWorldTransform * offsetMatrix

RESULT:         model * invModel * boneWorldTransform * offsetMatrix * vertexPosition
                       ↓
                    (identity)
                = boneWorldTransform * offsetMatrix * vertexPosition

This correctly places the vertex in world space at the ragdoll body's position.
```

### System Execution Order

```
EACH FRAME:
   animationSystem()        ← skipped for entities where ragdoll.active == true
   ragdollPhysicsSystem()   ← simulates active ragdolls (runs in fixed timestep)
   ragdollRenderSystem()    ← overrides bone matrices with ragdoll poses
   renderSystem()           ← draws meshes using the skinning shader (unchanged)
```

The `animationSystem` should skip entities with an active ragdoll. Add a check at the top of the animation loop:

```cpp
// In animationSystem, at the start of the per-entity loop:
if (registry.all_of<Ragdoll>(entity)) {
    const auto& ragdoll = registry.get<Ragdoll>(entity);
    if (ragdoll.active) continue;  // Ragdoll drives the bones now
}
```

---

## Ragdoll Settling and Cleanup

Active ragdolls consume CPU every frame. Once a ragdoll has come to rest, there is no reason to keep simulating it. We also need to limit the total number of active ragdolls to prevent performance degradation in a room full of corpses.

### Settling Detection

The `ragdollPhysicsSystem` already detects settling by checking if all body velocities are below a threshold. When settled, it zeros all velocities and sets `ragdoll.settled = true`. The render system continues to use the frozen body positions — the corpse stays visible in its final resting pose.

### Cleanup System

```cpp
// In src/engine/ecs/systems/ragdoll_cleanup_system.cpp

#include "engine/ecs/components/ragdoll.h"
#include <entt/entt.hpp>
#include <vector>
#include <algorithm>

constexpr float RAGDOLL_MAX_LIFETIME = 30.0f;   // Destroy after 30 seconds
constexpr int   MAX_ACTIVE_RAGDOLLS = 8;         // Max simultaneous ragdolls

void ragdollCleanupSystem(entt::registry& registry, float dt) {
    // ─── Remove ragdolls that have exceeded their lifetime ────────
    auto view = registry.view<Ragdoll>();
    std::vector<entt::entity> toDestroy;

    for (auto [entity, ragdoll] : view.each()) {
        if (!ragdoll.active) continue;

        if (ragdoll.activeTime > RAGDOLL_MAX_LIFETIME) {
            toDestroy.push_back(entity);
        }
    }

    // ─── Enforce maximum active ragdoll count ─────────────────────
    // If we have too many, remove the oldest ones first
    struct RagdollInfo {
        entt::entity entity;
        float activeTime;
    };

    std::vector<RagdollInfo> activeRagdolls;
    for (auto [entity, ragdoll] : view.each()) {
        if (ragdoll.active) {
            activeRagdolls.push_back({ entity, ragdoll.activeTime });
        }
    }

    if (static_cast<int>(activeRagdolls.size()) > MAX_ACTIVE_RAGDOLLS) {
        // Sort by active time (oldest first)
        std::sort(activeRagdolls.begin(), activeRagdolls.end(),
                  [](const RagdollInfo& a, const RagdollInfo& b) {
                      return a.activeTime > b.activeTime;
                  });

        int excessCount = static_cast<int>(activeRagdolls.size()) - MAX_ACTIVE_RAGDOLLS;
        for (int i = 0; i < excessCount; i++) {
            toDestroy.push_back(activeRagdolls[i].entity);
        }
    }

    // ─── Destroy marked entities ──────────────────────────────────
    for (auto entity : toDestroy) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
}
```

### Optional: Fade-Out Before Destruction

Instead of popping out of existence, you can fade the corpse over 1-2 seconds. Add a `FadeOut` component that the render system reads to reduce alpha:

```cpp
struct FadeOut {
    float duration = 2.0f;
    float elapsed = 0.0f;
};

// In the cleanup system, instead of immediate destruction:
if (ragdoll.activeTime > RAGDOLL_MAX_LIFETIME - 2.0f) {
    if (!registry.all_of<FadeOut>(entity)) {
        registry.emplace<FadeOut>(entity, FadeOut{ 2.0f, 0.0f });
    }
}

// In the render system:
if (registry.all_of<FadeOut>(entity)) {
    auto& fade = registry.get<FadeOut>(entity);
    float alpha = 1.0f - (fade.elapsed / fade.duration);
    shader.setFloat("alpha", glm::max(alpha, 0.0f));
}
```

---

## Death Impulse

The death impulse is what makes ragdolls satisfying. Without it, every enemy just crumples straight down. With it, a shotgun blast sends the body flying backward, a headshot snaps the head back while the body follows, and an explosion throws limbs outward.

### Applying the Impulse

```cpp
// In src/engine/physics/ragdoll_impulse.h

#pragma once

#include "engine/ecs/components/ragdoll.h"
#include <glm/glm.hpp>

// Apply an impulse to the ragdoll at the moment of death.
// direction: normalised direction the damage came FROM (e.g. bullet travel direction)
// force: strength of the impulse (scaled by weapon damage)
// hitBoneIndex: which bone was hit (-1 = apply to all bodies evenly)
void applyDeathImpulse(Ragdoll& ragdoll,
                        const glm::vec3& direction,
                        float force,
                        int hitBoneIndex) {
    const RagdollDef& def = *ragdoll.def;

    // Scale force into a velocity impulse
    // Typical values: pistol = 3-5, shotgun = 8-12, rocket = 15-25
    float impulseStrength = force * 0.15f;

    // Find the body closest to the hit bone
    int hitBodyIndex = -1;
    if (hitBoneIndex >= 0) {
        for (size_t i = 0; i < def.bodies.size(); i++) {
            if (def.bodies[i].boneIndex == hitBoneIndex) {
                hitBodyIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (hitBodyIndex >= 0) {
        // ─── Localised impulse: hit body gets full force ──────────
        float hitBodyMass = def.bodies[hitBodyIndex].mass;
        glm::vec3 impulse = direction * impulseStrength / hitBodyMass;
        ragdoll.bodies[hitBodyIndex].linearVelocity += impulse;

        // Neighbours get a reduced impulse (propagation)
        for (const auto& joint : def.joints) {
            int neighbourIndex = -1;
            if (joint.bodyIndexA == hitBodyIndex) {
                neighbourIndex = joint.bodyIndexB;
            } else if (joint.bodyIndexB == hitBodyIndex) {
                neighbourIndex = joint.bodyIndexA;
            }

            if (neighbourIndex >= 0) {
                float neighbourMass = def.bodies[neighbourIndex].mass;
                glm::vec3 reducedImpulse = direction * impulseStrength * 0.4f
                                           / neighbourMass;
                ragdoll.bodies[neighbourIndex].linearVelocity += reducedImpulse;
            }
        }

        // Add angular velocity to the hit body for a spinning effect
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 torque = glm::cross(direction, up) * impulseStrength * 2.0f;
        ragdoll.bodies[hitBodyIndex].angularVelocity += torque;
    } else {
        // ─── Uniform impulse: spread across all bodies ────────────
        for (size_t i = 0; i < ragdoll.bodies.size(); i++) {
            float bodyMass = def.bodies[i].mass;
            glm::vec3 impulse = direction * impulseStrength / bodyMass;
            ragdoll.bodies[i].linearVelocity += impulse;
        }
    }
}
```

### Hit Location Effects

Different hit locations produce different ragdoll reactions:

```
HIT LOCATION       EFFECT                           VISUAL RESULT
────────────────────────────────────────────────────────────────────
Headshot            Impulse on head body              Head snaps back, body
                    + slight whole-body push           stumbles backward

Chest               Impulse on torso body             Body doubles over
                    + angular velocity                 backward or to the side

Legs                Impulse on leg body               Legs fly up,
                    (low on the body)                  character falls

Explosion           Uniform impulse on all bodies     Full-body launch,
(from below)        direction = up + outward            limbs flail
```

### Gibbing Threshold

If the damage is extreme (rocket direct hit, point-blank shotgun), skip the ragdoll entirely and spawn gibs — small physics-driven chunks that fly apart. This uses the particle system from Chapter 20.

```cpp
// In the damage system, before triggering ragdoll:
constexpr float GIB_THRESHOLD = 100.0f;  // Damage that triggers gibbing

void applyDamage(entt::registry& registry, entt::entity target,
                  float damage, const glm::vec3& direction, int hitBone) {
    auto& health = registry.get<Health>(target);
    health.current -= damage;
    health.lastDamageDirection = direction;
    health.lastDamageAmount = damage;
    health.lastHitBoneIndex = hitBone;

    if (health.current <= 0.0f) {
        if (damage >= GIB_THRESHOLD) {
            // Extreme damage — spawn gibs instead of ragdoll
            spawnGibs(registry, target, direction, damage);
            registry.destroy(target);
        }
        // Otherwise, ragdollTransitionSystem handles it next frame
    }
}

void spawnGibs(entt::registry& registry, entt::entity target,
                const glm::vec3& direction, float force) {
    const auto& transform = registry.get<Transform>(target);

    // Spawn 8-12 small physics chunks
    int gibCount = 8 + (rand() % 5);
    for (int i = 0; i < gibCount; i++) {
        auto gib = registry.create();

        // Random offset from entity centre
        glm::vec3 offset(
            (float(rand()) / RAND_MAX - 0.5f) * 0.5f,
            (float(rand()) / RAND_MAX) * 1.0f,
            (float(rand()) / RAND_MAX - 0.5f) * 0.5f
        );

        registry.emplace<Transform>(gib, transform.position + offset);
        registry.emplace<MeshRenderer>(gib, MeshRenderer{ "gib_chunk" });

        // Random velocity based on explosion direction
        glm::vec3 gibVel = direction * force * 0.1f + glm::vec3(
            (float(rand()) / RAND_MAX - 0.5f) * 5.0f,
            float(rand()) / RAND_MAX * 8.0f,
            (float(rand()) / RAND_MAX - 0.5f) * 5.0f
        );

        registry.emplace<RigidBody>(gib, RigidBody{
            0.5f, gibVel, glm::vec3(0.0f, -9.81f, 0.0f)
        });
        registry.emplace<Lifetime>(gib, Lifetime{ 5.0f });
    }
}
```

---

## C++ Concepts

### Quaternion Integration for Angular Velocity

In the physics system, we need to update a body's orientation by its angular velocity each frame. With Euler angles, this is error-prone — the order you apply the three rotations matters, and gimbal lock makes some orientations unreachable. With quaternions, integration is both correct and straightforward.

The mathematical basis: the time derivative of a unit quaternion `q` under angular velocity `w` is:

```
dq/dt = 0.5 * quat(0, w.x, w.y, w.z) * q
```

We approximate the integral with a first-order Euler step:

```cpp
glm::quat integrateRotation(const glm::quat& q, const glm::vec3& w, float dt) {
    glm::quat wQuat(0.0f, w.x, w.y, w.z);
    glm::quat dq = 0.5f * wQuat * q;
    glm::quat result = q + dq * dt;
    return glm::normalize(result);  // Must re-normalise
}
```

The `normalize` call at the end is critical. Quaternions represent rotations only when they have unit length. Each integration step introduces a tiny numerical error that accumulates over time. Without re-normalisation, the quaternion's length drifts away from 1.0, and the rotation matrix it produces will contain scaling artifacts — the mesh stretches and distorts. A single `normalize` per body per frame prevents this.

### Iterative Constraint Solving (Gauss-Seidel)

Ragdoll joints form a system of constraints that must all be satisfied simultaneously. Each joint says "these two bodies must be connected at this point." But satisfying one joint may violate another.

The Gauss-Seidel approach solves this iteratively:

1. Loop through all joints
2. For each joint, compute the position error (how far the anchor points are apart)
3. Apply a correction: push both bodies toward each other
4. Repeat from step 1

Each pass reduces the total error. The key property of Gauss-Seidel (vs Jacobi) is that each correction immediately uses the results of previous corrections in the same iteration. This means corrections propagate along the chain within a single iteration, giving faster convergence.

```
Jacobi:     All corrections computed from OLD positions → slow convergence
Gauss-Seidel: Each correction uses LATEST positions → fast convergence

With 6 iterations, Gauss-Seidel typically reduces joint error below 1mm.
With 6 iterations, Jacobi might still have 5mm+ error.
```

The trade-off: Gauss-Seidel is order-dependent (the result depends on which joint you solve first). In practice, this produces no visible artifacts for ragdolls.

### Shared Immutable Data vs Per-Instance Mutable State

The `RagdollDef` is created once and shared by every enemy of the same type. It is never modified after creation. The `Ragdoll` component holds per-instance data that changes every frame — positions, velocities, active state.

This is the same pattern used throughout the engine:
- `Skeleton` (shared) vs `Animator` (per-instance)
- `AnimationClip` (shared) vs `Animator.currentTime` (per-instance)
- Mesh data on GPU (shared) vs `Transform` (per-instance)

The pattern saves memory (one `RagdollDef` for 50 zombies, not 50 copies) and enables cache-friendly iteration (the `Ragdoll` component is small and tightly packed, while the definition data is accessed through a pointer only when needed).

In C++ terms, this is enforced through `const` pointer:

```cpp
struct Ragdoll {
    const RagdollDef* def;     // const — systems cannot modify the definition
    std::vector<RagdollBodyState> bodies;  // non-const — systems modify freely
};
```

Any system that tries to write through `def` gets a compile error. The compiler enforces the shared-immutable contract.

---

## What's Next

In **Chapter 42**, we'll build an animation layer system for playing different animations on the upper and lower body simultaneously. A character will run (lower body) while aiming and shooting (upper body), with smooth blending at the spine where the two layers meet. This extends the skeletal animation system from Chapter 33 with per-bone weight masks that control which bones each layer affects.
