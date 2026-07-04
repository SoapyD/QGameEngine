# Chapter 23: Enemy Behaviour — Sensing, Chasing, Attacking

## What You'll Learn
- Where the **`aiSystem`** slots into the tick order — *after* `moverSyncSystem` and *before*
  `joltWorld.step()` — so a grunt's `MoveKinematic` target is swept the same tick, exactly like a lift
- Why reading the player's position at the top of the tick means the AI acts on **last tick's**
  position (a one-tick lag), and why that's harmless
- Using **`AIState.target` as the aggro latch** — null until acquired, latched to the player until they
  escape — and the **detect-range vs pursue-range** hysteresis that stops flickering aggro
- Writing **`clearLineOfSight`**: a ray from the grunt's eye to the player, blocked by level wall
  surfaces and solid props (reusing `raycastEntities`) but never by the player itself
- How LoS **gates acquisition and attack** but *not* pursuit — an enemy you duck behind cover still
  hunts you
- The **`Idle → Chase → Attack`** state machine, and how `target`, range, and LoS pick the state
- Steering a **kinematic body** toward the player with `MoveKinematic`, and facing it down its movement
  direction with `faceDir`
- Landing a **melee attack** — `applyDamage` on a cooldown, feeding the player's flash/pain/knockback,
  with the gauntlet sound
- The design decision of **kinematic-steer locomotion for v1**, its wall-clipping tradeoff, and the
  `monster_ai` headless scenario that pins the whole behaviour down

---

## Where We Are

Chapter 22 built the grunt as a *thing*: a solid, coloured box that stands where you spawn it, blocks
your path, blinks white when shot, and dies with a sound. It was deliberately inert. The `AIState`
component was already carrying an unused `state`, a `target`, and an `attackCooldown`, and the grunt
already had a **kinematic Jolt body** — the same body type a lift uses — sitting there waiting to be
*driven* rather than parked.

This chapter fills those fields in. We write one new system, `aiSystem`, that turns the passive dummy
into an enemy that notices you, walks toward you, and hits you when it gets close. By the end the grunt
will:

1. **Sense** the player — acquire aggro on line-of-sight within detect range, and hold it until you
   flee out of pursue range.
2. **Chase** — steer its kinematic body toward you and turn to face the way it's moving.
3. **Attack** — melee you on a cooldown once it's in range, driving the exact same
   flash/pain/knockback feedback your own weapons already produce.

We build it in the order that makes each piece stand on the last: first *where* the system runs in the
tick, then *what it senses*, then the *state machine* those senses drive, and finally the two actions —
chase and attack — the states select.

One thing we deliberately **do not** teach here: the grunt's Chase state follows a *route* of
waypoints, and those waypoints come from A\* pathfinding over a navigation grid. That machinery
(`NavGrid`, `findPath`, `buildNavGrid`, the `AIPath` waypoint list) is a whole chapter of its own —
**Chapter 24**. In this chapter the path is a black box: Chase asks for a direction to steer, and we
treat "which direction" as already answered. Everything else about the behaviour is here.

---

## Step 1: Where the AI Runs in the Tick

Before a single line of behaviour, decide *when* it runs. The grunt moves by driving a kinematic body,
and Chapter 15 taught the hard-won rule for kinematic bodies: you must set their target **before** the
physics step that sweeps them, or the player resolves collision against last tick's position and the
world visibly desyncs. Movers (lifts, doors) already obey this — `moverSyncSystem` pushes their targets
just before `joltWorld.step()`. The AI is no different: it's another producer of kinematic targets, so
it belongs in the same slot.

Here is the tick order in `stepSimulation` (`src/engine/app/simulation.cpp`), with the new line:

```cpp
    void stepSimulation(entt::registry& registry, JoltWorld& joltWorld, const Level& level, float dt)
    {
        // Tick order (eval 05 §2 / 08 §8.1 fix):
        // Movers animate and their kinematic bodies are swept by the physics
        // step BEFORE the player's CharacterVirtual ExtendedUpdate, so the
        // player resolves collision against the lift's CURRENT position rather
        // than its previous-tick position. This removes the boarding desync.
        weaponSwitchSystem(registry);
        moverSystem(registry);          // animate doors/lifts
        moverSyncSystem(registry);      // push mover targets to Jolt
        aiSystem(registry, level);      // enemies sense/chase/attack → MoveKinematic
        joltWorld.step(dt);             // sweep kinematic + dynamic bodies
        joltSyncSystem(registry);       // read body transforms back to ECS
        playerCharacterSystem(registry);// player resolves against moved world
        combatSystem(registry, level);
        lifetimeSystem(registry);
        triggerSystem(registry);
        pickupSystem(registry);         // grant + consume items on touch
        playerDeathSystem(registry);
        enemyDeathSystem(registry);     // remove grunts whose health hit 0
        demoResetSystem(registry);
    }
```

`aiSystem(registry, level)` sits immediately after `moverSyncSystem` and immediately before
`joltWorld.step()`. That placement buys two things at once:

- The grunt's `MoveKinematic` target, set inside `aiSystem`, is swept by the very next call —
  `joltWorld.step(dt)` — so the grunt physically moves *this* tick, and the player's
  `playerCharacterSystem` (which runs after the step) resolves against the grunt's new position. No
  one-tick lag on the *movement* side; the grunt is exactly as solid-while-moving as a lift.
- The attack half feeds `applyDamage`, and `enemyDeathSystem` runs later in the same tick — so if an
  attack were ever to reduce a *shared* health pool, the ordering is already sane.

Add the include at the top of `simulation.cpp` alongside the other enemy system:

```cpp
#include "engine/ecs/systems/enemy/ai_system.h"
#include "engine/ecs/systems/enemy/enemy_death_system.h"
```

> **Why read the player's position at the top of the AI tick, when that position is from *last*
> tick?** `aiSystem` runs before `playerCharacterSystem`, so the `Position` it reads for the player is
> the value written by *last* tick's physics — the player hasn't moved yet this frame. That's a genuine
> one-tick lag on *sensing*: the grunt steers toward where you were 1/60th of a second ago. It is
> completely invisible. At 60 Hz a sprinting player moves a couple of centimetres per tick, far less
> than the grunt's own step, its collider radius, or the half-metre waypoint tolerance. Trying to
> eliminate the lag — running the AI *after* the player moves — would put the grunt's `MoveKinematic`
> target *after* `joltWorld.step()`, which reintroduces exactly the kinematic desync the whole tick
> order exists to avoid. A one-tick sensing lag is the correct trade for a zero-tick movement lag.

The declaration in `src/engine/ecs/systems/enemy/ai_system.h` states that contract plainly:

```cpp
#pragma once

#include <entt/entt.hpp>

struct Level;

// Enemy behaviour: sense the player (detect range + line-of-sight), chase by
// steering the kinematic body toward them, and melee-attack in range. Runs
// BEFORE the physics step so the grunt's MoveKinematic target is swept this
// tick. Death and hit/death feedback are handled by enemyDeathSystem.
void aiSystem(entt::registry& registry, const Level& level);
```

Note it takes `const Level&` as well as the registry — the AI needs the level geometry to test line of
sight against walls, which is Step 3.

---

## Step 2: The Tuning Constants and Finding the Player

Create `src/engine/ecs/systems/enemy/ai_system.cpp`. Everything the behaviour is tuned by lives in one
anonymous-namespace block of constants at the top, so the numbers are all in one place:

```cpp
namespace
{
    constexpr float kDetectRange  = 20.0f;  // acquire the player within this (needs LoS)
    constexpr float kPursueRange  = 28.0f;  // once aggroed, chase until beyond this
    constexpr float kAttackRange  = 2.2f;
    constexpr float kMoveSpeed    = 3.0f;   // units / second
    constexpr float kAttackDamage = 8.0f;
    constexpr float kAttackPeriod = 1.0f;   // seconds between hits
    constexpr float kEyeOffset    = 0.6f;
    constexpr float kRepathPeriod = 0.4f;   // seconds between A* recomputes
    constexpr float kWaypointHit  = 0.5f;   // distance at which a waypoint is "reached"
    constexpr int   kRepathBudget = 4;      // A* recomputes allowed per tick (stagger enemies)
}
```

Three of these — `kRepathPeriod`, `kWaypointHit`, `kRepathBudget` — belong to the pathfinding this
chapter treats as a black box; we'll meet them properly in Chapter 24. The ones that matter here are
the ranges (`kDetectRange`, `kPursueRange`, `kAttackRange`), the locomotion speed (`kMoveSpeed`), and
the melee (`kAttackDamage`, `kAttackPeriod`). `kEyeOffset` is how far above the grunt's origin its
"eye" sits for the line-of-sight test.

The system opens by pulling what it needs from the registry context and finding the player once:

```cpp
void aiSystem(entt::registry& registry, const Level& level)
{
    const float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;
    auto& bodyInterface = registry.ctx().get<JoltWorld>().getBodyInterface();
    const NavGrid* nav = registry.ctx().find<NavGrid>();

    entt::entity player = entt::null;
    glm::vec3 playerPos(0.0f);
    for (auto [e, pos] : registry.view<Position, TagPlayer>().each())
    { player = e; playerPos = pos.value; break; }
    if (player == entt::null) return;

    int repathBudget = kRepathBudget;

    for (auto [entity, ai, pos, body, path] : registry.view<AIState, Position, JoltBody, AIPath>().each())
    {
```

`dt` is the fixed timestep (all our systems step at a fixed rate). `bodyInterface` is the handle we use
to drive the kinematic body. `nav` is the navigation grid — a `find` (not `get`) because it may be
absent, and it's the pathfinding black box we ignore until Chapter 24. We locate the single player
once, cache its `Position`, and bail early if there is no player.

The per-enemy loop views `<AIState, Position, JoltBody, AIPath>`. `AIState` is the enemy marker and
holds the behaviour state; `Position` is where it is; `JoltBody` is the kinematic body id we steer;
`AIPath` is the waypoint list the Chase step follows. That last component means the grunt archetype
from Chapter 22 needs one extra line — in `src/engine/level/spawn_monster.cpp`, right after the
`AIState`:

```cpp
        reg.emplace<AIState>(e);
        reg.emplace<AIPath>(e);
```

Without an `AIPath` a grunt would fall out of the view entirely and never think. Its *contents* are
pathfinding's business (Chapter 24); all this chapter needs is that the component exists so the enemy
is picked up.

> **Why is `nav` a `ctx().find<NavGrid>()` (nullable) but `PhysicsConfig` and `JoltWorld` are
> `ctx().get<>()` (asserted present)?** Physics and the fixed timestep are foundational — if they're
> missing the whole simulation is broken, so a hard `get` that aborts is the honest failure. The
> navigation grid, by contrast, is an *optional accelerant*: a level with no baked grid should still
> run, with enemies that steer straight at you rather than routing around walls. Making `nav` nullable
> lets the Chase code degrade gracefully to naive steering (Step 5) instead of crashing, which is
> exactly the fallback we want while the pathfinding of the next chapter is being brought up.

---

## Step 3: Line of Sight

Everything the grunt "sees" runs through one helper, `clearLineOfSight`. It answers a single question:
is there an unobstructed straight line from point A to point B, ignoring the grunt itself and the
player? It lives in the same anonymous namespace:

```cpp
    bool clearLineOfSight(entt::registry& reg, const Level& level, entt::entity self,
                          entt::entity player, glm::vec3 from, glm::vec3 to)
    {
        glm::vec3 delta = to - from;
        float dist = glm::length(delta);
        if (dist < 0.001f) return true;
        Ray ray{ from, delta / dist };

        for (const auto& sector : level.sectors)
            for (const auto& s : sector.surfaces)
            {
                AABB box;
                box.min = glm::min(glm::min(s.vertices[0], s.vertices[1]),
                                   glm::min(s.vertices[2], s.vertices[3])) - glm::vec3(0.05f);
                box.max = glm::max(glm::max(s.vertices[0], s.vertices[1]),
                                   glm::max(s.vertices[2], s.vertices[3])) + glm::vec3(0.05f);
                auto hit = rayIntersectionsAABB(ray, box);
                if (hit && *hit < dist - 0.1f) return false;
            }

        auto hit = raycastEntities(reg, ray, self, dist);
        return !(hit && hit->entity != player && hit->distance < dist - 0.1f);
    }
```

The helper builds a `Ray` from `from` toward `to`, of length `dist`, then checks two families of
occluders:

1. **Level walls.** It walks every surface of every sector, wraps each surface's four vertices in a
   tight `AABB` (padded 0.05 so grazing rays still count), and intersects the ray with it via
   `rayIntersectionsAABB`. If any wall is hit *before* the target (`*hit < dist - 0.1f`), the line is
   blocked — return `false`. The `- 0.1f` slack keeps a surface *at* the target from counting as a
   blocker.
2. **Solid props.** It reuses `raycastEntities` from `combat_internal.h` — the very same entity ray
   test your hitscan weapons fire through — passing `self` as the entity to ignore. If it hits
   *something*, and that something is **not the player**, and it's closer than the target, the line is
   blocked.

That final line is the crucial one: `hit->entity != player`. The player must never block their own
line of sight — otherwise the grunt could never "see" you, because you'd occlude yourself. Passing
`self` as the ignore argument does the same job for the grunt: it can't block its own view. So the LoS
is clear unless a *wall* or a *third solid entity* stands between the grunt and the player.

We use `clearLineOfSight` in exactly two roles, and it's worth being precise about them up front:

- It **gates acquisition** — a grunt only notices you if it can see you (Step 4).
- It **gates the attack** — a grunt only swings if it has a clear shot (Step 6).
- It does **not gate pursuit** — once aggroed, a grunt keeps chasing even when you break the line of
  sight by ducking behind cover (Step 5). Losing sight of you doesn't make it forget you.

> **Why reuse `raycastEntities` from the combat module for enemy vision, rather than writing a
> bespoke sight query?** Because "is there a solid thing between these two points" is the exact
> question a bullet asks, and combat already answers it correctly against `AABBCollider`. Reusing it
> means the grunt's eyes and the player's gun agree about what's solid — if a crate blocks your shot,
> it blocks the grunt's view, with no chance of the two drifting apart as colliders change. It also
> keeps the vision test honest about props: anything the level spawns with a solid collider becomes
> cover for free, the same way it became a bullet-stop for free in Chapter 22. The only thing the sight
> test adds on top of the combat ray is the wall sweep and the two ignore rules (self and player),
> because unlike a bullet, a *line of sight* must pass through both endpoints.

A second tiny helper turns the grunt to face a direction — we'll use it whenever the grunt moves or
attacks:

```cpp
    void faceDir(entt::registry& reg, entt::entity e, glm::vec3 d)
    {
        if (glm::length(d) < 0.001f) return;
        if (auto* rot = reg.try_get<Rotation>(e))
            rot->euler.y = glm::degrees(std::atan2(d.x, d.z));
    }
```

It writes the entity's yaw (`euler.y`) from a horizontal direction using `atan2(d.x, d.z)`, and does
nothing for a zero-length direction. Purely cosmetic — it doesn't affect physics — but it's what makes
the grunt look *at* you instead of sliding sideways like a hockey puck.

---

## Step 4: Sensing and the State Machine

With the senses in place, the per-enemy loop body is a compact state machine. Start by computing the
geometry to the player and the line of sight, at the top of the loop:

```cpp
        auto haltInPlace = [&] {
            bodyInterface.MoveKinematic(body.id,
                JPH::RVec3(pos.value.x, pos.value.y, pos.value.z), JPH::Quat::sIdentity(), dt);
        };

        glm::vec3 flat = playerPos - pos.value; flat.y = 0.0f;
        float dist = glm::length(flat);
        glm::vec3 toPlayer = dist > 0.001f ? flat / dist : glm::vec3(0.0f);
        bool los = clearLineOfSight(registry, level, entity, player,
                                    pos.value + glm::vec3(0.0f, kEyeOffset, 0.0f),
                                    playerPos + glm::vec3(0.0f, 0.4f, 0.0f));
```

`flat` is the horizontal vector to the player (`y` zeroed — grunts don't fly or aim up), `dist` its
length, `toPlayer` its normalised direction. `los` runs the sight test from the grunt's *eye*
(`pos + kEyeOffset` up) to the player's *chest* (`playerPos + 0.4` up), so a crouch-height crate breaks
the line the way you'd expect.

`haltInPlace` is a lambda that drives the kinematic body to *its own current position* — a
`MoveKinematic` with zero displacement. A kinematic body must be told a target every tick it exists, or
Jolt keeps sweeping it toward its last commanded target; "stand still" is spelled "move to where I
already am".

Now the aggro latch. `AIState.target` does double duty: it's both *who to attack* and the boolean *am I
aggroed*. Null means unaware; set to the player means hunting.

```cpp
        // ─── Aggro: acquire on sight, drop when the player escapes ───
        if (ai.target == entt::null)
        {
            if (los && dist < kDetectRange) ai.target = player;
        }
        else if (dist > kPursueRange)
        {
            ai.target = entt::null;
        }

        if (ai.target == entt::null)
        {
            ai.state = AIStateKind::Idle;
            path.waypoints.clear();
            haltInPlace();
            continue;
        }
```

Read the two ranges carefully, because they're deliberately different:

- **Acquire** (`kDetectRange = 20`) requires *both* line of sight *and* proximity. A grunt only wakes
  up if it can see you and you're within 20 units.
- **Drop** (`kPursueRange = 28`) is purely distance, and *larger*. Once aggroed, the grunt only gives
  up when you get more than 28 units away — line of sight is irrelevant to *keeping* aggro.

That gap between 20 (acquire) and 28 (drop) is **hysteresis**. If a single threshold governed both,
a player hovering right at the edge of detection would make the grunt flicker awake-and-asleep every
few ticks. The wider drop-range means: hard to notice you (needs sight, needs 20 units), but once it
has, it commits and won't lose interest until you've clearly fled. If `target` ends up null — never
acquired, or just dropped — the grunt is `Idle`: clear any stale path, `haltInPlace`, and `continue`
to the next enemy.

> **Why let `target` double as the aggro flag instead of adding a separate `bool aggroed`?** Because
> the two facts are never independent: a grunt is aggroed *if and only if* it has a target to chase.
> Encoding that as one nullable `entt::entity` makes an illegal state — "aggroed but no target", or
> "has a target but not aggroed" — literally unrepresentable, and it's the field Chapter 22 already put
> on `AIState` in anticipation. Every consumer just asks `ai.target == entt::null`, and the two-branch
> latch above is the entire aggro lifecycle: acquire in the first branch, release in the second. Fewer
> fields, no way for them to disagree.

---

## Step 5: Chase — Steering the Kinematic Body

If the grunt has a target but isn't yet in range for a swing, it's in `Chase`. First the state and the
range test that would instead choose `Attack` (covered in Step 6) — but let's follow the Chase branch,
which runs when the grunt is *not* in attack range:

```cpp
        // ─── Chase: path toward the player, routing around obstacles ─
        ai.state = AIStateKind::Chase;
        path.repathTimer -= dt;
        bool atEnd = path.index >= path.waypoints.size();
        if (nav && repathBudget > 0 && (path.waypoints.empty() || atEnd || path.repathTimer <= 0.0f))
        {
            path.waypoints = findPath(*nav, pos.value, playerPos);
            path.index = 0;
            path.repathTimer = kRepathPeriod;
            --repathBudget;
        }

        // Advance the follow cursor, then steer to the current waypoint (or, with
        // no path, straight at the player).
        glm::vec3 stepDir = toPlayer;
        if (path.index < path.waypoints.size())
        {
            glm::vec3 toWp = path.waypoints[path.index] - pos.value; toWp.y = 0.0f;
            if (glm::length(toWp) < kWaypointHit) ++path.index;
            if (path.index < path.waypoints.size())
            {
                toWp = path.waypoints[path.index] - pos.value; toWp.y = 0.0f;
                if (glm::length(toWp) > 0.001f) stepDir = glm::normalize(toWp);
            }
        }

        glm::vec3 target = pos.value + stepDir * kMoveSpeed * dt;
        bodyInterface.MoveKinematic(body.id,
            JPH::RVec3(target.x, target.y, target.z), JPH::Quat::sIdentity(), dt);
        faceDir(registry, entity, stepDir);
```

For this chapter, split that block into two parts:

**The black box (Chapter 24).** The `findPath(*nav, ...)` call and the waypoint-follow logic decide
*which direction* to steer. It asks A\* over the navigation grid for a route to the player, follows
those waypoints in turn (advancing `path.index` as each is reached within `kWaypointHit`), and produces
`stepDir` — a unit direction to walk this tick. Do not worry about how the route is computed here; that
is entirely Chapter 24's subject. The one thing to note is the **fallback**: `stepDir` is *initialised*
to `toPlayer` (straight at the player), and the path logic only overrides it when there's a valid
waypoint. So with no navigation grid, or before the first path is computed, the grunt simply steers
directly at you.

**The locomotion (this chapter).** Once `stepDir` is chosen, the movement is three lines:

- `target = pos.value + stepDir * kMoveSpeed * dt` — take a `kMoveSpeed` (3 units/s) step in that
  direction.
- `bodyInterface.MoveKinematic(body.id, ...)` — command the kinematic body to that target, swept by
  the physics step that runs next tick-line (Step 1). Because it's kinematic, Jolt slides it there and
  it shoves the player if it walks into them — but it is never itself pushed off course.
- `faceDir(registry, entity, stepDir)` — turn the grunt to look the way it's walking.

That's the whole of chasing: pick a direction, step the kinematic target, face it.

> **Why drive the grunt with `MoveKinematic` steering — a hand-rolled velocity — instead of giving it a
> `CharacterVirtual` like the player?** This is the deliberate v1 tradeoff. A `CharacterVirtual` is the
> "correct" locomotion for something that walks: it does swept collision, slides along walls, steps up
> ledges, and respects gravity, all per tick. But it is heavy — every enemy would need its own
> `ExtendedUpdate`, ground detection, and movement tuning — and none of that machinery is needed to get
> a grunt that walks at you across open floor. Kinematic steering is dramatically simpler: set a target,
> let Jolt sweep the body, done. It reuses the *exact* body type and drive call the movers already use,
> so it drops straight into the existing tick order with no new physics code. **The cost:** a kinematic
> body steered *straight* at the player will happily try to walk *through* a wall — it has no swept
> collision response of its own, it just goes where told. That's precisely the limitation that motivates
> the next chapter: the A\* path exists to feed `stepDir` a route that *goes around* walls, so the
> naive straight-line steer is only ever used in open space where it can't clip. Kinematic steering plus
> pathfinding gets us believable movement without the weight of a full character controller per enemy.

---

## Step 6: Attack — Melee on a Cooldown

Between "aggroed" and "chasing" sits the attack. Once the grunt has a target, before choosing Chase, it
counts down its cooldown and checks whether it's close enough with a clear shot:

```cpp
        if (ai.attackCooldown > 0.0f)
            ai.attackCooldown = std::max(0.0f, ai.attackCooldown - dt);

        // ─── Attack: in range with a clear shot ──────────────────
        if (los && dist <= kAttackRange)
        {
            ai.state = AIStateKind::Attack;
            path.waypoints.clear();
            faceDir(registry, entity, toPlayer);
            haltInPlace();
            if (ai.attackCooldown <= 0.0f && applyDamage(registry, player, kAttackDamage))
            {
                queueSoundAt(registry, "weapon.gauntlet", pos.value);
                ai.attackCooldown = kAttackPeriod;
            }
            continue;
        }
```

The attack fires only when **both** conditions hold: the player is within `kAttackRange` (2.2 units)
**and** there's a clear line of sight. In the `Attack` state the grunt stops moving (`haltInPlace`,
clear the path), turns to face the player (`faceDir(toPlayer)`), and — if its cooldown has expired —
lands a hit.

The hit itself is one call: `applyDamage(registry, player, kAttackDamage)`. This is the *same*
`applyDamage` your weapons call, so a grunt's melee gets the player's damage feedback for free — the
pain voice, the red hurt-flash, any knockback — exactly as if the player had been shot. `applyDamage`
returns a `bool` for whether the hit actually landed (it can fail if the target has no `Health` or is
invulnerable), and we only spend the cooldown and play the sound if it did: `weapon.gauntlet`, played
positionally at the grunt via `queueSoundAt` so it's quieter from across the room. Set
`attackCooldown = kAttackPeriod` (1 second) and `continue`; the countdown at the top of the loop drains
it back to zero over the next second, pacing the grunt to one swing per second.

> **Why guard the cooldown reset and sound behind `applyDamage(...)` returning true, rather than always
> resetting the cooldown when in range?** Because a swing that connects and a swing that whiffs on an
> invulnerable or dead target are different events, and only the connecting one should cost the grunt
> its attack tempo and play the impact sound. Threading the `applyDamage` return through the `&&` means
> the gauntlet *thunk* and the one-second recovery are tied to an actual hit landing, not merely to the
> grunt being close. It also means that if the player becomes momentarily invulnerable (say, just after
> a respawn), the grunt keeps *trying* every tick — cooldown never spent — and connects the instant the
> invulnerability lapses, instead of wasting a full second of cooldown on an ineffective swing.

The three states are now complete. To see them as one machine, read the branches in the order the code
evaluates them each tick:

1. **No target?** → `Idle`. Halt, clear path, done. (Step 4)
2. **In range with LoS?** → `Attack`. Halt, face, swing on cooldown. (Step 6)
3. **Otherwise** → `Chase`. Steer the kinematic body along the path toward the player. (Step 5)

`target`, `dist`, and `los` are the only inputs, and each tick lands the grunt in exactly one state.

---

## Step 7: The Headless Scenario

The behaviour is exactly the kind of thing that rots silently — a tick-order shuffle, a flipped range
comparison, a broken sight test, and the grunt either never wakes or never sleeps, with nothing on
screen obviously wrong. So it gets a headless scenario that pins all three phases down. In
`src/harness/headless_main.cpp`:

```cpp
    // Enemy behaviour: no line-of-sight → stays put; open LoS → chases and
    // attacks (the player takes damage).
    bool scenario_monster_ai(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        // The front grunt (lowest z) is at a known spawn (13, .95, 8).
        entt::entity grunt = entt::null; float bestZ = 1e9f;
        for (auto [e, ai, pos] : reg.view<AIState, Position>().each())
            if (pos.value.z < bestZ) { bestZ = pos.value.z; grunt = e; }
        if (grunt == entt::null) return report("monster_ai", false, "no grunt");

        glm::vec3 gpos = reg.get<Position>(grunt).value;
        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // ── Blocked LoS: hide behind the shelf; grunt must stay Idle + not move.
        teleportPlayer(reg, player, glm::vec3(24.0f, halfY + 0.05f, 5.0f));
        for (int i = 0; i < 60; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool idleHidden = reg.get<AIState>(grunt).state == AIStateKind::Idle;
        float movedHidden = glm::length(reg.get<Position>(grunt).value - gpos);

        // ── Open LoS: stand ~10 units in front; grunt should close in.
        glm::vec3 seen = gpos + glm::vec3(0.0f, 0.0f, 10.0f); seen.y = halfY + 0.05f;
        teleportPlayer(reg, player, seen);
        float distStart = glm::length(reg.get<Position>(grunt).value - reg.get<Position>(player).value);
        bool sawChase = false;
        for (int i = 0; i < 150; i++)
        {
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
            if (reg.valid(grunt) && reg.get<AIState>(grunt).state == AIStateKind::Chase) sawChase = true;
        }
        float distChase = glm::length(reg.get<Position>(grunt).value - reg.get<Position>(player).value);
        bool chased = sawChase && distChase < distStart - 1.0f;

        // ── Attack: keep still; the player should take damage.
        float hpStart = reg.get<Health>(player).current;
        for (int i = 0; i < 240; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool attacked = reg.get<Health>(player).current < hpStart;

        char buf[240];
        std::snprintf(buf, sizeof(buf),
            "hidden idle=%d moved=%.2f; dist %.1f->%.1f chase=%d; attack=%d",
            idleHidden ? 1 : 0, movedHidden, distStart, distChase, chased ? 1 : 0, attacked ? 1 : 0);
        return report("monster_ai", idleHidden && movedHidden < 0.3f && chased && attacked, buf);
    }
```

It picks the front grunt (lowest `z`, the one spawned at `(13, 0.95, 8)`) and drives three phases:

- **Blocked LoS.** Teleport the player to `(24, _, 5)` — around a shelf, out of the grunt's sight —
  and tick 60 frames with no input. Assert the grunt is still `Idle` and has moved less than 0.3
  units. This is Step 3's sight test and Step 4's acquisition gate working: no line of sight means no
  aggro, means no movement.
- **Open LoS.** Teleport the player ~10 units straight in front, in clear view, and tick 150 frames.
  Assert the grunt entered `Chase` at some point (`sawChase`) *and* actually closed the gap by more
  than a metre (`distChase < distStart - 1.0f`). This is acquisition plus Step 5's chase locomotion.
- **Attack.** Keep still for another 240 frames and assert the player's `Health` dropped. This is Step
  6 — the grunt reached attack range and landed melee hits.

The scenario passes only if `idleHidden && movedHidden < 0.3f && chased && attacked` — every phase.
Register it in `main`'s dispatch alongside the others:

```cpp
    else if (scenario == "monster_ai")       pass = scenario_monster_ai(registry, jolt, level, dt);
```

> **Why assert on both "entered Chase" *and* "the distance actually shrank", rather than just checking
> the state flag?** Because the state and the outcome can fail independently, and each catches a
> different class of bug. Checking only `state == Chase` would pass even if the grunt got *stuck* on
> geometry and never moved — the flag says "chasing" while the body sits still. Checking only that the
> distance shrank could pass by accident if the *player* happened to drift. Requiring both — it
> declared Chase *and* it measurably closed in — pins the contract that "Chase" means real forward
> progress toward the player, which is exactly the invariant a future physics or pathfinding change
> could break without touching the state machine at all.

---

## Step 8: CMake

The one new translation unit joins the `qengine_lib` target in `CMakeLists.txt`:

```cmake
	src/engine/ecs/systems/enemy/ai_system.cpp
	src/engine/ecs/systems/enemy/enemy_death_system.cpp
```

`ai_system.h`, the `AIPath` addition to `gameplay.h`, and the tick-order edit in `simulation.cpp` are
all header/inline changes that compile into their includers, so they need no CMake entry of their own.
(The build also grows two pathfinding files — `ai/build_nav_grid.cpp` and `ai/find_path.cpp` — but
those belong to Chapter 24 and are the black box this chapter deliberately doesn't open.)

---

## What Changed — Summary

| File | Change |
|------|--------|
| `ecs/systems/enemy/ai_system.h` | **New** — declares `aiSystem(registry, level)`, the sense/chase/attack behaviour. |
| `ecs/systems/enemy/ai_system.cpp` | **New** — tuning constants, `clearLineOfSight` + `faceDir` helpers, and the `Idle → Chase → Attack` state machine driving the kinematic body via `MoveKinematic`. |
| `app/simulation.cpp` | Call `aiSystem(registry, level)` after `moverSyncSystem`, before `joltWorld.step()`; include `ai_system.h`. |
| `ecs/components/gameplay.h` | `AIState.target` comment now reads "null = not aggroed"; `AIPath` present as the enemy's waypoint list. |
| `level/spawn_monster.cpp` | Grunt archetype gains `reg.emplace<AIPath>(e)` so enemies are picked up by the AI view. |
| `harness/headless_main.cpp` | **New** `scenario_monster_ai` — blocked LoS → Idle+still, open LoS → chase, in range → attacks the player; registered in the dispatch. |
| `CMakeLists.txt` | Add `ai_system.cpp` to `qengine_lib`. |

---

## What You Should See

Run `build/QEngine.exe`:

1. **The grunts wake up.** Walk into a grunt's view within ~20 units and it turns toward you and starts
   moving — no longer the passive pillar of Chapter 22.
2. **They chase you.** A grunt steers across the open floor at a steady walk, facing the way it moves.
   Duck behind cover and it *keeps coming* — breaking line of sight doesn't shake it while you're still
   within pursue range.
3. **They hit you.** Let one close to melee range and it stops, faces you, and swings roughly once a
   second — each hit thunks the gauntlet sound, flashes your screen with pain, and chips your health,
   the same feedback your own weapons produce.
4. **They give up if you flee.** Sprint more than ~28 units away and the grunt drops aggro, returns to
   `Idle`, and stands still again.
5. **Straight-line steering clips walls.** If a grunt has an unobstructed sightline but a wall between
   you, you may see it try to walk *into* the wall — the kinematic body has no collision response of its
   own. That's the limitation the next chapter fixes.
6. **The headless harness passes `monster_ai`** — silently, no window — asserting Idle-while-hidden,
   chase-on-sight, and damage-in-range all hold.

---

## What's Next

The grunt now senses, chases, and attacks — a real enemy. But Step 5 left a visible seam: steered
*straight* at you, its kinematic body walks into walls, because a kinematic body only goes where it's
told and has no swept collision response of its own. The Chase code already anticipates the fix — it
initialises `stepDir` to the straight-line direction but *overrides* it with a waypoint whenever a path
is available. **Chapter 24** opens the black box we've been steering around all chapter: building a
navigation grid from the level (`buildNavGrid`), running A\* over it (`findPath`) to produce the
`AIPath` waypoints, and the repath budget that staggers recomputes across many enemies — turning the
naive straight-line chase into a grunt that routes *around* the geometry to reach you.
