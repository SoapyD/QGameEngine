# Chapter 17b: Types & The Combat Split

Previously, in Chapter 17a, we wrote the coding standard and built the convention checker. Now we apply it.

## What You'll Learn
- The `types/` folder rule: where incidental param/return structs and standalone enums live, and why ECS components do not move
- Extracting `EntityHit`, `MeshAssets`, `SystemPhase`, `AABB`, `Ray`/`RayHit` into `types/` leaves
- Why a *collection of free functions* is the cleanest possible target for the "one thing per file" rule
- The internal-header pattern: a private `combat_internal.h` shared only between sibling `.cpp` files
- Splitting the 524-line `combat_system.cpp` god-file into eight one-function files plus a thin orchestrator
- Why consumers (`simulation.cpp`) stay untouched when the public header is unchanged
- Wiring the new `combat/*.cpp` source list into `CMakeLists.txt`

---

## Step 1: Types Live in `types/`

### Why a `types/` folder at all?

The coding standard from Chapter 17a (CODING_STANDARD §2) draws a line between two kinds of data:

1. **ECS *components*** — the data the registry stores on entities. These are first-class citizens of the engine. They stay in `components/` and are never demoted.
2. **Incidental types** — param/return structs that exist only to pass a tuple of values between functions, plus standalone enums that aren't part of any component's contract. These clutter implementation files. They move to a `types/` folder near their domain.

The rule, precisely:

- **Incidental param/return structs** (e.g. `EntityHit`, the result of a raycast) → a `types/` leaf header near their domain.
- **Standalone enums** (e.g. `SystemPhase`, which exists for documentation, not runtime dispatch) → a `types/` leaf.
- **ECS components stay in `components/`.** A component is not a "type" in this sense — it is a unit of entity state.
- **Enums that are part of a component contract stay with the component.** `WeaponType` and `FireMode` belong to `components/combat.h`; `MoverState` and `TriggerAction` belong to `components/gameplay.h`. Moving them would split a component from its own vocabulary.

The migration audit (plan 03) inventoried every loose type by `grep` and assigned each a verdict. We work the incidental ones here.

### Why move one type at a time, leaving a re-include behind?

Types are *passive* — moving one carries near-zero behavioural risk. The real hazard is **include breakage**: a relocated struct changes the path every consumer must use. The safe migration is: cut the declaration into its new `types/` leaf, leave the original file `#include`-ing the new leaf so existing consumers compile unchanged, then update direct consumers to the leaf and build after each move. The headless suite (six scenarios) catches any wiring slip.

Note two of these are *relocations* of files that were already type-only headers: `system_phase.h` moved out of `systems/` (a standalone enum has no business living among the systems), and `aabb.h` moved out of `physics/` directly into `physics/types/`. The raycast *functions* stay in `physics/raycast.h` — only the `Ray`/`RayHit` value types move.

### `ecs/types/entity_hit.h`

The return type of an entity raycast. It was previously declared inside `combat_system.cpp` — exactly the incidental struct the rule targets.

```cpp
#pragma once
// Result of an entity raycast — closest entity hit and where. (Relocated from
// combat_system.cpp per CODING_STANDARD §2.)

#include <entt/entt.hpp>
#include <glm/glm.hpp>

struct EntityHit
{
	entt::entity entity;
	float distance;
	glm::vec3 point;
};
```

### `ecs/types/mesh_assets.h`

Shared render handles for the showcase's factory entities. Relocated out of `level/factories.h`.

```cpp
#pragma once
// Shared render handles for the showcase's cube-based factory entities.
// (Relocated from level/factories.h per CODING_STANDARD §2.)

namespace factories
{
    struct MeshAssets
    {
        unsigned int cubeVAO = 0;
        unsigned int cubeIndexCount = 0;
        unsigned int litShader = 0;
    };
}
```

### `ecs/types/system_phase.h`

A standalone enum that exists purely for documentation (and a possible future scheduler) — not for runtime dispatch. It moved out of `systems/` into `ecs/types/`.

```cpp
#pragma once

// Defines the conceptual phases of the game loop.
// Systems should run in this order. This enum exists for documentation
// and future use (e.g. a scheduler), not for runtime dispatch.
//
// Phase order:
//   1. Input       - Poll events, read input state.
//                    Must run before anything reads input.
//
//   2. Physics     - Fixed timestep. Gravity, friction, collision detection
//                    and response, movement, ground detection.
//                    Runs 0-N times per frame inside the accumulator loop.
//
//   3. GameLogic   - Gameplay rules that respond to physics results.
//                    Health, scoring, state machines, AI decisions.
//                    Runs once per frame, after all physics steps.
//
//   4. LateUpdate  - Post-logic cleanup. Camera follow, animation blending,
//                    transform hierarchy propagation.
//
//   5. Render      - Read positions, submit draw calls. Must be last.

enum class SystemPhase
{
	Input,
	Physics,
	GameLogic,
	LateUpdate,
	Render
};
```

### `physics/types/aabb.h`

Already a type-only header, but it lived directly in `physics/`. It moves under `physics/types/` for consistency. It is a value type used by combat hit tests and trigger overlap.

```cpp
#pragma once
// AABB (Axis-Aligned Bounding Box) — value type used by combat hit tests and
// trigger overlap. (Relocated from physics/aabb.h per CODING_STANDARD §2.)

#include <glm/glm.hpp>
#include <optional>

struct AABB {
	glm::vec3 min;
	glm::vec3 max;

	// create from center and half-extends
	static AABB fromCentreSize
	(
		const glm::vec3& center,
		const glm::vec3& halfExtends
	)
	{
		return
		{
			center - halfExtends,
			center + halfExtends
		};
	};

	glm::vec3 center() const { return (min + max) * 0.5f; }
	glm::vec3 size() const { return max - min; }
	glm::vec3 halfExtends() const { return size() * 0.5f; }

	// does this AABB contain a point?
	bool contains(const glm::vec3& point) const
	{
		return
		point.x >= min.x && point.x <= max.x &&
		point.y >= min.y && point.y <= max.y &&
		point.z >= min.z && point.z <= max.z;
	}

	// do two AABBs overlap?
	bool intersects(const AABB& other) const
	{
		return
		(min.x <= other.max.x && max.x >= other.min.x) &&
		(min.y <= other.max.y && max.y >= other.min.y) &&
		(min.z <= other.max.z && max.z >= other.min.z);
	}

	// epand this AABB to include a point
	void encapsulate(const glm::vec3& point)
	{
		min = glm::min(min, point);
		max = glm::max(max, point);
	}

	// translate (move) the AABB
	AABB translated(const glm::vec3& offset) const
	{
		return { min + offset, max + offset};
	}
};
```

### `physics/types/ray.h`

The `Ray` and `RayHit` value types, split out of `physics/raycast.h`. The raycast *functions* (`rayIntersectionsAABB`, etc.) stay in `physics/raycast.h` — only the passive data moves.

```cpp
#pragma once
// Ray + RayHit — value types for raycasting. (Relocated from physics/raycast.h
// per CODING_STANDARD §2; the raycast *functions* stay in physics/raycast.h.)

#include <glm/glm.hpp>
#include <entt/entt.hpp>

struct Ray
{
	glm::vec3 origin;
	glm::vec3 direction; //should be normalised

	glm::vec3 pointAt(float t) const
	{
		return origin + direction * t;
	}
};

struct RayHit
{
	float distance; // how far along the ray
	glm::vec3 point; // world-space hit point
	glm::vec3 normal; // surface normal at hit point
	entt::entity entity; // what was hit
};
```

With the type layout stable, the splits land against fixed include paths.

---

## Step 2: The Combat Split

### Why combat is the perfect first split

`combat_system.cpp` was a 524-line file, and — crucially — it was **not a class**. It was a *collection of free functions*: `combatSystem`, `fireHitscan`, `fireProjectile`, `raycastEntities`, `spawnTracer`, `applySplashDamage`, `applySpread`, `boxHitsLevel`, `updateProjectiles`. There was no shared mutable state, no `this`, no constructor — just functions calling functions in the same translation unit.

That is exactly the shape the "one public thing per file" rule was written for. When a file is one big class, splitting means inventing private helper headers and worrying about state. When a file is a bag of free functions, each function can simply move to its own `.cpp` with no behavioural change at all. The only thing the functions shared was their forward declarations, and those become a single private header.

This is also the most-tested path in the engine (the `rocket_vs_floor` and hitscan scenarios), so we do it as one focused step and verify immediately.

### Target layout

```
ecs/systems/combat/
├── combat_internal.h     ← private shared declarations (the helpers)
├── combat_system.h       ← public API (unchanged signature)
├── combat_system.cpp     ← thin orchestrator (combatSystem only)
├── apply_spread.cpp      ← applySpread
├── raycast_entities.cpp  ← raycastEntities
├── spawn_tracer.cpp      ← spawnTracer
├── splash_damage.cpp     ← applySplashDamage
├── fire_hitscan.cpp      ← fireHitscan
├── fire_projectile.cpp   ← fireProjectile
├── box_hits_level.cpp    ← boxHitsLevel
└── update_projectiles.cpp← updateProjectiles
```

One public function per file. The orchestrator keeps only `combatSystem` — the entry point — and delegates everything else to its siblings.

### Why an internal header?

The eight helpers call each other: `fireHitscan` calls `applySpread`, `raycastEntities`, and `spawnTracer`; `updateProjectiles` calls `boxHitsLevel` and `applySplashDamage`. Once they live in separate translation units, each caller needs the others' declarations.

We do **not** put those declarations in the public `combat_system.h` — consumers like `simulation.cpp` have no business seeing `applySpread`. Instead we create a *private* header, `combat_internal.h`, that lives inside the `combat/` folder and is included only by the split `.cpp` files. Its header comment states the contract explicitly: "NOT the public API — that is combat_system.h." This is the internal-header pattern: one private header carries the shared vocabulary of a folder, the public header carries only the entry point.

### `combat/combat_internal.h`

```cpp
#pragma once
// Internal helpers shared across the combat system's split .cpp files. NOT the
// public API — that is combat_system.h. Each helper is defined in its own file.

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <optional>

#include "engine/ecs/components/combat.h"   // Weapon, CombatResources
#include "engine/ecs/types/entity_hit.h"
#include "engine/level/level.h"
#include "engine/physics/types/aabb.h"
#include "engine/physics/types/ray.h"

// Perturb a fire direction by a random cone (shotgun spread).
glm::vec3 applySpread(const glm::vec3& direction, float spread);

// Closest entity hit by a ray (ignoring `ignore`, within `maxRange`).
std::optional<EntityHit> raycastEntities(entt::registry& registry, const Ray& ray,
                                         entt::entity ignore, float maxRange);

// Spawn a short-lived wireframe tracer between start and end.
void spawnTracer(entt::registry& registry, const glm::vec3& start, const glm::vec3& end,
                 const CombatResources& resources);

// Radial damage + knockback around a point (rockets/grenades).
void applySplashDamage(entt::registry& registry, const glm::vec3 center, float radius,
                       float maxDamage, entt::entity ignore);

// Fire a hitscan weapon (raycast vs entities + level, damage, tracer).
void fireHitscan(entt::registry& registry, const Level& level, entt::entity shooter,
                 const Weapon& weapon, const glm::vec3& origin, const glm::vec3& direction,
                 const CombatResources& resources);

// Spawn a moving projectile entity.
void fireProjectile(entt::registry& registry, entt::entity shooter, const Weapon& weapon,
                    const glm::vec3& origin, const glm::vec3& direction,
                    const CombatResources& resources);

// True if an axis-aligned box overlaps any level surface (walls/floors aren't
// ECS entities, so projectile/hitscan code must test them explicitly).
bool boxHitsLevel(const Level& level, const AABB& box);

// Advance projectiles, resolve level + entity collisions, destroy on impact.
void updateProjectiles(entt::registry& registry, const Level& level, float dt);
```

Notice the includes pull in the `types/` leaves we built in Step 1: `entity_hit.h`, `physics/types/aabb.h`, `physics/types/ray.h`. The split could not have landed cleanly until those types had stable homes — this is why plan 03 ran first.

### `apply_spread.cpp`

The simplest leaf: perturb a direction by a random cone. It owns the file-local RNG.

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

#include <random>

// random number generator for spread
static std::mt19937 rng(std::random_device{}());

// apply spread to a direction vector
glm::vec3 applySpread(const glm::vec3& direction, float spread)
{
	if (spread <= 0.0f) return direction;

	std::uniform_real_distribution<float> dist(-spread, spread);
	glm::vec3 spread_dir = direction;
	spread_dir.x += dist(rng);
	spread_dir.y += dist(rng);
	spread_dir.z += dist(rng);
	return glm::normalize(spread_dir);
}
```

### `raycast_entities.cpp`

Finds the closest entity hit by a ray, ignoring the shooter and triggers.

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"
#include "engine/physics/types/aabb.h"

// find the closest entity hit by a ray
std::optional<EntityHit> raycastEntities
(
	entt::registry& registry,
	const Ray& ray,
	entt::entity ignore,
	float maxRange
)
{
	std::optional<EntityHit> closest;
	float closestDist = maxRange;

	auto view = registry.view<Position, AABBCollider>();
	for (auto [entity, pos, col] : view.each())
	{
		if (entity == ignore) continue;
		if (col.isTrigger) continue;

		AABB box = AABB::fromCentreSize(pos.value, col.halfExtents);
		auto hit = rayIntersectionsAABB(ray, box);

		if (hit.has_value() && hit.value() < closestDist)
		{
			closestDist = hit.value();
			closest = EntityHit
			{
				entity,
				hit.value(),
				ray.pointAt(hit.value())
			};
		}
	}

	return closest;
}
```

### `spawn_tracer.cpp`

Spawns a thin wireframe cube stretched and rotated between two points — the debug visualisation of a hitscan shot.

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"

#include <cmath>

// ─── Hitscan tracer (debug visualisation) ────────────────────────
// Spawns a thin wireframe cube stretched between start and end,
// rotated to align with the fire direction.
void spawnTracer
(
	entt::registry& registry,
	const glm::vec3& start,
	const glm::vec3& end,
	const CombatResources& resources
)
{
	glm::vec3 diff = end - start;
	float length = glm::length(diff);
	if (length < 0.01f) return;

	glm::vec3 dir = diff / length;
	glm::vec3 midpoint = (start + end) * 0.5f;

	// Calculate Euler angles to align the cube's Z axis with the ray direction
	// Yaw: rotation around Y axis (horizontal angle)
	float yaw = glm::degrees(std::atan2(dir.x, dir.z));
	// Pitch: rotation around X axis (vertical angle)
	float pitch = glm::degrees(-std::asin(dir.y));

	auto tracer = registry.create();
	registry.emplace<Position>(tracer, midpoint);
	registry.emplace<Rotation>(tracer, glm::vec3(pitch, yaw, 0.0f));
	registry.emplace<Scale>(tracer, glm::vec3(0.03f, 0.03f, length));
	registry.emplace<MeshRenderer>
	(
		tracer,
		resources.cubeVAO,
		0u,
		resources.shaderId,
		resources.tracerTextureId,
		true,
		resources.cubeIndexCount
	);
	registry.emplace<TagDebugWireframe>(tracer);
	registry.emplace<Lifetime>(tracer, 2.0f);  // Hang in the air for 2 seconds
}
```

### `splash_damage.cpp`

Radial damage and knockback around an explosion centre. Hurts entities with `Health`, pushes anything with `Velocity`, and shoves Jolt rigid bodies (even those without `Health`).

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"

// ─── Splash damage ──────────────────────────────────────────────
void applySplashDamage
(
	entt::registry& registry,
	const glm::vec3 center,
	float radius,
	float maxDamage,
	entt::entity ignore
)
{
	auto view = registry.view<Position, Health>();

	for (auto [entity, pos, health] : view.each())
	{
		if (entity == ignore) continue;

		float distance = glm::length(pos.value - center);
		if (distance > radius) continue;

		// Linear falloff: full damage at center, zero at edge
		float scale = 1.0f - (distance / radius);
		float damage = maxDamage * scale;
		if (health.invulnerableTimer <= 0.0f)
		{
			float before = health.current;
			health.current -= damage;
			if (health.current < 0.0f) health.current = 0.0f;

			// trigger damage flash if health actually decreated
			if (health.current < before && registry.all_of<DamageFlash>(entity))
			{
				auto& flash = registry.get<DamageFlash>(entity);
				flash.timer = flash.duration;
			}
		}

		// knockback - push entity away from explosion
		if (registry.all_of<Velocity>(entity))
		{
			glm::vec3 pushDir = glm::normalize(pos.value - center);
			float knockback = damage *  0.5f;
			registry.get<Velocity>(entity).value += pushDir * knockback;
		}
	}

	// push Jolt bodies away from explosion (even without Health)
	auto joltView = registry.view<Position, JoltBody>();
	for (auto [entity, pos, joltBody] : joltView.each())
	{
		if (entity == ignore) continue;

		float distance = glm::length(pos.value - center);
		if (distance > radius) continue;

		float scale = 1.0f - (distance / radius);
		glm::vec3 pushDir = (distance > 0.01f)
			? glm::normalize(pos.value - center)
			: glm::vec3(0.0f, 1.0f, 0.0f);
		float knockback = maxDamage * scale * 2.0f;

		auto& jolt = registry.ctx().get<JoltWorld>();
		jolt.getBodyInterface().AddImpulse
		(
			joltBody.id,
			JPH::Vec3(pushDir.x * knockback, pushDir.y * knockback, pushDir.z * knockback)
		);
	}
}
```

### `fire_hitscan.cpp`

Fires one ray per pellet: raycasts entities, raycasts level surfaces, applies damage/flash/knockback to the closer hit, and spawns a tracer.

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"
#include "engine/physics/types/aabb.h"

// ─── Fire hitscan ───────────────────────────────────────────────
void fireHitscan
(
	entt::registry& registry,
	const Level& level,
	entt::entity shooter,
	const Weapon& weapon,
	const glm::vec3& origin,
	const glm::vec3& direction,
	const CombatResources& resources
)
{
	for (int i = 0; i < weapon.pelletCount; i++)
	{
		glm::vec3 dir = applySpread(direction, weapon.spread);
		Ray ray { origin, dir };

		// check against entities
		auto entityHit = raycastEntities(registry, ray, shooter, weapon.range);

		// check against level geometry (floors, walls, ceilings)
		float levelDist = weapon.range;
		for (const auto& sector : level.sectors)
		{
			for (const auto& surface : sector.surfaces)
			{
				// Build a thin AABB for the surface
				AABB surfBox;
				surfBox.min = glm::min(
					glm::min(surface.vertices[0], surface.vertices[1]),
					glm::min(surface.vertices[2], surface.vertices[3]));
				surfBox.max = glm::max(
					glm::max(surface.vertices[0], surface.vertices[1]),
					glm::max(surface.vertices[2], surface.vertices[3]));
				// Inflate thin axes so the AABB has volume
				surfBox.min -= glm::vec3(0.05f);
				surfBox.max += glm::vec3(0.05f);

				auto surfHit = rayIntersectionsAABB(ray, surfBox);
				if (surfHit.has_value() && surfHit.value() < levelDist)
				{
					levelDist = surfHit.value();
				}
			}
		}

		// determine hit point for the tracer
		glm::vec3 hitPoint;
		if (entityHit.has_value() && entityHit->distance < levelDist)
		{
			hitPoint = entityHit->point;

			// apply damage
			if (registry.all_of<Health>(entityHit->entity))
			{
				auto& health = registry.get<Health>(entityHit->entity);
				if (health.invulnerableTimer <= 0.0f)
				{
					float before = health.current;
					health.current -= weapon.damage;
					if (health.current < 0.0f) health.current = 0.0f;  // NEW

					// trigger damage flash if health actually decreated
					if (health.current < before && registry.all_of<DamageFlash>(entityHit->entity))
					{
						auto& flash = registry.get<DamageFlash>(entityHit->entity);
						flash.timer = flash.duration;
					}

					// Knockback: push target in the direction of the shot
					if (health.current < before && registry.all_of<PendingKnockback>(entityHit->entity))
					{
						glm::vec3 knockDir = glm::normalize(direction);
						registry.get<PendingKnockback>(entityHit->entity).impulse += knockDir * 1.0f;
					}
				}
			}
		}
		else
		{
			// No entity hit — tracer extends to max range (or level hit)
			hitPoint = origin + dir * levelDist;
		}

		// Offset the tracer start to a "gun barrel" position — slightly
		// down and right of the camera so it's not edge-on invisible.
		// The ray itself fires from camera centre for accurate aiming.
		glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
		glm::vec3 tracerStart = origin + right * 0.3f - glm::vec3(0, 0.2f, 0);
		spawnTracer(registry, tracerStart, hitPoint, resources);
	}
}
```

### `fire_projectile.cpp`

Spawns a moving projectile entity with position, velocity, collider, `Projectile` component, lifetime, and a visible cube mesh.

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"

// ─── Fire projectile ────────────────────────────────────────────
void fireProjectile
(
	entt::registry& registry,
	entt::entity shooter,
	const Weapon& weapon,
	const glm::vec3& origin,
	const glm::vec3& direction,
	const CombatResources& resources
)
{
	auto projectile = registry.create();

	// Spawn slightly in front of the shooter so it doesn't collide immediately
	registry.emplace<Position>(projectile, origin + direction * 0.5f);
	registry.emplace<Velocity>(projectile, direction * weapon.projectileSpeed);
	registry.emplace<AABBCollider>
	(
		projectile,
		glm::vec3(0.15f, 0.15f, 0.15f),
		false
	);
	registry.emplace<Projectile>
	(
		projectile,
		weapon.damage,
		weapon.splashRadius,
		weapon.splashDamage,
		shooter
	);
	registry.emplace<Lifetime>(projectile, 10.0f); // Despawn after 10 seconds

	// visual: a small coloured cube
	registry.emplace<Scale>(projectile, glm::vec3(0.3f));
	registry.emplace<MeshRenderer>
	(
		projectile,
		resources.cubeVAO,
		0u,
		resources.shaderId,
		resources.projectileTextureId,
		true,
		resources.cubeIndexCount
	);
}
```

### `box_hits_level.cpp`

Tests whether an axis-aligned box overlaps any level surface. Level geometry isn't made of ECS entities, so projectile collision must check it explicitly.

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

// True if `box` overlaps any level surface. Level geometry isn't made of ECS
// entities, so projectile (and could-be hitscan) collision must test it here.
bool boxHitsLevel(const Level& level, const AABB& box)
{
	for (const auto& sector : level.sectors)
	{
		for (const auto& surface : sector.surfaces)
		{
			AABB surfBox;
			surfBox.min = glm::min(
				glm::min(surface.vertices[0], surface.vertices[1]),
				glm::min(surface.vertices[2], surface.vertices[3]));
			surfBox.max = glm::max(
				glm::max(surface.vertices[0], surface.vertices[1]),
				glm::max(surface.vertices[2], surface.vertices[3]));
			// Inflate thin axes so the AABB has volume
			surfBox.min -= glm::vec3(0.05f);
			surfBox.max += glm::vec3(0.05f);

			if (box.intersects(surfBox)) return true;
		}
	}
	return false;
}
```

### `update_projectiles.cpp`

Advances each projectile by velocity, tests it against level geometry then every entity collider, applies damage/flash/knockback/splash and Jolt impulses on impact, and queues consumed projectiles for destruction.

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/types/aabb.h"
#include "engine/physics/jolt_world.h"

#include <vector>

// ─── Projectile movement & collision ─────────────────────────────
// Advance each projectile, test it against level geometry and entity
// colliders, apply damage/knockback/splash on impact, then destroy.
void updateProjectiles(entt::registry& registry, const Level& level, float dt)
{
	auto projView = registry.view<Position, Velocity, AABBCollider, Projectile>();
	std::vector<entt::entity> toDestroy;

	for (auto [projEntity, pos, vel, col, proj] : projView.each())
	{
		// move projectile (no Jolt body — simple velocity integration)
		pos.value += vel.value * dt;

		AABB projBox = AABB::fromCentreSize(pos.value, col.halfExtents);

		// ── Level geometry collision ────────────────────────────
		// Walls/floors are not ECS entities, so the entity sweep below
		// never sees them — without this, projectiles tunnel through the
		// level (eval 07 §7.2).
		if (boxHitsLevel(level, projBox))
		{
			if (proj.splashRadius > 0.0f)
			{
				applySplashDamage(registry, pos.value,
					proj.splashRadius, proj.splashDamage, proj.owner);
			}
			toDestroy.push_back(projEntity);
			continue;  // projectile consumed by the wall/floor
		}

		// check against ALL colliders (not just entities with Health)
		auto entityView = registry.view<Position, AABBCollider>();
		for (auto [target, tPos, tCol] : entityView.each())
		{
			if (target == projEntity) continue;  // don't collide with self
			if (target == proj.owner) continue;
			if (tCol.isTrigger) continue;

			AABB targetBox = AABB::fromCentreSize(tPos.value, tCol.halfExtents);
			if (projBox.intersects(targetBox))
			{
				// apply damage if the target has Health
				if (registry.all_of<Health>(target))
				{
					auto& health = registry.get<Health>(target);
					if (health.invulnerableTimer <= 0.0f)
					{
						float before = health.current;
						health.current -= proj.damage;
						if (health.current < 0.0f) health.current = 0.0f;  // NEW

						// trigger damage flash if health actually decreated
						if (health.current < before && registry.all_of<DamageFlash>(target))
						{
							auto& flash = registry.get<DamageFlash>(target);
							flash.timer = flash.duration;
						}

						// Knockback: push target away from projectile
						if (health.current < before && registry.all_of<PendingKnockback>(target))
						{
							glm::vec3 knockDir = glm::normalize(vel.value);
							registry.get<PendingKnockback>(target).impulse += knockDir * 1.6f;
						}
					}
				}

				// push Jolt bodies on impact
				if (registry.all_of<JoltBody>(target))
				{
					auto& joltBody = registry.get<JoltBody>(target);
					auto& jolt = registry.ctx().get<JoltWorld>();
					glm::vec3 dir = glm::normalize(vel.value);
					float impulseMag = proj.damage * 2.0f;
					jolt.getBodyInterface().AddImpulse
					(
						joltBody.id,
						JPH::Vec3(dir.x * impulseMag, dir.y * impulseMag, dir.z * impulseMag)
					);
				}

				// splash damage — hurt nearby entities too
				if (proj.splashRadius > 0.0f)
				{
					applySplashDamage
					(
						registry,
						pos.value,
						proj.splashRadius,
						proj.splashDamage,
						proj.owner
					);
				}

				toDestroy.push_back(projEntity);
				break;
			}
		}
	}

	for(auto e : toDestroy)
	{
		if (registry.valid(e))
		{
			registry.destroy(e);
		}
	}
}
```

### The public header: `combat/combat_system.h`

This is the **only** header consumers ever include. Its signature is identical to the pre-split header — that is the whole point. The implementation moved underneath it; the contract did not.

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/level/level.h"

// Public entry point for the combat system. Implementation is split across
// systems/combat/*.cpp (see combat_internal.h for the shared helpers).
void combatSystem(entt::registry& registry, const Level& level);
```

### The thin orchestrator: `combat/combat_system.cpp`

After the split, `combat_system.cpp` keeps **only** `combatSystem`. It ticks weapon cooldowns, reads fire input, computes the fire origin, dispatches to `fireHitscan` or `fireProjectile`, then advances projectiles via `updateProjectiles`. Every heavy operation is a one-line call to a sibling file.

```cpp
#include "engine/ecs/systems/combat/combat_system.h"
#include "engine/ecs/systems/combat/combat_internal.h"

#include "engine/ecs/components.h"
#include "engine/physics/physics_config.h"

// ─── Main combat system ─────────────────────────────────────────
// Ticks weapon cooldowns, dispatches fire input to hitscan/projectile, then
// advances live projectiles. The heavy lifting lives in the sibling files
// (fire_hitscan, fire_projectile, update_projectiles, ...).
void combatSystem
(
	entt::registry& registry,
	const Level& level
)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	const auto& resources = registry.ctx().get<CombatResources>();
	float dt = config.fixedDeltaTime;

	 // ─── Weapon cooldowns ────────────────────────────────────────
	 auto weaponView = registry.view<WeaponInventory>();
	 for (auto [entity, inv] : weaponView.each())
	 {
		for (auto& weapon : inv.weapons)
		{
			if (weapon.cooldownRemaining > 0.0f)
			{
				weapon.cooldownRemaining -= dt;
			}
		}
	 }

	 // ─── Handle fire input ───────────────────────────────────────

	 auto shooterView = registry.view<Position, PlayerInput, WeaponInventory>();

	 for (auto [entity, pos, input, inv] : shooterView.each())
	 {
		if (!input.fire) continue;
		if (inv.weapons.empty()) continue;

		Weapon& weapon = inv.weapons[inv.currentWeapon];
		if (weapon.cooldownRemaining > 0.0f) continue;

        // Get firing direction from camera front vector
        // The camera direction is written into the registry context each frame
		const auto& cameraDir = registry.ctx().get<CameraDirection>().value;

		// Fire from eye height — position is at body centre, offset upward
		float eyeOffset = 0.0f;
		if (registry.all_of<AABBCollider>(entity))
		{
			eyeOffset = registry.get<AABBCollider>(entity).halfExtents.y * kEyeHeightFraction;
		}
		glm::vec3 fireOrigin = pos.value + glm::vec3(0.0f, eyeOffset, 0.0f);

		if (weapon.fireMode == FireMode::Hitscan)
		{
			fireHitscan(registry, level, entity, weapon, fireOrigin, cameraDir, resources);
		}
		else
		{
			fireProjectile(registry, entity, weapon, fireOrigin, cameraDir, resources);
		}

		weapon.cooldownRemaining = weapon.fireRate;
	 }

	// ─── Projectile movement & collision ─────────────────────────
	updateProjectiles(registry, level, dt);
}
```

### Why consumers are unchanged

`simulation.cpp` (and any other caller) still writes:

```cpp
#include "engine/ecs/systems/combat/combat_system.h"
...
combatSystem(registry, level);
```

The public header path and the function signature are untouched, so not a single line in the call sites changes. The entire restructure is invisible above the folder boundary — exactly the property that makes it safe to verify with the existing scenarios rather than rewriting tests.

---

## Step 3: Wiring the Build

### Source list

The old single entry `src/engine/ecs/systems/combat_system.cpp` is replaced by the new `combat/*.cpp` list. In `CMakeLists.txt`, inside the `qengine_lib` target's source list, the combat block reads:

```cmake
	src/engine/ecs/systems/combat/apply_spread.cpp
	src/engine/ecs/systems/combat/box_hits_level.cpp
	src/engine/ecs/systems/combat/combat_system.cpp
	src/engine/ecs/systems/combat/fire_hitscan.cpp
	src/engine/ecs/systems/combat/fire_projectile.cpp
	src/engine/ecs/systems/combat/raycast_entities.cpp
	src/engine/ecs/systems/combat/spawn_tracer.cpp
	src/engine/ecs/systems/combat/splash_damage.cpp
	src/engine/ecs/systems/combat/update_projectiles.cpp
```

Every split `.cpp` must be listed — CMake compiles only the translation units it is told about. The headers (`combat_internal.h`, `combat_system.h`) are not listed; they are pulled in by `#include`.

The `types/` leaves from Step 1 add nothing here: they are header-only, so there is no build-list change for them. Only types that carry an associated `.cpp` would touch the source list (plan 03 §3) — and these don't.

### Include-path convention

Every split `.cpp` opens with the same first line:

```cpp
#include "engine/ecs/systems/combat/combat_internal.h"
```

This is the convention for a folder split behind an internal header: the **first** include in each sibling `.cpp` is the private internal header, by its full `engine/...` path from the `src` include root (`target_include_directories(qengine_lib PUBLIC src)`). The orchestrator additionally includes the *public* header first, because it implements that public entry point:

```cpp
#include "engine/ecs/systems/combat/combat_system.h"
#include "engine/ecs/systems/combat/combat_internal.h"
```

Domain-specific includes (`engine/ecs/components.h`, `engine/physics/raycast.h`, `<random>`, `<vector>`) follow, scoped to what each file actually uses — `apply_spread.cpp` pulls in only `<random>`, while `update_projectiles.cpp` pulls in Jolt, AABB, and `<vector>`.

After wiring, rebuild with `C:\msys64\ucrt64\bin` on PATH and run the six headless scenarios. `rocket_vs_floor` and the hitscan scenario exercise the split path directly; a green suite confirms the restructure was behaviour-preserving.

---

## Summary

| File | Change |
|------|--------|
| `ecs/types/entity_hit.h` | **New leaf** — `EntityHit` relocated out of `combat_system.cpp`. |
| `ecs/types/mesh_assets.h` | **New leaf** — `MeshAssets` relocated out of `level/factories.h`. |
| `ecs/types/system_phase.h` | **Relocated** — `SystemPhase` enum moved out of `systems/`. |
| `physics/types/aabb.h` | **Relocated** — `AABB` moved out of `physics/`. |
| `physics/types/ray.h` | **New leaf** — `Ray`/`RayHit` split out of `physics/raycast.h`; functions stay put. |
| `combat/combat_internal.h` | **New** — private shared declarations for the eight helpers. |
| `combat/combat_system.h` | Public entry point, signature unchanged. |
| `combat/combat_system.cpp` | Reduced to the `combatSystem` orchestrator; delegates to siblings. |
| `combat/{apply_spread,raycast_entities,spawn_tracer,splash_damage,fire_hitscan,fire_projectile,box_hits_level,update_projectiles}.cpp` | **New** — one public function each, extracted verbatim from the god-file. |
| `CMakeLists.txt` | Replaced the single `combat_system.cpp` entry with the nine `combat/*.cpp` files. |

The combat split worked cleanly because the file was a bag of free functions, not a class — the rule's ideal target. The internal-header pattern keeps the helpers private to the folder while the public header (and therefore every consumer) stays frozen. Because the type layout was stabilised first, the splits landed against fixed include paths and the six scenarios verified behaviour without a single test rewrite.

## What's Next

The combat system was the easy win. Next we tackle a harder shape — a system with private static helpers and GL state — and finish relocating the files that don't belong in `ecs/`. In **Chapter 17c — HUD Split, Relocations & Domain Grouping** (`Chapter_17c_Hud_Split_Relocations_And_Grouping.md`), we split `debug_hud_system.cpp` into its `draw_*` leaves, relocate physics bodies and level content out of `ecs/`, and group the remaining systems into domain folders.
