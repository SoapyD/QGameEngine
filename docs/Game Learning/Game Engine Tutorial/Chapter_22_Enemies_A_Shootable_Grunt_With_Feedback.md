# Chapter 22: Enemies — a Shootable Grunt With Feedback

## What You'll Learn
- Adding an **`AIState` component** — the marker that says "this entity is an enemy" plus the
  behaviour-state fields a future chapter will drive
- Building a **grunt archetype** (`spawnMonsterGrunt`) as a coloured box with `Health`,
  `DamageFlash`, and `AIState` — and *why it deliberately leaves out* `TagTriggerable` and
  `PendingKnockback`
- Why the archetype lives in its **own `.cpp` file** rather than in `factories.cpp`
- Registering a new `monster_grunt` **classname** so the data-driven spawner (Chapter 18) can place
  enemies, and dropping two of them into the showcase level
- Giving every enemy a **kinematic Jolt body** in `buildWorld` so it stands upright and blocks the
  player — and the tradeoff that choice makes
- How the grunt becomes **shootable and damageable for free**, because `raycastEntities` keys off
  `AABBCollider` and `applyDamage` keys off `Health` — no new combat code
- Writing **`enemyDeathSystem`**: fading the hit-flash timer *and* removing dead enemies (death
  sound, Jolt body teardown, entity destroy), and where it slots in the tick order
- The **white hit-flash** feedback loop: a per-hit flesh-hit sound (guarded to once per flash) and a
  flat-white model tint via `renderSystem`'s `colorOverride`
- Wiring it: CMake, and a new headless-harness scenario that proves the grunt blocks *and* dies

---

## Where We Are

Chapter 21 turned the arsenal into something legible — a gun in your hands, a weapon bar, pickups
drawn as their models. But there was still nothing to point those weapons *at*. The world had
health boxes, ammo, doors, lifts, and lava, and not a single thing that could be hurt.

This chapter adds the first enemy: a **grunt**. Deliberately, it does almost nothing — it stands
where you spawn it, blocks your path like a wall of meat, takes damage when you shoot it, blinks
white when hit, and dies (with a sound) when its health runs out. That's the whole feature. There
is **no chasing and no attacking yet** — the behaviour state machine is a separate future chapter.
What we build here is the *setup*: the enemy as a physical, shootable, killable object, plus the
feedback that makes shooting it feel like it landed.

The surprising thing you'll notice is how *little* new code this takes. Most of the combat, physics,
and rendering machinery from earlier chapters already generalises — the grunt mostly falls out of
components that already exist. We build it in the order that makes each piece stand on the last:

1. **The `AIState` component** — the data that marks an enemy.
2. **The grunt archetype** — the factory that assembles one.
3. **The `monster_grunt` classname** — so the level can place it.
4. **A kinematic body** — so it's solid.
5. **Shooting it** — which needs no new code at all.
6. **`enemyDeathSystem`** — cleaning up the dead.
7. **The hit-flash** — the feedback polish.

---

## Step 1: The `AIState` Component

Data before systems. Before anything can *be* an enemy, we need a component that marks it as one and
holds the state a behaviour system will later read. Add it to the gameplay component header,
`src/engine/ecs/components/gameplay.h`:

```cpp
// ─── Enemy AI ────────────────────────────────────────────────────
enum class AIStateKind
{
	Idle,   // standing still (the only state the setup plan produces)
	Chase,  // moving toward the target
	Attack, // in range, attacking on a cooldown
	Dead    // health depleted; awaiting cleanup
};

// Marks an enemy and holds its behaviour state. Created by the setup plan (the
// grunt archetype); the state machine that drives it lands in the behaviour plan.
struct AIState
{
	AIStateKind  state = AIStateKind::Idle;
	float        attackCooldown = 0.0f;      // seconds until the next attack is allowed
	entt::entity target = entt::null;        // who to chase/attack (resolved by behaviour)
};
```

`AIState` does double duty. First, its mere *presence* on an entity is the definition of "this is an
enemy" — every enemy loop we write in this chapter is `registry.view<..., AIState>()`. Second, it
carries the fields a behaviour system will need: which `AIStateKind` the grunt is in, a cooldown for
pacing attacks, and the `target` entity it will chase. For now those fields sit at their defaults
(`Idle`, `0.0f`, `entt::null`) — nothing reads them yet.

> **Why define the whole `AIStateKind` enum and the `target`/`attackCooldown` fields now, when this
> chapter only ever produces `Idle` and never touches them?** Because the *shape* of the enemy is
> the interesting decision, and it's cheap to get right up front. The setup and the behaviour are two
> plans, but they describe one component — if we shipped a bare `struct AIState {};` marker now and
> grew it later, every archetype and save-path would have to change shape when behaviour lands. By
> declaring the full state now, the grunt archetype (`reg.emplace<AIState>(e)`) is already correct;
> the behaviour chapter only has to add a *system* that reads these fields, not migrate the data.
> This is the same "state lives on the component, the system fills it in later" split we used for the
> weapon viewmodel's animation state in Chapter 21.

---

## Step 2: The Grunt Archetype

Now the factory that assembles a grunt from components. It gets its own file,
`src/engine/level/spawn_monster.cpp`:

```cpp
#include "engine/level/factories.h"

#include "engine/ecs/components.h"   // Position, Health, AIState, Colour, ...

// Enemy grunt archetype. Kept in its own file (factories.cpp is at its size cap).
// A solid coloured box that can be shot (raycastEntities keys off AABBCollider),
// takes damage (applyDamage keys off Health), and blocks the player (its
// kinematic Jolt body is created in buildWorld). Behaviour is a separate plan.

namespace factories
{
    entt::entity spawnMonsterGrunt(entt::registry& reg, const MeshAssets& a, glm::vec3 pos)
    {
        auto e = reg.create();
        reg.emplace<Position>(e, pos);
        reg.emplace<Rotation>(e, glm::vec3(0.0f));
        reg.emplace<Scale>(e, glm::vec3(0.8f, 1.8f, 0.8f));          // humanoid-ish box
        reg.emplace<AABBCollider>(e, glm::vec3(0.4f, 0.9f, 0.4f), false); // solid: shootable + blocks
        reg.emplace<MeshRenderer>(e, cubeRenderer(a, 0u));           // untextured — coloured below
        reg.emplace<Colour>(e, glm::vec4(0.75f, 0.15f, 0.15f, 1.0f)); // red, via renderSystem albedo
        reg.emplace<Health>(e, 50.0f, 50.0f, 0.0f);
        reg.emplace<DamageFlash>(e, 0.0f, 0.12f);   // brief white blink when shot (renderSystem)
        reg.emplace<AIState>(e);
        // No TagTriggerable: player-only volumes (lava, teleporters) must not
        // affect the grunt. It is NOT given PendingKnockback either (kinematic
        // bodies ignore impulses — knockback is deferred to the behaviour plan).
        return e;
    }
}
```

Look at what a grunt actually *is*: eight components, every one of which we've built in an earlier
chapter.

- `Position`, `Rotation`, `Scale` — the transform. The scale `(0.8, 1.8, 0.8)` stretches the unit
  cube into a tall, thin, humanoid-ish box.
- `AABBCollider` with `isTrigger = false` — a *solid* collider, half-extents `(0.4, 0.9, 0.4)`.
  Solid means two things at once, and both matter: `raycastEntities` will hit it (so it's
  shootable, Step 5) and `buildWorld` will give it a physics body (so it's a wall, Step 4).
- `MeshRenderer` via `cubeRenderer(a, 0u)` — the shared cube mesh with texture id `0` (untextured),
  because we're going to flat-colour it instead.
- `Colour` — the RGBA struct from Chapter 21 that drives `renderSystem`'s flat-albedo path. A dull
  red so the grunt reads as a hostile object, not scenery.
- `Health{50, 50, 0}` — 50 current, 50 max, no invulnerability. This is what makes it *damageable*.
- `DamageFlash{0.0f, 0.12f}` — timer starts at zero (not flashing), flash lasts 0.12 s. This drives
  the hit-flash in Step 7.
- `AIState` — the marker from Step 1, defaulted to `Idle`.

`cubeRenderer` is the same small helper the other factories use — it packs the shared cube's VAO,
index count, and lit shader out of `MeshAssets` into a `MeshRenderer`.

The two *omissions* in the trailing comment are as deliberate as the components that are present:

- **No `TagTriggerable`.** In Chapter 11 the trigger system checked `TagPlayer`; it was later
  generalised to a `TagTriggerable` tag so anything *could* opt into trigger volumes. The grunt
  simply doesn't opt in — so lava, teleporters, and jump pads (all player-only fantasies for now)
  slide right past it. We'll see the other side of this in Step 5.
- **No `PendingKnockback`.** Combat can shove things via a deferred-impulse component, but the grunt
  gets a *kinematic* body (Step 4), and kinematic bodies ignore impulses by design. Adding
  `PendingKnockback` would be dead weight — knockback for enemies is deferred to the behaviour plan,
  which will move them by driving the kinematic target, not by pushing them.

> **Why does this archetype live in its own `spawn_monster.cpp` instead of alongside the other
> `spawn*` factories in `factories.cpp`?** The codebase enforces a per-file size cap (Chapter 17),
> and `factories.cpp` was already sitting at it after Chapter 21 added `spawnWeaponPickup`. Rather
> than blow the cap, the grunt gets its own translation unit — still in `namespace factories`, still
> declared in the shared `factories.h`, still using the same `cubeRenderer` helper and `MeshAssets`
> bundle. To a caller nothing changed: `factories::spawnMonsterGrunt(...)` looks identical to
> `factories::spawnPlayer(...)`. The split is purely to keep each file under the standard's ceiling,
> and it's a pattern you'll keep reaching for as archetypes accumulate.

Declare it in `src/engine/level/factories.h` next to the other factories:

```cpp
    // Enemy grunt: a solid, coloured humanoid-ish box with Health + AIState. Its
    // kinematic Jolt body (so it stands + blocks the player) is created later in
    // buildWorld, like movers. No behaviour yet — see the AI behaviour plan.
    entt::entity spawnMonsterGrunt(entt::registry& reg, const MeshAssets& a, glm::vec3 pos);
```

---

## Step 3: Register the `monster_grunt` Classname

The grunt is an archetype, but nothing places it yet. We route it through the data-driven classname
dispatch from Chapter 18, so a level can spawn enemies by name just like it spawns doors and
pickups.

In `src/engine/level/classname_factory.cpp`, add a thin adaptor that unpacks a `SpawnParams` into the
factory call, alongside the other `make_*` functions:

```cpp
    entt::entity make_monster_grunt(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnMonsterGrunt(reg, ctx.assets, p.origin);
    }
```

and register it in the classname `table()`:

```cpp
                { "info_teleport_destination", &make_info_teleport_destination },
                { "func_decor",                &make_func_decor },
                { "_wireframe",                &make_wireframe },
                { "monster_grunt",             &make_monster_grunt },
```

That's the whole registration: any level descriptor with `classname = "monster_grunt"` now spawns a
grunt at its `origin`. To actually *see* one, drop a couple into the showcase level in
`src/engine/level/showcase_descriptor.cpp`, after the item pickups:

```cpp
    // ─── Enemies: a couple of grunts to shoot (they just stand there for now) ──
    d.push_back({ .classname = "monster_grunt", .origin = glm::vec3(13.0f, 0.95f, 8.0f)  });
    d.push_back({ .classname = "monster_grunt", .origin = glm::vec3(8.0f,  0.95f, 22.0f) });
```

The `y = 0.95` puts the grunt's base on the floor (its collider half-height is `0.9`). Two of them,
spread across the arena, so you can walk up to one and shoot the other from range.

> **Why funnel the grunt through the classname table instead of just calling `spawnMonsterGrunt`
> directly from the showcase code?** Because the classname table is the seam that a real level
> format plugs into. The showcase descriptor is a temporary, hand-written stand-in for a
> designer-authored `.map` file — and the whole point of Chapter 18 was that *both* go through the
> same `classname → factory` dispatch. Register `monster_grunt` once here and it's simultaneously
> placeable from the showcase list *and* from any future map a designer authors, with no extra
> plumbing. The `make_monster_grunt` adaptor exists only to bridge the generic `SpawnParams` (which
> a map parser produces) to the typed `spawnMonsterGrunt` signature.

---

## Step 4: A Kinematic Body So It Stands and Blocks

A grunt with just an `AABBCollider` is shootable, but it isn't *solid* to the physics world — you'd
walk straight through it, and it would have no reason to stay upright on the floor. We fix that in
`buildWorld` (`src/engine/app/simulation.cpp`), right after the existing loop that gives movers their
kinematic bodies:

```cpp
        // Kinematic bodies for movers (lifts, doors)
        auto moverView = registry.view<Position, AABBCollider, Mover>();
        for (auto [entity, pos, col, mover] : moverView.each())
        {
            createKinematicBody(registry, entity);
        }

        // Kinematic bodies for enemies — they stand upright and block the player;
        // the behaviour plan drives their movement (like movers) later.
        auto enemyView = registry.view<Position, AABBCollider, AIState>();
        for (auto [entity, pos, col, ai] : enemyView.each())
        {
            createKinematicBody(registry, entity);
        }
```

The loop is a near-copy of the mover loop, keyed on `AIState` instead of `Mover`. Every enemy gets a
`createKinematicBody` — the same call lifts and doors use — which registers a Jolt kinematic body
from the entity's `Position` and `AABBCollider`. Because it runs during `buildWorld`, it happens once
at level load, and (like the movers) *before* `initPlayerCharacter`, so the player's
`CharacterVirtual` resolves against the grunts as solid geometry from its very first step.

> **Why a *kinematic* body for an enemy, rather than a dynamic rigid body or a `CharacterVirtual`
> like the player?** Each option trades something. A **dynamic** body would be pushed around by
> gravity and impulses — the grunt would topple, slide when shot, and generally behave like a
> ragdoll, which is not what a standing enemy wants (and it'd need us to fight it upright every
> tick). A **`CharacterVirtual`** is the right long-term answer for something that *walks*, but it's
> heavier: it needs per-tick `ExtendedUpdate` calls and its own movement code, none of which exists
> yet. A **kinematic** body is the sweet spot for the setup milestone: it's immovable by physics (so
> it stands rock-still and blocks the player perfectly) but its position is fully script-controlled,
> exactly like a lift. The tradeoff is that it *ignores impulses* — you can't knock it back by
> shooting it — which is precisely why Step 2 skipped `PendingKnockback`. When the behaviour chapter
> teaches grunts to move, it drives the kinematic target the same way `moverSyncSystem` drives a
> lift, so this body type carries straight over.

---

## Step 5: Shootable and Damageable — for Free

Here's the payoff for building the grunt out of existing components: **shooting it required writing
no new combat code.** Both halves of "hit it and hurt it" already generalise off components the
grunt has.

The hit test, `raycastEntities` (`src/engine/ecs/systems/combat/raycast_entities.cpp`), walks every
entity that has a *solid* collider — it never mentions the player, enemies, or any tag:

```cpp
	auto view = registry.view<Position, AABBCollider>();
	for (auto [entity, pos, col] : view.each())
	{
		if (entity == ignore) continue;
		if (col.isTrigger) continue;

		AABB box = AABB::fromCentreSize(pos.value, col.halfExtents);
		auto hit = rayIntersectionsAABB(ray, box);
		...
```

The grunt has a `Position` and a non-trigger `AABBCollider`, so it's already in that view. When your
weapon's ray sweeps through it, `raycastEntities` returns it as the closest `EntityHit` with no
change whatsoever.

The damage application, `applyDamage` (`src/engine/ecs/apply_damage.cpp`), keys off `Health`:

```cpp
bool applyDamage(entt::registry& registry, entt::entity target, float amount)
{
	if (amount <= 0.0f) return false;
	if (!registry.all_of<Health>(target)) return false;

	auto& health = registry.get<Health>(target);
	...
	health.current -= remaining;
	if (health.current < 0.0f) health.current = 0.0f;
```

The grunt has `Health{50, 50, 0}`, so `applyDamage` does exactly what it does to the player: subtract
the damage, clamp at zero. The combat system that already calls `raycastEntities` then `applyDamage`
for hitscan weapons never learns that enemies exist — it just hits a collider and damages a thing
with health, and the grunt happens to be both.

The trigger system generalisation from an earlier chapter is the *third* piece, and it's why the
grunt safely omits `TagTriggerable`. In `trigger_system.cpp`:

```cpp
		// Check against all triggerable entities (currently just the player,
		// but enemies/props can opt in via TagTriggerable without changing this).
		auto entityView = registry.view<Position, AABBCollider, TagTriggerable>();
```

Trigger volumes only sweep entities tagged `TagTriggerable`. The grunt isn't tagged, so it's invisible
to lava and teleporters — a hostile monster that can't be killed by your own environmental traps, and
can't ride your teleporters. All three behaviours — hittable, damageable, ignored-by-triggers — come
from *which components the archetype has*, not from any enemy-specific code path.

> **Why is "no new combat code" a feature and not just luck?** It's the ECS design paying off. Because
> combat is written against *capabilities* (`AABBCollider` = "occupies space", `Health` = "can be
> hurt", `TagTriggerable` = "reacts to volumes") rather than against *entity kinds* ("the player",
> "an enemy"), a new kind of thing is defined entirely by the set of capabilities you give it. The
> grunt is "a solid, hurtable thing that ignores player volumes" — spelled out as a component set in
> Step 2 — and every system that cares about one of those capabilities picks it up automatically.
> The alternative, a system full of `if (isEnemy)` branches, would need editing in a dozen places to
> add a monster; here we edited zero.

---

## Step 6: `enemyDeathSystem` — Cleaning Up the Dead

Damage drops the grunt's health to zero, but nothing yet *removes* it — a corpse at 0 HP would just
stand there. And the hit-flash timer, once set, needs to be counted back down each tick. Both jobs are
small, per-enemy, and run every frame, so they share one system.

Create `src/engine/ecs/systems/enemy/enemy_death_system.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

// Per-tick enemy upkeep that isn't behaviour: fades each enemy's hit-flash
// timer (the brief white blink when shot), and cleans up enemies whose Health
// has reached zero — plays a death sound, removes the Jolt body, destroys the
// entity. Runs after combat has applied damage. (Chase/attack behaviour is a
// separate system — see the AI behaviour plan.)
void enemyDeathSystem(entt::registry& registry);
```

and `src/engine/ecs/systems/enemy/enemy_death_system.cpp`:

```cpp
#include "engine/ecs/systems/enemy/enemy_death_system.h"

#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"
#include "engine/audio/queue_sound.h"

#include <algorithm>
#include <vector>

void enemyDeathSystem(entt::registry& registry)
{
    const float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;

    // One pass over the enemies: fade hit-flashes and collect the dead.
    std::vector<entt::entity> dead;
    for (auto [entity, ai] : registry.view<AIState>().each())
    {
        if (DamageFlash* flash = registry.try_get<DamageFlash>(entity); flash && flash->timer > 0.0f)
            flash->timer = std::max(0.0f, flash->timer - dt);

        if (Health* health = registry.try_get<Health>(entity); health && health->current <= 0.0f)
            dead.push_back(entity);
    }

    if (dead.empty()) return;

    // Destroy dead enemies: pop a sound, remove the Jolt body, drop the entity.
    auto& bodyInterface = registry.ctx().get<JoltWorld>().getBodyInterface();
    for (entt::entity e : dead)
    {
        queueSoundAt(registry, "combat.explosion_small", registry.get<Position>(e).value);
        if (const JoltBody* body = registry.try_get<JoltBody>(e))
        {
            bodyInterface.RemoveBody(body->id);
            bodyInterface.DestroyBody(body->id);
        }
        registry.destroy(e);
    }
}
```

The system makes a single pass over every `AIState` entity and does two independent things:

1. **Fade the hit-flash.** If the enemy has a `DamageFlash` with time still on the clock, subtract
   this tick's `dt` (clamped at zero). This is what makes the white blink from Step 7 last only 0.12 s
   and then stop — the flash timer counts down here, one tick at a time.
2. **Collect the dead.** If the enemy's `Health` has reached zero, remember it in a `dead` vector. We
   *don't* destroy it inside the loop — mutating the registry while iterating a view is asking for
   trouble — so we defer.

If nobody died, we return early. Otherwise we grab Jolt's body interface once and, for each dead
grunt: queue the positional death sound (`combat.explosion_small`, played at the grunt's `Position`
via `queueSoundAt`), tear down its kinematic Jolt body (`RemoveBody` then `DestroyBody` — the mirror
of the `createKinematicBody` from Step 4), and finally `registry.destroy(e)` to drop the entity and
all its components.

> **Why collect the dead into a vector and destroy them in a second pass, instead of destroying inline
> as we find them?** EnTT views iterate over the component pools directly; calling
> `registry.destroy(e)` mid-iteration mutates the very pool you're walking, which can invalidate the
> iterator and skip or double-visit entities. The two-pass split — read-only scan, then a clean second
> loop over a plain `std::vector<entt::entity>` — sidesteps that entirely. It also lets the common
> case (nobody died this tick) bail with a single `dead.empty()` check before we even fetch the Jolt
> body interface.

Now wire it into the tick order in `stepSimulation` (`simulation.cpp`), immediately after
`playerDeathSystem`:

```cpp
        pickupSystem(registry);         // grant + consume items on touch
        playerDeathSystem(registry);
        enemyDeathSystem(registry);     // remove grunts whose health hit 0
        demoResetSystem(registry);
```

The slot is chosen carefully. `combatSystem` runs earlier in the same tick and is what applies the
damage; the two death systems run *after* it, so a grunt that dropped to zero this tick is cleaned up
this tick. And `enemyDeathSystem` sits right beside `playerDeathSystem` — the two are symmetric
"something hit zero health, deal with it" passes — just before `demoResetSystem`, which is the very
last thing each tick.

---

## Step 7: The Hit-Flash — Making a Shot Land

A grunt that silently loses health is technically working but feels dead. The last piece is the
*feedback*: a white flash and a wet sound each time a shot connects, so the hit reads. Three tiny edits
across the code we've already touched, and they all key off the `DamageFlash` component the archetype
already carries (Step 2).

**The sound.** Back in `applyDamage` (`src/engine/ecs/apply_damage.cpp`), the block that already
played the player's pain voice now also plays a flesh-hit for enemies:

```cpp
	// Pain/hit voice, once per flash window (so a shotgun's pellets fire it once).
	if (!wasFlashing)
	{
		if (registry.all_of<TagPlayer>(target))
			queueSound(registry, "player.pain");
		else if (registry.all_of<AIState, Position>(target))
			queueSoundAt(registry, "combat.flesh_hit", registry.get<Position>(target).value);
	}
```

The `wasFlashing` guard is the clever bit, and it predates this chapter. A few lines earlier,
`applyDamage` records whether a flash was *already* active before it refreshes the timer:

```cpp
	bool wasFlashing = false;
	if (registry.all_of<DamageFlash>(target))
	{
		auto& flash = registry.get<DamageFlash>(target);
		wasFlashing = flash.timer > 0.0f;
		flash.timer = flash.duration;
	}
```

So the sound only fires on the *leading edge* of a flash — the first hit of a burst. A shotgun sends
many pellets in one tick; without the guard each pellet would stack a flesh-hit and the grunt would
squelch a dozen times at once. With it, the burst plays one hit. The enemy branch also requires
`Position` because `queueSoundAt` needs a location to play the sound *at* — the flesh-hit is
positional, so it gets quieter with distance, unlike the player's own pain voice which is played flat.

**The visual.** `renderSystem` (`src/engine/ecs/systems/render/render_system.cpp`) already had a
`colorOverride` uniform for drawing debug wireframes bright green. We extend that same override to
flash a damaged entity flat white:

```cpp
		// Flat colour override for debug wireframes (+ enemy hit flash)
		loc = glGetUniformLocation(mesh.shaderId, "colorOverride");
		if (registry.all_of<TagDebugWireframe>(entity))
			glUniform4f(loc, 0.0f, 1.0f, 0.0f, 1.0f);  // bright green
		else if (const DamageFlash* f = registry.try_get<DamageFlash>(entity); f && f->timer > 0.0f)
			glUniform4f(loc, 1.0f, 1.0f, 1.0f, 1.0f);  // hit flash — brief flat white
		else
			glUniform4f(loc, 0.0f, 0.0f, 0.0f, 0.0f);   // disabled (normal lighting)
```

While a `DamageFlash.timer` is above zero, the entity draws as flat white `(1, 1, 1, 1)`; otherwise
the override is disabled `(0, 0, 0, 0)` and normal lighting resumes. `enemyDeathSystem` (Step 6) is
what winds that timer back down to zero, so the white flash lasts exactly the `DamageFlash.duration`
of `0.12 s` set in the archetype, then the grunt returns to its red albedo.

> **Why reuse the debug-wireframe `colorOverride` for the hit-flash instead of adding a dedicated
> "flash" uniform?** `colorOverride` already means precisely what we want: "ignore this surface's
> normal shading and paint it this flat colour." A hit-flash *is* a flat colour override — a brief
> full-white wash. Reusing the existing uniform means no shader change, no new pipeline state, and one
> unambiguous rule for the fragment stage: if `colorOverride.a > 0`, use it. The `if/else if/else`
> chain also encodes a clean priority — debug wireframe wins over flash wins over normal — so an
> entity can never be told two conflicting things in the same draw. The whole feature is a single
> extra `else if`.

Notice the flash and the sound are driven by the *same* `DamageFlash` state but consumed in different
places — the sound at the moment damage is applied (`applyDamage`, on the leading edge), the visual
every frame the timer is live (`renderSystem`), and the fade once per tick (`enemyDeathSystem`). One
component, three readers, no coordination needed between them.

---

## Step 8: CMake and the Headless Harness

Two new `.cpp` files join the `qengine_lib` target in `CMakeLists.txt` — the archetype and the death
system:

```cmake
	src/engine/level/factories.cpp
	src/engine/level/spawn_monster.cpp
	...
	src/engine/ecs/systems/pickup/pickup_system.cpp
	src/engine/ecs/systems/enemy/enemy_death_system.cpp
```

The `enemy_death_system.h` and the `gameplay.h` component change are header-only and compile into
their includers, so they need no CMake entry.

The important verification is the new headless scenario. `src/harness/headless_main.cpp` gains
`scenario_monster_grunt`, which proves *both* halves of the feature — the grunt blocks the player
while alive, and it can be shot dead:

```cpp
    // A monster_grunt from the showcase: while alive it blocks the player, and it
    // can be shot dead (health drops, then the entity is removed).
    bool scenario_monster_grunt(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        entt::entity grunt = entt::null;
        for (auto e : reg.view<AIState, Health, Position, AABBCollider>()) { grunt = e; break; }
        if (grunt == entt::null) return report("monster_grunt", false, "no monster_grunt spawned");

        glm::vec3 gpos = reg.get<Position>(grunt).value;
        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // ── Collision: stand in front (+Z), push toward the grunt, expect blocked.
        glm::vec3 front = gpos + glm::vec3(0.0f, 0.0f, 1.4f); front.y = halfY + 0.05f;
        teleportPlayer(reg, player, front);
        Input push; push.wishDir = glm::vec3(0.0f, 0.0f, -1.0f);
        for (int i = 0; i < 90; i++) { applyInput(reg, player, push); qengine::stepSimulation(reg, jolt, level, dt); }
        float blockedZ = reg.get<Position>(player).value.z;
        // grunt front face ~ gpos.z + 0.4, player radius ~0.3 → stop near gpos.z+0.7;
        // must not tunnel to the far side (z < gpos.z).
        bool blocked = blockedZ > gpos.z + 0.5f;

        // ── Damage + death: stand back, aim, fire the shotgun until it dies.
        glm::vec3 stand = gpos + glm::vec3(0.0f, 0.0f, 3.0f); stand.y = halfY + 0.05f;
        teleportPlayer(reg, player, stand);
        glm::vec3 aim = glm::normalize(gpos - (stand + glm::vec3(0.0f, halfY * 0.7f, 0.0f)));

        Input idle; idle.lookDir = aim;
        for (int i = 0; i < 5; i++) { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }

        float hpBefore = reg.get<Health>(grunt).current;
        float hpAfter1 = hpBefore;
        bool died = false;
        for (int shot = 0; shot < 12 && !died; ++shot)
        {
            Input fire; fire.lookDir = aim; fire.fire = true;
            applyInput(reg, player, fire);
            qengine::stepSimulation(reg, jolt, level, dt);
            if (shot == 0 && reg.valid(grunt)) hpAfter1 = reg.get<Health>(grunt).current;
            for (int i = 0; i < 40 && reg.valid(grunt); i++)
            { applyInput(reg, player, idle); qengine::stepSimulation(reg, jolt, level, dt); }
            died = !reg.valid(grunt);
        }

        bool damaged = hpAfter1 < hpBefore;
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "blockedZ=%.2f (grunt z=%.2f) blocked=%d; hp %.0f->%.0f died=%d",
            blockedZ, gpos.z, blocked ? 1 : 0, hpBefore, hpAfter1, died ? 1 : 0);
        return report("monster_grunt", blocked && damaged && died, buf);
    }
```

The scenario has two phases, one per capability:

- **Blocked.** Teleport the player just in front of the grunt (on its `+Z` side, at standing height),
  then drive `wishDir` straight into it for 90 ticks. The assertion `blockedZ > gpos.z + 0.5f` checks
  the player got *stopped* by the grunt's kinematic body — it neither tunnelled through nor slid to the
  far side. This is Step 4 working.
- **Killed.** Teleport the player back three metres, aim at the grunt's centre of mass, then fire the
  shotgun up to twelve times, ticking 40 frames between shots to let the weapon cool down. After the
  first shot it records `hpAfter1` to confirm `Health` actually dropped (Step 5's damage), and the loop
  ends when `reg.valid(grunt)` goes false — i.e. `enemyDeathSystem` destroyed the entity (Step 6). The
  scenario passes only if `blocked && damaged && died` — all three.

Register it in `main`'s scenario dispatch alongside the others:

```cpp
    else if (scenario == "weapon_pickup")    pass = scenario_weapon_pickup(registry, jolt, level, dt);
    else if (scenario == "monster_grunt")    pass = scenario_monster_grunt(registry, jolt, level, dt);
```

> **Why is it worth a headless scenario when there's "nothing to test" — the grunt just stands
> there?** Because "just stands there, solidly" and "dies when shot enough" are exactly the two
> invariants that are easy to break silently as the engine grows. If a future change to body creation
> makes the grunt non-solid, or a tick-order shuffle stops `enemyDeathSystem` from seeing the damage,
> nothing on screen would obviously scream — but `scenario_monster_grunt` fails immediately, headless,
> with no window and no audio device. It's the same regression net the other five scenarios form:
> cheap to run, and it pins the feature's contract in code so the behaviour chapter can build on top of
> a grunt that provably blocks and provably dies.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/ecs/components/gameplay.h` | **New** `AIStateKind` enum + `AIState` component (the enemy marker + behaviour-state fields). |
| `level/spawn_monster.cpp` | **New file** — `spawnMonsterGrunt` archetype (coloured box, `Health`, `DamageFlash`, `AIState`; omits `TagTriggerable`/`PendingKnockback`). |
| `level/factories.h` | Declare `spawnMonsterGrunt`. |
| `level/classname_factory.cpp` | `make_monster_grunt` adaptor + `"monster_grunt"` table entry. |
| `level/showcase_descriptor.cpp` | Two `monster_grunt` placements in the showcase. |
| `app/simulation.cpp` | `buildWorld` gives every `AIState` entity a `createKinematicBody`; `stepSimulation` calls `enemyDeathSystem` after `playerDeathSystem`. |
| `ecs/apply_damage.cpp` | Enemy hits play `combat.flesh_hit` (positional, once per flash window). |
| `ecs/systems/enemy/enemy_death_system.{h,cpp}` | **New** — fade hit-flash timers + destroy dead enemies (death sound, Jolt body teardown, entity destroy). |
| `ecs/systems/render/render_system.cpp` | `colorOverride` now flashes flat white while a `DamageFlash.timer > 0`. |
| `CMakeLists.txt` | Add `spawn_monster.cpp` and `enemy_death_system.cpp`. |
| `harness/headless_main.cpp` | **New** `scenario_monster_grunt` — grunt blocks the player and is shot dead. |

---

## What You Should See

Run `build/QEngine.exe`:

1. **Two red pillars in the arena.** Tall, dull-red boxes standing where the showcase placed them —
   the grunts. They don't move.
2. **They're solid.** Walk into one and you stop dead, exactly as you would against a wall. You can't
   push through it or shove it aside.
3. **Shooting them lands.** Fire your shotgun at a grunt and it blinks bright white on each burst, with
   a wet flesh-hit sound that's quieter the further away you are. Keep firing and after a handful of
   shots it vanishes with a small explosion sound.
4. **Environmental traps ignore them.** The grunts aren't hurt by lava and don't ride teleporters —
   they have no `TagTriggerable`, so player-only volumes pass right through them.
5. **The headless harness passes `monster_grunt`** — silently, no window — asserting the grunt both
   blocked the player and was shot dead (`blocked && damaged && died`).

---

## What's Next

The grunt is now a real, physical, killable object with feedback that makes shooting it satisfying —
but it is still completely passive. It stands where you put it, waits to be shot, and never fights
back. The `AIState` component is already carrying an unused `state`, `target`, and `attackCooldown`,
and the kinematic body is already the right shape to be *driven* rather than just parked. The next
chapter fills those in: a behaviour system that flips a grunt from `Idle` to `Chase` when it notices
the player, steers its kinematic body toward you (the way `moverSyncSystem` steers a lift), and drops
into `Attack` on a cooldown once it's in range — turning the shootable dummy you built here into an
enemy that shoots back.
