# Chapter 31b: Ranged Enemies and Projectile Factions

## What You'll Learn
- How to give a projectile a **side** — a `Faction { Player, Enemy }` tag on the `Projectile` component —
  and why *deriving* it from the shooter in `fireProjectile` beats threading a faction argument through
  every fire helper
- The **friendly-fire guard**: `updateProjectiles` skipping same-faction targets *and* other projectiles, so
  enemy bolts hit you but not each other, and player shots hit enemies but not you
- A real bug the same session surfaced and fixed — an enemy's own in-flight bolt **blocking its own line of
  sight** — solved by making `raycastEntities` ignore projectiles entirely
- Modelling "this enemy shoots" as **data**, not a class split: a `RangedAttack` component, and a
  `spawnMonsterRanged` archetype that is just the grunt plus that component and a different tint
- Wiring the new archetype through the whole spawn pipeline: `factories.h`, `classname_factory.cpp`, the
  showcase descriptor, and the `.map` loader's ground-lift table
- The ranged branch of `aiSystem`: **hold at standoff range → telegraph (windup) → fire a dodgeable bolt**,
  built on the Chapter 31a character locomotion, and how a projectile's travel time *is* the telegraph
- Reusing the combat projectile path from the enemy side via `aiFireEnemyBolt` (a throwaway `Weapon` carries
  the ranged stats), and the `aiClearLineOfSight` helper that keeps an enemy honest about what it can see
- Proving all of it headless: the `monster_ranged` scenario (aggro → standoff → damage) and the
  `friendly_fire` scenario (the faction guard, both directions)

---

## Where We Are

Chapter 31a rebuilt enemy *locomotion*: grunts became `CharacterVirtual`s that sweep against the world
instead of clipping through it, and gained a physical inner body so they still block the player. That made
them solid and well-behaved — but they're still **melee-only**. A grunt is only a threat if it can reach you;
keep your distance and it's harmless.

This chapter makes an enemy dangerous at range. That is more than "let the enemy call the fire function,"
because the combat code so far has been strictly **player → enemy**: projectiles are spawned by the player and
assumed to hit enemies. Turn that around — an enemy firing at the player — and two things break unless we fix
them first:

1. **Friendly fire.** With no notion of *which side* a shot belongs to, an enemy's bolt would damage other
   enemies (and, symmetrically, splash from a player rocket could hurt the player). Shots need an owner side.
2. **Self-blocking line of sight.** An enemy checks line of sight by raycasting toward the player before it
   fires. The moment it fires, its own bolt is an entity sitting right in front of its eye — and the very next
   tick that bolt *blocks the raycast*, so the enemy decides it can't see you and stops shooting. Its own
   ammunition blinds it.

So the order of this chapter is: fix factions, fix the LoS raycast, add the `RangedAttack` data and the
archetype, then add the behaviour branch that ties it together. Everything is grounded in
`src/engine/ecs/components/combat.h`, `gameplay.h`, `systems/combat/{fire_projectile,update_projectiles,
raycast_entities}.cpp`, `systems/enemy/{ai_fire_bolt,ai_line_of_sight,ai_system}.cpp`,
`src/engine/level/{factories.h,spawn_monster.cpp,classname_factory.cpp,showcase_descriptor.cpp,
map_to_descriptors.cpp}`, and `src/harness/headless_main.cpp`.

---

## Step 1: The Data — a `Faction` on Every Projectile

A shot needs to know whose side it's on. That's a two-value enum and one new field on the existing
`Projectile` component, both in `src/engine/ecs/components/combat.h`:

```cpp
// Which side a shot belongs to, so projectiles never damage their own side.
// Derived from the shooter (enemies = Enemy, everything else = Player) — see
// fireProjectile. Neutral targets (props/cubes) match neither and stay hittable.
enum class Faction
{
	Player,
	Enemy
};
```

```cpp
// attacked to projectile entities
struct Projectile
{
	float damage;
	float splashRadius;
	float splashDamage;
	entt::entity owner = entt::null; // Who fired it (for kill credit)
	Faction faction = Faction::Player; // whose shot — friendly-fire guard
};
```

Two sides only — `Player` and `Enemy`. Note the deliberate asymmetry the comment calls out: a target that is
*neither* (a physics prop, a demo cube) matches no faction, so it stays hittable by everyone. Faction is a
guard against friendly fire, not a whitelist of valid targets — the default is "hittable," and faction only
*subtracts* your own side.

> **Why a two-value `enum` rather than a `TagPlayerProjectile` / `TagEnemyProjectile` pair of marker
> components?** Because a shot is always *exactly one* side, and a field that can hold one of two values
> models that better than two tags that could (by a bug) both be present or both be absent. A single
> `Faction faction` on the `Projectile` you already emplace costs nothing extra — no second component, no
> second `any_of` lookup — and it reads naturally at the guard site (`proj.faction == Faction::Enemy`).
> Reserving "neither faction" for props falls out for free: props simply have no `Projectile` and no side,
> so the guard, which only ever compares a projectile's faction to a target's *type*, leaves them alone.

---

## Step 2: Deriving the Faction from the Shooter

The obvious way to set a projectile's faction is to pass it into `fireProjectile`. The chosen way is to
*derive* it from the shooter, so no fire-helper signature changes. In
`src/engine/ecs/systems/combat/fire_projectile.cpp`:

```cpp
	// Whose shot this is — enemies fire Enemy projectiles (won't hit other
	// enemies), everything else fires Player projectiles. See updateProjectiles.
	const Faction faction = registry.any_of<AIState>(shooter) ? Faction::Enemy : Faction::Player;
	registry.emplace<Projectile>
	(
		projectile,
		weapon.damage,
		weapon.splashRadius,
		weapon.splashDamage,
		shooter,
		faction
	);
```

`fireProjectile` already takes the `shooter` entity (it's stored as the projectile's `owner` for kill
credit). Whether that shooter is an enemy is a one-line query — `registry.any_of<AIState>(shooter)` — because
`AIState` is *the* marker of an enemy. An enemy shooter yields `Faction::Enemy`; anything else (the player)
yields `Faction::Player`. The signature of `fireProjectile` is untouched, and so is every existing call site.

> **Why derive the faction from the shooter instead of adding a `Faction` parameter to `fireProjectile`?**
> Because the shooter *already fully determines* the side — an enemy fires enemy shots, the player fires
> player shots, and there's no case where the same shooter needs to fire either side. Passing a faction
> argument would be redundant information the caller has to compute correctly every time, and a chance to get
> it wrong (an enemy accidentally passing `Faction::Player`). Deriving it centralises the rule in one place —
> "is the shooter an enemy?" — so it's impossible for a caller to disagree with it. It also means the
> Chapter 31a `aiFireEnemyBolt` and the player's fire path call the *identical* `fireProjectile`, and each
> gets the right faction automatically from *who* is shooting, not from remembering to pass the right flag.

---

## Step 3: The Friendly-Fire Guard in `updateProjectiles`

With every projectile carrying a side, the collision loop in
`src/engine/ecs/systems/combat/update_projectiles.cpp` enforces the rule. Two new `continue`s join the
existing self/owner/trigger skips:

```cpp
		// check against ALL colliders (not just entities with Health)
		auto entityView = registry.view<Position, AABBCollider>();
		for (auto [target, tPos, tCol] : entityView.each())
		{
			if (target == projEntity) continue;  // don't collide with self
			if (target == proj.owner) continue;
			if (tCol.isTrigger) continue;
			if (registry.any_of<Projectile>(target)) continue;  // bolts pass through each other
			// Friendly-fire guard: a shot never damages its own side. Props (neither
			// player nor enemy) match no faction and stay hittable.
			if (proj.faction == Faction::Enemy  && registry.any_of<AIState>(target))  continue;
			if (proj.faction == Faction::Player && registry.any_of<TagPlayer>(target)) continue;
```

Read the two guard lines together with the projectile-skip above them:

- **`if (registry.any_of<Projectile>(target)) continue;`** — a bolt passes through another bolt. Without
  this, two projectiles in flight could collide with each other and detonate mid-air, and (worse) an enemy's
  bolt could be "hit" by the player's, cancelling both. Projectiles are not obstacles to each other.
- **`proj.faction == Faction::Enemy && any_of<AIState>(target)`** — an enemy bolt never damages an enemy.
- **`proj.faction == Faction::Player && any_of<TagPlayer>(target)`** — a player bolt never damages the player
  (this matters for splash: a rocket fired at your feet shouldn't gib you via the direct-hit path).

Anything that is neither an `AIState` nor a `TagPlayer` — a prop, a demo cube — passes both guards and stays
hittable by either side, exactly as the comment promises. Everything downstream of the guard (damage,
knockback, the HUD hit/kill signals) is unchanged; the guard just decides *whether we got this far*.

> **Why guard on the target's *type* (`AIState` / `TagPlayer`) rather than storing a faction on every
> potential target too?** Because only two kinds of thing have a "side" — enemies and the player — and both
> already have a defining marker component (`AIState`, `TagPlayer`). Reusing those markers means faction
> membership is derived from what an entity *already is*, with no parallel `Faction` field to keep in sync on
> hundreds of colliders (props, pickups, tracers). The projectile is the only thing that needs to *carry* a
> faction, because it's the only thing that outlives the instant of firing and has to remember whose it was;
> everything it might hit can be classified on the spot by the component it already has.

---

## Step 4: The Self-Blocking Bolt Bug — `raycastEntities` Ignores Projectiles

Here's the subtle one. An enemy decides whether it may fire by checking line of sight — a raycast from its
eye to the player, via `aiClearLineOfSight`, which calls the shared `raycastEntities`. The problem: the
instant the enemy fires, its bolt spawns *right in front of its eye* and becomes a solid entity. On the very
next tick the LoS raycast hits the enemy's own bolt, concludes the player is obscured, and the enemy stops
shooting — blinded by its own ammunition.

The fix is to make projectiles invisible to entity raycasts, in
`src/engine/ecs/systems/combat/raycast_entities.cpp`:

```cpp
	auto view = registry.view<Position, AABBCollider>();
	for (auto [entity, pos, col] : view.each())
	{
		if (entity == ignore) continue;
		if (col.isTrigger) continue;
		// Projectiles are ephemeral — they must not block sightlines (an enemy's
		// own bolt would break its line of sight) or be shootable targets.
		if (registry.any_of<Projectile>(entity)) continue;
```

One `continue`: a projectile is never a raycast hit. That means an in-flight bolt blocks neither an enemy's
line of sight nor the player's hitscan — you can't accidentally "shoot" an enemy's bolt out of the air, and
an enemy can keep firing through its own volley. Projectiles resolve their own collisions in
`updateProjectiles` (Step 3); they have no business being obstacles in a *sightline* query.

`aiClearLineOfSight` (from `src/engine/ecs/systems/enemy/ai_line_of_sight.cpp`, one of the Chapter 31a file
splits) is where that raycast lives — it tests level surfaces first, then entities:

```cpp
bool aiClearLineOfSight(entt::registry& reg, const Level& level, entt::entity self,
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

It ignores `self` (the enemy doesn't block its own view), returns *not clear* if any level surface or any
non-player entity sits between the eye and the player, and — crucially — thanks to Step 4, never counts a
projectile as that blocker. The `- 0.1f` slack on each comparison keeps a surface or entity *at* the target
distance from counting as an obstruction in front of it.

> **Why fix this in `raycastEntities` (shared by all raycasts) rather than only in the enemy's LoS check?**
> Because "a projectile is not a raycastable obstacle" is true *everywhere*, not just for enemy sight. The
> player's hitscan uses the same `raycastEntities`; if a bolt could block a raycast, a player could
> accidentally "hit" a passing enemy bolt with the railgun, or an enemy's bolt could shield the player from
> the player's own shot. Projectiles are ephemeral, fast-moving, and resolve their own collisions in
> `updateProjectiles` — they are never meant to be sightline geometry. Putting the skip in the one shared
> raycast makes the property universal and keeps the enemy LoS helper simple: it doesn't need a special case,
> because the raycast it calls already tells the truth about what's solid.

---

## Step 5: Modelling "This Enemy Shoots" as a Component

Now the enemy side. Rather than a whole new enemy *class*, a ranged enemy is a grunt that *also* has a
`RangedAttack` component — the data that says "hold at range and fire." From
`src/engine/ecs/components/gameplay.h`:

```cpp
// Present on ranged enemies. aiSystem holds them at standoff range and, on a
// clear shot, plays a brief telegraph (windup) then fires a dodgeable projectile
// at the player. Absent → the enemy is melee-only. Fired shots are Enemy-faction
// (they can't hurt other enemies). See aiSystem's ranged branch.
struct RangedAttack
{
	float range           = 16.0f;  // fire when the player is within this (and visible)
	float standoffMin     = 7.0f;   // back off if the player closes inside this
	float damage          = 10.0f;  // per bolt
	float projectileSpeed = 12.0f;  // slow enough to dodge
	float windup          = 0.5f;   // telegraph before the shot lands (seconds)
	float cooldown        = 1.6f;   // seconds between shots
	float windupTimer     = 0.0f;   // >0 while telegraphing the current shot
};
```

Every tuning knob of ranged behaviour is here: the `range` it fires within, the `standoffMin` it refuses to
let the player closer than, the `damage` and `projectileSpeed` of each bolt (deliberately slow — 12 units/s —
so you can sidestep it), the `windup` telegraph length, the `cooldown` between shots, and the live
`windupTimer` that counts down while it's telegraphing. `standoffMin < range` defines the band the enemy tries
to fight in: closer than `standoffMin` it backs off, within `[standoffMin, range]` it holds and shoots,
beyond `range` it closes the distance.

The archetype is then trivially small — `spawnMonsterRanged` in `src/engine/level/spawn_monster.cpp` is the
grunt plus the component plus a tint:

```cpp
    // Ranged variant: the grunt archetype plus a RangedAttack, tinted differently
    // so it reads as a distinct threat. aiSystem keeps it at distance and fires
    // dodgeable bolts; it is otherwise identical (shootable, blocks, dies).
    entt::entity spawnMonsterRanged(entt::registry& reg, const MeshAssets& a, glm::vec3 pos)
    {
        auto e = spawnMonsterGrunt(reg, a, pos);
        reg.emplace<RangedAttack>(e);
        reg.get<Colour>(e).value = glm::vec4(0.35f, 0.35f, 0.85f, 1.0f);  // blue — ranged
        return e;
    }
```

It *calls* `spawnMonsterGrunt` — so it inherits the full grunt (collider, health, `AIState`, `AIPath`, and
the `CharacterVirtual` built later in `buildWorld`) — then adds a `RangedAttack` and recolours it blue so a
ranged enemy reads as a distinct threat on sight. A melee grunt is red; a ranged grunt is blue; they are the
same entity otherwise.

> **Why a `RangedAttack` component on the grunt rather than a separate `monster_ranged` archetype built from
> scratch?** Because melee-vs-ranged is a *difference in one behaviour*, not a difference in kind. Everything
> else about the two enemies is identical: they're the same box, the same health, the same collider, they
> die the same way, they path the same way. Modelling ranged as an added component means all of that is
> shared automatically (`spawnMonsterRanged` literally begins by spawning a grunt), and `aiSystem` decides
> melee-vs-ranged with a single `registry.try_get<RangedAttack>(entity)` — presence of the component *is* the
> branch. It also keeps the door open for composition: a future enemy could carry `RangedAttack` *and* some
> other behaviour component without a combinatorial explosion of archetype classes. Data-as-behaviour scales;
> a class-per-variant doesn't.

---

## Step 6: Wiring the Archetype Through the Spawn Pipeline

`spawnMonsterRanged` is declared alongside the grunt in `src/engine/level/factories.h`:

```cpp
    // Enemy ranged grunt: like the grunt but carries a RangedAttack, so aiSystem
    // holds it at standoff range and fires dodgeable Enemy-faction projectiles.
    entt::entity spawnMonsterRanged(entt::registry& reg, const MeshAssets& a, glm::vec3 pos);
```

The `.map` loader needs a `classname` → factory mapping, added in `src/engine/level/classname_factory.cpp` —
a small `make_` shim plus a table entry:

```cpp
    entt::entity make_monster_ranged(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p)
    {
        return spawnMonsterRanged(reg, ctx.assets, p.origin);
    }
```

```cpp
                { "monster_grunt",             &make_monster_grunt },
                { "monster_ranged",            &make_monster_ranged },
```

The in-code showcase gets one, placed across the arena from the melee grunts so you meet it at range, in
`src/engine/level/showcase_descriptor.cpp`:

```cpp
    // ─── Enemies: melee grunts that chase, plus a ranged grunt that keeps its
    //     distance and lobs dodgeable bolts. ─────────────────────────────────
    d.push_back({ .classname = "monster_grunt",  .origin = glm::vec3(13.0f, 0.95f, 8.0f)  });
    d.push_back({ .classname = "monster_grunt",  .origin = glm::vec3(8.0f,  0.95f, 22.0f) });
    d.push_back({ .classname = "monster_ranged", .origin = glm::vec3(24.0f, 0.95f, 18.0f) });
```

And — the easy one to forget — the `.map` ground-lift table from Chapter 30 needs the new class, so a
floor-placed ranged grunt stands on the floor instead of sinking, in
`src/engine/level/map_to_descriptors.cpp`:

```cpp
    float groundHalfHeight(const std::string& cls)
    {
        if (cls == "monster_grunt" || cls == "monster_ranged") return 0.9f;  // grunt collider
        return 0.0f;
    }
```

Because `spawnMonsterRanged` reuses the grunt's collider (half-Y `0.9`), the ground lift is the same `0.9` —
so the single condition simply grows to cover both classnames.

> **Why does a new enemy class touch *four* files (factory, `.map` classname table, showcase, ground-lift)?**
> Because those are the four independent surfaces an entity can enter the world through, and each is a
> deliberate registration point rather than a magic auto-discovery. `factories.h`/`spawn_monster.cpp` is
> *how* to build it; `classname_factory.cpp` is how a **`.map`** names it; `showcase_descriptor.cpp` is the
> hard-coded arena that includes it; `map_to_descriptors.cpp`'s `groundHalfHeight` is the loader dialect from
> Chapter 30 that has to know its collider height to place its feet. Missing any one leaves a subtle gap — a
> factory no `.map` can reach, or a `.map` grunt that spawns buried. Listing them here is the checklist:
> add an enemy, touch these four, and it's fully wired for both the showcase and authored levels.

---

## Step 7: The Ranged Branch in `aiSystem` — Standoff, Telegraph, Fire

Now the behaviour. `aiSystem` already had the melee flow from Chapter 23 (in range + clear shot → attack on a
cooldown) and the chase flow from Chapter 24 (path toward the player). The ranged branch slots in *before*
the melee branch, gated on the presence of a `RangedAttack`, in
`src/engine/ecs/systems/enemy/ai_system.cpp`:

```cpp
        // ─── Ranged attack: hold at standoff, telegraph, then fire a bolt ──
        if (RangedAttack* r = registry.try_get<RangedAttack>(entity))
        {
            if (los && dist <= r->range)
            {
                ai.state = AIStateKind::Attack;
                path.waypoints.clear();
                faceDir(registry, entity, toPlayer);
                // Keep distance: back off if the player closes inside standoffMin,
                // otherwise hold position and shoot.
                move(dist < r->standoffMin ? -toPlayer * kMoveSpeed : glm::vec3(0.0f));

                if (r->windupTimer > 0.0f)
                {
                    r->windupTimer = std::max(0.0f, r->windupTimer - dt);
                    if (r->windupTimer <= 0.0f)   // telegraph elapsed → release
                    {
                        glm::vec3 eye = pos.value + glm::vec3(0.0f, kEyeOffset, 0.0f);
                        glm::vec3 aim = glm::normalize((playerPos + glm::vec3(0.0f, 0.4f, 0.0f)) - eye);
                        aiFireEnemyBolt(registry, entity, eye, aim, *r, combatRes);
                        ai.attackCooldown = r->cooldown;
                    }
                }
                else if (ai.attackCooldown <= 0.0f)
                {
                    r->windupTimer = r->windup;   // begin the telegraph (aim + pause)
                }
                continue;
            }
            r->windupTimer = 0.0f;   // out of range/LoS — abandon any half-aimed shot
            // fall through to Chase to close the distance
        }
        // ─── Melee attack: in range with a clear shot ──────────────────
        else if (los && dist <= kAttackRange)
        {
```

Walk the ranged flow, which uses the Chapter 31a `move` lambda (sweep the character, mirror its position):

1. **Fire condition** — `los && dist <= r->range`: the enemy must both *see* the player (via
   `aiClearLineOfSight`, Step 4) and have them within `range`. Otherwise it resets `windupTimer` to zero
   (abandoning any half-aimed shot) and falls through to the Chase branch to close the gap.
2. **Standoff movement** — it faces the player and calls
   `move(dist < r->standoffMin ? -toPlayer * kMoveSpeed : glm::vec3(0.0f))`. Inside `standoffMin` it walks
   *away* from the player (`-toPlayer`); otherwise it holds ground (`0`). This is the "keep your distance"
   behaviour: a ranged enemy backs off rather than walking into melee.
3. **The telegraph state machine** — three cases on `windupTimer` and `attackCooldown`:
   - if `windupTimer > 0`, it's mid-telegraph: decrement it, and when it reaches zero, *release the shot* —
     compute the eye position and a fresh aim vector at the player's chest, fire via `aiFireEnemyBolt`, and
     start the `cooldown`;
   - else if the cooldown is up (`attackCooldown <= 0`), *begin* a telegraph by setting `windupTimer =
     r->windup`;
   - otherwise (cooling down), do nothing but hold and face.

The sequence per shot is therefore: cooldown expires → `windupTimer` set (the enemy visibly aims and pauses)
→ `windup` seconds later the bolt fires → cooldown restarts. The pause *is* the telegraph — a beat where the
enemy is squared up and about to shoot — and the bolt's slow travel is a second telegraph. Between them the
player has time to break line of sight or sidestep.

> **Why a hold-and-aim pause as the telegraph instead of a distinct visual cue like a muzzle flash?** Because
> the two natural telegraphs — a *pause before firing* and a *slow, visible projectile* — are already the
> most readable ones in an FPS, and they cost nothing extra. A ranged grunt that squares up, holds for half a
> second, then lobs a bolt you can see coming is legible without any new art or effects: you learn "it's
> winding up, move" from its posture and "here comes the bolt, dodge" from the projectile itself. A separate
> muzzle-flash cue would be polish on top, not a prerequisite. Building the telegraph out of timing and
> projectile speed — knobs that already exist on `RangedAttack` — means the *feel* is tunable per enemy
> (a slower `windup` and `projectileSpeed` = a more forgiving enemy) without touching the renderer.

Note the `move(...)` and HUD lines in the surrounding code: the melee branch sets a HUD damage-direction
signal when it lands a hit (`hud->damageDir = …`), and the standoff branch's projectile does the same via
`updateProjectiles`. Those HUD signals are the subject of Chapter 31c — here they're just passengers.

---

## Step 8: Firing the Bolt — `aiFireEnemyBolt` Reuses the Combat Path

The enemy doesn't have a `WeaponInventory`; it has a `RangedAttack`. So `aiFireEnemyBolt` builds a *throwaway*
`Weapon` on the stack from the ranged stats and hands it to the same `fireProjectile` the player uses. From
`src/engine/ecs/systems/enemy/ai_fire_bolt.cpp`:

```cpp
// Fire one dodgeable Enemy-faction bolt from `eye` toward `dir`. Reuses the combat
// projectile path — a throwaway Weapon carries the ranged stats. No splash, so it
// can never friendly-fire other enemies.
void aiFireEnemyBolt(entt::registry& reg, entt::entity self, glm::vec3 eye, glm::vec3 dir,
                     const RangedAttack& r, const CombatResources& res)
{
    Weapon w{};
    w.type            = WeaponType::Nailgun;      // borrows the nailgun fire sound
    w.fireMode        = FireMode::Projectile;
    w.damage          = r.damage;
    w.projectileSpeed = r.projectileSpeed;
    w.splashRadius    = 0.0f;
    fireProjectile(reg, self, w, eye, dir, res);
    queueSoundAt(reg, "weapon.nailgun", eye);
}
```

The `Weapon` is a local — it exists only for the duration of the call, carrying `damage` and
`projectileSpeed` from the `RangedAttack` into `fireProjectile`. Because the shooter `self` is an enemy,
Step 2's derivation stamps the resulting projectile `Faction::Enemy` automatically — no faction argument
anywhere. `splashRadius` is explicitly zero, so an enemy bolt can *only* direct-hit; it has no area effect
that could catch another enemy even if the faction guard somehow missed. A nailgun fire sound plays at the
muzzle so the shot is audible.

> **Why fabricate a temporary `Weapon` rather than give enemies a real `WeaponInventory` or add an
> enemy-specific fire function?** Because the projectile machinery is already exactly what a ranged enemy
> needs — it spawns a moving, colliding, damaging bolt with a lifetime — and the only thing `fireProjectile`
> reads from a `Weapon` is a handful of scalar stats. Building a stack `Weapon` from the `RangedAttack` is the
> cheapest possible adapter between "enemy stats" and "the combat API," with no new fire path to maintain and
> no inventory an enemy would never otherwise use. Setting `splashRadius = 0` explicitly is belt-and-braces:
> the faction guard already stops an enemy bolt hurting enemies, but a splashless bolt *can't* hurt them even
> through the splash path, so the "no friendly fire" guarantee holds by two independent mechanisms.

---

## Step 9: Proving It Headless — `monster_ranged` and `friendly_fire`

Two new scenarios in `src/harness/headless_main.cpp` pin the behaviour. The first, `monster_ranged`, asserts
the whole loop: aggro, hold standoff, telegraph, fire, damage the player:

```cpp
    // Ranged enemy: aggros at distance, HOLDS standoff (doesn't rush to melee),
    // telegraphs, then fires dodgeable bolts that damage the player.
    bool scenario_monster_ranged(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);

        // Isolate: drop the melee grunts so only the ranged enemy acts / blocks LoS.
        std::vector<entt::entity> melee;
        for (auto e : reg.view<AIState>()) if (!reg.any_of<RangedAttack>(e)) melee.push_back(e);
        for (auto e : melee) reg.destroy(e);

        entt::entity ranged = entt::null;
        for (auto e : reg.view<AIState, RangedAttack, Position>()) { ranged = e; break; }
        if (ranged == entt::null) return report("monster_ranged", false, "no ranged enemy");

        float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        // Face-off in the clear x=16 lane: enemy south, player 10 units north — inside
        // range (16) but beyond standoffMin (7), so it should hold and shoot.
        teleportEnemy(reg, ranged, glm::vec3(16.0f, 0.95f, 8.0f));
        teleportPlayer(reg, player, glm::vec3(16.0f, halfY + 0.05f, 18.0f));

        float hpStart = reg.get<Health>(player).current;
        for (int i = 0; i < 40; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        bool aggroed = reg.valid(ranged) && reg.get<AIState>(ranged).target != entt::null;

        bool sawAttack = false;
        for (int i = 0; i < 400; i++)
        {
            applyInput(reg, player, Input{});
            qengine::stepSimulation(reg, jolt, level, dt);
            if (reg.valid(ranged) && reg.get<AIState>(ranged).state == AIStateKind::Attack) sawAttack = true;
        }
        float hpEnd    = reg.get<Health>(player).current;
        bool  damaged  = hpEnd < hpStart;

        float finalDist  = reg.valid(ranged)
            ? glm::length(reg.get<Position>(ranged).value - reg.get<Position>(player).value) : 0.0f;
        float standoffMin = reg.valid(ranged) ? reg.get<RangedAttack>(ranged).standoffMin : 0.0f;
        bool  held        = finalDist > standoffMin;   // never closed to melee
        // …
        return report("monster_ranged", aggroed && sawAttack && damaged && held, buf);
    }
```

Four assertions, all of which must hold: the enemy **aggroed** (its `target` latched onto the player), it
entered the **Attack** state at some point, the player took **damage** (`hpEnd < hpStart` — a bolt landed),
and it **held** standoff (its final distance stayed beyond `standoffMin`, i.e. it never rushed into melee).
The player is placed 10 units north — inside `range` (16) but outside `standoffMin` (7) — precisely the band
where the enemy should stand and shoot. Note the isolation: the melee grunts are destroyed first so only the
ranged enemy acts (and so a melee grunt can't wander in front of the bolt).

The second, `friendly_fire`, tests the faction guard *directly* — independent of AI aim — by spawning
projectiles by hand and checking who they hurt:

```cpp
    // Friendly-fire guard: an Enemy-faction projectile must NOT damage an enemy,
    // but a Player-faction one must. Tests updateProjectiles' faction check directly
    // (independent of AI aim). Also: a Player projectile must not damage the player.
    bool scenario_friendly_fire(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        // …
        // Spawn a projectile overlapping `target` and let it resolve.
        auto fireAt = [&](entt::entity target, Faction faction) {
            glm::vec3 tp = reg.get<Position>(target).value;
            auto p = reg.create();
            reg.emplace<Position>(p, tp - glm::vec3(0.0f, 0.0f, 0.3f));
            reg.emplace<Velocity>(p, glm::vec3(0.0f, 0.0f, 2.0f));   // into the target
            reg.emplace<AABBCollider>(p, glm::vec3(0.15f), false);
            reg.emplace<Projectile>(p, 50.0f, 0.0f, 0.0f, entt::null, faction);
            reg.emplace<Lifetime>(p, 1.0f);
            for (int i = 0; i < 4; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }
        };

        float dEnemyBefore = reg.get<Health>(dummy).current;
        fireAt(dummy, Faction::Enemy);                       // same side → no damage
        float dAfterEnemy  = reg.valid(dummy) ? reg.get<Health>(dummy).current : -1.0f;

        fireAt(dummy, Faction::Player);                      // opposite side → damage
        float dAfterPlayer = reg.valid(dummy) ? reg.get<Health>(dummy).current : -1.0f;

        float pBefore = reg.get<Health>(player).current;
        fireAt(player, Faction::Player);                     // own side → no self-damage
        float pAfter  = reg.get<Health>(player).current;

        bool enemyBoltSpared  = dAfterEnemy  == dEnemyBefore;   // enemy bolt didn't hurt enemy
        bool playerBoltHit    = dAfterPlayer <  dAfterEnemy;    // player bolt hurt enemy
        bool playerSelfSpared = pAfter       == pBefore;        // player bolt didn't hurt player
        // …
        return report("friendly_fire", enemyBoltSpared && playerBoltHit && playerSelfSpared, buf);
    }
```

It fires three hand-built bolts at point-blank into an inert enemy `dummy` (an `AIState` entity with *no*
`JoltCharacter`/`AIPath`, so `aiSystem` never moves it — a still, deterministic target) and the player:

- an **Enemy**-faction bolt at the enemy dummy → must **not** damage it (`enemyBoltSpared`);
- a **Player**-faction bolt at the enemy dummy → must damage it (`playerBoltHit`);
- a **Player**-faction bolt at the player → must **not** self-damage (`playerSelfSpared`).

All three must hold for the scenario to pass. Both are registered in the dispatch alongside the existing enemy
scenarios:

```cpp
    else if (scenario == "monster_ranged")   pass = scenario_monster_ranged(registry, jolt, level, dt);
    else if (scenario == "friendly_fire")    pass = scenario_friendly_fire(registry, jolt, level, dt);
```

The existing melee scenarios were tightened to keep measuring the *melee* grunt now that a ranged one shares
the arena: `monster_grunt`, `monster_ai`, and `monster_path` each skip any enemy with a `RangedAttack` when
they pick "the grunt" (e.g. `if (reg.any_of<RangedAttack>(e)) continue;`), so a stray blue enemy can't be
mistaken for the red one under test.

> **Why test the faction guard with hand-spawned projectiles in `friendly_fire` when `monster_ranged` already
> fires real enemy bolts?** Because the two scenarios test *different* things, and mixing them would make each
> weaker. `monster_ranged` tests the whole *behaviour* — does the AI aggro, hold, telegraph, and land a shot?
> — which is inherently timing- and aim-dependent. `friendly_fire` tests the *rule* — does a same-faction bolt
> spare its side? — and that rule must hold regardless of whether any AI ever aims correctly. Spawning the
> projectiles directly, overlapping the target, removes all the AI variability and pins the faction check to a
> single deterministic frame: bolt in, resolve, did health change? Testing the guard independently of the aim
> means a future AI bug can't accidentally hide a faction regression, and vice versa.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/ecs/components/combat.h` | New `enum class Faction { Player, Enemy }`; new `Faction faction = Faction::Player` field on `Projectile`. |
| `engine/ecs/systems/combat/fire_projectile.cpp` | Derives the projectile's faction from the shooter (`any_of<AIState>(shooter) ? Enemy : Player`) — no signature change. |
| `engine/ecs/systems/combat/update_projectiles.cpp` | Friendly-fire guard: skip other projectiles (`any_of<Projectile>`), skip same-faction targets (`Enemy`+`AIState`, `Player`+`TagPlayer`). Props match neither and stay hittable. |
| `engine/ecs/systems/combat/raycast_entities.cpp` | Ignore projectiles in entity raycasts (`any_of<Projectile>` → `continue`) — fixes an enemy's own bolt blocking its line of sight, and stops bolts being shootable. |
| `engine/ecs/components/gameplay.h` | New `RangedAttack { range, standoffMin, damage, projectileSpeed, windup, cooldown, windupTimer }` component — the data that makes an enemy ranged. |
| `engine/level/spawn_monster.cpp` | New `spawnMonsterRanged` = grunt + `RangedAttack` + blue tint. |
| `engine/level/factories.h` | Declares `spawnMonsterRanged`. |
| `engine/level/classname_factory.cpp` | `make_monster_ranged` shim + `"monster_ranged"` table entry for the `.map` loader. |
| `engine/level/showcase_descriptor.cpp` | Adds one `monster_ranged` to the showcase arena. |
| `engine/level/map_to_descriptors.cpp` | `groundHalfHeight` returns `0.9` for `monster_ranged` too (same grunt collider) so a floor-placed one stands correctly. |
| `engine/ecs/systems/enemy/ai_fire_bolt.cpp` | New `aiFireEnemyBolt`: builds a throwaway `Weapon` from `RangedAttack` stats, fires via `fireProjectile` (auto-tagged `Enemy`), plays the nailgun sound. No splash. |
| `engine/ecs/systems/enemy/ai_line_of_sight.cpp` | `aiClearLineOfSight` — the enemy's sight raycast (level surfaces + entities, ignoring self, player, and now projectiles). |
| `engine/ecs/systems/enemy/ai_system.cpp` | New ranged branch before melee: hold at standoff (`move` back off inside `standoffMin`), telegraph (`windupTimer`), fire on release, restart `cooldown`; out of range/LoS → abandon windup, fall through to Chase. |
| `harness/headless_main.cpp` | New `monster_ranged` + `friendly_fire` scenarios; melee scenarios skip `RangedAttack` enemies when picking the grunt under test. |

---

## What You Should See

Run `build/QEngine.exe` (the showcase):

1. **A blue enemy that keeps its distance.** The `monster_ranged` grunt aggroes on sight but *doesn't* rush
   you — it holds at range and backs off if you close inside its standoff band.
2. **Telegraphed, dodgeable bolts.** It squares up, pauses briefly, then lobs a slow bolt you can sidestep;
   stand still and it chips your health, strafe and you dodge.
3. **No friendly fire.** Its bolts pass harmlessly through the red melee grunts; your rocket splash at your
   own feet doesn't gib you.
4. **It keeps shooting through its own volley.** An enemy no longer stops firing because its previous bolt is
   in front of its face — the LoS raycast ignores projectiles.

Headless:

5. **`monster_ranged` passes** — aggroed, entered Attack, damaged the player, held standoff (never closed to
   melee).
6. **`friendly_fire` passes** — enemy bolt spared the enemy, player bolt hurt it, player bolt spared the
   player.
7. **`monster_grunt`, `monster_ai`, `monster_path` still pass** — the melee grunt is unchanged (and the
   scenarios now explicitly pick the melee one).

---

## What's Next

Enemies now fight at range and never friendly-fire — which means, for the first time, the player takes hits
they didn't line up for, from a direction they may not be looking. That raises a readability problem the HUD
has to answer: *where did that come from, and did my shot connect?* **Chapter 31c** grows the crosshair and
HUD to reflect all of this — a dynamic crosshair whose spread reacts to movement and firing, hit and kill
markers, a low-ammo cue, and a damage-direction chevron that points at the attacker (fed by exactly the
enemy melee and ranged hits built in 31a and 31b) — with the whole HUD *state* moved into the ECS context so
it's testable headless.
