# Chapter 20a: Polish Systems Cleanup

> **Prerequisites:** Chapter 20 (Particles, Effects & Polish) completed. You should have a working particle pool, screen shake, view bobbing, weapon recoil, and several interpolation utility functions.

---

## Time for Another Cleanup

You know the rhythm by now. Chapters 5a, 10a, and 15a each followed the same pattern: the features work, the code does not scale. Chapter 20 added a lot of visual polish, and it all feels great in the game. But the code that drives it has the usual post-feature smell -- global structs, standalone functions, hardcoded magic numbers, and scattered math utilities with no clear home.

Let us take inventory. Open your `main.cpp` and the files you touched in Chapter 20. You will find something like this:

```cpp
// Somewhere near the top of main() or at file scope
ScreenShake screenShake;
ViewBob viewBob;
WeaponRecoil recoil;

// In the update section
updateShake(screenShake, dt);
updateRecoil(recoil, dt);

// In the render section
glm::mat4 view = camera.getViewMatrix();
view = glm::translate(view, getShakeOffset(screenShake));
view = glm::translate(view, getViewBobOffset(viewBob, playerSpeed, dt));
```

And scattered across one or more files:

```cpp
// Standalone functions that operate on structs they don't own
void triggerShake(ScreenShake& shake, float intensity, float duration);
void updateShake(ScreenShake& shake, float dt);
glm::vec3 getShakeOffset(const ScreenShake& shake);
glm::vec3 getViewBobOffset(ViewBob& bob, float playerSpeed, float dt);
void applyRecoil(WeaponRecoil& recoil, float amount);
void updateRecoil(WeaponRecoil& recoil, float dt);

// Particle emitters full of magic numbers
void emitMuzzleFlash(ParticlePool& pool, const glm::vec3& position, const glm::vec3& direction);
void emitExplosion(ParticlePool& pool, const glm::vec3& position);
void emitWallSparks(ParticlePool& pool, const glm::vec3& position, const glm::vec3& normal);

// Math utilities floating at file scope
float lerp(float a, float b, float t);
float smoothstep(float edge0, float edge1, float x);
float easeIn(float t);
float easeOut(float t);
float spring(float t, float frequency, float damping);
```

Count the problems:

1. **`ScreenShake`, `ViewBob`, and `WeaponRecoil` are local variables, not ECS components.** Every other piece of game state lives in the registry. These three live as loose structs in `main()`. If any system wants to trigger a screen shake (an explosion, a damage event, a heavy door slamming), it needs a direct reference to that struct. We solved this exact problem for HUD state in Chapter 15a by making `DamageFlash`, `HUDMessages`, and `CrosshairStyle` into ECS components. The polish effects deserve the same treatment.

2. **Six standalone functions that take a struct reference as their first parameter.** `triggerShake(shake, ...)`, `updateShake(shake, ...)`, `getShakeOffset(shake)` -- every function takes a `ScreenShake&`. Same pattern for `ViewBob` and `WeaponRecoil`. Back in Chapter 5a, we identified this as a code smell: if every function needs the same data, that data and those functions want to be together. But in this case, the right answer is not necessarily a class -- it is ECS systems that read and write components.

3. **Particle emitters hardcode all their parameters.** Want to tweak the muzzle flash colour? Find it inside `emitMuzzleFlash()`. Want a green explosion for a poison rocket? Copy the entire `emitExplosion()` function and change the colours. Every new effect variant means a new function with duplicated structure. The parameters should be data, not code.

4. **Interpolation functions are scattered with no clear home.** `lerp`, `smoothstep`, `easeIn`, `easeOut`, and `spring` are used by particles, camera effects, animation, and UI. They are pure math with no dependencies. They deserve a dedicated header that any system can include without pulling in unrelated code.

5. **The render loop manually composes camera effects.** The view matrix construction reads `screenShake`, `viewBob`, and `recoil` in-line, mixing rendering concerns with game state. A camera effects system in `Phase::LateUpdate` should compose these before the render phase ever sees them.

Here is our plan:

| Problem | Solution |
|---|---|
| `ScreenShake`, `ViewBob`, `WeaponRecoil` as loose locals | ECS components on the player entity |
| Standalone update functions | `cameraEffectsUpdateSystem()` in Phase::GameLogic |
| Manual view matrix composition | `cameraEffectsApplySystem()` in Phase::LateUpdate |
| Hardcoded particle emitter functions | `ParticleEmitterDef` data struct + generic `emitParticles()` |
| Scattered interpolation functions | `engine/core/math_utils.h` header |

---

## C++ Concept: Data-Driven Design

This cleanup chapter leans heavily on a principle called **data-driven design** -- the idea that behaviour should be defined by data rather than by code.

Consider the muzzle flash emitter from Chapter 20:

```cpp
void emitMuzzleFlash(ParticlePool& pool, const glm::vec3& position,
                     const glm::vec3& direction) {
	Particle* flash = pool.allocate();
	if (flash) {
		flash->position = position + direction * 0.3f;
		flash->velocity = direction * 2.0f;
		flash->colorStart = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);   // magic number
		flash->colorEnd   = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f);   // magic number
		flash->sizeStart = 0.3f;                                    // magic number
		flash->sizeEnd = 0.1f;                                      // magic number
		flash->lifetime = 0.06f;                                    // magic number
		flash->age = 0.0f;
	}
	// ... more hardcoded sparks ...
}
```

Every number is baked into the code. To create a new effect, you write a new function. To tweak an existing effect, you edit code, recompile, and restart the game.

The data-driven alternative: define a struct that holds all the parameters an emitter needs, and write one generic function that reads the struct:

```cpp
// Data: can be defined at compile time, loaded from file, or built at runtime
constexpr ParticleEmitterDef MUZZLE_FLASH_DEF = { /* ... */ };

// Code: one function handles all emitter types
emitParticles(pool, MUZZLE_FLASH_DEF, position, direction);
```

Now creating a new effect means creating a new `ParticleEmitterDef` -- no new code, no recompilation if you load definitions from a file. Tweaking means changing a number in a data definition, not hunting through a function body. And the single `emitParticles()` function is the only place you need to test and debug.

This is the same principle behind `PhysicsConfig` from Chapter 10a and `CrosshairStyle` from Chapter 15a: put tuneable values in data, not in code.

---

## Step 1: MathUtils Header

Let us start with the simplest change. The five interpolation functions from Chapter 20 are pure math -- they have no dependencies, no state, no side effects. They belong in a utility header that anything in the engine can include.

### Why a header-only namespace?

These functions are small, stateless, and benefit from inlining. A header-only approach means zero additional compilation units and zero link-time cost. We use `inline` to satisfy the one-definition rule (ODR), since the header may be included from multiple translation units.

### engine/core/math_utils.h

```cpp
// engine/core/math_utils.h
#pragma once

#include <algorithm>
#include <cmath>

namespace MathUtils
{
	// ─── Linear interpolation ────────────────────────────────────
	// Constant speed from a to b. The workhorse of game math.
	// Note: GLM provides glm::mix() which does the same thing for
	// vec types. This version handles plain floats cleanly.

	inline float lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	// ─── Smoothstep ──────────────────────────────────────────────
	// Hermite interpolation — starts slow, speeds up, slows down.
	// Useful for smooth transitions: doors opening, fade effects,
	// camera transitions. Maps [edge0, edge1] to [0, 1].

	inline float smoothstep(float edge0, float edge1, float x)
	{
		float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// ─── Ease In (quadratic) ─────────────────────────────────────
	// Starts slow, ends fast. Used for things accelerating:
	// objects falling, energy building, charging effects.

	inline float easeIn(float t)
	{
		return t * t;
	}

	// ─── Ease Out (quadratic) ────────────────────────────────────
	// Starts fast, ends slow. Used for things decelerating:
	// objects coming to rest, explosions expanding, UI sliding in.

	inline float easeOut(float t)
	{
		return 1.0f - (1.0f - t) * (1.0f - t);
	}

	// ─── Spring (damped oscillation) ─────────────────────────────
	// Overshoots the target and oscillates back. Used for weapon
	// recoil, bouncy UI elements, anything with physical spring feel.
	// frequency: oscillations per unit time (higher = faster wobble)
	// damping: how quickly oscillation dies (higher = less bounce)

	inline float spring(float t, float frequency = 4.0f, float damping = 5.0f)
	{
		return 1.0f - std::exp(-damping * t)
			* std::cos(frequency * 3.14159265f * t);
	}

	// ─── Remap ───────────────────────────────────────────────────
	// Maps a value from one range to another.
	// remap(0.5, 0, 1, 10, 20) = 15

	inline float remap(float value,
	                    float inMin, float inMax,
	                    float outMin, float outMax)
	{
		float t = (value - inMin) / (inMax - inMin);
		return outMin + (outMax - outMin) * t;
	}

} // namespace MathUtils
```

### What changed

The five functions from Chapter 20 are now in one place with consistent documentation. We also added `remap()`, which is a common utility we will use later. Every function is `inline` in a namespace, so there is no `.cpp` file and nothing to add to `CMakeLists.txt`.

### Updating existing code

Find every callsite where you used the old standalone functions and switch to the namespaced versions:

```cpp
// Before
float t = smoothstep(0.0f, 1.0f, progress);
float val = lerp(startVal, endVal, t);

// After
float t = MathUtils::smoothstep(0.0f, 1.0f, progress);
float val = MathUtils::lerp(startVal, endVal, t);
```

Then delete the old standalone function definitions. If they were at file scope in a `.cpp` file, remove them entirely. If they were in a header, replace the header with an include of `math_utils.h`.

---

## Step 2: Camera Effect Components

In Chapter 15a, we moved `DamageFlash`, `HUDMessages`, and `CrosshairStyle` from loose locals to ECS components on the player entity. The three camera effect structs from Chapter 20 need the same treatment.

### Why components?

The argument is identical to Chapter 15a. When `ScreenShake` is a local variable in `main()`, triggering a shake from an explosion system requires threading a `ScreenShake&` through the call chain. When it is a component on the player entity, any system with registry access can trigger a shake:

```cpp
// Before: coupling
void onExplosion(ScreenShake& shake, const glm::vec3& pos, /* ... */) {
	triggerShake(shake, intensity, 0.3f);
}

// After: decoupled
void onExplosion(entt::registry& registry, entt::entity player, /* ... */) {
	auto& shake = registry.get<ScreenShake>(player);
	shake.trigger(intensity, 0.3f);
}
```

This is the Mediator pattern through the registry, exactly as we described in Chapter 15a.

### The Components

Add these to a new `polish_components.h` file, or add them to your existing components header alongside the HUD components from Chapter 15a:

```cpp
// engine/ecs/polish_components.h
#pragma once

#include <glm/glm.hpp>

// ─── ScreenShake ─────────────────────────────────────────────────
// Attached to the player entity. When triggered, applies a
// sine-wave offset to the camera view matrix.
//
// Systems that want to shake the camera write to this component.
// The camera effects system reads it in LateUpdate.

struct ScreenShake
{
	float intensity = 0.0f;
	float duration = 0.0f;
	float timer = 0.0f;
	float frequency = 25.0f;

	// Trigger a shake. Only overrides if the new shake is stronger
	// than the current one — prevents weak shakes from cancelling
	// strong ones mid-animation.
	void trigger(float newIntensity, float newDuration)
	{
		if (newIntensity > intensity)
		{
			intensity = newIntensity;
			duration = newDuration;
			timer = 0.0f;
		}
	}
};

// ─── ViewBob ─────────────────────────────────────────────────────
// Attached to the player entity. Simulates head movement while
// walking. The bob offset is computed from player speed and applied
// to the camera in LateUpdate.

struct ViewBob
{
	float bobTime = 0.0f;
	float bobAmountY = 0.03f;
	float bobAmountX = 0.015f;
	float bobSpeed = 10.0f;
};

// ─── WeaponRecoil ────────────────────────────────────────────────
// Attached to the player entity. When a weapon fires, the current
// pitch is kicked upward. It then recovers toward zero over time.
// Applied as a pitch offset on the camera.

struct WeaponRecoil
{
	float currentPitch = 0.0f;
	float targetPitch = 0.0f;
	float recovery = 10.0f;

	void kick(float amount)
	{
		targetPitch = amount;
	}
};

// ─── CameraEffectOffset ──────────────────────────────────────────
// Attached to the player entity. Accumulates the combined offset
// from all camera effects (shake, bob, recoil) each frame.
// The camera system reads this in LateUpdate to modify the view
// matrix. This separates "what offset to apply" from "how to
// apply it", keeping the render loop clean.

struct CameraEffectOffset
{
	glm::vec3 positionOffset = glm::vec3(0.0f);
	float pitchOffset = 0.0f;
};
```

### Why `ScreenShake::trigger()` has a method

Same rationale as `HUDMessages::add()` from Chapter 15a. The "only override if stronger" invariant is simple but easy to forget if every caller must implement it manually. Putting it in the struct prevents the repeated two-line pattern from being missed. We are not building a complex class -- just encapsulating a universal invariant.

### Why `CameraEffectOffset`?

This is new and worth explaining. In Chapter 20, the render loop manually composed the shake offset, bob offset, and recoil pitch into the view matrix:

```cpp
// Chapter 20: render loop knows about every effect
glm::mat4 view = camera.getViewMatrix();
view = glm::translate(view, getShakeOffset(screenShake));
view = glm::translate(view, getViewBobOffset(viewBob, playerSpeed, dt));
camera.addPitchOffset(recoil.currentPitch);
```

This means the render loop must know about every camera effect. Add a new effect (landing impact, water sway, concussion blur), and you add another line to the render loop.

With `CameraEffectOffset`, the update system computes the combined offset in `Phase::GameLogic`, and the apply system writes it to the camera in `Phase::LateUpdate`. The render loop sees none of it:

```cpp
// After: render loop is clean
glm::mat4 view = camera.getViewMatrix();
// CameraEffectOffset was already applied to the camera in LateUpdate
```

This is the same separation we enforced in Chapter 15a: update logic in GameLogic, application in LateUpdate, rendering in Render.

---

## Step 3: Camera Effects Systems

Now we write the systems that update and apply the camera effect components. These replace the standalone `updateShake()`, `getShakeOffset()`, `getViewBobOffset()`, and `updateRecoil()` functions.

### engine/ecs/systems/camera_effects_system.h

```cpp
// engine/ecs/systems/camera_effects_system.h
#pragma once

#include <entt/entt.hpp>

// ─── Camera Effects Update ───────────────────────────────────────
// Runs in Phase::GameLogic. Ticks shake/recoil timers and computes
// the combined camera offset into CameraEffectOffset.
//
// Replaces the standalone updateShake(), getShakeOffset(),
// getViewBobOffset(), and updateRecoil() functions from Chapter 20.

void cameraEffectsUpdateSystem(entt::registry& registry, float dt);

// ─── Camera Effects Apply ────────────────────────────────────────
// Runs in Phase::LateUpdate. Reads CameraEffectOffset and applies
// it to the camera's view state. This runs after all game logic
// has finalised the offset values.

void cameraEffectsApplySystem(entt::registry& registry);
```

### engine/ecs/systems/camera_effects_system.cpp

```cpp
// engine/ecs/systems/camera_effects_system.cpp
#include "engine/ecs/systems/camera_effects_system.h"
#include "engine/ecs/polish_components.h"
#include "engine/ecs/components.h"  // for Velocity, Camera, etc.
#include "engine/core/math_utils.h"

#include <glm/glm.hpp>
#include <cmath>

// ─── Camera Effects Update ───────────────────────────────────────
// Phase::GameLogic — tick effect timers, compute combined offset.

void cameraEffectsUpdateSystem(entt::registry& registry, float dt)
{
	auto view = registry.view<ScreenShake, ViewBob, WeaponRecoil,
	                          CameraEffectOffset>();

	for (auto [entity, shake, bob, recoil, offset] : view.each())
	{
		// Reset the combined offset each frame
		offset.positionOffset = glm::vec3(0.0f);
		offset.pitchOffset = 0.0f;

		// ─── Screen Shake ────────────────────────────────────
		if (shake.duration > 0.0f)
		{
			shake.timer += dt;

			if (shake.timer >= shake.duration)
			{
				// Shake finished
				shake.intensity = 0.0f;
				shake.duration = 0.0f;
				shake.timer = 0.0f;
			}
			else
			{
				// Decay intensity over time
				float progress = shake.timer / shake.duration;
				float currentIntensity = shake.intensity * (1.0f - progress);

				// Sine waves at different frequencies for organic feel
				float t = shake.timer * shake.frequency;
				float x = std::sin(t * 1.0f) * currentIntensity;
				float y = std::sin(t * 1.7f) * currentIntensity;
				float z = std::sin(t * 0.5f) * currentIntensity * 0.3f;

				offset.positionOffset += glm::vec3(x, y, z);
			}
		}

		// ─── View Bob ────────────────────────────────────────
		// We need the player's horizontal speed to drive the bob.
		// If the entity has a Velocity component, use its XZ magnitude.
		float playerSpeed = 0.0f;
		if (registry.all_of<Velocity>(entity))
		{
			auto& vel = registry.get<Velocity>(entity);
			playerSpeed = glm::length(glm::vec2(vel.value.x, vel.value.z));
		}

		if (playerSpeed < 0.5f)
		{
			// Not moving — decay bob smoothly
			bob.bobTime = MathUtils::lerp(bob.bobTime, 0.0f, dt * 5.0f);
		}
		else
		{
			bob.bobTime += dt * bob.bobSpeed;
		}

		float bobY = std::sin(bob.bobTime) * bob.bobAmountY;
		float bobX = std::sin(bob.bobTime * 0.5f) * bob.bobAmountX;
		offset.positionOffset += glm::vec3(bobX, bobY, 0.0f);

		// ─── Weapon Recoil ───────────────────────────────────
		// Snap toward target, then recover toward zero.
		recoil.currentPitch = MathUtils::lerp(
			recoil.currentPitch, recoil.targetPitch, dt * 20.0f);
		recoil.targetPitch = MathUtils::lerp(
			recoil.targetPitch, 0.0f, dt * recoil.recovery);

		offset.pitchOffset += recoil.currentPitch;
	}
}

// ─── Camera Effects Apply ────────────────────────────────────────
// Phase::LateUpdate — read the computed offset and apply to camera.

void cameraEffectsApplySystem(entt::registry& registry)
{
	auto view = registry.view<CameraEffectOffset>();

	for (auto [entity, offset] : view.each())
	{
		// Store the offset for the render system to read.
		// The render system will apply it to the view matrix.
		//
		// We do NOT modify the camera's actual position or rotation here.
		// The offset is temporary — it is recomputed every frame.
		// Storing it as a component means the render system can read it
		// without knowing about shakes, bobs, or recoil.
		//
		// If you have a Camera component on this entity:
		//   auto& cam = registry.get<Camera>(entity);
		//   cam.setEffectOffset(offset.positionOffset, offset.pitchOffset);
		//
		// Or the render system can read CameraEffectOffset directly.
		// Either approach works. Choose whichever fits your Camera class.
	}
}
```

### Updated Render Loop

The render section of your game loop simplifies considerably:

```cpp
// Before (Chapter 20):
glm::mat4 view = camera.getViewMatrix();
view = glm::translate(view, getShakeOffset(screenShake));
view = glm::translate(view, getViewBobOffset(viewBob, playerSpeed, dt));
camera.addPitchOffset(recoil.currentPitch);

// After (Chapter 20a):
// All effect offsets were computed in GameLogic and applied in LateUpdate.
// The render system reads the final camera state — it does not know about
// individual effects.
auto& offset = registry.get<CameraEffectOffset>(player);
glm::mat4 view = camera.getViewMatrix();
view = glm::translate(view, offset.positionOffset);
```

Better yet, if your `Camera` class absorbs the offset internally (via `setEffectOffset()` in `cameraEffectsApplySystem`), the render loop does not mention effects at all:

```cpp
glm::mat4 view = camera.getViewMatrix();  // includes effect offsets
```

---

## Step 4: ParticleEmitterDef

This is the data-driven particle refactoring. Instead of one function per effect, we define a struct that describes an emitter, and a single function that reads it.

### The Definition Struct

```cpp
// engine/particles/particle_emitter_def.h
#pragma once

#include <glm/glm.hpp>

// ─── ParticleEmitterDef ──────────────────────────────────────────
// A data-driven description of a particle effect. Contains all the
// parameters needed to spawn a burst of particles. No code, no
// behaviour — just numbers.
//
// Use with emitParticles() to spawn effects. Define presets as
// constexpr globals, or load them from a file at runtime.

struct ParticleEmitterDef
{
	// ─── Spawn count ─────────────────────────────────────────
	int count = 10;                  // Number of particles to spawn

	// ─── Speed ───────────────────────────────────────────────
	float speedMin = 2.0f;           // Minimum initial speed
	float speedMax = 6.0f;           // Maximum initial speed

	// ─── Colour ──────────────────────────────────────────────
	glm::vec4 colorStart = glm::vec4(1.0f);  // Colour at birth
	glm::vec4 colorEnd   = glm::vec4(0.0f);  // Colour at death

	// ─── Size ────────────────────────────────────────────────
	float sizeStart = 0.1f;          // Size at birth
	float sizeEnd   = 0.05f;         // Size at death

	// ─── Lifetime ────────────────────────────────────────────
	float lifetimeMin = 0.2f;        // Minimum lifetime in seconds
	float lifetimeMax = 0.5f;        // Maximum lifetime in seconds

	// ─── Spread ──────────────────────────────────────────────
	float spread = 0.3f;             // Random deviation from direction
	                                 // (0 = tight beam, 1 = wide cone)

	// ─── Physics ─────────────────────────────────────────────
	float gravity = 9.81f;           // Gravity applied to particles
	                                 // (0 = no gravity, useful for sparks)

	// ─── Offset ──────────────────────────────────────────────
	float spawnOffset = 0.0f;        // Offset along direction from origin
};
```

### C++ Concept: constexpr Aggregate Initialisation

Since `ParticleEmitterDef` is a plain aggregate (all public members, no user-declared constructors), we can initialise it with `constexpr` designated initialisers. This means the compiler can evaluate the entire definition at compile time -- zero runtime cost, and the data lives in the read-only segment of the executable.

```cpp
// These are compile-time constants, not runtime allocations
constexpr ParticleEmitterDef MUZZLE_FLASH_DEF = {
	.count = 6,
	.speedMin = 3.0f,
	.speedMax = 8.0f,
	// ... etc
};
```

Designated initialisers (the `.field = value` syntax) were standardised in C++20. They make aggregate initialisation self-documenting -- you can see which field each value corresponds to without counting commas. If your compiler does not support C++20 designated initialisers, you can use positional initialisation instead, but designated initialisers are strongly preferred for readability.

### Predefined Effect Definitions

```cpp
// engine/particles/particle_emitter_defs.h
#pragma once

#include "engine/particles/particle_emitter_def.h"

namespace ParticleDefs
{
	// ─── Muzzle Flash ────────────────────────────────────────
	// Brief, bright burst at the weapon barrel.
	// 1 large flash particle + 5 small sparks.

	constexpr ParticleEmitterDef MuzzleFlashCore {
		.count        = 1,
		.speedMin     = 1.5f,
		.speedMax     = 2.5f,
		.colorStart   = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f),
		.colorEnd     = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f),
		.sizeStart    = 0.3f,
		.sizeEnd      = 0.1f,
		.lifetimeMin  = 0.05f,
		.lifetimeMax  = 0.07f,
		.spread       = 0.05f,
		.gravity      = 0.0f,
		.spawnOffset  = 0.3f
	};

	constexpr ParticleEmitterDef MuzzleFlashSparks {
		.count        = 5,
		.speedMin     = 3.0f,
		.speedMax     = 8.0f,
		.colorStart   = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f),
		.colorEnd     = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f),
		.sizeStart    = 0.05f,
		.sizeEnd      = 0.02f,
		.lifetimeMin  = 0.1f,
		.lifetimeMax  = 0.2f,
		.spread       = 0.3f,
		.gravity      = 0.0f,
		.spawnOffset  = 0.2f
	};

	// ─── Explosion ───────────────────────────────────────────
	// Large central flash + outward debris + rising smoke.

	constexpr ParticleEmitterDef ExplosionCore {
		.count        = 1,
		.speedMin     = 0.0f,
		.speedMax     = 0.5f,
		.colorStart   = glm::vec4(1.0f, 0.9f, 0.7f, 1.0f),
		.colorEnd     = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f),
		.sizeStart    = 0.5f,
		.sizeEnd      = 3.0f,
		.lifetimeMin  = 0.25f,
		.lifetimeMax  = 0.35f,
		.spread       = 1.0f,
		.gravity      = 0.0f,
		.spawnOffset  = 0.0f
	};

	constexpr ParticleEmitterDef ExplosionDebris {
		.count        = 30,
		.speedMin     = 2.0f,
		.speedMax     = 10.0f,
		.colorStart   = glm::vec4(1.0f, 0.7f, 0.2f, 1.0f),
		.colorEnd     = glm::vec4(0.3f, 0.1f, 0.0f, 0.0f),
		.sizeStart    = 0.15f,
		.sizeEnd      = 0.05f,
		.lifetimeMin  = 0.3f,
		.lifetimeMax  = 0.7f,
		.spread       = 0.8f,
		.gravity      = 9.81f,
		.spawnOffset  = 0.0f
	};

	constexpr ParticleEmitterDef ExplosionSmoke {
		.count        = 10,
		.speedMin     = 1.0f,
		.speedMax     = 3.0f,
		.colorStart   = glm::vec4(0.3f, 0.3f, 0.3f, 0.6f),
		.colorEnd     = glm::vec4(0.1f, 0.1f, 0.1f, 0.0f),
		.sizeStart    = 0.5f,
		.sizeEnd      = 2.0f,
		.lifetimeMin  = 1.0f,
		.lifetimeMax  = 2.0f,
		.spread       = 0.6f,
		.gravity      = -1.0f,   // Negative: smoke rises
		.spawnOffset  = 0.0f
	};

	// ─── Wall Hit Sparks ─────────────────────────────────────
	// Directional sparks bouncing off a surface along its normal.

	constexpr ParticleEmitterDef WallSparks {
		.count        = 8,
		.speedMin     = 2.0f,
		.speedMax     = 6.0f,
		.colorStart   = glm::vec4(1.0f, 0.9f, 0.6f, 1.0f),
		.colorEnd     = glm::vec4(0.5f, 0.2f, 0.0f, 0.0f),
		.sizeStart    = 0.04f,
		.sizeEnd      = 0.01f,
		.lifetimeMin  = 0.15f,
		.lifetimeMax  = 0.4f,
		.spread       = 0.5f,
		.gravity      = 9.81f,
		.spawnOffset  = 0.05f
	};

} // namespace ParticleDefs
```

### The Generic Emitter Function

```cpp
// engine/particles/particle_emitter.h
#pragma once

#include "engine/particles/particle_emitter_def.h"

#include <glm/glm.hpp>
#include <random>

class ParticlePool;  // forward declaration

// ─── emitParticles ───────────────────────────────────────────────
// Spawn particles according to a definition. This is the single
// generic emitter function that replaces emitMuzzleFlash(),
// emitExplosion(), and emitWallSparks().
//
// position:  world-space origin of the effect
// direction: primary direction (normalised). Particles spread around
//            this axis. For omnidirectional effects (explosions),
//            pass any normalised vector — the high spread value
//            will override the directionality.

inline void emitParticles(ParticlePool& pool,
                          const ParticleEmitterDef& def,
                          const glm::vec3& position,
                          const glm::vec3& direction)
{
	// Thread-local RNG to avoid re-seeding every call.
	// This is safe because particle emission only happens
	// on the main thread.
	thread_local std::mt19937 rng(std::random_device{}());

	std::uniform_real_distribution<float> speedDist(def.speedMin, def.speedMax);
	std::uniform_real_distribution<float> lifeDist(def.lifetimeMin, def.lifetimeMax);
	std::uniform_real_distribution<float> spreadDist(-def.spread, def.spread);

	for (int i = 0; i < def.count; i++)
	{
		Particle* p = pool.allocate();
		if (!p) break;  // Pool exhausted

		// Build a randomised velocity around the primary direction
		glm::vec3 randomDir = direction
			+ glm::vec3(spreadDist(rng), spreadDist(rng), spreadDist(rng));

		// Normalise to prevent zero-length vectors (can happen with
		// high spread and unlucky random values)
		float len = glm::length(randomDir);
		if (len > 0.001f)
			randomDir /= len;
		else
			randomDir = direction;

		float speed = speedDist(rng);

		p->position   = position + direction * def.spawnOffset;
		p->velocity   = randomDir * speed;
		p->colorStart = def.colorStart;
		p->colorEnd   = def.colorEnd;
		p->sizeStart  = def.sizeStart;
		p->sizeEnd    = def.sizeEnd;
		p->lifetime   = lifeDist(rng);
		p->age        = 0.0f;
	}
}
```

### Before vs After: Emitter Calls

The effect trigger code becomes data-driven:

```cpp
// Before (Chapter 20): one function per effect, magic numbers inside
emitMuzzleFlash(particles, fireOrigin, fireDir);
emitExplosion(particles, explosionPos);
emitWallSparks(particles, hitPoint, hitNormal);

// After (Chapter 20a): one function, different data
emitParticles(particles, ParticleDefs::MuzzleFlashCore,  fireOrigin, fireDir);
emitParticles(particles, ParticleDefs::MuzzleFlashSparks, fireOrigin, fireDir);

emitParticles(particles, ParticleDefs::ExplosionCore,   explosionPos, glm::vec3(0, 1, 0));
emitParticles(particles, ParticleDefs::ExplosionDebris, explosionPos, glm::vec3(0, 1, 0));
emitParticles(particles, ParticleDefs::ExplosionSmoke,  explosionPos, glm::vec3(0, 1, 0));

emitParticles(particles, ParticleDefs::WallSparks, hitPoint, hitNormal);
```

The explosion is now three calls instead of one, but each call is simple and the parameters are visible. More importantly, creating a new effect (poison cloud, blood spray, teleport sparkle) means defining a new `ParticleEmitterDef` -- no new code to write or debug.

---

## Step 5: Putting It All Together

### Updated Setup Code

Here is how the polish systems initialise in `main()` after the cleanup:

```cpp
// ─── Attach polish components to the player entity ───────────
registry.emplace<ScreenShake>(player);
registry.emplace<ViewBob>(player);
registry.emplace<WeaponRecoil>(player);
registry.emplace<CameraEffectOffset>(player);
```

Compare this to the Chapter 20 setup:

```cpp
// Before: scattered locals in main()
ScreenShake screenShake;
ViewBob viewBob;
WeaponRecoil recoil;
```

The struct definitions are almost identical. What changed is *where they live* -- in the registry, not in `main()`.

### Updated Game Loop

Here is the relevant part of the game loop with the cleanup changes integrated:

```cpp
while (!window.shouldClose())
{
	fixedTimestep.accumulate();

	// -- Phase: Input --
	input.update();
	window.pollEvents();
	inputSystem(registry);

	// -- Phase: Physics (fixed timestep) --
	while (fixedTimestep.step())
	{
		gravitySystem(registry);
		movementSystem(registry);
		collisionSystem(registry);
		jumpSystem(registry);
	}

	// -- Phase: GameLogic --
	float dt = fixedTimestep.getTimestep();  // or your frame delta
	hudUpdateSystem(registry, dt);
	cameraEffectsUpdateSystem(registry, dt);    // NEW: tick shake/bob/recoil

	// -- Phase: LateUpdate --
	cameraEffectsApplySystem(registry);         // NEW: write offset to camera
	cameraFollowSystem(registry);

	// -- Phase: Render --
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	float aspectRatio = static_cast<float>(window.getWidth())
		/ static_cast<float>(window.getHeight());

	// View matrix now includes camera effect offsets automatically
	auto& offset = registry.get<CameraEffectOffset>(player);
	glm::mat4 view = camera.getViewMatrix();
	view = glm::translate(view, offset.positionOffset);

	renderSystem(registry, view, projection);
	renderParticles(particlePool, particleShader, quadVAO,
	                camera, aspectRatio);

	hudRenderer.render(registry,
		static_cast<float>(window.getWidth()),
		static_cast<float>(window.getHeight()));

	window.swapBuffers();
}
```

The camera effect lines in the render section are now just a component read and a translate, rather than three separate function calls with different signatures. And the update logic lives where it belongs -- in `Phase::GameLogic`, not mixed into the render section.

### Triggering Effects from Game Systems

With the polish effects as components, triggering them from any system is clean:

```cpp
// Weapon fired:
auto& shake = registry.get<ScreenShake>(player);
shake.trigger(0.05f, 0.1f);

auto& recoil = registry.get<WeaponRecoil>(player);
recoil.kick(3.0f);

emitParticles(particles, ParticleDefs::MuzzleFlashCore,  fireOrigin, fireDir);
emitParticles(particles, ParticleDefs::MuzzleFlashSparks, fireOrigin, fireDir);

// Rocket exploded:
float dist = glm::length(playerPos - explosionPos);
float strength = std::max(0.0f, 1.0f - dist / 20.0f);
shake.trigger(strength * 0.5f, 0.3f);

emitParticles(particles, ParticleDefs::ExplosionCore,   explosionPos, glm::vec3(0, 1, 0));
emitParticles(particles, ParticleDefs::ExplosionDebris, explosionPos, glm::vec3(0, 1, 0));
emitParticles(particles, ParticleDefs::ExplosionSmoke,  explosionPos, glm::vec3(0, 1, 0));

// Player took damage:
shake.trigger(0.2f, 0.15f);
auto& flash = registry.get<DamageFlash>(player);
flash.timer = flash.duration;
```

None of these lines require a reference to `screenShake`, `viewBob`, or `recoil` as local variables. They all go through the registry.

---

## Updated File Structure

After this chapter, your project tree has these new and modified files:

```
src/
  engine/
    core/
      math_utils.h               <- NEW: lerp, smoothstep, easeIn, easeOut, spring, remap
      fixed_timestep.h           <- UNCHANGED
      mesh_factory.h             <- UNCHANGED
      resource_manager.h         <- UNCHANGED
    ecs/
      polish_components.h        <- NEW: ScreenShake, ViewBob, WeaponRecoil, CameraEffectOffset
      hud_components.h           <- UNCHANGED (from 15a)
      systems/
        camera_effects_system.h  <- NEW: cameraEffectsUpdateSystem, cameraEffectsApplySystem
        camera_effects_system.cpp <- NEW: implementation
        hud_system.h             <- UNCHANGED (from 15a)
    particles/
      particle_emitter_def.h     <- NEW: ParticleEmitterDef struct
      particle_emitter_defs.h    <- NEW: predefined effect definitions (namespace ParticleDefs)
      particle_emitter.h         <- NEW: generic emitParticles() function
      particle_pool.h            <- UNCHANGED (from Chapter 20)
  main.cpp                       <- MODIFIED: uses components, systems, ParticleDefs
```

Add the new `.cpp` file to your `CMakeLists.txt`:

```cmake
add_executable(QEngine
	# ... existing files ...
	src/engine/ecs/systems/camera_effects_system.cpp
)
```

Note that `math_utils.h`, `polish_components.h`, `particle_emitter_def.h`, `particle_emitter_defs.h`, and `particle_emitter.h` are all header-only. Only `camera_effects_system.cpp` is a new compilation unit.

---

## Build and Test

Rebuild the project:

```bash
cmake --build build
```

You should see the exact same game feel as before: screen shake on explosions, view bob while walking, weapon recoil on fire, particles spawning with the correct colours and behaviours. The behaviour is identical. The architecture is fundamentally better.

If something does not work:

1. **Check that components are attached.** If the player entity does not have `ScreenShake`, `ViewBob`, `WeaponRecoil`, and `CameraEffectOffset`, the camera effects system will not find anything to update. The system iterates over entities with all four components.

2. **Check the system order.** `cameraEffectsUpdateSystem()` must run in GameLogic (before LateUpdate). `cameraEffectsApplySystem()` must run in LateUpdate (before Render). If the apply runs before the update, you get last frame's offsets.

3. **Check that old standalone functions are removed.** If you still have `updateShake()`, `getShakeOffset()`, etc. defined somewhere, the linker will not complain (they are different functions), but you might accidentally call the old versions instead of letting the system handle it.

4. **Check the particle defs.** If particles look wrong, compare your `ParticleDefs` values against the hardcoded numbers in the old `emitMuzzleFlash()`, `emitExplosion()`, and `emitWallSparks()` functions. The numbers should match exactly.

5. **Check `MathUtils` includes.** If the camera effects system fails to compile, make sure `engine/core/math_utils.h` is on the include path. The `MathUtils::lerp()` calls replace direct `lerp()` calls.

---

## Before vs After: Summary

| Aspect | Before (Chapter 20) | After (Chapter 20a) |
|---|---|---|
| **Screen shake state** | Local `ScreenShake` in `main()` | Component on player entity |
| **View bob state** | Local `ViewBob` in `main()` | Component on player entity |
| **Weapon recoil state** | Local `WeaponRecoil` in `main()` | Component on player entity |
| **Effect update logic** | Standalone functions called in render section | `cameraEffectsUpdateSystem()` in Phase::GameLogic |
| **View matrix composition** | Three manual calls in render loop | `CameraEffectOffset` component read once |
| **Triggering a shake** | Needs `ScreenShake&` reference | Any system reads from registry |
| **Muzzle flash** | `emitMuzzleFlash()` with hardcoded colours | `emitParticles(pool, ParticleDefs::MuzzleFlashCore, ...)` |
| **Explosion** | `emitExplosion()` with hardcoded colours | Three `emitParticles()` calls with named defs |
| **Wall sparks** | `emitWallSparks()` with hardcoded colours | `emitParticles(pool, ParticleDefs::WallSparks, ...)` |
| **Creating a new effect** | Write a new function, hardcode numbers | Define a new `ParticleEmitterDef` |
| **Interpolation functions** | Scattered at file scope | `engine/core/math_utils.h` namespace |
| **New .cpp files** | 0 | 1 (`camera_effects_system.cpp`) |
| **New headers** | 0 | 6 |

---

## What We Accomplished

No new features. No new visual output. The game looks and feels exactly the same. Here is what changed underneath:

1. **Camera effects are ECS components.** `ScreenShake`, `ViewBob`, `WeaponRecoil`, and `CameraEffectOffset` live on the player entity. Any system can trigger a shake or recoil kick through the registry, without needing a direct reference to a local variable. This follows the same pattern we established for `DamageFlash` and `HUDMessages` in Chapter 15a.

2. **Update logic is in the right phase.** The camera effects update system runs in `Phase::GameLogic`, where all state mutations belong. The apply system runs in `Phase::LateUpdate`, reading the final computed values. The render loop only reads a single component. This matches the phase ordering from Chapter 10a.

3. **Particle emitters are data-driven.** The `ParticleEmitterDef` struct describes an effect with numbers. The `emitParticles()` function reads the struct and spawns particles. Creating a new effect means defining a new struct, not writing new code. Tweaking an effect means changing numbers, not debugging logic.

4. **Math utilities have a home.** `MathUtils::lerp`, `smoothstep`, `easeIn`, `easeOut`, `spring`, and `remap` live in a single header with consistent documentation. Any system in the engine can include it without pulling in unrelated dependencies.

5. **The render loop is shorter.** Three manual function calls with different signatures became one component read and one `glm::translate`. The loop reads as intent, not implementation.

The pattern across all our cleanup chapters is consistent: identify loose state, make it a component; identify scattered functions, consolidate them into systems or utility headers; identify magic numbers, make them data. Chapters 5a, 10a, 15a, and now 20a all follow this same discipline. Each time, the code gets shorter and the architecture gets more uniform.

---

## Exercises

1. **Landing impact shake.** Add a system that triggers a screen shake when the player lands after falling. Detect the landing by checking when the player transitions from airborne to grounded (the `OnGround` component from Chapter 10). Scale the shake intensity by fall speed. This exercises the component-based shake trigger pattern.

2. **Custom particle effect.** Define a new `ParticleEmitterDef` for a teleport effect: blue-white sparkles that spiral upward with negative gravity and a wide spread. Spawn it when the player presses a key. This exercises the data-driven emitter pattern -- you should not need to write any new functions, only a new definition.

3. **Smooth bob decay.** The current bob implementation snaps `bobTime` toward zero when the player stops moving. Replace this with an `easeOut` curve from `MathUtils` so the bob decays smoothly over about half a second. Compare the feel with the abrupt snap. This exercises the math utilities.

4. **Recoil pattern per weapon.** Create a `RecoilPattern` struct with `kickAmount`, `recoverySpeed`, and `frequency` fields. Store it in the `WeaponRecoil` component or alongside it. When the player fires different weapons, apply the appropriate pattern. This extends the data-driven philosophy from particles to recoil.

5. **Runtime particle tweaking.** Store a `ParticleEmitterDef` as a registry context variable (like `PhysicsConfig` from Chapter 10a). Build a debug overlay (using the `HUDRenderer` from Chapter 15a) that displays the current values and lets you modify them with keyboard shortcuts. Fire the effect to see changes live. This combines three cleanup patterns into one exercise.

---

*Next up: **Chapter 21 -- Game State Machine**, where we will build a state system for menu, gameplay, and pause screens. The component-based approach we have been refining across these cleanup chapters will make state transitions clean -- each state just attaches and detaches the components it needs.*
