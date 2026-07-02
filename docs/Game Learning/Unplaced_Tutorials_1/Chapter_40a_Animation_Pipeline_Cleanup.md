# Chapter 40a: Animation Pipeline Cleanup

> **Prerequisites:** Chapter 40 (Animation Events & Notifies) completed. You should have a working event dispatch system with `AnimationEventQueue`, `collectEvents()`, `eventDispatchSystem()`, and the `DamageWindowActive` component. Your animation clips embed events in JSON, and the dispatch system routes them to sound, particle, damage window, and custom handlers.

---

## Time for Another Cleanup

You know the rhythm by now. Chapters 5a, 10a, 15a, 20a, 25a, 30a, and 35a each followed the same pattern: the features work, the code does not scale. Chapters 36 through 40 added skeletal animation loading, animation events, event detection with loop wrapping, and a dispatch system that routes events to gameplay handlers. The results are impressive -- animation-driven footsteps, timed damage windows, bone-positioned particles. But the code that powers it has the usual post-feature smell.

Open your `event_dispatch_system.cpp` and the animation-related files you touched in Chapters 36 through 40. Count the problems:

```cpp
// event_dispatch_system.cpp — CURRENT STATE (Chapter 40)

void eventDispatchSystem(entt::registry& registry) {
    auto& eventQueue = registry.ctx().get<AnimationEventQueue>();

    for (const auto& fired : eventQueue.events) {
        entt::entity entity = fired.entity;
        if (!registry.valid(entity)) continue;

        std::visit([&](const auto& params) {
            using T = std::decay_t<decltype(params)>;

            if constexpr (std::is_same_v<T, SoundParams>) {
                glm::vec3 position = registry.get<Transform>(entity).position;
                AudioManager::play3D(params.soundName, position,
                                      params.volume, params.pitch);
            }
            else if constexpr (std::is_same_v<T, ParticleParams>) {
                glm::vec3 spawnPos;
                if (params.boneIndex >= 0) {
                    spawnPos = getBoneWorldPosition(registry, entity,
                                                     params.boneIndex);
                } else {
                    spawnPos = registry.get<Transform>(entity).position;
                }
                spawnPos += params.offset;

                auto emitter = registry.create();
                registry.emplace<Transform>(emitter, spawnPos);
            }
            else if constexpr (std::is_same_v<T, DamageWindowParams>) {
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
                handleCustomAnimEvent(registry, entity, params);
            }

        }, fired.event.params);
    }
}
```

And the custom handler:

```cpp
void handleCustomAnimEvent(entt::registry& registry, entt::entity entity,
                            const CustomParams& params) {
    if (params.tag == "camera_shake") {
        // ...
    }
    else if (params.tag == "spawn_shell_casing") {
        // ...
    }
    else if (params.tag == "footstep_query_surface") {
        // ...
    }
}
```

And the `Animator` component from Chapter 40:

```cpp
struct Animator {
    const AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    float previousTime = 0.0f;
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

And the bone mask utilities from Chapter 42, which build masks programmatically:

```cpp
BoneMask upperBodyMask = buildBoneMask(skeleton, "Spine");
BoneMask lowerBodyMask = invertMask(upperBodyMask, skeleton.bones.size());
BoneMask armsMask      = combineMasks(buildBoneMask(skeleton, "LeftShoulder"),
                                       buildBoneMask(skeleton, "RightShoulder"));
BoneMask headMask      = buildBoneMask(skeleton, "Neck");
BoneMask fullBodyMask  = buildFullBodyMask(skeleton);
```

Three problems stand out:

1. **The dispatch system uses hardcoded `if constexpr` type matching.** Every time we add a new event type -- and we will, as the game grows -- we must modify `eventDispatchSystem()`, add a new branch, and recompile every file that includes it. The `handleCustomAnimEvent()` function has the same problem with string matching: each new custom event tag requires a new `else if` branch. This is the variant-visit equivalent of a switch statement that grows without bound.

2. **Animation components are accumulating and mixing concerns.** The `Animator` component holds hot data (currentTime, speed, previousTime) that changes every frame alongside cold data (skeleton pointer, clip pointers) that changes only on animation transitions. The `AnimationEventQueue` sits in registry context. The `DamageWindowActive` component is a transient tag driven entirely by animation events. We have not audited what touches what, and the `Animator` struct is getting large enough to matter for cache performance.

3. **Bone masks are built in code and discarded.** Chapter 42 constructs masks with `buildBoneMask(skeleton, "Spine")` at setup time, but these masks are not named, not stored, and not reusable. If two systems both need "upper_body," they each build it independently. The mask definitions are scattered across initialisation code instead of being centralised and data-driven.

Here is our plan:

| Problem | Solution |
|---|---|
| Hardcoded `if constexpr` dispatch chain | `AnimEventDispatcher` with registered handler callbacks |
| Growing `else if` chain in custom handler | String-keyed map for custom event handlers |
| Animator mixing hot and cold data | Split into `AnimatorState` (hot) and `AnimatorConfig` (cold) |
| Bone masks built ad-hoc, unnamed | `BoneMaskLibrary` loading named presets from JSON |

---

## Step 1: AnimEventDispatcher — Replacing the if constexpr Chain

The `if constexpr` pattern in `eventDispatchSystem` is elegant for a fixed set of types, but it violates the open-closed principle: adding a new event type means modifying the dispatch function. We want game code to *register* handlers at startup, and the dispatch system to route events to them without knowing what they do.

### The Handler Type

Each handler is a function that takes the registry, the entity, and the event parameters. Since different event types have different parameter structs, we use `std::function` with the specific parameter type:

```cpp
// In src/engine/animation/anim_event_dispatcher.h

#pragma once

#include "engine/animation/animation_event.h"
#include <entt/entt.hpp>
#include <functional>
#include <unordered_map>
#include <string>
#include <iostream>
#include <variant>

// ─── Handler signature for typed events ──────────────────────────
// Each handler receives the registry, the entity that fired the event,
// and the specific parameter struct for that event type.

using SoundHandler    = std::function<void(entt::registry&, entt::entity, const SoundParams&)>;
using ParticleHandler = std::function<void(entt::registry&, entt::entity, const ParticleParams&)>;
using DamageHandler   = std::function<void(entt::registry&, entt::entity, const DamageWindowParams&)>;
using CustomHandler   = std::function<void(entt::registry&, entt::entity, const CustomParams&)>;
```

### The Dispatcher Class

The `AnimEventDispatcher` holds one handler per `AnimEventType` for the typed events, and a string-keyed map for custom events. Registration happens at startup. Dispatch happens every frame.

```cpp
// In src/engine/animation/anim_event_dispatcher.h (continued)

class AnimEventDispatcher
{
public:
    // ─── Register handlers for typed events ──────────────────
    // Each AnimEventType gets exactly one handler. Registering
    // a new handler for the same type replaces the old one.

    void registerSoundHandler(SoundHandler handler)
    {
        m_soundHandler = std::move(handler);
    }

    void registerParticleHandler(ParticleHandler handler)
    {
        m_particleHandler = std::move(handler);
    }

    void registerDamageWindowHandler(DamageHandler handler)
    {
        m_damageHandler = std::move(handler);
    }

    // ─── Register handlers for custom events ─────────────────
    // Custom events are keyed by their string tag. Multiple
    // custom handlers coexist — one per tag.

    void registerCustomHandler(const std::string& tag, CustomHandler handler)
    {
        m_customHandlers[tag] = std::move(handler);
        std::cout << "AnimEventDispatcher: registered custom handler '"
                  << tag << "'" << std::endl;
    }

    // ─── Dispatch a single event ─────────────────────────────
    // Called by eventDispatchSystem for each fired event. Routes
    // to the appropriate handler based on the variant type.

    void dispatch(entt::registry& registry, entt::entity entity,
                  const AnimEventParams& params) const
    {
        std::visit([&](const auto& p) {
            using T = std::decay_t<decltype(p)>;

            if constexpr (std::is_same_v<T, SoundParams>) {
                if (m_soundHandler) {
                    m_soundHandler(registry, entity, p);
                }
            }
            else if constexpr (std::is_same_v<T, ParticleParams>) {
                if (m_particleHandler) {
                    m_particleHandler(registry, entity, p);
                }
            }
            else if constexpr (std::is_same_v<T, DamageWindowParams>) {
                if (m_damageHandler) {
                    m_damageHandler(registry, entity, p);
                }
            }
            else if constexpr (std::is_same_v<T, CustomParams>) {
                dispatchCustom(registry, entity, p);
            }
        }, params);
    }

private:
    // ─── Custom event dispatch ───────────────────────────────
    // Looks up the handler by tag. Unregistered tags are silently
    // ignored (not every custom tag needs a handler at all times).

    void dispatchCustom(entt::registry& registry, entt::entity entity,
                        const CustomParams& params) const
    {
        auto it = m_customHandlers.find(params.tag);
        if (it != m_customHandlers.end()) {
            it->second(registry, entity, params);
        }
    }

    SoundHandler    m_soundHandler;
    ParticleHandler m_particleHandler;
    DamageHandler   m_damageHandler;

    std::unordered_map<std::string, CustomHandler> m_customHandlers;
};
```

### What Changed

The `if constexpr` chain still exists inside `dispatch()`, but it is now a thin routing layer -- it does not contain any gameplay logic. The actual handler code (playing sounds, spawning particles, toggling damage windows) lives in the functions registered at startup. Adding a new custom event tag means calling `registerCustomHandler()` once. No modification to the dispatch class. No recompilation of the dispatch system.

Notice that `dispatch()` still uses `std::visit` with `if constexpr` internally. We have not eliminated the pattern -- we have moved it from a system that contains gameplay logic into a class that contains only routing logic. The dispatcher's `dispatch()` method is stable. It changes only when a new variant alternative is added to `AnimEventParams`, which is a much rarer event than adding a new gameplay handler.

### Why Not a Fully Generic Handler Map?

You might wonder: why not key handlers by `std::type_index` and avoid the `if constexpr` entirely? Something like:

```cpp
std::unordered_map<std::type_index, std::function<void(...)>> m_handlers;
```

This would be fully generic, but it requires type erasure on the parameter type -- every handler would receive a `const void*` or a `std::any`, and would need to cast back to the correct type. We lose compile-time type safety. The `if constexpr` approach keeps the compiler checking that `SoundHandler` really receives `SoundParams`, not `DamageWindowParams`. For four typed event categories, the explicit approach is clearer and safer.

---

## Step 2: Animation Component Audit

The `Animator` component from Chapter 40 has grown to hold both frame-rate-sensitive playback state and relatively static configuration. Let us measure what we have and decide what to split.

### Before: The Monolithic Animator

```cpp
// Animator — Chapter 40 state
struct Animator {
    const AnimationClip* currentClip = nullptr;   // 8 bytes (pointer)
    float currentTime = 0.0f;                     // 4 bytes
    float previousTime = 0.0f;                    // 4 bytes
    bool looping = true;                          // 1 byte (+3 padding)
    float speed = 1.0f;                           // 4 bytes

    const AnimationClip* previousClip = nullptr;  // 8 bytes (pointer)
    float previousClipTime = 0.0f;                // 4 bytes
    float blendTime = 0.0f;                       // 4 bytes
    float blendTimer = 0.0f;                      // 4 bytes

    std::vector<glm::mat4> boneMatrices;          // 24 bytes (vector header)
    const Skeleton* skeleton = nullptr;            // 8 bytes (pointer)
};
// Total: ~76 bytes (approximate, depends on alignment)
```

Every frame, the animation system reads and writes `currentTime`, `previousTime`, and `speed`. It reads `looping`, `currentClip`, and occasionally `previousClip` and the blend fields during transitions. The `boneMatrices` vector is written every frame (the output). The `skeleton` pointer is read every frame but never written after setup.

The problem: `boneMatrices` is a `std::vector`, which means a heap allocation. The vector header (pointer + size + capacity) sits inline in the component, but the actual matrix data is on the heap. When the animation system iterates over all `Animator` components, it touches the inline fields in cache-friendly order, but then chases a pointer to the heap for each entity's bone matrices. This is unavoidable for variable-length data, but we can at least separate the fields the animation system touches every frame from the fields it touches rarely.

### After: AnimatorState (Hot) + AnimatorConfig (Cold)

```cpp
// In src/engine/ecs/components/animator_state.h

#pragma once

#include "engine/animation/animation_clip.h"

// ─── AnimatorState ───────────────────────────────────────────────
// Hot data: touched every frame by animationSystem.
// Kept small so iterating over all animated entities stays
// cache-friendly.

struct AnimatorState {
    float currentTime = 0.0f;        // 4 bytes
    float previousTime = 0.0f;       // 4 bytes
    float speed = 1.0f;              // 4 bytes
    bool looping = true;             // 1 byte (+3 padding)

    // Cross-fade progress (active only during transitions)
    float blendTimer = 0.0f;         // 4 bytes
    float blendTime = 0.0f;          // 4 bytes
    float previousClipTime = 0.0f;   // 4 bytes
};
// Total: 28 bytes (fits in half a cache line)
```

```cpp
// In src/engine/ecs/components/animator_config.h

#pragma once

#include "engine/animation/animation_clip.h"
#include "engine/animation/skeleton.h"
#include <glm/glm.hpp>
#include <vector>

// ─── AnimatorConfig ──────────────────────────────────────────────
// Cold data: clip pointers, skeleton reference, and the output
// bone matrices. Changes only on animation transitions (clip
// switches) or during setup.

struct AnimatorConfig {
    const AnimationClip* currentClip = nullptr;    // 8 bytes
    const AnimationClip* previousClip = nullptr;   // 8 bytes
    const Skeleton* skeleton = nullptr;             // 8 bytes

    // Output: computed each frame, but stored here because the
    // rendering system needs access. The vector header is cold
    // (rarely resized), but the data it points to is written
    // every frame. This is the best we can do without a separate
    // output buffer.
    std::vector<glm::mat4> boneMatrices;           // 24 bytes (header)
};
// Total: 48 bytes
```

### sizeof Comparison

```
BEFORE (monolithic Animator):
    sizeof(Animator) ≈ 76 bytes
    One component per entity in a single pool.

AFTER (split):
    sizeof(AnimatorState)  = 28 bytes   ← hot, iterated every frame
    sizeof(AnimatorConfig) = 48 bytes   ← cold, touched on transitions + output

    Combined: 76 bytes (same total), but the hot path only
    touches 28 bytes per entity before needing Config data.
```

The improvement is modest for small entity counts but significant when iterating over hundreds of animated entities. The `AnimatorState` pool fits more components per cache line (two per 64-byte line versus one for the monolithic struct). The animation system's time-advance loop touches only `AnimatorState`. It reads `AnimatorConfig` only when it needs the clip pointer and skeleton -- which it does every frame for bone evaluation, but by that point it has already decided *whether* to evaluate (skipping entities with null clips). The early-out check (`if (!config.currentClip)`) happens on the cold data, but the time arithmetic happens on the hot data.

### System and Component Access Map

With the split, here is which systems touch which components:

```
SYSTEM                          READS                    WRITES
───────────────────────────────────────────────────────────────────
animationSystem                 AnimatorState            AnimatorState
                                AnimatorConfig           AnimatorConfig.boneMatrices

eventDispatchSystem             AnimationEventQueue      (via handlers)
                                AnimatorConfig           ---
                                Transform                ---

meleeDamageSystem               DamageWindowActive       Health
                                Transform                ---
                                AnimatorConfig           ---

skinnedMeshRenderSystem         AnimatorConfig            ---
                                Transform                ---

AI / gameplay systems           ---                      AnimatorState (speed)
                                                         AnimatorConfig (clip changes)
```

The `AnimationEventQueue` remains in registry context -- it is a singleton, not per-entity. The `DamageWindowActive` component is transient: added by the damage window handler, removed by the same handler, consumed by `meleeDamageSystem`. It does not need splitting.

### Migration: Keeping the Old Name as an Alias

To avoid a massive rename across every file that uses `Animator`, we provide a backward-compatible access pattern:

```cpp
// In src/engine/ecs/components/animator.h (updated)

#pragma once

#include "engine/ecs/components/animator_state.h"
#include "engine/ecs/components/animator_config.h"

// ─── Convenience: creating an animated entity ────────────────────
// Instead of the old single-component emplace, callers now set up
// both components. This helper keeps the call site clean.

inline void emplaceAnimator(entt::registry& registry, entt::entity entity,
                             const Skeleton* skeleton,
                             const AnimationClip* initialClip,
                             bool looping = true, float speed = 1.0f)
{
    auto& state = registry.emplace<AnimatorState>(entity);
    state.looping = looping;
    state.speed = speed;

    auto& config = registry.emplace<AnimatorConfig>(entity);
    config.skeleton = skeleton;
    config.currentClip = initialClip;

    if (skeleton) {
        config.boneMatrices.resize(skeleton->bones.size(), glm::mat4(1.0f));
    }
}

// ─── Convenience: switching animation clips ──────────────────────
// Encapsulates the cross-fade setup that used to be scattered
// across AI and gameplay code.

inline void switchAnimation(entt::registry& registry, entt::entity entity,
                             const AnimationClip* newClip,
                             float blendDuration = 0.2f)
{
    auto& state = registry.get<AnimatorState>(entity);
    auto& config = registry.get<AnimatorConfig>(entity);

    if (config.currentClip == newClip) return;  // Already playing

    config.previousClip = config.currentClip;
    state.previousClipTime = state.currentTime;
    state.blendTime = blendDuration;
    state.blendTimer = 0.0f;

    config.currentClip = newClip;
    state.currentTime = 0.0f;
    state.previousTime = 0.0f;
}
```

The `emplaceAnimator()` and `switchAnimation()` helpers replace the scattered setup code that used to directly poke at `Animator` fields. Existing code that used `registry.get<Animator>(entity).speed = 2.0f` now becomes `registry.get<AnimatorState>(entity).speed = 2.0f` -- a straightforward find-and-replace.

---

## Step 3: Bone Mask Presets — BoneMaskLibrary

Chapter 42 introduced `buildBoneMask()` and the supporting utilities. But the mask definitions are scattered through initialisation code: one system builds "upper_body" by calling `buildBoneMask(skeleton, "Spine")`, another system builds the same mask independently. There is no central registry of named masks, and the bone names are hardcoded strings.

We want this:

```cpp
// At setup time:
boneMaskLibrary.load("assets/animation/bone_masks.json", skeleton);

// At runtime:
const BoneMask* upperBody = boneMaskLibrary.getMask("upper_body");
const BoneMask* lowerBody = boneMaskLibrary.getMask("lower_body");
```

### The JSON Format

```json
{
    "masks": [
        {
            "name": "full_body",
            "mode": "all"
        },
        {
            "name": "upper_body",
            "mode": "subtree",
            "rootBone": "Spine"
        },
        {
            "name": "lower_body",
            "mode": "invert",
            "source": "upper_body"
        },
        {
            "name": "arms_only",
            "mode": "combine",
            "sources": [
                { "mode": "subtree", "rootBone": "LeftShoulder" },
                { "mode": "subtree", "rootBone": "RightShoulder" }
            ]
        },
        {
            "name": "head_only",
            "mode": "subtree",
            "rootBone": "Neck"
        }
    ]
}
```

The `mode` field tells the library how to build each mask:
- `"all"` -- every bone set (full body)
- `"subtree"` -- a root bone and all its descendants
- `"invert"` -- the complement of another named mask
- `"combine"` -- the union of multiple sub-masks

This covers every mask pattern from Chapter 42 without hardcoding bone names in C++.

### BoneMaskLibrary

```cpp
// In src/engine/animation/bone_mask_library.h

#pragma once

#include "engine/animation/bone_mask.h"
#include "engine/animation/skeleton.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <vector>

// ─── BoneMaskLibrary ─────────────────────────────────────────────
// Loads named bone mask presets from a JSON file and caches them.
// Animation layers reference masks by name instead of building
// them in code.
//
// Usage:
//   BoneMaskLibrary maskLib;
//   maskLib.load("assets/animation/bone_masks.json", skeleton);
//   const BoneMask* upper = maskLib.getMask("upper_body");

class BoneMaskLibrary
{
public:
    // ─── Load presets from a JSON file ───────────────────────
    // Requires a Skeleton reference to resolve bone names to
    // indices. Masks are built and cached immediately.
    // Returns true on success.

    bool load(const std::string& filePath, const Skeleton& skeleton)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            std::cerr << "BoneMaskLibrary: failed to open '"
                      << filePath << "'" << std::endl;
            return false;
        }

        nlohmann::json j;
        try
        {
            file >> j;
        }
        catch (const nlohmann::json::parse_error& e)
        {
            std::cerr << "BoneMaskLibrary: parse error in '"
                      << filePath << "': " << e.what() << std::endl;
            return false;
        }

        if (!j.contains("masks") || !j["masks"].is_array())
        {
            std::cerr << "BoneMaskLibrary: expected 'masks' array in '"
                      << filePath << "'" << std::endl;
            return false;
        }

        // Two-pass approach: first pass builds subtree and all masks,
        // second pass resolves invert and combine that reference other masks.
        // This allows masks to reference each other regardless of order.

        // First pass
        for (const auto& entry : j["masks"])
        {
            std::string name = entry.value("name", "");
            std::string mode = entry.value("mode", "");

            if (name.empty()) continue;

            if (mode == "all")
            {
                m_masks[name] = buildFullBodyMask(skeleton);
            }
            else if (mode == "subtree")
            {
                std::string rootBone = entry.value("rootBone", "");
                m_masks[name] = buildBoneMask(skeleton, rootBone);
            }
        }

        // Second pass
        for (const auto& entry : j["masks"])
        {
            std::string name = entry.value("name", "");
            std::string mode = entry.value("mode", "");

            if (name.empty()) continue;

            if (mode == "invert")
            {
                std::string source = entry.value("source", "");
                auto it = m_masks.find(source);
                if (it != m_masks.end())
                {
                    m_masks[name] = invertMask(it->second,
                                                skeleton.bones.size());
                }
                else
                {
                    std::cerr << "BoneMaskLibrary: invert source '"
                              << source << "' not found for mask '"
                              << name << "'" << std::endl;
                }
            }
            else if (mode == "combine")
            {
                BoneMask combined;
                if (entry.contains("sources") && entry["sources"].is_array())
                {
                    for (const auto& src : entry["sources"])
                    {
                        std::string srcMode = src.value("mode", "");
                        if (srcMode == "subtree")
                        {
                            std::string rootBone = src.value("rootBone", "");
                            combined = combineMasks(combined,
                                buildBoneMask(skeleton, rootBone));
                        }
                        else if (src.contains("name"))
                        {
                            // Reference an already-loaded mask by name
                            std::string refName = src["name"].get<std::string>();
                            auto it = m_masks.find(refName);
                            if (it != m_masks.end())
                            {
                                combined = combineMasks(combined, it->second);
                            }
                        }
                    }
                }
                m_masks[name] = combined;
            }
        }

        std::cout << "BoneMaskLibrary: loaded " << m_masks.size()
                  << " mask presets" << std::endl;
        return true;
    }

    // ─── Retrieve a mask by name ─────────────────────────────
    // Returns a pointer to the cached mask, or nullptr if not
    // found. The pointer remains valid for the library's lifetime.

    const BoneMask* getMask(const std::string& name) const
    {
        auto it = m_masks.find(name);
        if (it != m_masks.end())
        {
            return &it->second;
        }

        std::cerr << "BoneMaskLibrary: mask '" << name
                  << "' not found" << std::endl;
        return nullptr;
    }

    // ─── Check if a mask exists ──────────────────────────────
    bool hasMask(const std::string& name) const
    {
        return m_masks.find(name) != m_masks.end();
    }

    // ─── Register a mask programmatically ────────────────────
    // For masks that cannot be expressed in JSON (e.g., computed
    // at runtime based on equipment).

    void registerMask(const std::string& name, const BoneMask& mask)
    {
        m_masks[name] = mask;
    }

    // ─── Clear all cached masks ──────────────────────────────
    void clear()
    {
        m_masks.clear();
    }

private:
    std::unordered_map<std::string, BoneMask> m_masks;
};
```

### Integrating with Animation Layers

With the library in place, animation layer setup becomes data-driven. Instead of building masks inline, layers reference them by name:

**Before (Chapter 42):**

```cpp
// Scattered across setup code
BoneMask upperBodyMask = buildBoneMask(skeleton, "Spine");

AnimationLayer shootLayer;
shootLayer.clip = &shootClip;
shootLayer.mask = upperBodyMask;
shootLayer.blendMode = LayerBlendMode::Override;
```

**After (Chapter 40a):**

```cpp
// Masks loaded once from JSON
auto& maskLib = registry.ctx().get<BoneMaskLibrary>();

AnimationLayer shootLayer;
shootLayer.clip = &shootClip;

const BoneMask* upper = maskLib.getMask("upper_body");
if (upper) {
    shootLayer.mask = *upper;
}

shootLayer.blendMode = LayerBlendMode::Override;
```

The difference seems small in isolation, but it means:
- Mask definitions are centralised in one JSON file, not scattered across setup functions
- Artists can adjust which bones belong to "upper_body" without touching C++ (they might want to include or exclude the spine root, for example)
- Multiple systems that need the same mask get identical results, guaranteed
- New masks (e.g., "left_arm_only" for a shield block animation) require only a JSON entry

---

## Step 4: Updated eventDispatchSystem

With the `AnimEventDispatcher` in place, the dispatch system becomes trivially simple:

### Before (Chapter 40)

```cpp
// event_dispatch_system.cpp — BEFORE
// 50+ lines of gameplay logic inside std::visit

void eventDispatchSystem(entt::registry& registry) {
    auto& eventQueue = registry.ctx().get<AnimationEventQueue>();

    for (const auto& fired : eventQueue.events) {
        entt::entity entity = fired.entity;
        if (!registry.valid(entity)) continue;

        std::visit([&](const auto& params) {
            using T = std::decay_t<decltype(params)>;

            if constexpr (std::is_same_v<T, SoundParams>) {
                glm::vec3 position = registry.get<Transform>(entity).position;
                AudioManager::play3D(params.soundName, position,
                                      params.volume, params.pitch);
            }
            else if constexpr (std::is_same_v<T, ParticleParams>) {
                glm::vec3 spawnPos;
                if (params.boneIndex >= 0) {
                    spawnPos = getBoneWorldPosition(registry, entity,
                                                     params.boneIndex);
                } else {
                    spawnPos = registry.get<Transform>(entity).position;
                }
                spawnPos += params.offset;
                auto emitter = registry.create();
                registry.emplace<Transform>(emitter, spawnPos);
            }
            else if constexpr (std::is_same_v<T, DamageWindowParams>) {
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
                handleCustomAnimEvent(registry, entity, params);
            }

        }, fired.event.params);
    }
}
```

### After (Chapter 40a)

```cpp
// In src/engine/ecs/systems/event_dispatch_system.cpp — AFTER

#include "engine/ecs/systems/event_dispatch_system.h"
#include "engine/ecs/components/animation_event_queue.h"
#include "engine/animation/anim_event_dispatcher.h"

void eventDispatchSystem(entt::registry& registry)
{
    auto& eventQueue = registry.ctx().get<AnimationEventQueue>();
    const auto& dispatcher = registry.ctx().get<AnimEventDispatcher>();

    for (const auto& fired : eventQueue.events)
    {
        entt::entity entity = fired.entity;

        // Skip if entity was destroyed between event collection and dispatch
        if (!registry.valid(entity)) continue;

        dispatcher.dispatch(registry, entity, fired.event.params);
    }
}
```

That is the entire function. Seven lines of logic. The gameplay-specific handling has moved to the registered handler functions, which we set up next.

---

## Step 5: Handler Registration and System Initialisation

All the gameplay logic that used to live inside `eventDispatchSystem` now lives in standalone handler functions, registered at startup. This is where the previous inline code moves to.

### Handler Definitions

```cpp
// In src/engine/animation/anim_event_handlers.h

#pragma once

#include "engine/animation/animation_event.h"
#include "engine/animation/animation_utils.h"
#include "engine/ecs/components/transform.h"
#include "engine/ecs/components/animator_config.h"
#include "engine/ecs/components/damage_window_active.h"
#include "engine/audio/audio_manager.h"
#include "engine/physics/raycast.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <cstdlib>

// ─── Sound handler ───────────────────────────────────────────────
// Plays a 3D positional sound at the entity's position.

inline void handleSoundEvent(entt::registry& registry, entt::entity entity,
                              const SoundParams& params)
{
    glm::vec3 position = registry.get<Transform>(entity).position;
    AudioManager::play3D(params.soundName, position,
                          params.volume, params.pitch);
}

// ─── Particle handler ────────────────────────────────────────────
// Spawns a particle emitter at a bone position (or entity origin).

inline void handleParticleEvent(entt::registry& registry, entt::entity entity,
                                 const ParticleParams& params)
{
    glm::vec3 spawnPos;
    if (params.boneIndex >= 0) {
        spawnPos = getBoneWorldPosition(registry, entity, params.boneIndex);
    } else {
        spawnPos = registry.get<Transform>(entity).position;
    }
    spawnPos += params.offset;

    auto emitter = registry.create();
    registry.emplace<Transform>(emitter, spawnPos);
    // registry.emplace<ParticleEmitter>(emitter, params.effectName);
}

// ─── Damage window handler ───────────────────────────────────────
// Adds or removes the DamageWindowActive component to control
// when melee attacks deal damage.

inline void handleDamageWindowEvent(entt::registry& registry, entt::entity entity,
                                     const DamageWindowParams& params)
{
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
        if (registry.all_of<DamageWindowActive>(entity)) {
            registry.remove<DamageWindowActive>(entity);
        }
    }
}

// ─── Custom: footstep with surface query ─────────────────────────

inline void handleFootstepSurfaceQuery(entt::registry& registry,
                                        entt::entity entity,
                                        const CustomParams& params)
{
    const auto& transform = registry.get<Transform>(entity);

    RayHit hit;
    glm::vec3 origin = transform.position + glm::vec3(0.0f, 0.1f, 0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);

    if (!raycast(registry, origin, direction, 2.0f, hit)) return;

    std::string soundName;
    switch (hit.surfaceMaterial) {
        case SurfaceMaterial::Stone:  soundName = "footstep_stone";  break;
        case SurfaceMaterial::Metal:  soundName = "footstep_metal";  break;
        case SurfaceMaterial::Dirt:   soundName = "footstep_dirt";   break;
        case SurfaceMaterial::Wood:   soundName = "footstep_wood";   break;
        case SurfaceMaterial::Water:  soundName = "footstep_splash"; break;
        default:                      soundName = "footstep_stone";  break;
    }

    float pitch = 0.9f + static_cast<float>(std::rand() % 20) / 100.0f;
    AudioManager::play3D(soundName, transform.position, 0.5f, pitch);
}

// ─── Custom: camera shake ────────────────────────────────────────

inline void handleCameraShake(entt::registry& registry,
                               entt::entity entity,
                               const CustomParams& params)
{
    // Apply screen shake with intensity from event data
    // (ScreenShake component from Chapter 20a)
    // auto& shake = registry.ctx().get<ScreenShake>();
    // shake.intensity = params.value;
}

// ─── Custom: spawn shell casing ──────────────────────────────────

inline void handleSpawnShellCasing(entt::registry& registry,
                                    entt::entity entity,
                                    const CustomParams& params)
{
    const auto& config = registry.get<AnimatorConfig>(entity);
    if (!config.skeleton) return;

    int ejectionBone = config.skeleton->findBone("EjectionPort");
    if (ejectionBone < 0) return;

    glm::vec3 spawnPos = getBoneWorldPosition(registry, entity, ejectionBone);

    auto casing = registry.create();
    registry.emplace<Transform>(casing, spawnPos);
    // registry.emplace<MeshRenderer>(casing, MeshRenderer{ "shell_casing" });
    // registry.emplace<RigidBody>(casing, ...);
    // registry.emplace<Lifetime>(casing, Lifetime{ 3.0f });
}
```

### Registration at Startup

```cpp
// In src/engine/animation/register_anim_handlers.h

#pragma once

#include "engine/animation/anim_event_dispatcher.h"
#include "engine/animation/anim_event_handlers.h"

// ─── registerAnimEventHandlers ───────────────────────────────────
// Call this once during engine initialisation. Registers all
// animation event handlers with the dispatcher. The dispatcher
// is stored in registry context so the dispatch system can find it.

inline void registerAnimEventHandlers(entt::registry& registry)
{
    auto& dispatcher = registry.ctx().emplace<AnimEventDispatcher>();

    // ─── Typed event handlers ────────────────────────────────
    dispatcher.registerSoundHandler(handleSoundEvent);
    dispatcher.registerParticleHandler(handleParticleEvent);
    dispatcher.registerDamageWindowHandler(handleDamageWindowEvent);

    // ─── Custom event handlers (keyed by tag) ────────────────
    dispatcher.registerCustomHandler("footstep_query_surface",
                                      handleFootstepSurfaceQuery);
    dispatcher.registerCustomHandler("camera_shake",
                                      handleCameraShake);
    dispatcher.registerCustomHandler("spawn_shell_casing",
                                      handleSpawnShellCasing);

    std::cout << "Animation event handlers registered." << std::endl;
}
```

### Updated setupScene

Here is how everything comes together during initialisation:

```cpp
// In setupScene() or engine initialisation — AFTER (Chapter 40a)

#include "engine/animation/register_anim_handlers.h"
#include "engine/animation/bone_mask_library.h"

void setupScene(entt::registry& registry, ResourceManager& resources)
{
    // ─── Existing setup (from previous chapters) ─────────────
    registry.ctx().emplace<AnimationEventQueue>();
    ResourceManager* resPtr = &resources;
    registry.ctx().emplace<ResourceManager*>(resPtr);

    // ─── NEW: Register animation event handlers ──────────────
    // This replaces the hardcoded dispatch logic. Game-specific
    // handlers are registered here. The dispatch system finds
    // the dispatcher in registry context.
    registerAnimEventHandlers(registry);

    // ─── NEW: Load bone mask presets ─────────────────────────
    // Masks are loaded once and shared by all systems that need
    // them. The skeleton must be loaded first.
    const Skeleton* skeleton = /* loaded from model */;

    auto& maskLib = registry.ctx().emplace<BoneMaskLibrary>();
    maskLib.load("assets/animation/bone_masks.json", *skeleton);

    // ─── Load animation data (from Chapter 36+) ─────────────
    // Animation clips with embedded events loaded from JSON/model files.
    // ...

    // ─── Create animated entity example ──────────────────────
    auto enemy = registry.create();
    registry.emplace<Transform>(enemy);

    // Use the convenience helper instead of manual component setup
    const AnimationClip* idleClip = /* loaded clip */;
    emplaceAnimator(registry, enemy, skeleton, idleClip);

    // ─── Set up animation layer with named mask ──────────────
    // (For entities using the layer system from Chapter 42)
    const BoneMask* upperMask = maskLib.getMask("upper_body");
    // Use upperMask when configuring overlay layers...
}
```

Compare this to the Chapter 40 setup, where `eventDispatchSystem` contained all the gameplay logic inline, bone masks were built with scattered `buildBoneMask()` calls, and the `Animator` component was emplaced by directly setting a dozen fields. The new version is shorter, more modular, and entirely data-driven where it matters.

---

## Step 6: Updated animationSystem

The animation system needs minor updates to work with the split components:

### Before (Chapter 40)

```cpp
void animationSystem(entt::registry& registry, float dt) {
    auto& eventQueue = registry.ctx().get<AnimationEventQueue>();
    eventQueue.events.clear();

    auto view = registry.view<Animator>();

    for (auto [entity, animator] : view.each()) {
        if (!animator.currentClip || !animator.skeleton) continue;

        animator.previousTime = animator.currentTime;

        float timeAdvance = dt * animator.speed
                          * animator.currentClip->ticksPerSecond;
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

        collectEvents(*animator.currentClip, animator.previousTime,
                       animator.currentTime, wrapped, entity,
                       eventQueue.events);

        // ... bone transform computation, blending ...
        animator.boneMatrices = std::move(currentTransforms);
    }
}
```

### After (Chapter 40a)

```cpp
// In src/engine/ecs/systems/animation_system.cpp — AFTER

#include "engine/ecs/systems/animation_system.h"
#include "engine/ecs/components/animator_state.h"
#include "engine/ecs/components/animator_config.h"
#include "engine/ecs/components/animation_event_queue.h"
#include "engine/animation/animation_utils.h"
#include "engine/animation/animation_event.h"

void animationSystem(entt::registry& registry, float dt)
{
    // Clear the event queue at the start of each frame
    auto& eventQueue = registry.ctx().get<AnimationEventQueue>();
    eventQueue.events.clear();

    // Iterate over entities that have both state and config
    auto view = registry.view<AnimatorState, AnimatorConfig>();

    for (auto [entity, state, config] : view.each())
    {
        if (!config.currentClip || !config.skeleton) continue;

        // ─── Save previous time (hot data) ───────────────────
        state.previousTime = state.currentTime;

        // ─── Advance animation time (hot data) ───────────────
        float timeAdvance = dt * state.speed
                          * config.currentClip->ticksPerSecond;
        state.currentTime += timeAdvance;

        bool wrapped = false;

        if (state.looping) {
            if (state.currentTime > config.currentClip->duration) {
                wrapped = true;
                state.currentTime = fmod(state.currentTime,
                                          config.currentClip->duration);
            }
        } else {
            if (state.currentTime > config.currentClip->duration) {
                state.currentTime = config.currentClip->duration;
            }
        }

        // ─── Collect animation events ────────────────────────
        collectEvents(
            *config.currentClip,
            state.previousTime,
            state.currentTime,
            wrapped,
            entity,
            eventQueue.events
        );

        // ─── Compute bone transforms ─────────────────────────
        std::vector<glm::mat4> currentTransforms = computeBoneTransforms(
            *config.skeleton,
            *config.currentClip,
            state.currentTime
        );

        // ─── Cross-fade blending ─────────────────────────────
        if (config.previousClip && state.blendTimer < state.blendTime)
        {
            state.blendTimer += dt;
            float blendFactor = glm::clamp(
                state.blendTimer / state.blendTime, 0.0f, 1.0f);

            state.previousClipTime += dt * state.speed
                * config.previousClip->ticksPerSecond;
            if (state.previousClipTime > config.previousClip->duration) {
                state.previousClipTime = fmod(state.previousClipTime,
                    config.previousClip->duration);
            }

            std::vector<glm::mat4> prevTransforms = computeBoneTransforms(
                *config.skeleton,
                *config.previousClip,
                state.previousClipTime
            );

            size_t boneCount = config.skeleton->bones.size();
            config.boneMatrices.resize(boneCount);
            for (size_t i = 0; i < boneCount; i++) {
                for (int col = 0; col < 4; col++) {
                    config.boneMatrices[i][col] = glm::mix(
                        prevTransforms[i][col],
                        currentTransforms[i][col],
                        blendFactor
                    );
                }
            }

            if (state.blendTimer >= state.blendTime) {
                config.previousClip = nullptr;
                state.blendTimer = 0.0f;
            }
        }
        else
        {
            config.boneMatrices = std::move(currentTransforms);
        }
    }
}
```

The logic is identical to Chapter 40. The only difference is accessing `state.currentTime` and `config.currentClip` instead of `animator.currentTime` and `animator.currentClip`. The split is mechanical and safe.

---

## C++ Concept Sidebar: The Observer Pattern and When Not to Use It

The `AnimEventDispatcher` uses a pattern that is close to, but deliberately simpler than, the classic **observer pattern**. In the observer pattern, multiple listeners can subscribe to the same event, and the dispatcher notifies all of them:

```cpp
// Classic observer pattern (NOT what we built)
class EventBus {
    std::unordered_map<EventType, std::vector<Listener*>> m_listeners;
public:
    void subscribe(EventType type, Listener* listener);
    void publish(EventType type, const EventData& data);
};
```

We chose a simpler approach: one handler per event type for typed events, one handler per tag for custom events. Why?

**1. One handler is usually enough.** A sound event needs exactly one handler: the one that plays the sound. A damage window event needs exactly one handler: the one that toggles the component. Multiple handlers for the same event type would mean multiple systems fighting over the same action. In ECS, this is usually a design smell -- it means the event is doing too much, or the systems are not properly separated.

**2. Multiple handlers create ordering problems.** If two listeners handle the same event, which runs first? The observer pattern does not define this. In a frame-sensitive game loop, undefined ordering leads to subtle bugs. With one handler per type, there is no ambiguity.

**3. `std::function` has overhead.** Each `std::function` is typically 32-48 bytes (it may heap-allocate for large captures). For our four typed handlers and a handful of custom handlers, this is negligible. But if we used `std::vector<std::function>` per event type with dozens of subscribers, the memory and indirection costs add up. Game engines that need high-throughput event systems often use function pointers or compile-time dispatch instead.

**4. The extension point is custom handlers, not multiple handlers.** When a new game feature needs to respond to animation events, the right approach is usually a new custom event tag with its own handler -- not a second handler on an existing type. The `registerCustomHandler("new_feature", myHandler)` call is the extension mechanism.

The observer pattern is powerful when you genuinely have multiple independent consumers for the same event (GUI systems, logging, analytics). For our animation event pipeline, the simpler "one handler per type" model is the right fit. Use the simplest pattern that solves your actual problem.

---

## Updated File Structure

After this chapter, your animation-related files look like this:

```
src/
  engine/
    animation/
      animation_event.h              <- UNCHANGED (from Ch 40)
      animation_clip.h               <- UNCHANGED (from Ch 33/40)
      animation_utils.h              <- UNCHANGED (from Ch 40)
      animation_loader.cpp           <- UNCHANGED (from Ch 40)
      bone_mask.h                    <- UNCHANGED (from Ch 42)
      bone_mask_library.h            <- NEW: named preset loading from JSON
      anim_event_dispatcher.h        <- NEW: handler registry + dispatch routing
      anim_event_handlers.h          <- NEW: standalone handler functions
      register_anim_handlers.h       <- NEW: one-call registration at startup
    ecs/
      components/
        animator.h                   <- MODIFIED: now includes convenience helpers,
                                                  delegates to split components
        animator_state.h             <- NEW: hot animation playback data
        animator_config.h            <- NEW: cold clip/skeleton/output data
        animation_event_queue.h      <- UNCHANGED (from Ch 40)
        damage_window_active.h       <- UNCHANGED (from Ch 40)
        transform.h                  <- UNCHANGED
      systems/
        animation_system.cpp         <- MODIFIED: uses AnimatorState + AnimatorConfig
        event_dispatch_system.cpp    <- MODIFIED: delegates to AnimEventDispatcher
        event_dispatch_system.h      <- UNCHANGED (same signature)
        melee_damage_system.cpp      <- MODIFIED: uses AnimatorConfig instead of Animator
  main.cpp                           <- MODIFIED: calls registerAnimEventHandlers(),
                                                  loads bone mask presets
assets/
  animation/
    bone_masks.json                  <- NEW: named bone mask presets
```

---

## Before vs After: Summary

| Aspect | Before (Chapter 40) | After (Chapter 40a) |
|---|---|---|
| **Event dispatch logic** | 50+ lines inside `std::visit` with gameplay code | 7-line loop delegating to `AnimEventDispatcher` |
| **Adding a sound handler** | Modify `eventDispatchSystem`, add `if constexpr` branch | Call `dispatcher.registerSoundHandler(fn)` at startup |
| **Adding a custom event** | Add `else if` in `handleCustomAnimEvent` | Call `dispatcher.registerCustomHandler("tag", fn)` |
| **Handler code location** | Inline in dispatch system | Standalone functions in `anim_event_handlers.h` |
| **Animator component** | 76-byte monolith mixing hot and cold data | 28-byte `AnimatorState` (hot) + 48-byte `AnimatorConfig` (cold) |
| **Creating an animated entity** | Manually set 10+ fields on `Animator` | `emplaceAnimator(registry, entity, skeleton, clip)` |
| **Switching animation clips** | Manually poke at blend fields | `switchAnimation(registry, entity, newClip, blendTime)` |
| **Bone mask "upper_body"** | `buildBoneMask(skeleton, "Spine")` scattered in code | `maskLib.getMask("upper_body")` from JSON |
| **Bone mask definitions** | Hardcoded bone names in C++ | JSON presets in `bone_masks.json` |
| **Adding a new bone mask** | New C++ code, recompile | New JSON entry, restart |

---

## Build and Test

Rebuild the project:

```bash
cmake --build build
```

Everything should behave identically to Chapter 40. Animation events still fire at the correct frames. Footstep sounds still play when feet contact the ground. Damage windows still open and close on melee attacks. The dispatch system still routes events correctly.

If something does not work:

1. **Check that `registerAnimEventHandlers()` runs before the game loop.** If the dispatcher is not in registry context, `eventDispatchSystem` will crash trying to `get<AnimEventDispatcher>()`. Make sure the registration call happens during initialisation.

2. **Check that both `AnimatorState` and `AnimatorConfig` are emplaced.** The `emplaceAnimator()` helper handles this, but if you have existing entity creation code that manually emplaces `Animator`, update it to emplace both split components. The `animationSystem` view requires both.

3. **Check the bone mask JSON.** If `BoneMaskLibrary::load()` fails, masks will be empty (all bits cleared), and animation layers using them will produce no visible output on those bones. Check the console for error messages about missing bone names.

4. **Check handler registration order.** Custom handlers must be registered before any animation events fire. If `registerAnimEventHandlers()` runs after the first `animationSystem()` call, the first frame's events will be dispatched to an empty handler map and silently dropped.

---

## What's Next

No new features. No new visual output. The game looks and feels exactly the same. Here is what changed underneath:

1. **Event dispatch is extensible without modifying the dispatch system.** The `AnimEventDispatcher` separates routing from handling. New event types and custom tags are added by registering handlers at startup. The dispatch system itself is stable -- it will not change again until the variant type changes.

2. **Animation components are split by access pattern.** `AnimatorState` holds the frame-rate-sensitive playback data that the animation system touches every frame. `AnimatorConfig` holds the clip pointers, skeleton reference, and output matrices. The split improves cache behaviour when iterating over many animated entities and makes the data layout explicit.

3. **Bone masks are data-driven.** The `BoneMaskLibrary` loads named presets from JSON and provides lookup by name. Animation layers reference masks by name instead of building them in code. Mask definitions are centralised, reusable, and editable without recompilation.

4. **Convenience helpers encapsulate common patterns.** `emplaceAnimator()` and `switchAnimation()` replace the scattered field-by-field setup code, reducing bugs and keeping the API surface small.

The pattern across all our cleanup chapters holds: identify hardcoded logic, extract it behind registries and lookup tables; identify monolithic data, split it by access pattern; identify scattered definitions, centralise them in data files. Chapters 5a through 40a all follow this discipline. Each pass, the codebase becomes more modular, more data-driven, and more ready for the features ahead.

---

*Next up: **Chapter 41 -- Ragdoll Physics**, where we convert the skeletal animation system into a physics simulation at the moment of death. Each bone becomes a rigid body connected by constraints, and the character crumbles under gravity. The split `AnimatorState`/`AnimatorConfig` components from this chapter will make the handoff from animation to physics clean -- we swap out `AnimatorState` for physics-driven bone updates while `AnimatorConfig` continues to provide the skeleton and output matrices the renderer needs.*
