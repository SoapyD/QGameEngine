# Chapter 40: Animation Events & Notifies

## What You'll Learn
- Why animations need to trigger gameplay actions at specific frames
- Designing an AnimationEvent data structure embedded in animation clips
- Detecting crossed events as the animation system advances time
- Handling edge cases: looping wraps, variable framerates, batched events
- An AnimationEventQueue component for decoupled event delivery
- An eventDispatchSystem that routes events to sound, particles, damage, and custom handlers
- Extracting world-space bone positions for placing effects at skeleton joints
- Authoring animation events in JSON data files
- Using `std::variant` and `std::visit` for type-safe event parameters

---

## The Problem

In Chapter 33, we built a skeletal animation system. Bones move, meshes deform, characters walk and attack. But there is a gap between what the player *sees* and what the game *does*. Right now, the animation is purely visual — it cannot trigger anything.

Consider a walking enemy. The walk cycle plays, legs swing forward and back, but no footstep sounds play. If we trigger a footstep sound when the walk animation starts, the timing is wrong — the foot hasn't hit the ground yet. If we guess the timing with a fixed delay, it drifts out of sync when animation speed changes or when different walk cycles have different cadences.

The same problem appears everywhere:

```
PROBLEM                              WRONG SOLUTION             RIGHT SOLUTION
─────────────────────────────────────────────────────────────────────────────────
Footstep sounds                      Play at animation start    Fire at foot-contact frame
Muzzle flash particle                Spawn when fire() called   Spawn at barrel-flash frame
Melee damage                         Apply during entire clip   Apply only in swing window
Reload click sounds                  Hardcode 0.3s delay        Fire at mag-out, mag-in frames
Shell casing ejection                Spawn immediately          Spawn at ejection-port frame
```

The right solution is **animation events** — metadata embedded in the animation clip that says "at frame 8, play a footstep sound" or "at frame 5, start the damage window." The animation system detects when playback crosses an event's timestamp and fires it. Other systems pick up the event and do the actual work.

This pattern decouples animation from gameplay. The animator (the person, not the component) places events in the tool. The engine fires them at the right moment. Game systems respond without knowing anything about animation internals.

---

## Animation Event Data Structure

An animation event is a point in time within a clip, tagged with a type and carrying parameters. We store events directly inside the `AnimationClip` from Chapter 33.

### Event Types

```cpp
// In src/engine/animation/animation_event.h

#pragma once

#include <string>
#include <variant>
#include <vector>
#include <glm/glm.hpp>

enum class AnimEventType {
    Sound,          // Play an audio clip
    Particle,       // Spawn a particle emitter
    DamageWindow,   // Open or close a damage window
    Custom          // Extensible tag for game-specific logic
};
```

### Event Parameters

Different event types need different data. A sound event needs a sound file name and a volume. A particle event needs an effect name and a bone to attach it to. A damage window event needs to know whether it is opening or closing. We use `std::variant` to hold these different parameter types in a single field.

```cpp
// In src/engine/animation/animation_event.h (continued)

struct SoundParams {
    std::string soundName;   // Asset name: "footstep_stone", "gun_fire"
    float volume = 1.0f;
    float pitch = 1.0f;
};

struct ParticleParams {
    std::string effectName;  // Particle effect: "muzzle_flash", "dust_puff"
    int boneIndex = -1;      // Bone to spawn at (-1 = entity origin)
    glm::vec3 offset{0.0f};  // Local offset from bone position
};

struct DamageWindowParams {
    bool open = true;        // true = start dealing damage, false = stop
    float damageAmount = 10.0f;
    float radius = 1.0f;     // Hitbox radius
    int boneIndex = -1;      // Bone to centre the hitbox on
};

struct CustomParams {
    std::string tag;         // Game-specific: "spawn_shell_casing", "camera_shake"
    float value = 0.0f;
};

using AnimEventParams = std::variant<SoundParams, ParticleParams,
                                      DamageWindowParams, CustomParams>;
```

### The AnimationEvent Struct

```cpp
// In src/engine/animation/animation_event.h (continued)

struct AnimationEvent {
    float time;              // When in the clip this event fires (seconds)
    AnimEventType type;
    AnimEventParams params;
};
```

### Embedding Events in AnimationClip

We add a single field to the `AnimationClip` struct from Chapter 33:

```cpp
// In src/engine/animation/animation_clip.h (modified)

struct AnimationClip {
    std::string name;
    float duration;
    float ticksPerSecond;
    std::vector<BoneAnimation> channels;

    // NEW: events embedded in the clip timeline
    std::vector<AnimationEvent> events;  // Sorted by time, ascending
};
```

Events are sorted by time. This is important — the detection algorithm relies on it for efficient scanning.

### Example: Walk Cycle Events

```
Walk Cycle (duration: 1.0s, 30 fps)
──────────────────────────────────────────────────────────────

Frame:  0         8              15         22            30
        │         │              │          │             │
Time:   0.0      0.27           0.50       0.73          1.0
        │         │              │          │             │
Event:  ─────────[FOOTSTEP]─────│─────────[FOOTSTEP]─────│
                  left foot               right foot
                  contacts                contacts
                  ground                  ground
```

```cpp
// Example: building a walk cycle's events
AnimationClip walkClip;
walkClip.name = "walk";
walkClip.duration = 1.0f;
walkClip.ticksPerSecond = 30.0f;

walkClip.events = {
    { 0.27f, AnimEventType::Sound,
      SoundParams{ "footstep_left", 0.6f, 1.0f } },
    { 0.73f, AnimEventType::Sound,
      SoundParams{ "footstep_right", 0.6f, 1.0f } }
};
```

### Example: Melee Attack Events

```
Melee Attack (duration: 0.8s, 30 fps)
──────────────────────────────────────────────────────────────

Frame:  0    4       5          12          18         24
        │    │       │          │           │          │
Time:   0.0  0.13   0.17       0.40        0.60       0.80
        │    │       │          │           │          │
Event:  ─────[WHOOSH]│          │           │──────────│
                     [DMG_START]│           │
                                [DMG_END]  │
                                            clip ends
                     ├──────────┤
                     damage window active
```

```cpp
// Example: building a melee attack's events
AnimationClip meleeClip;
meleeClip.name = "melee_attack";
meleeClip.duration = 0.8f;
meleeClip.ticksPerSecond = 30.0f;

meleeClip.events = {
    { 0.13f, AnimEventType::Sound,
      SoundParams{ "whoosh_swing", 0.8f, 1.0f } },
    { 0.17f, AnimEventType::DamageWindow,
      DamageWindowParams{ true, 25.0f, 1.5f, -1 } },
    { 0.40f, AnimEventType::DamageWindow,
      DamageWindowParams{ false, 0.0f, 0.0f, -1 } }
};
```

---

## Event Detection in the Animation System

The animation system from Chapter 33 advances `currentTime` each frame. We need to detect every event between the old time and the new time. This must be correct even when the framerate varies and when the animation loops.

### The Core Idea

```
Previous frame:    currentTime = 0.20
This frame:        currentTime = 0.35  (dt advanced it by 0.15)

Events in clip:
    0.10   0.27   0.50   0.73

             ↑
     This event (0.27) falls between 0.20 and 0.35.
     It must fire this frame.
```

We scan the sorted event list for events whose time falls in the half-open interval `(previousTime, currentTime]`. We use a half-open interval so that events fire exactly once — they fire on the first frame where currentTime passes them.

### Handling Looping

When a looping animation wraps, `currentTime` resets to the beginning. If we only check `(previousTime, currentTime]`, we miss events near the end of the clip. The fix: split the check into two intervals.

```
Looping wrap:
    previousTime = 0.90
    clip duration = 1.00
    currentTime after wrap = 0.05

    Check interval 1: (0.90, 1.00]  — events near the end
    Check interval 2: (0.00, 0.05]  — events near the start
```

### Tracking Previous Time

We need to store the previous frame's animation time. Add one field to the `Animator` component:

```cpp
// In src/engine/ecs/components/animator.h (addition)

struct Animator {
    const AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    float previousTime = 0.0f;   // NEW: time at start of this frame
    bool looping = true;
    float speed = 1.0f;

    const AnimationClip* previousClip = nullptr;
    float previousClipTime = 0.0f;
    float blendTime = 0.0f;
    float blendTimer = 0.0f;

    std::vector<glm::mat4> boneMatrices;
    const Skeleton* skeleton = nullptr;
};
```

### Event Collection Function

This function scans a clip's event list and returns all events that fall in a time range. It handles both the normal case and the looping-wrap case.

```cpp
// In src/engine/animation/animation_event.h

#include <entt/entt.hpp>

struct FiredEvent {
    entt::entity entity;
    AnimationEvent event;
};

// Collect all events in the half-open interval (startTime, endTime].
// If the animation looped (wrapped is true), collect events in
// (startTime, clipDuration] and (0, endTime].
void collectEvents(
    const AnimationClip& clip,
    float startTime,
    float endTime,
    bool wrapped,
    entt::entity entity,
    std::vector<FiredEvent>& outEvents)
{
    if (clip.events.empty()) return;

    if (!wrapped) {
        // Normal case: simple forward scan
        for (const auto& evt : clip.events) {
            if (evt.time > startTime && evt.time <= endTime) {
                outEvents.push_back({ entity, evt });
            }
            // Events are sorted — if we pass endTime, stop early
            if (evt.time > endTime) break;
        }
    } else {
        // Looping wrap: two intervals
        // Interval 1: (startTime, clipDuration]
        for (const auto& evt : clip.events) {
            if (evt.time > startTime && evt.time <= clip.duration) {
                outEvents.push_back({ entity, evt });
            }
        }
        // Interval 2: (0, endTime]
        for (const auto& evt : clip.events) {
            if (evt.time > 0.0f && evt.time <= endTime) {
                outEvents.push_back({ entity, evt });
            }
            if (evt.time > endTime) break;
        }
    }
}
```

---

## AnimationEventQueue Component

Fired events need somewhere to go. We use a component that accumulates events during animation evaluation and is consumed by downstream systems.

```cpp
// In src/engine/ecs/components/animation_event_queue.h

#pragma once

#include "engine/animation/animation_event.h"
#include <vector>

struct AnimationEventQueue {
    std::vector<FiredEvent> events;
};
```

This is a **singleton component** — one per registry, not one per entity. We attach it to a dedicated entity or use EnTT's context storage:

```cpp
// At engine initialisation:
registry.ctx().emplace<AnimationEventQueue>();
```

Using `registry.ctx()` avoids creating a dummy entity. Any system can access the queue with:

```cpp
auto& queue = registry.ctx().get<AnimationEventQueue>();
```

---

## Modified Animation System

We modify the `animationSystem` from Chapter 33 to detect and collect events as it advances time. The key changes are: save `previousTime` before advancing, detect if wrapping occurred, and call `collectEvents`.

```cpp
// In src/engine/ecs/systems/animation_system.cpp (modified)

#include "engine/ecs/systems/animation_system.h"
#include "engine/ecs/components/animator.h"
#include "engine/ecs/components/animation_event_queue.h"
#include "engine/animation/animation_utils.h"
#include "engine/animation/animation_event.h"

void animationSystem(entt::registry& registry, float dt) {
    // Clear the event queue at the start of each frame
    auto& eventQueue = registry.ctx().get<AnimationEventQueue>();
    eventQueue.events.clear();

    auto view = registry.view<Animator>();

    for (auto [entity, animator] : view.each()) {
        if (!animator.currentClip || !animator.skeleton) continue;

        // ─── Save previous time ─────────────────────────────────
        animator.previousTime = animator.currentTime;

        // ─── Advance animation time ─────────────────────────────
        float timeAdvance = dt * animator.speed * animator.currentClip->ticksPerSecond;
        animator.currentTime += timeAdvance;

        bool wrapped = false;

        if (animator.looping) {
            if (animator.currentTime > animator.currentClip->duration) {
                wrapped = true;
                animator.currentTime = fmod(animator.currentTime,
                                             animator.currentClip->duration);
            }
        } else {
            if (animator.currentTime > animator.currentClip->duration) {
                animator.currentTime = animator.currentClip->duration;
            }
        }

        // ─── Collect animation events ───────────────────────────
        collectEvents(
            *animator.currentClip,
            animator.previousTime,
            animator.currentTime,
            wrapped,
            entity,
            eventQueue.events
        );

        // ─── Compute bone transforms (unchanged from Ch 33) ────
        std::vector<glm::mat4> currentTransforms = computeBoneTransforms(
            *animator.skeleton,
            *animator.currentClip,
            animator.currentTime
        );

        // ─── Blending (unchanged from Ch 33) ────────────────────
        if (animator.previousClip && animator.blendTimer < animator.blendTime) {
            animator.blendTimer += dt;
            float blendFactor = glm::clamp(animator.blendTimer / animator.blendTime,
                                            0.0f, 1.0f);

            animator.previousClipTime += dt * animator.speed
                                          * animator.previousClip->ticksPerSecond;
            if (animator.previousClipTime > animator.previousClip->duration) {
                animator.previousClipTime = fmod(animator.previousClipTime,
                                                  animator.previousClip->duration);
            }

            std::vector<glm::mat4> prevTransforms = computeBoneTransforms(
                *animator.skeleton,
                *animator.previousClip,
                animator.previousClipTime
            );

            size_t boneCount = animator.skeleton->bones.size();
            animator.boneMatrices.resize(boneCount);
            for (size_t i = 0; i < boneCount; i++) {
                for (int col = 0; col < 4; col++) {
                    animator.boneMatrices[i][col] = glm::mix(
                        prevTransforms[i][col],
                        currentTransforms[i][col],
                        blendFactor
                    );
                }
            }

            if (animator.blendTimer >= animator.blendTime) {
                animator.previousClip = nullptr;
                animator.blendTimer = 0.0f;
            }
        } else {
            animator.boneMatrices = std::move(currentTransforms);
        }
    }
}
```

The event collection adds minimal overhead — it is a linear scan over a small sorted array (most clips have fewer than 10 events). The existing bone transform computation is unchanged.

---

## Extracting World-Space Bone Positions

Before we dispatch events, we need a utility that other systems will rely on. When a particle event says "spawn at bone 12," we need the world-space position of bone 12. We already have the bone matrices from the animator and the entity's model matrix from its `Transform` component. Combining them gives us world space.

```cpp
// In src/engine/animation/animation_utils.h (addition)

#include "engine/ecs/components/animator.h"
#include "engine/ecs/components/transform.h"
#include <entt/entt.hpp>

// Returns the world-space position of a specific bone on an animated entity.
// The bone matrices transform from bone space to model space.
// The entity's model matrix transforms from model space to world space.
glm::vec3 getBoneWorldPosition(entt::registry& registry, entt::entity entity,
                                int boneIndex) {
    const auto& animator = registry.get<Animator>(entity);
    const auto& transform = registry.get<Transform>(entity);

    // Safety check
    if (boneIndex < 0 || boneIndex >= static_cast<int>(animator.boneMatrices.size())) {
        return transform.position;  // Fallback to entity origin
    }

    // The bone matrix includes the offset (inverse bind pose) baked in.
    // To get the bone's actual world position, we need the bone's world transform
    // WITHOUT the offset matrix. However, since we need just the position,
    // we can extract it by transforming the origin through the bone matrix
    // and then through the entity's model matrix.
    //
    // boneMatrices[i] = worldTransform[i] * offsetMatrix[i]
    // A point at the bone origin in bind pose is at: inverse(offsetMatrix) * vec4(0,0,0,1)
    // After animation: boneMatrices[i] * inverse(offsetMatrix[i]) * vec4(0,0,0,1)
    //
    // Simpler approach: the 4th column of (modelMatrix * boneMatrix * inverseOffset)
    // gives us the world position. But we don't store inverseOffset separately.
    //
    // Practical shortcut: the bone matrix applied to the bone's bind-pose origin
    // gives the model-space position. We just need vec4(0,0,0,1) transformed.

    glm::mat4 modelMatrix = transform.getModelMatrix();
    glm::vec4 boneOrigin = animator.boneMatrices[boneIndex] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 worldPos = modelMatrix * boneOrigin;

    return glm::vec3(worldPos);
}
```

This function is used by every system that needs to place something at a bone: sounds, particles, hitboxes, shell casings. It is cheap — two matrix-vector multiplies.

---

## Event Dispatch System

The `eventDispatchSystem` reads the `AnimationEventQueue` and routes each event to the appropriate handler. It runs after `animationSystem` and before gameplay systems that consume the results (like the melee damage system).

```
System execution order (relevant portion):

   animationSystem()         ← advances time, collects events
        │
        ▼
   eventDispatchSystem()     ← routes events to handlers
        │
        ├──→ AudioManager::play3D()       (Sound events)
        ├──→ spawn particle emitter        (Particle events)
        ├──→ add/remove DamageWindowActive (DamageWindow events)
        └──→ custom handler lookup         (Custom events)
        │
        ▼
   meleeDamageSystem()       ← checks DamageWindowActive component
   combatSystem()
   ...
```

### The Dispatch System

```cpp
// In src/engine/ecs/systems/event_dispatch_system.h

#pragma once

#include <entt/entt.hpp>

void eventDispatchSystem(entt::registry& registry);
```

```cpp
// In src/engine/ecs/systems/event_dispatch_system.cpp

#include "engine/ecs/systems/event_dispatch_system.h"
#include "engine/ecs/components/animation_event_queue.h"
#include "engine/ecs/components/transform.h"
#include "engine/ecs/components/animator.h"
#include "engine/ecs/components/damage_window_active.h"
#include "engine/audio/audio_manager.h"
#include "engine/animation/animation_utils.h"

// Forward declaration for custom event handler
void handleCustomAnimEvent(entt::registry& registry, entt::entity entity,
                            const CustomParams& params);

void eventDispatchSystem(entt::registry& registry) {
    auto& eventQueue = registry.ctx().get<AnimationEventQueue>();

    for (const auto& fired : eventQueue.events) {
        entt::entity entity = fired.entity;

        // Skip if entity was destroyed between event collection and dispatch
        if (!registry.valid(entity)) continue;

        std::visit([&](const auto& params) {
            using T = std::decay_t<decltype(params)>;

            if constexpr (std::is_same_v<T, SoundParams>) {
                // ─── Sound Event ─────────────────────────────────
                glm::vec3 position = registry.get<Transform>(entity).position;
                AudioManager::play3D(params.soundName, position,
                                      params.volume, params.pitch);
            }
            else if constexpr (std::is_same_v<T, ParticleParams>) {
                // ─── Particle Event ──────────────────────────────
                glm::vec3 spawnPos;
                if (params.boneIndex >= 0) {
                    spawnPos = getBoneWorldPosition(registry, entity,
                                                     params.boneIndex);
                } else {
                    spawnPos = registry.get<Transform>(entity).position;
                }
                spawnPos += params.offset;

                // Create a particle emitter entity (see Ch 20)
                auto emitter = registry.create();
                registry.emplace<Transform>(emitter, spawnPos);
                // registry.emplace<ParticleEmitter>(emitter, params.effectName);
                // The particle system from Ch 20 picks this up next frame
            }
            else if constexpr (std::is_same_v<T, DamageWindowParams>) {
                // ─── Damage Window Event ─────────────────────────
                if (params.open) {
                    registry.emplace_or_replace<DamageWindowActive>(
                        entity,
                        DamageWindowActive{
                            params.damageAmount,
                            params.radius,
                            params.boneIndex
                        }
                    );
                } else {
                    registry.remove<DamageWindowActive>(entity);
                }
            }
            else if constexpr (std::is_same_v<T, CustomParams>) {
                // ─── Custom Event ────────────────────────────────
                handleCustomAnimEvent(registry, entity, params);
            }

        }, fired.event.params);
    }
}
```

### The DamageWindowActive Component

This is a tag-like component. When it exists on an entity, the melee damage system knows that entity is in the "swinging" phase and should deal damage on contact.

```cpp
// In src/engine/ecs/components/damage_window_active.h

#pragma once

struct DamageWindowActive {
    float damage = 10.0f;
    float radius = 1.0f;
    int boneIndex = -1;    // Centre of the hitbox
};
```

The animation event system adds this component when the damage window opens and removes it when it closes. No other system needs to know about animation timing. The melee system just checks: "Does this entity have `DamageWindowActive`? If so, do a sphere overlap check."

### Custom Event Handler

The custom handler is a simple switch on the tag string. This is where game-specific behaviour lives — it is the extension point.

```cpp
// In src/engine/ecs/systems/event_dispatch_system.cpp (continued)

void handleCustomAnimEvent(entt::registry& registry, entt::entity entity,
                            const CustomParams& params) {
    if (params.tag == "camera_shake") {
        // Apply screen shake with intensity = params.value
        // auto& shake = registry.ctx().get<ScreenShake>();
        // shake.intensity = params.value;
    }
    else if (params.tag == "spawn_shell_casing") {
        // Spawn a physics shell casing at the ejection port bone
        // (See practical examples section below)
    }
    else if (params.tag == "footstep_query_surface") {
        // Play footstep based on surface material at entity position
        // (See practical examples section below)
    }
}
```

---

## Practical Examples

These examples show how the event system connects to existing QEngine systems. Each one demonstrates a different event type and dispatch pattern.

### Footstep System: Surface-Aware Sounds

Instead of embedding a fixed sound name in the event, we use a custom event that queries the surface material under the character's feet. The walk animation fires a `footstep_query_surface` custom event at each foot contact frame.

```cpp
// In src/engine/ecs/systems/footstep_system.cpp

#include "engine/physics/raycast.h"
#include "engine/audio/audio_manager.h"

void handleFootstep(entt::registry& registry, entt::entity entity) {
    const auto& transform = registry.get<Transform>(entity);

    // Cast a ray downward to find the surface material
    RayHit hit;
    glm::vec3 origin = transform.position + glm::vec3(0.0f, 0.1f, 0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);

    if (!raycast(registry, origin, direction, 2.0f, hit)) return;

    // Choose sound based on surface material tag
    std::string soundName;
    switch (hit.surfaceMaterial) {
        case SurfaceMaterial::Stone:  soundName = "footstep_stone";  break;
        case SurfaceMaterial::Metal:  soundName = "footstep_metal";  break;
        case SurfaceMaterial::Dirt:   soundName = "footstep_dirt";   break;
        case SurfaceMaterial::Wood:   soundName = "footstep_wood";   break;
        case SurfaceMaterial::Water:  soundName = "footstep_splash"; break;
        default:                      soundName = "footstep_stone";  break;
    }

    // Slight random pitch variation for natural feel
    float pitch = 0.9f + static_cast<float>(rand() % 20) / 100.0f;
    AudioManager::play3D(soundName, transform.position, 0.5f, pitch);
}
```

### Muzzle Flash: Particle + Light at Weapon Bone

The fire animation has a particle event at the barrel flash frame. The dispatch system spawns a particle emitter and a temporary point light at the weapon bone's world position.

```cpp
// In the ParticleParams handler (expanded):

// After computing spawnPos from the bone...
if (params.effectName == "muzzle_flash") {
    // Spawn particle emitter
    auto emitter = registry.create();
    registry.emplace<Transform>(emitter, spawnPos);
    // registry.emplace<ParticleEmitter>(emitter, "muzzle_flash");

    // Spawn temporary point light for one frame of illumination
    auto light = registry.create();
    registry.emplace<Transform>(light, spawnPos);
    registry.emplace<PointLight>(light, PointLight{
        glm::vec3(1.0f, 0.8f, 0.3f),  // Warm orange
        5.0f,                            // Intensity
        3.0f                             // Radius
    });
    registry.emplace<Lifetime>(light, Lifetime{ 0.05f });  // 50ms flash
}
```

### Melee Attack: Damage Window

The melee attack clip from the earlier example opens the damage window at frame 5 and closes it at frame 12. During that window, the `meleeDamageSystem` checks for overlapping enemies:

```cpp
// In src/engine/ecs/systems/melee_damage_system.cpp

void meleeDamageSystem(entt::registry& registry) {
    auto attackers = registry.view<DamageWindowActive, Transform, Animator>();
    auto targets = registry.view<Health, Transform>();

    for (auto [attacker, dmgWindow, atkTransform, animator] : attackers.each()) {
        // Get the hitbox centre — either at a specific bone or at entity origin
        glm::vec3 hitboxCentre;
        if (dmgWindow.boneIndex >= 0) {
            hitboxCentre = getBoneWorldPosition(registry, attacker,
                                                 dmgWindow.boneIndex);
        } else {
            hitboxCentre = atkTransform.position;
        }

        // Check against all damageable targets
        for (auto [target, health, targetTransform] : targets.each()) {
            if (target == attacker) continue;  // Don't hit yourself

            float distance = glm::length(targetTransform.position - hitboxCentre);
            if (distance < dmgWindow.radius) {
                health.current -= dmgWindow.damage;
                // Optional: apply knockback, spawn hit particles, etc.
            }
        }
    }
}
```

The beauty of this design: the melee system knows nothing about animation. It just checks for the `DamageWindowActive` component. The animation event system adds and removes that component at the right moments. Full decoupling.

### Reload: Multiple Timed Sounds

A reload animation fires three separate sound events at different frames:

```cpp
AnimationClip reloadClip;
reloadClip.name = "reload";
reloadClip.duration = 2.0f;
reloadClip.ticksPerSecond = 30.0f;

reloadClip.events = {
    { 0.3f, AnimEventType::Sound,
      SoundParams{ "magazine_out", 0.7f, 1.0f } },
    { 1.0f, AnimEventType::Sound,
      SoundParams{ "magazine_in", 0.8f, 1.0f } },
    { 1.5f, AnimEventType::Sound,
      SoundParams{ "bolt_rack", 0.9f, 1.0f } }
};
```

No custom code needed — the standard sound dispatch handles all three. The sounds play at exactly the right moments because the animator placed the events at the right frames.

### Shell Casing: Spawn Physics Entity at Bone

A custom event triggers spawning a shell casing entity with physics:

```cpp
// Clip event (at ejection frame):
{ 0.08f, AnimEventType::Custom,
  CustomParams{ "spawn_shell_casing", 0.0f } }

// In handleCustomAnimEvent:
if (params.tag == "spawn_shell_casing") {
    int ejectionBone = registry.get<Animator>(entity).skeleton->findBone("EjectionPort");
    if (ejectionBone < 0) return;

    glm::vec3 spawnPos = getBoneWorldPosition(registry, entity, ejectionBone);

    auto casing = registry.create();
    registry.emplace<Transform>(casing, spawnPos);
    registry.emplace<MeshRenderer>(casing, MeshRenderer{ "shell_casing" });
    registry.emplace<RigidBody>(casing, RigidBody{
        0.01f,                                      // mass (kg)
        glm::vec3(1.5f, 2.0f, 0.5f),               // initial velocity (eject right and up)
        glm::vec3(0.0f, -9.81f, 0.0f)               // gravity
    });
    registry.emplace<Lifetime>(casing, Lifetime{ 3.0f });  // Despawn after 3 seconds
}
```

---

## Authoring Events in Data

Animation events should be data-driven, not hard-coded. We store them in JSON alongside the animation clip data. This integrates with the model loading pipeline from Chapter 36.

### JSON Format

```json
{
    "name": "walk",
    "duration": 1.0,
    "ticksPerSecond": 30.0,
    "events": [
        {
            "time": 0.27,
            "type": "Sound",
            "params": {
                "soundName": "footstep_left",
                "volume": 0.6,
                "pitch": 1.0
            }
        },
        {
            "time": 0.73,
            "type": "Sound",
            "params": {
                "soundName": "footstep_right",
                "volume": 0.6,
                "pitch": 1.0
            }
        }
    ]
}
```

### A More Complex Example: Melee with Mixed Event Types

```json
{
    "name": "melee_attack",
    "duration": 0.8,
    "ticksPerSecond": 30.0,
    "events": [
        {
            "time": 0.13,
            "type": "Sound",
            "params": { "soundName": "whoosh_swing", "volume": 0.8, "pitch": 1.0 }
        },
        {
            "time": 0.17,
            "type": "DamageWindow",
            "params": { "open": true, "damageAmount": 25.0, "radius": 1.5, "boneIndex": -1 }
        },
        {
            "time": 0.40,
            "type": "DamageWindow",
            "params": { "open": false }
        },
        {
            "time": 0.17,
            "type": "Particle",
            "params": { "effectName": "sword_trail", "boneIndex": 8, "offset": [0, 0, 0] }
        }
    ]
}
```

### Parsing Events from JSON

When loading an animated model (Chapter 36), we parse the events array and populate the clip's event list:

```cpp
// In src/engine/animation/animation_loader.cpp

#include <nlohmann/json.hpp>
#include "engine/animation/animation_event.h"
#include "engine/animation/animation_clip.h"

using json = nlohmann::json;

AnimEventParams parseEventParams(const std::string& type, const json& j) {
    if (type == "Sound") {
        return SoundParams{
            j.value("soundName", ""),
            j.value("volume", 1.0f),
            j.value("pitch", 1.0f)
        };
    }
    else if (type == "Particle") {
        glm::vec3 offset(0.0f);
        if (j.contains("offset")) {
            offset = glm::vec3(j["offset"][0], j["offset"][1], j["offset"][2]);
        }
        return ParticleParams{
            j.value("effectName", ""),
            j.value("boneIndex", -1),
            offset
        };
    }
    else if (type == "DamageWindow") {
        return DamageWindowParams{
            j.value("open", true),
            j.value("damageAmount", 10.0f),
            j.value("radius", 1.0f),
            j.value("boneIndex", -1)
        };
    }
    else {
        return CustomParams{
            j.value("tag", type),
            j.value("value", 0.0f)
        };
    }
}

void loadAnimationEvents(AnimationClip& clip, const json& clipJson) {
    if (!clipJson.contains("events")) return;

    for (const auto& evtJson : clipJson["events"]) {
        AnimationEvent evt;
        evt.time = evtJson.value("time", 0.0f);

        std::string typeStr = evtJson.value("type", "Custom");

        if (typeStr == "Sound")             evt.type = AnimEventType::Sound;
        else if (typeStr == "Particle")     evt.type = AnimEventType::Particle;
        else if (typeStr == "DamageWindow") evt.type = AnimEventType::DamageWindow;
        else                                evt.type = AnimEventType::Custom;

        evt.params = parseEventParams(typeStr, evtJson["params"]);
        clip.events.push_back(evt);
    }

    // Ensure events are sorted by time (required by collectEvents)
    std::sort(clip.events.begin(), clip.events.end(),
              [](const AnimationEvent& a, const AnimationEvent& b) {
                  return a.time < b.time;
              });
}
```

---

## C++ Concepts

### `std::variant` — A Type-Safe Union

`std::variant` is a tagged union introduced in C++17. It holds one value from a fixed set of types, and it always knows which type is currently stored. Unlike a raw `union`, it is type-safe — you cannot accidentally read the wrong type.

```cpp
#include <variant>
#include <string>

// A variant that can hold an int, a float, or a string
std::variant<int, float, std::string> value;

value = 42;                     // Now holds an int
value = 3.14f;                  // Now holds a float
value = std::string("hello");   // Now holds a string

// Check which type is active
if (std::holds_alternative<int>(value)) {
    int i = std::get<int>(value);
}

// std::get throws std::bad_variant_access if the type is wrong
// float f = std::get<float>(value);  // throws! value currently holds a string
```

In our animation events, `AnimEventParams` is a variant over four parameter types. Each `AnimationEvent` stores exactly one of `SoundParams`, `ParticleParams`, `DamageWindowParams`, or `CustomParams`. The variant ensures that code handling a `SoundParams` can never accidentally read it as a `DamageWindowParams`.

### `std::visit` — Dispatching on Variant Type

`std::visit` calls a callable (lambda, function object) with the currently held value. Combined with `if constexpr`, it gives us compile-time type dispatch:

```cpp
#include <variant>
#include <iostream>

std::variant<int, float, std::string> value = 42;

std::visit([](const auto& v) {
    using T = std::decay_t<decltype(v)>;

    if constexpr (std::is_same_v<T, int>) {
        std::cout << "Integer: " << v << "\n";
    }
    else if constexpr (std::is_same_v<T, float>) {
        std::cout << "Float: " << v << "\n";
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "String: " << v << "\n";
    }
}, value);
```

This pattern is used directly in `eventDispatchSystem`. The `std::visit` call with a generic lambda and `if constexpr` branches replaces what would otherwise be a manual type check or a virtual function hierarchy. It is zero-cost at runtime — the compiler generates a direct jump table.

### Event Queues as a Decoupling Pattern

The `AnimationEventQueue` is a simple example of an **event queue** — a fundamental pattern for decoupling producers from consumers.

```
                  ┌────────────────────┐
                  │ AnimationEventQueue │
                  │                    │
  animationSystem │  [evt][evt][evt]   │  eventDispatchSystem
  ──── writes ──→ │                    │ ──── reads ──→
                  │  (vector of events)│
                  └────────────────────┘

  Producer does not know who consumes.
  Consumer does not know who produces.
  They share only the data format (FiredEvent).
```

This is the same pattern used by operating systems (event loops), GUI frameworks (message queues), and game engines (command buffers). The producer and consumer run at different times, in different systems, and can be changed independently. The queue is the contract between them.

In ECS, this pattern preserves the rule that systems have no state and components have no behaviour. The queue is a component (data). Writing to it and reading from it are done by separate systems (behaviour). Neither system holds a reference to the other.

---

## What's Next

In **Chapter 41**, we'll build a ragdoll physics system for death transitions. When an enemy's health reaches zero, the skeletal animation system hands off to a physics simulation — each bone becomes a rigid body connected by constraints, and the character crumbles under gravity. This bridges the animation system from this chapter with real-time physics, turning stiff "death animations" into dynamic, unrepeatable collapses.
