# Chapter 31a: Enemies That Don't Clip Walls — CharacterVirtual Locomotion

## What You'll Learn
- Why an enemy driven by a **kinematic Jolt body** and `MoveKinematic` clips *through* walls when the A*
  path cuts a corner — the difference between *teleporting a body to a target* and *sweeping a character
  against the world*
- How to give an enemy the same collided locomotion the player already has: a **`CharacterVirtual`** built
  by a new one-time pass, `initEnemyCharacters`, mirroring the player's `initPlayerCharacter`
- The one thing an enemy needs that the player doesn't — a physical **inner body** (Jolt 5.2's
  `mInnerBodyShape` / `mInnerBodyLayer`) on `Layers::MOVING` — so the character *itself* moves by sweeping
  yet still **blocks the player** and separates from other enemies
- Driving that character each tick with **`ExtendedUpdate`** (gravity + stick-to-floor + walk-stairs) via a
  small `aiStepCharacter` helper, and why that update is what stops the corner-clip
- Re-homing ownership of the enemy's `Position`: `aiSystem` now *writes* it (the character is authoritative),
  and `joltSyncSystem` **skips enemies** automatically because they no longer carry a `JoltBody`
- Splitting `ai_system.cpp` into focused helpers (`ai_step_character`, `ai_line_of_sight`, `ai_fire_bolt`
  behind `ai_support.h`) to respect the one-function-per-file + size-cap conventions from Chapter 17
- Why the enemy's body needs **no manual teardown** — the `CharacterVirtual` destructor drops its own inner
  body — and how the headless harness's teleport/clear helpers change to match

---

## Where We Are

Chapters 22–24 built the enemy up in three passes: a shootable grunt with hit feedback (22), a sense →
chase → melee state machine (23), and A* pathfinding over a nav grid so the grunt routes around walls
instead of walking into them (24). That grunt moved by being a **kinematic Jolt body**: each tick `aiSystem`
computed a step target and called `bodyInterface.MoveKinematic(...)` to slide the body toward it.

That worked until the path cut a corner. `MoveKinematic` doesn't *sweep* a body through the world resolving
collisions — it sets the body's velocity so it *arrives* at the target next step. When the A* path threads
close past a wall corner (octile A* on a grid legitimately produces diagonal, corner-hugging routes), the
body's straight-line slide between two waypoints can pass **through** the wall's edge. The player watched
grunts clip the shelf on `monster_path`, half-disappearing into geometry they should have squeezed around.

This chapter fixes locomotion at the root: enemies stop being kinematic bodies and become
**`CharacterVirtual`s**, exactly like the player. A `CharacterVirtual` *sweeps* against the world every
update — it collides, slides along walls, steps up seams, sticks to floors — so a corner-cut path no longer
punches through geometry. The only wrinkle is that the player's character deliberately has *no* physical
presence (nothing needs to collide *with* the player), whereas an enemy must still block the player and push
apart from other enemies. Jolt 5.2 solves that with an **inner body**, and that inner body is the heart of
this chapter.

Everything below is grounded in the files changed this session:
`src/engine/ecs/systems/enemy/init_enemy_characters.{h,cpp}`, `ai_step_character.cpp`, `ai_support.h`,
`ai_system.{h,cpp}`, `enemy_death_system.cpp`, `src/engine/app/simulation.cpp`, `src/engine/level/factories.h`,
and `src/harness/headless_main.cpp`.

---

## Step 1: The Symptom — MoveKinematic Slides *Through* Corners

The Chapter 24 grunt was a kinematic body. Its locomotion, in the old `ai_system.cpp`, was a `MoveKinematic`
call each tick:

```cpp
    for (auto [entity, ai, pos, body, path] : registry.view<AIState, Position, JoltBody, AIPath>().each())
    {
        // …
        glm::vec3 target = pos.value + stepDir * kMoveSpeed * dt;
        bodyInterface.MoveKinematic(body.id, /* to `target` over dt */ …);
```

`MoveKinematic` is a *promise about position*: "be at `target` after `dt`." Jolt computes the velocity that
gets the body there and integrates it — but a kinematic body is authoritative. It does **not** stop when it
meets a wall; it pushes dynamic things out of its way and keeps going. So when the follow cursor advances to
a waypoint whose straight line clips a wall corner, the body slides right across the corner and momentarily
overlaps the wall. On `monster_path` — a grunt routing past a shelf that spans `x[18,22] z[3,7]` — the grunt
visibly cut *into* the shelf on the diagonal.

The player's character never has this problem, because the player isn't a kinematic body: it's a
`CharacterVirtual`, and a `CharacterVirtual` moves by **sweeping**. Give it a desired velocity and it walks
that velocity *against the collision world*, resolving contacts as it goes — sliding along a wall it hits
rather than passing through it. The fix, then, is to make the enemy locomote the same way.

> **Why not just fix the path so it never cuts corners?** Because corner-cutting is a *feature* of a good
> grid path — an 8-connected A* that refuses all diagonals produces stair-stepped, obviously-robotic routes,
> and one that allows diagonals will always hug corners to stay short. Chapter 24's `findPath` already
> forbids cutting *across* a blocked cell's corner, but a path can legitimately run a diagonal that grazes a
> wall's outside edge within the grunt's body radius. The real defect isn't the path — it's that the
> *locomotion* trusted the path's centre-line absolutely and never checked it against geometry. A swept
> character checks every tick, so it tolerates a path that skims a corner: it simply slides along the wall
> for a frame instead of tunnelling. Fixing locomotion is more general than fixing the path, and it's the
> item the pathfinding plan explicitly deferred as "most likely to bite."

---

## Step 2: The Data — an Enemy `CharacterVirtual` With an Inner Body

An enemy already carries a `JoltCharacter` slot once we build one — the same component the player uses,
from `src/engine/ecs/components/physics.h`:

```cpp
struct JoltCharacter
{
	JPH::Ref<JPH::CharacterVirtual> character;
};
```

The new work is a one-time setup pass that fills that slot for every enemy. It lives in its own file,
`src/engine/ecs/systems/enemy/init_enemy_characters.cpp`, and reads the enemy's existing `AABBCollider` to
size a capsule:

```cpp
void initEnemyCharacters(entt::registry& registry)
{
    auto& jolt = registry.ctx().get<JoltWorld>();

    auto view = registry.view<Position, AABBCollider, AIState>(entt::exclude<JoltCharacter>);
    for (auto [entity, pos, col, ai] : view.each())
    {
        (void)ai;   // view filter only — enemies are the AIState entities
        // Capsule from the collider's half-extents (radius + the two caps).
        float radius     = col.halfExtents.x;
        float halfHeight = col.halfExtents.y - radius;
        if (halfHeight < 0.01f) halfHeight = 0.01f;

        JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(halfHeight, radius);

        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape                 = capsule;
        settings->mMaxSlopeAngle         = JPH::DegreesToRadians(50.0f);
        settings->mMaxStrength           = 100.0f;
        settings->mMass                  = 70.0f;
        settings->mPredictiveContactDistance = 0.1f;
        settings->mInnerBodyShape        = capsule;         // physical presence...
        settings->mInnerBodyLayer        = Layers::MOVING;  // ...that blocks the player

        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            settings,
            JPH::RVec3(pos.value.x, pos.value.y, pos.value.z),
            JPH::Quat::sIdentity(),
            0,
            jolt.physicsSystem.get());

        registry.emplace<JoltCharacter>(entity, character);
    }
}
```

Read the view filter first: `registry.view<Position, AABBCollider, AIState>(entt::exclude<JoltCharacter>)`.
It selects everything that is an enemy (`AIState`) with a body-collider and a position, **excluding** any
that already have a character — so the pass is idempotent and only ever builds a character once per enemy.
`AIState` is the marker that says "this is an enemy," which is why the loop `(void)ai`'s the state itself: it
never reads it, it only needs the *presence* of the component to filter.

The capsule is derived from the collider the factory already set: `radius = halfExtents.x` (0.4 for a grunt)
and `halfHeight = halfExtents.y − radius` (0.9 − 0.4 = 0.5), the height of the cylindrical middle between the
two hemispherical caps. That produces a capsule of the same footprint and height as the box the mapper and
the shooter reason about, so the character stands exactly where the grunt's box was.

The settings block is the player's character settings *plus two lines*:

```cpp
        settings->mInnerBodyShape = capsule;         // physical presence...
        settings->mInnerBodyLayer = Layers::MOVING;  // ...that blocks the player
```

That is the whole reason enemies differ from the player. A `CharacterVirtual` is, by default, a *ghost that
sweeps*: it collides *against* the world but has no body the world can collide *with*. That's perfect for the
player (nothing in the game needs to bump into the player's collider). But an enemy must **block** the player
and **separate** from other enemies — you can't walk through a grunt, and two grunts can't occupy the same
cell. Jolt 5.2's answer is an *inner body*: a real physics body, shaped by `mInnerBodyShape`, on the layer
`mInnerBodyLayer`, that Jolt keeps glued to the character's position automatically. Put it on `Layers::MOVING`
— the same layer the old standalone kinematic enemy body used — and the player's own `CharacterVirtual`
sweeps against it and stops, exactly as before.

> **Why does the character need a *separate* inner body at all — why not just make the enemy a plain
> kinematic body like before, but sweep it?** Because a kinematic body and a swept character are two
> different animals, and you cannot have one thing be both. A kinematic body *moves the world*; a
> `CharacterVirtual` is *moved by the world*. The enemy needs to be moved by the world (so it stops at
> walls), which makes it a character — but a bare character is intangible, so the player would walk straight
> through it. The inner body is Jolt's designed solution to precisely this: it gives an otherwise-intangible
> swept character a physical shell that other things collide with, without making the character itself
> kinematic. The character auto-ignores its *own* inner body (so it doesn't collide with itself), and Jolt
> moves the inner body in lock-step with the character each update — so from the outside the enemy is a solid
> obstacle, and from the inside it's a well-behaved swept walker. One entity, two roles, cleanly separated.

---

## Step 3: Driving the Character — `ExtendedUpdate` in `aiStepCharacter`

Building the character is half of it; the other half is *moving* it each tick. A `CharacterVirtual` doesn't
move because you set a `Position` — you give it a desired velocity and call an update that sweeps it. That
update is factored into its own helper so the state machine stays readable, `aiStepCharacter` in
`src/engine/ecs/systems/enemy/ai_step_character.cpp`:

```cpp
namespace
{
    constexpr float kStepHeight = 0.5f;   // seam/step the enemy walks up (like the player)
}

// Step an enemy's CharacterVirtual one tick with a desired horizontal velocity,
// letting gravity + stick-to-floor keep it grounded. Collided locomotion — this
// is what stops the follower clipping walls on a corner-cut (the whole point).
void aiStepCharacter(JPH::CharacterVirtual* ch, glm::vec3 horiz, float dt, JoltWorld& jolt)
{
    bool onGround = ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    float vy = onGround ? 0.0f
                        : ch->GetLinearVelocity().GetY() - jolt.physicsSystem->GetGravity().Length() * dt;
    ch->SetLinearVelocity(JPH::Vec3(horiz.x, vy, horiz.z));

    JPH::CharacterVirtual::ExtendedUpdateSettings us;
    us.mStickToFloorStepDown = JPH::Vec3(0.0f, -kStepHeight, 0.0f);
    us.mWalkStairsStepUp     = JPH::Vec3(0.0f,  kStepHeight, 0.0f);
    ch->ExtendedUpdate(dt,
        -ch->GetUp() * jolt.physicsSystem->GetGravity().Length(),
        us,
        jolt.physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        jolt.physicsSystem->GetDefaultLayerFilter(Layers::MOVING),
        {}, {}, *jolt.tempAllocator);
}
```

The caller passes a **horizontal** velocity (`horiz`) — where the grunt wants to walk this tick, on the
ground plane. `aiStepCharacter` supplies the vertical component itself: if the character is `OnGround`, `vy`
is zero (no falling); otherwise it accumulates gravity so an enemy that walks off a ledge drops naturally.
Those combine into the character's linear velocity, and then `ExtendedUpdate` does the real work:

- **it sweeps** the character by that velocity against the `MOVING`/`NON_MOVING` collision layers, resolving
  contacts — this is the collided motion that slides along a wall instead of clipping it;
- **`mStickToFloorStepDown`** keeps the character glued to a floor that drops slightly beneath it (so it
  doesn't launch off the top of a ramp or a small lip);
- **`mWalkStairsStepUp`** lets it climb a seam or step up to `kStepHeight` (0.5) without stopping — the same
  half-unit step tolerance the player has.

The result is that an enemy walks like the player walks: against the world, up small steps, stuck to the
floor. A path that grazes a corner now produces a frame of *sliding along the wall*, not a frame of being
*inside* it.

> **Why re-derive the vertical velocity in the helper instead of letting the caller pass a full 3D velocity?**
> Because gravity and ground-sticking are *locomotion concerns*, not *behaviour concerns*. The AI state
> machine's job is to decide a direction on the ground plane — "walk toward this waypoint," "hold position,"
> "back off" — all of which are horizontal. Whether the character is falling, grounded, or stepping up a lip
> is physics, and it's identical for every enemy in every state. Folding it into `aiStepCharacter` means the
> state machine never thinks about `y`: it hands over a flat direction and trusts the helper to keep the
> grunt on the floor. That's the same division the player's movement code uses, and it keeps the ranged/melee/
> chase branches (next chapters) from each having to re-implement gravity.

---

## Step 4: Re-homing Ownership — `aiSystem` Writes `Position`, `joltSyncSystem` Skips Enemies

Under the old design the enemy's `Position` was *read from Jolt*: the grunt was a `JoltBody`, and
`joltSyncSystem` copied every `JoltBody`'s transform back into its `Position` after the physics step. Now the
enemy has no `JoltBody` at all — it has a `JoltCharacter` — so that sync no longer touches it. Ownership of
the enemy's `Position` moves to `aiSystem`, which reads the character's swept position straight out of Jolt.

The mechanism is a small `move` lambda at the top of each enemy's loop iteration in
`src/engine/ecs/systems/enemy/ai_system.cpp`:

```cpp
    for (auto [entity, ai, pos, joltChar, path] : registry.view<AIState, Position, JoltCharacter, AIPath>().each())
    {
        JPH::CharacterVirtual* ch = joltChar.character.GetPtr();

        // Move the character with a horizontal velocity (0 = hold ground) and mirror
        // its collided position back into ECS. joltSyncSystem skips enemies now — they
        // have no JoltBody — so aiSystem owns their Position.
        auto move = [&](glm::vec3 horiz) {
            aiStepCharacter(ch, horiz, dt, jolt);
            JPH::RVec3 cp = ch->GetPosition();
            pos.value = glm::vec3(cp.GetX(), cp.GetY(), cp.GetZ());
        };
```

Every branch of the behaviour calls `move(...)`: `move(glm::vec3(0.0f))` to hold ground while idle or
attacking, `move(stepDir * kMoveSpeed)` to chase. Each call sweeps the character and then copies its
*resolved* position back into the ECS `Position` — so `Position` always reflects where the character actually
ended up after collision, not where it wished to go.

The view itself changed to match: it's now `<AIState, Position, JoltCharacter, AIPath>` (the old version
iterated `JoltBody`). And `joltSyncSystem` needs **no change** — it still iterates `<Position, JoltBody>`,
and because enemies no longer have a `JoltBody`, they simply fall out of its view. The header records the new
contract, `src/engine/ecs/systems/enemy/ai_system.h`:

```cpp
// Enemy behaviour: sense the player (detect range + line-of-sight), chase by
// driving a CharacterVirtual along the A* path (collided locomotion — no wall
// clipping), and melee-attack in range. Runs BEFORE the physics step and owns
// the enemy's Position (joltSyncSystem skips them — they have no JoltBody).
// Death and hit/death feedback are handled by enemyDeathSystem.
void aiSystem(entt::registry& registry, const Level& level);
```

The tick order in `stepSimulation` is unchanged from Chapter 24 — `aiSystem` still runs *before*
`joltWorld.step` and `joltSyncSystem` — but the reason is now sharper. `aiSystem` sweeps the enemy character
(itself a physics operation, but one Jolt does immediately inside `ExtendedUpdate`), the world step then
integrates the player's character and any dynamic bodies against the enemy's *now-current* inner body, and
`joltSyncSystem` mirrors back only the remaining `JoltBody` entities (movers, props). The chase-branch
locomotion that ends the loop shows `move` in its natural home:

```cpp
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

        faceDir(registry, entity, stepDir);
        move(stepDir * kMoveSpeed);
```

The A* follow logic (advance the cursor when within `kWaypointHit` of a waypoint, steer at the next one) is
exactly Chapter 24's; only the final line changed — `MoveKinematic(...)` became `move(stepDir * kMoveSpeed)`,
routing the same steering vector through the swept character.

> **Why give `aiSystem` ownership of the enemy `Position` rather than keep the enemy as a body and let
> `joltSyncSystem` own it as before?** Because the authoritative source of an enemy's position is now the
> *character*, and the character is stepped inside `aiSystem`. Keeping a separate `JoltBody` in sync as well
> would mean two representations of the same enemy — the character that actually walks, and a body that
> mirrors it — which is exactly the kind of dual-ownership that breeds desync bugs (the boarding-lift desync
> in Chapter 15 was one). The clean model is: the character is the enemy's physical self, `aiSystem` sweeps
> it and reads its result, and `joltSyncSystem` — which exists to copy *bodies* back to the ECS — never has
> to know enemies exist. One owner, one source of truth, and the sync system's contract (`Position` follows
> `JoltBody`) stays literally true because enemies simply aren't bodies any more.

---

## Step 5: Splitting `ai_system.cpp` to Stay Under the Size Cap

Adding character locomotion (this chapter), line-of-sight, and the ranged-fire path (Chapter 31b) to
`ai_system.cpp` would have blown past the file/function size caps the project adopted in Chapter 17. So the
glue helpers were lifted out into sibling files behind one shared header,
`src/engine/ecs/systems/enemy/ai_support.h`:

```cpp
// Support helpers for aiSystem, split out to keep ai_system.cpp focused on the
// state machine (CODING_STANDARD §4 size cap). Physics/geometry/combat glue only.

namespace JPH { class CharacterVirtual; }
struct JoltWorld;
struct Level;
struct RangedAttack;
struct CombatResources;

// Step an enemy CharacterVirtual one tick with a desired horizontal velocity,
// letting gravity + stick-to-floor keep it grounded (collided locomotion).
void aiStepCharacter(JPH::CharacterVirtual* ch, glm::vec3 horiz, float dt, JoltWorld& jolt);

// True if `self` has an unobstructed line from `from` to `to` (level surfaces +
// solid entities, ignoring the player itself and in-flight projectiles).
bool aiClearLineOfSight(entt::registry& reg, const Level& level, entt::entity self,
                        entt::entity player, glm::vec3 from, glm::vec3 to);

// Fire one dodgeable Enemy-faction bolt from `eye` toward `dir` using the combat
// projectile path. No splash — it can never friendly-fire other enemies.
void aiFireEnemyBolt(entt::registry& reg, entt::entity self, glm::vec3 eye, glm::vec3 dir,
                     const RangedAttack& r, const CombatResources& res);
```

Three helpers, each in its own `.cpp`:

- **`ai_step_character.cpp`** — `aiStepCharacter`, this chapter's locomotion.
- **`ai_line_of_sight.cpp`** — `aiClearLineOfSight`, the sensing raycast (covered in detail in Chapter 31b,
  because its projectile-ignoring behaviour is what fixed a real bug).
- **`ai_fire_bolt.cpp`** — `aiFireEnemyBolt`, ranged fire (Chapter 31b).

`ai_system.cpp` keeps only the state machine — the `for` loop over enemies, the aggro/idle/attack/chase
decisions — and calls into these. The header uses **forward declarations** (`namespace JPH { class
CharacterVirtual; }`, `struct JoltWorld;`, …) rather than including Jolt and the level headers, so pulling
in `ai_support.h` doesn't drag the whole physics stack into every translation unit that touches it.

> **Why split by *helper role* (locomotion / sensing / firing) rather than, say, one big `ai_helpers.cpp`?**
> Because the project's convention is one function per file (Chapter 17), and each of these is a genuinely
> separate concern with a different set of dependencies: `aiStepCharacter` needs Jolt's character API,
> `aiClearLineOfSight` needs the level geometry and the raycast, `aiFireEnemyBolt` needs the combat
> projectile path. Lumping them together would make a file that includes *all three* dependency sets and
> recompiles whenever any of them changes; splitting them means a change to the fire path doesn't touch the
> locomotion translation unit at all. The shared `ai_support.h` is deliberately thin — forward declarations
> and three signatures — so it's cheap to include and names exactly the seam between the state machine and
> its glue.

---

## Step 6: Death Cleanup — the Character Drops Its Own Inner Body

An enemy that used to be a `JoltBody` had to have that body explicitly removed and destroyed when it died —
otherwise a dangling body would linger in the physics world. A `CharacterVirtual` is easier: it *owns* its
inner body, and its destructor removes and destroys it. So `enemyDeathSystem` gets **simpler**, from
`src/engine/ecs/systems/enemy/enemy_death_system.cpp`:

```cpp
    // Destroy dead enemies: pop a sound, drop the entity. The enemy's
    // CharacterVirtual (JoltCharacter) removes and destroys its own inner body in
    // its destructor when the component is erased — no manual body teardown needed.
    for (entt::entity e : dead)
    {
        queueSoundAt(registry, "combat.explosion_small", registry.get<Position>(e).value);
        registry.destroy(e);
    }
```

`registry.destroy(e)` erases every component on the entity, including the `JoltCharacter`. Erasing the
`JoltCharacter` drops the last `JPH::Ref` to the `CharacterVirtual`, whose destructor tears down the inner
body. No `RemoveBody` / `DestroyBody` calls, no risk of forgetting one — the lifetime of the physical
presence is tied to the lifetime of the component that represents it.

> **Why is "the destructor cleans up" worth calling out — isn't RAII just how C++ works?** Because it's the
> payoff of choosing the character-with-inner-body design over a hand-managed body. The old kinematic-body
> enemy stored a raw `JPH::BodyID`, and a `BodyID` is *not* an owning handle — it's an index the death system
> had to remember to `RemoveBody` + `DestroyBody`, and any death path that forgot leaked a body. The
> `CharacterVirtual` is a `JPH::Ref`-counted object that owns its inner body, so wrapping it in a
> `JoltCharacter` component makes the *entity's* lifetime govern the *body's* lifetime for free. Killing an
> enemy is now literally `registry.destroy(e)` and the physics cleans itself up — which is exactly the
> invariant you want when enemies die in the middle of combat, splash damage, or a level teardown, from any
> number of code paths.

---

## Step 7: Wiring — Build the Characters in `buildWorld`

The characters are built once, at scene setup, by a single call in `buildWorld`
(`src/engine/app/simulation.cpp`), placed deliberately *after* the level's static bodies exist so first-tick
ground detection is correct:

```cpp
        // Static bodies from level geometry
        createLevelBodies(registry, level);
        joltWorld.physicsSystem->OptimizeBroadPhase();

        // Kinematic bodies for movers (lifts, doors)
        auto moverView = registry.view<Position, AABBCollider, Mover>();
        for (auto [entity, pos, col, mover] : moverView.each())
            createKinematicBody(registry, entity);

        // Enemies move with a CharacterVirtual (collided locomotion, driven by
        // aiSystem) whose kinematic inner body still blocks the player. Built here,
        // after level bodies exist, so first-tick ground detection is correct.
        initEnemyCharacters(registry);

        // …

        // Player CharacterVirtual — must come after level bodies exist
        initPlayerCharacter(registry);
```

The ordering matters: `createLevelBodies` puts the floors and walls into Jolt, `OptimizeBroadPhase` indexes
them, and only then does `initEnemyCharacters` build characters — so the very first `ExtendedUpdate` a grunt
runs finds the floor beneath it and reports `OnGround`, instead of thinking it's in mid-air and applying a
tick of gravity. It sits right alongside the equivalent `initPlayerCharacter` call, because the two are the
same operation for the two kinds of character. Its header, `init_enemy_characters.h`, spells out the once-only
contract:

```cpp
// One-time setup: build a CharacterVirtual for each enemy (Position + AABBCollider
// + AIState) so aiSystem drives them with COLLIDED locomotion (no wall-clipping on
// corner-cuts), mirroring the player. Each character carries a kinematic INNER BODY
// on Layers::MOVING so it still blocks the player and separates from other enemies;
// the character ignores its own inner body, and the inner body is destroyed with
// the character. Call once after level bodies exist, before the first aiSystem tick.
void initEnemyCharacters(entt::registry& registry);
```

And the new files are added to the build in `CMakeLists.txt`, in the enemy-systems block:

```cmake
	src/engine/ecs/systems/enemy/ai_system.cpp
	src/engine/ecs/systems/enemy/ai_step_character.cpp
	src/engine/ecs/systems/enemy/ai_line_of_sight.cpp
	src/engine/ecs/systems/enemy/ai_fire_bolt.cpp
	src/engine/ecs/systems/enemy/enemy_death_system.cpp
	src/engine/ecs/systems/enemy/init_enemy_characters.cpp
```

(`ai_line_of_sight.cpp` and `ai_fire_bolt.cpp` are compiled here but their contents belong to Chapter 31b.)

---

## Step 8: The Harness Follows Suit — Teleport and Clear a Character, Not a Body

The headless scenarios in `src/harness/headless_main.cpp` had helpers that manipulated the enemy *body*
directly — teleporting it, and removing it for pure player-physics tests. Both switch to the character. The
teleport helper now moves the `CharacterVirtual` (which drags its inner body along, so the player still
collides with it):

```cpp
    // Hard-teleport an enemy (its CharacterVirtual + ECS mirror). SetPosition also
    // moves the character's inner body, so the player still collides against it.
    void teleportEnemy(entt::registry& reg, entt::entity e, glm::vec3 p)
    {
        auto& ch = reg.get<JoltCharacter>(e).character;
        ch->SetPosition(JPH::RVec3(p.x, p.y, p.z));
        ch->SetLinearVelocity(JPH::Vec3::sZero());
        reg.get<Position>(e).value = p;
    }
```

`SetPosition` on the character moves the inner body too — that's the property the comment leans on — so a
teleported enemy is still a solid obstacle at its new spot. And `clearEnemies` no longer has to hunt down and
destroy a body; it just destroys the entity and lets the character's destructor do the rest:

```cpp
    // Remove all enemies (entity + its CharacterVirtual, whose destructor drops the
    // inner body). Pure player-physics scenarios call this so aggroed grunts can't
    // wander in and disturb the measurement.
    void clearEnemies(JoltWorld&, entt::registry& reg)
    {
        std::vector<entt::entity> es;
        for (auto e : reg.view<AIState>()) es.push_back(e);
        for (auto e : es) reg.destroy(e);
    }
```

The `JoltWorld&` parameter is now unnamed — the function no longer needs it, but keeping it in the signature
means none of the call sites changed. The Chapter 24 pathfinding scenario, `monster_path`, retargets its
teleport to the new helper (`teleportEnemy(reg, grunt, glm::vec3(16.0f, 0.95f, 5.0f))`) and otherwise asserts
the same thing it always did: the grunt routes past the shelf without ending up inside it — which is now true
by construction, because the swept character can't be inside a wall.

> **Why keep the unused `JoltWorld&` parameter in `clearEnemies` instead of removing it?** Because it's a
> pure API-churn saving: `clearEnemies(jolt, reg)` is called from several scenarios, and dropping the
> parameter would mean editing every one of them for no behavioural gain. Leaving the parameter unnamed says
> exactly the right thing to the compiler and the reader — "this function used to need the physics world and
> no longer does" — while keeping the call sites stable. It's the same reasoning as the `(void)ai` in
> `initEnemyCharacters`: name-drop the thing you're deliberately not using so the intent is explicit, and
> don't ripple a signature change through the harness just to tidy one argument.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/ecs/systems/enemy/init_enemy_characters.{h,cpp}` | **New.** `initEnemyCharacters` builds a `CharacterVirtual` for every `<Position, AABBCollider, AIState>` enemy (capsule from the collider), with a kinematic **inner body** (`mInnerBodyShape`/`mInnerBodyLayer = Layers::MOVING`) so it still blocks the player. Excludes enemies that already have a `JoltCharacter` (idempotent). |
| `engine/ecs/systems/enemy/ai_step_character.cpp` | **New.** `aiStepCharacter` steps an enemy character one tick: supplies vertical velocity (gravity / ground-stick), then `ExtendedUpdate` sweeps it (collided locomotion, `kStepHeight = 0.5` stair step). |
| `engine/ecs/systems/enemy/ai_support.h` | **New.** Thin shared header (forward decls) declaring `aiStepCharacter`, `aiClearLineOfSight`, `aiFireEnemyBolt` — the seam between the state machine and its glue. |
| `engine/ecs/systems/enemy/ai_system.{cpp,h}` | Enemy view changes to `<AIState, Position, JoltCharacter, AIPath>`; a `move(horiz)` lambda sweeps the character via `aiStepCharacter` and mirrors its resolved position into `Position`; the old `MoveKinematic` chase call becomes `move(stepDir * kMoveSpeed)`. `aiSystem` now owns the enemy `Position`. |
| `engine/ecs/systems/enemy/enemy_death_system.cpp` | Simplified: `registry.destroy(e)` erases the `JoltCharacter`, whose destructor drops the inner body — no manual `RemoveBody`/`DestroyBody`. |
| `engine/app/simulation.cpp` | `buildWorld` calls `initEnemyCharacters(registry)` after level bodies exist (correct first-tick ground state), beside `initPlayerCharacter`. |
| `engine/level/factories.h` | Doc comment on `spawnMonsterGrunt` updated: the grunt's `CharacterVirtual` (stands, blocks, walks) is created in `buildWorld`. |
| `harness/headless_main.cpp` | `teleportKinematic` → `teleportEnemy` (moves the character + inner body); `clearEnemies` destroys the entity and lets the destructor clean up; `monster_path` retargets to `teleportEnemy`. |
| `CMakeLists.txt` | Adds `ai_step_character.cpp`, `ai_line_of_sight.cpp`, `ai_fire_bolt.cpp`, `init_enemy_characters.cpp` to `qengine_lib`. |

---

## What You Should See

Run `build/QEngine.exe` (the showcase) or a `.map` with grunts:

1. **Grunts no longer clip walls.** A grunt chasing you around a corner slides *along* the wall for a frame
   instead of half-vanishing into it — because it's now swept against the world every tick, not teleported
   to a target.
2. **Grunts still block you and each other.** Walk into a grunt and you stop; two grunts converging on you
   push apart instead of overlapping — the inner body is doing its job.
3. **Grunts stand on the floor from frame one.** Because the characters are built after the level bodies
   exist, no grunt does a first-tick fall.

Headless:

4. **`monster_path` still passes** — the grunt routes past the shelf and its measured position never lands
   inside the shelf AABB (now true by construction: a swept character can't be inside a wall).
5. **`monster_grunt` and `monster_ai` still pass** — the melee behaviour is unchanged; only how the grunt
   *moves* changed.

---

## What's Next

Enemies now move like the player does — collided, grounded, blocking — which removes the last locomotion
glitch from the chase behaviour and, just as importantly, gives them a *physical presence* that the rest of
the game can reason about. That presence is the foundation for the next step: making enemies dangerous at a
distance. **Chapter 31b** adds ranged attacks — a `RangedAttack` component, a standoff-and-telegraph branch
in `aiSystem`, and dodgeable enemy bolts — and the friendly-fire system (projectile *factions*) that makes
enemy fire hit the player without enemies shooting each other. The `aiClearLineOfSight` and `aiFireEnemyBolt`
helpers stubbed out here get their full treatment there.
