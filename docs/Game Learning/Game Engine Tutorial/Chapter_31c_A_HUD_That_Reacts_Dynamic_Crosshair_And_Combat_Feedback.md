# Chapter 31c: A HUD That Reacts — Dynamic Crosshair and Combat Feedback

## What You'll Learn
- The central design idea: put the HUD's *state* — crosshair spread, markers, low-ammo, damage direction —
  in a **`HudSignals` context struct**, recomputed each tick by a `hudSignalSystem`, so the whole thing is
  **testable headless** even though the OpenGL draw needs a live GL context
- Splitting "compute the signal" (headless, in `stepSimulation`) from "draw the signal" (GL, in
  `debugHudSystem`), and why the transient markers are *set at their event sites* but *decayed* in one place
- A **dynamic crosshair** whose gap is `base + weapon-spread + movement + recoil`, with **recoil derived from
  the weapon's cooldown** (no extra combat write) so it kicks on fire and settles as the weapon becomes ready
- **Hit and kill markers** — a white "X" on a shot that damages an enemy, red on a killing blow — set from
  both the hitscan and projectile impact paths
- A **damage-direction chevron** that points at whatever just hurt you, fed by enemy melee (`aiSystem`) and
  enemy bolts (`updateProjectiles`), and projected from a **world-XZ direction** to a **screen angle** using
  the camera's facing
- A **low-ammo red cue** on the ammo readout, computed from the current weapon's pool and its low threshold
- A **`showDebug` flag** that gates the FPS/debug text so the production HUD is clean
- Proving the lot headless with the `hud_signals` scenario — asserting *state*, not pixels

---

## Where We Are

The HUD so far (Chapters 16, 17c) is an immediate-mode overlay in `systems/debug_hud/`: a static crosshair,
health/armour bars, an ammo readout, a weapon bar, a damage-flash tint, and debug text (FPS, position). It's
functional, but it's a *readout* — it shows numbers, it doesn't *react*. Meanwhile Chapters 31a and 31b turned
enemies into real threats that chase, block, and shoot you from a distance you may not be facing. The HUD now
has things worth reacting to.

This chapter grows the HUD from a readout into **game feel**: a crosshair that breathes with your movement and
firing, markers that confirm your shots landed, a cue when you're low on ammo, and a chevron that tells you
which way the hit came from. None of that pulls in a UI framework — it's more of the same immediate-mode
`draw_*` passes — but it introduces one architectural move that's the real lesson: the HUD's *state* moves
out of the draw code and into the ECS **context**, where it can be computed and *tested without a GL context*.

Everything below is grounded in `src/engine/ecs/components/core.h`,
`src/engine/ecs/systems/hud/hud_signal_system.{h,cpp}`, `systems/debug_hud/{debug_hud_system,draw_crosshair,
draw_hit_marker,draw_damage_arc,draw_ammo}.cpp` (+ `debug_hud_internal.h`),
`systems/combat/{fire_hitscan,update_projectiles}.cpp`, `systems/enemy/ai_system.cpp`,
`src/engine/app/simulation.cpp`, and `src/harness/headless_main.cpp`.

---

## Step 1: The Idea — HUD State Lives in a `HudSignals` Context Struct

The draw code needs a GL context; the *decisions* behind it don't. Whether the crosshair should be wide or
tight, whether a hit-marker is live, how low your ammo is — those are computable from the ECS with no window
open. So they move into a plain struct in the registry context, `HudSignals` in
`src/engine/ecs/components/core.h`:

```cpp
// Transient HUD event signals (combat/AI → HUD), recomputed each fixed tick by
// hudSignalSystem and read by debugHudSystem. Lives in the registry context so
// the *state* is testable headless — the GL HUD only draws it. Timers count down
// in seconds; a value > 0 means "show this for now".
struct HudSignals
{
	float     crosshairGap    = 2.0f;            // half-gap (px): base + spread + movement + recoil
	float     recoil          = 0.0f;            // kick derived from the current weapon's cooldown
	float     hitMarkerTimer  = 0.0f;            // >0 briefly after a player shot damages an enemy
	float     killMarkerTimer = 0.0f;            // >0 briefly after a player shot kills an enemy
	float     damageDirTimer  = 0.0f;            // >0 briefly after the player takes a hit
	glm::vec2 damageDir       = glm::vec2(0.0f); // world-XZ unit dir toward the last attacker
	bool      lowAmmo         = false;           // current weapon's pool at/under the low threshold
	bool      showDebug       = true;            // gate the FPS/debug text (clean production HUD)

	static constexpr float kMarkerTime    = 0.25f; // hit/kill marker lifetime
	static constexpr float kDamageDirTime = 1.2f;  // damage-direction arc lifetime
};
```

Every reactive element of the HUD is a field here. Two kinds live together:

- **Continuous state**, recomputed from scratch every tick — `crosshairGap`, `recoil`, `lowAmmo`.
- **Transient events**, *set* to a lifetime when they happen and *counted down* to zero — the three `*Timer`s
  and `damageDir`. A timer `> 0` means "show this for now"; the two `static constexpr` lifetimes (0.25s for
  markers, 1.2s for the damage arc) are the durations.

It's installed once, in `buildWorld` (`src/engine/app/simulation.cpp`), right beside the sound queue:

```cpp
        // Context singletons: sound-event queue (windowed drains it) + HUD signal
        // state (updated headless by hudSignalSystem, drawn by the GL HUD).
        registry.ctx().emplace<SoundQueue>();
        registry.ctx().emplace<HudSignals>();
```

> **Why move HUD state into the ECS context at all — the old HUD just read the ECS directly at draw time?**
> Because "read the ECS directly at draw time" ties the *logic* to the *draw*, and the draw needs a GL
> context the headless harness doesn't have. By computing the signals into a `HudSignals` struct during the
> simulation tick — which the harness runs — the *decisions* become assertable without a window: a test can
> fire a shot and check `hud.hitMarkerTimer > 0`, or move the player and check `hud.crosshairGap` grew, with
> no pixels involved. The GL HUD then becomes a dumb renderer of whatever `HudSignals` holds. This is the
> same split the engine uses elsewhere (compute in a system, render from context) and it's what makes an
> otherwise untestable, GL-bound feature — a *reactive crosshair* — provable in a headless scenario.

---

## Step 2: Computing the Signals — `hudSignalSystem`

The continuous fields and the timer decay are recomputed each fixed tick by a new system,
`src/engine/ecs/systems/hud/hud_signal_system.cpp`. It runs last in `stepSimulation` so it sees the settled
state of the tick:

```cpp
void hudSignalSystem(entt::registry& registry)
{
    HudSignals* hud = registry.ctx().find<HudSignals>();
    if (!hud) return;
    const float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;

    // Decay the transient event markers (set at their event sites).
    hud->hitMarkerTimer  = std::max(0.0f, hud->hitMarkerTimer  - dt);
    hud->killMarkerTimer = std::max(0.0f, hud->killMarkerTimer - dt);
    hud->damageDirTimer  = std::max(0.0f, hud->damageDirTimer  - dt);

    entt::entity player = entt::null;
    for (auto e : registry.view<TagPlayer, WeaponInventory>()) { player = e; break; }
    if (player == entt::null) return;

    const auto& inv = registry.get<WeaponInventory>(player);
    const Weapon& weapon = inv.weapons[inv.currentWeapon];

    // Recoil is derived from how far into its cooldown the weapon is — full kick the
    // instant it fires, decaying to zero as it becomes ready again.
    hud->recoil = (weapon.fireRate > 0.0f)
        ? (weapon.cooldownRemaining / weapon.fireRate) * kRecoilKick : 0.0f;

    float spreadTerm = weapon.spread * kSpreadPx;
    float moveTerm   = std::min(horizontalSpeed(registry, player), kMaxMove) * kMovePx;
    hud->crosshairGap = kBaseGap + spreadTerm + moveTerm + hud->recoil;

    // Low-ammo cue: fewer than kLowShots shots left in this weapon's pool.
    if (const Ammo* ammo = registry.try_get<Ammo>(player))
    {
        int perShot = std::max(1, weapon.ammoPerShot);
        hud->lowAmmo = inv.owned[inv.currentWeapon]
                    && ammoInPool(*ammo, weapon.type) < kLowShots * perShot;
    }
    else hud->lowAmmo = false;
}
```

Note the two-phase structure. First it **decays** the three event timers (they were *set* elsewhere; here they
tick down). Then it **recomputes** the continuous fields from the current weapon and player state. The
constants live in an anonymous namespace at the top of the file:

```cpp
    constexpr float kBaseGap    = 2.0f;   // resting crosshair half-gap (px)
    constexpr float kSpreadPx   = 40.0f;  // weapon spread (radians) → px
    constexpr float kMovePx     = 0.9f;   // per unit/s of horizontal speed
    constexpr float kMaxMove    = 7.0f;   // movement term saturates here
    constexpr float kRecoilKick = 8.0f;   // full kick right after a shot
    constexpr int   kLowShots   = 4;      // "low ammo" when fewer than this many shots remain
```

The system is wired into `stepSimulation` as the last step, so the crosshair reflects everything that happened
this tick:

```cpp
        enemyDeathSystem(registry);     // remove grunts whose health hit 0
        demoResetSystem(registry);
        hudSignalSystem(registry);      // recompute crosshair spread / markers / low-ammo
```

The header (`hud_signal_system.h`) states the contract — and, importantly, that this system only *decays* the
event timers; it never sets them:

```cpp
// Recompute the player-facing HUD signals each fixed tick: crosshair spread
// (base + weapon spread + movement + recoil), the low-ammo flag, and the decay
// of the transient hit/kill/damage-direction timers. Runs in stepSimulation so
// the HUD *state* is exercised headless; the GL HUD (debugHudSystem) only draws
// what this leaves in the HudSignals context. Hit/kill/damage-dir timers are
// *set* at their event sites (combat / aiSystem); this only decays them.
void hudSignalSystem(entt::registry& registry);
```

> **Why set the event timers at their event sites (combat / AI) but decay them here, rather than doing both in
> one place?** Because the *set* and the *decay* have different natural owners. A hit-marker's trigger is a
> single, specific moment — a bolt or a hitscan actually damaging an enemy — and that moment is inside
> `updateProjectiles` / `fireHitscan`, which already know it happened and have the target in hand. But the
> *decay* is uniform, per-tick bookkeeping that doesn't care where the event came from; doing it in one place
> (`hudSignalSystem`) means there's exactly one line per timer that counts it down, with no risk of a timer
> being set-and-forgotten. Set-at-source, decay-in-one-place is the same pattern the `DamageFlash` timers use:
> the interesting event stamps a lifetime, and a single system ages every lifetime down. It keeps the event
> sites focused ("I hit something → stamp the marker") and the aging trivial ("subtract dt, clamp at zero").

---

## Step 3: The Dynamic Crosshair

The crosshair gap `hudSignalSystem` computes is `kBaseGap + spreadTerm + moveTerm + recoil`. Read the three
dynamic terms:

- **`spreadTerm = weapon.spread * kSpreadPx`** — a weapon with a wider cone of inaccuracy shows a wider
  crosshair, so accuracy is *legible per weapon*: the shotgun's spread makes a visibly bigger reticle than
  the railgun's zero spread.
- **`moveTerm = min(horizontalSpeed, kMaxMove) * kMovePx`** — moving opens the crosshair (up to a saturation
  cap), telling you your shots are less accurate on the move. `horizontalSpeed` reads the player character's
  Jolt velocity directly.
- **`recoil`** — a kick that spikes the instant you fire and settles as the weapon cools down (Step 4).

That single `gap` scalar is handed to the crosshair draw, which is otherwise Chapter 16's crosshair with one
new parameter. From `src/engine/ecs/systems/debug_hud/draw_crosshair.cpp`:

```cpp
void drawCrosshair
(
	float centreX, float centreY, float gap,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& color
)
{
	// Crosshair: two lines gapped by `gap` px each side of centre. The gap grows
	// with weapon spread / movement / recoil (see hudSignalSystem).
	float arm = 10.0f;

	// 4 vertices: horizontal line (2) + vertical line (2)
	float vertices[] =
	{
		// horizontal line
		centreX - arm, centreY, 0.0f,
		centreX - gap, centreY, 0.0f,
		centreX + gap, centreY, 0.0f,
		centreX + arm, centreY, 0.0f,
		// vertical line
		centreX, centreY - arm, 0.0f,
		centreX, centreY - gap, 0.0f,
		centreX, centreY + gap, 0.0f,
		centreX, centreY + arm, 0.0f
	};
	// … upload + GL_LINES draw of 4 segments …
}
```

The four arms each run from `arm` (10px, the outer tip) inward to `gap` (the inner end). A bigger `gap` pushes
the inner ends outward — the crosshair *opens*. The orchestrator passes the live gap from the context, in
`src/engine/ecs/systems/debug_hud/debug_hud_system.cpp`:

```cpp
	// Crosshair (screen centre) — gap grows with spread/movement/recoil.
	float cx = windowWidth * 0.5f;
	float cy = windowHeight * 0.5f;
	float gap = sig ? sig->crosshairGap : 2.0f;
	drawCrosshair(cx, cy, gap, shader, ortho, glm::vec3(1.0f));
```

`sig` is `registry.ctx().find<HudSignals>()`; if for some reason it's absent, the gap falls back to the resting
`2.0`. The draw is pure geometry — all the *behaviour* is upstream in `hudSignalSystem`.

> **Why compute one combined `crosshairGap` scalar in the system rather than pass spread, movement, and recoil
> to `drawCrosshair` and let it add them?** Because the *policy* — how spread, movement, and recoil combine
> into a gap — is game logic, not drawing, and game logic belongs in the headless-testable system. `drawCrosshair`
> should know one thing: how far apart to put the arms. Folding the three terms into a single `crosshairGap` in
> `hudSignalSystem` means the combination rule is computed where it can be asserted (the `hud_signals` scenario
> checks `crosshairGap` grew with movement and firing) and the draw stays a trivial function of one number.
> It also means the *same* gap is available to anything else that might want it later without re-deriving the
> formula.

---

## Step 4: Recoil Derived From the Weapon's Cooldown

The neat trick in the gap formula is `recoil`. Rather than have the combat code *write* a recoil value when it
fires — an extra coupling from combat into the HUD — the HUD *derives* recoil from a value combat already
maintains: the weapon's `cooldownRemaining`. From `hudSignalSystem`:

```cpp
    // Recoil is derived from how far into its cooldown the weapon is — full kick the
    // instant it fires, decaying to zero as it becomes ready again.
    hud->recoil = (weapon.fireRate > 0.0f)
        ? (weapon.cooldownRemaining / weapon.fireRate) * kRecoilKick : 0.0f;
```

Every weapon already has a `fireRate` (seconds between shots) and a `cooldownRemaining` that combat sets to
`fireRate` on firing and decays to zero as the weapon becomes ready again. The ratio
`cooldownRemaining / fireRate` is therefore **1.0 the instant you fire** and **0.0 when the weapon is ready** —
a normalised "how recently did I shoot?" that's already sitting in the `Weapon`. Multiply by `kRecoilKick`
(8px) and you have a recoil term that spikes on fire and settles as the cooldown elapses, for free.

> **Why derive recoil from the cooldown instead of giving `HudSignals` its own recoil value that the fire code
> sets and decays?** Because the cooldown *already is* a "time since I fired, normalised" signal — adding a
> parallel recoil timer would be a second thing tracking the same fact, that combat would have to remember to
> set on every fire path and that could drift out of sync with the actual cooldown. Deriving it means recoil
> is, by construction, exactly in step with the weapon's readiness: it's full at the moment of firing and gone
> the moment you can fire again, with no extra state and no write from combat into the HUD. It also makes
> recoil automatically *per-weapon* — a slow weapon with a long `fireRate` has a recoil kick that visibly
> lingers, a fast one snaps back — without any per-weapon recoil tuning, because the shape falls out of the
> cooldown the weapon already has.

---

## Step 5: Hit and Kill Markers — Set at the Impact Sites

A hit-marker is the little "X" that flashes over your crosshair when a shot connects — white for a hit, red
for a kill. The *timers* are fields on `HudSignals`, but they're **set at the impact sites**, because that's
where "a player shot damaged an enemy" is known. Both fire paths set them.

The hitscan path, in `src/engine/ecs/systems/combat/fire_hitscan.cpp`, right after `applyDamage` succeeds on an
enemy:

```cpp
				// HUD hit/kill marker (a player shot landed on an enemy).
				if (registry.any_of<AIState>(entityHit->entity))
					if (HudSignals* hud = registry.ctx().find<HudSignals>())
					{
						hud->hitMarkerTimer = HudSignals::kMarkerTime;
						const Health* h = registry.try_get<Health>(entityHit->entity);
						if (h && h->current <= 0.0f) hud->killMarkerTimer = HudSignals::kMarkerTime;
					}
```

The projectile path, in `src/engine/ecs/systems/combat/update_projectiles.cpp`, does the same on a *player*
bolt hitting an enemy (and, as a bonus we'll use in Step 6, sets the damage direction on an *enemy* bolt
hitting the player):

```cpp
						// HUD signals: a player bolt hitting an enemy → hit/kill marker;
						// an enemy bolt hitting the player → damage-direction (toward the
						// source, i.e. against the bolt's travel).
						if (HudSignals* hud = registry.ctx().find<HudSignals>())
						{
							if (proj.faction == Faction::Player && registry.any_of<AIState>(target))
							{
								hud->hitMarkerTimer = HudSignals::kMarkerTime;
								const Health* h = registry.try_get<Health>(target);
								if (h && h->current <= 0.0f) hud->killMarkerTimer = HudSignals::kMarkerTime;
							}
							else if (proj.faction == Faction::Enemy && registry.any_of<TagPlayer>(target))
							{
								glm::vec2 from(-vel.value.x, -vel.value.z);
								if (glm::length(from) > 0.001f) hud->damageDir = glm::normalize(from);
								hud->damageDirTimer = HudSignals::kDamageDirTime;
							}
						}
```

Both paths use the same logic: the shot damaged something, that something is an enemy (`AIState`), so stamp
`hitMarkerTimer`; and if the enemy's health is now `<= 0`, it's a kill, so *also* stamp `killMarkerTimer`. The
timers are set to `kMarkerTime` (0.25s) and then decayed by `hudSignalSystem` (Step 2). The draw picks kill
over hit when both are live, in `debug_hud_system.cpp`:

```cpp
	// Hit / kill marker (kill wins if both are live).
	if (sig && sig->killMarkerTimer > 0.0f)
		drawHitMarker(cx, cy, shader, ortho, glm::vec3(1.0f, 0.25f, 0.2f));
	else if (sig && sig->hitMarkerTimer > 0.0f)
		drawHitMarker(cx, cy, shader, ortho, glm::vec3(1.0f));
```

`drawHitMarker` (`src/engine/ecs/systems/debug_hud/draw_hit_marker.cpp`) is a small diagonal "X" — four short
line segments angling out from just off-centre, coloured by the caller (white for hit, red for kill):

```cpp
// A small diagonal "X" over the crosshair — the classic "your shot connected" cue.
// Drawn white for a hit, red for a kill (colour chosen by the caller).
void drawHitMarker
(
	float centreX, float centreY,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& color
)
{
	float in  = 5.0f;   // inner gap from centre
	float out = 11.0f;  // outer reach
	// … four diagonal segments (top-left/right, bottom-left/right) …
	glDrawArrays(GL_LINES, 0, 8);
	// …
}
```

> **Why set the markers where the damage lands rather than have `hudSignalSystem` scan for recent hits?**
> Because "a player shot connected" is a *fact known only at the moment of impact* — it needs the specific
> target, whether it was an enemy, and whether that hit was fatal, all of which `fireHitscan` and
> `updateProjectiles` have in hand right there. A system running later would have to reconstruct that from
> some persisted record ("which enemy lost health this tick, and to whose shot?"), which is exactly the
> transient event you'd have to *store* anyway — so you might as well store the conclusion (a marker timer) at
> the source. The event site knows the answer; `hudSignalSystem` just ages it. That's why the marker fields
> live in the shared `HudSignals` context: the combat systems *write* them, the HUD *reads* them, and the
> context is the channel between the two.

---

## Step 6: The Damage-Direction Chevron

When something hurts you, a red chevron appears around the crosshair pointing at the source — so you know
which way to turn. This pays off directly from Chapter 31b: a ranged enemy can hit you from a direction you're
not facing, and the chevron tells you where. It has two feeders and one projection.

**Feeder 1 — enemy bolts**, already shown in Step 5's `updateProjectiles` block: an `Enemy`-faction bolt
hitting the player sets `damageDir` to `-velocity` (the direction *back toward* where the bolt came from) and
stamps `damageDirTimer`.

**Feeder 2 — enemy melee**, in `src/engine/ecs/systems/enemy/ai_system.cpp`, when a grunt lands a melee hit:

```cpp
            if (ai.attackCooldown <= 0.0f && applyDamage(registry, player, kAttackDamage))
            {
                queueSoundAt(registry, "weapon.gauntlet", pos.value);
                ai.attackCooldown = kAttackPeriod;
                // HUD damage-direction: the melee hit came from this grunt.
                if (HudSignals* hud = registry.ctx().find<HudSignals>())
                {
                    hud->damageDir = glm::length(flat) > 0.001f
                        ? glm::vec2(toPlayer.x, toPlayer.z) * -1.0f : glm::vec2(0.0f);
                    hud->damageDirTimer = HudSignals::kDamageDirTime;
                }
            }
```

`toPlayer` points from the grunt to the player, so `-toPlayer` points from the player back at the grunt —
`damageDir` always stores the **world-XZ unit direction toward the attacker**.

The projection from that world direction to a *screen angle* happens at draw time, because it depends on where
the camera is looking, in `debug_hud_system.cpp`:

```cpp
	// Damage-direction chevron: world-XZ attacker dir → screen angle vs camera yaw.
	if (sig && sig->damageDirTimer > 0.0f)
	{
		const auto* cam = registry.ctx().find<CameraDirection>();
		glm::vec2 fwd = cam ? glm::vec2(cam->value.x, cam->value.z) : glm::vec2(0.0f, -1.0f);
		if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
		glm::vec2 right(-fwd.y, fwd.x);            // world screen-right in XZ
		glm::vec2 d = sig->damageDir;
		float angle = std::atan2(glm::dot(d, right), glm::dot(d, fwd));  // 0 = ahead
		float alpha = sig->damageDirTimer / HudSignals::kDamageDirTime;
		drawDamageArc(cx, cy, angle, alpha, shader, ortho);
	}
```

It builds a screen basis from the camera's forward direction (`fwd`) and its perpendicular (`right`), then
projects the stored world direction onto that basis: `atan2(dot(d, right), dot(d, fwd))` gives the angle of the
attacker *relative to where you're looking* — `0` means dead ahead, positive means to your right. The chevron's
opacity fades as `damageDirTimer` runs down. `drawDamageArc` (`draw_damage_arc.cpp`) places a red chevron at
that angle on a ring around the crosshair:

```cpp
// A red chevron placed around the crosshair at `screenAngle` (0 = straight ahead
// / up on screen, +x = to the right, growing clockwise), pointing outward toward
// where the last damage came from. Fades out via `alpha`.
void drawDamageArc
(
	float centreX, float centreY, float screenAngle, float alpha,
	unsigned int shaderId, const glm::mat4& projection
)
{
	const float radius = 46.0f;   // distance from crosshair centre
	const float depth  = 10.0f;   // chevron length (tip vs base)
	const float half   = 9.0f;    // chevron half-width
	// … build screen basis at the angle, place tip/base/wings, draw 2 segments …
}
```

> **Why store the damage direction in *world* XZ and convert to a screen angle at draw time, rather than store
> the screen angle directly?** Because the attacker's world position is fixed at the instant of the hit, but
> *where you're looking* keeps changing while the chevron is on screen (1.2 seconds is plenty of time to
> turn). If the chevron baked in a screen angle at hit-time, it would point at a fixed spot on the HUD and
> lie the moment you rotated. Storing the *world* direction and re-projecting it every frame against the live
> camera facing means the chevron keeps pointing at the actual attacker as you turn — spin toward it and the
> chevron swings to dead-ahead, which is exactly the feedback loop it's for. The world direction is the
> durable fact; the screen angle is a per-frame view of it.

---

## Step 7: The Low-Ammo Cue and the `showDebug` Gate

Two smaller signals round it out. The **low-ammo** flag (`hud->lowAmmo`, computed in Step 2 — true when the
current weapon's pool holds fewer than `kLowShots` shots' worth) turns the ammo readout red, in
`src/engine/ecs/systems/debug_hud/draw_ammo.cpp`:

```cpp
		// Red when the current pool is low (flag computed by hudSignalSystem).
		const HudSignals* hud = registry.ctx().find<HudSignals>();
		glm::vec3 col = (hud && hud->lowAmmo) ? glm::vec3(0.9f, 0.1f, 0.1f) : glm::vec3(0.0f);
		drawText(x, y, ammoText, shaderId, projection, scale, col);
```

The `ammoInPool` helper in `hud_signal_system.cpp` maps the weapon type to the right pool (shells / nails /
rockets / cells), and `lowAmmo` is only set when the weapon is actually `owned` — so an unowned slot never
false-alarms.

The **`showDebug`** flag gates the FPS/debug text so the production HUD is clean, in `debug_hud_system.cpp`:

```cpp
	// FPS (top-left, white) on a legibility panel — debug-only, hidden in the clean
	// production HUD unless HudSignals::showDebug is on.
	if (!sig || sig->showDebug)
	{
		char fpsText[64];
		snprintf(fpsText, sizeof(fpsText), "FPS: %.0f", fps);
		drawPanel(2.0f, 2.0f, 120.0f, 22.0f, shader, ortho, panelColor, panelAlpha);
		drawText(5.0f, 5.0f, fpsText, shader, ortho, textScale, glm::vec3(1.0f));
	}
```

`showDebug` defaults to `true`, so the FPS text still shows unless something flips the flag. Everything else on
the HUD — bars, ammo, weapon bar, crosshair, markers — is always drawn; only the developer text is gated.

> **Why compute `lowAmmo` as a flag in `hudSignalSystem` rather than let `drawAmmo` compare the ammo count to a
> threshold itself?** Because `drawAmmo` runs only when there's a GL context, and the low-ammo *rule* — which
> pool this weapon draws from, what the threshold is in shots, whether the weapon is even owned — is exactly
> the kind of decision the headless harness should be able to assert. Computing it once in `hudSignalSystem`
> puts the rule where it's testable (the `hud_signals` scenario flips the shell count and checks `lowAmmo`
> toggles) and leaves `drawAmmo` to do the one thing it must do in GL: pick a colour. The `showDebug` flag,
> by contrast, is a pure presentation toggle with no logic behind it, so it's read straight where the text is
> drawn — there's nothing to compute or test, only a bool to honour.

---

## Step 8: Proving It Headless — the `hud_signals` Scenario

Because all the state lives in `HudSignals`, the whole HUD is assertable with no window. The `hud_signals`
scenario in `src/harness/headless_main.cpp` exercises every signal and checks the *state*, not pixels:

```cpp
    // HUD signal state (ctx, testable headless): crosshair spread widens with
    // movement + firing, low-ammo flag, hit/kill markers on shooting an enemy, and
    // the damage-direction timer when the player is hit.
    bool scenario_hud_signals(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        const HudSignals& hud = reg.ctx().get<HudSignals>();
        float halfY = reg.get<AABBCollider>(player).halfExtents.y;
        clearEnemies(jolt, reg);   // isolate crosshair/marker measurements

        auto idle = [&]{ applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); };

        // Baseline gap (idle, settled) in the open x=10 lane.
        teleportPlayer(reg, player, glm::vec3(10.0f, halfY, 15.0f));
        for (int i = 0; i < 40; i++) idle();
        float restGap = hud.crosshairGap;

        // Movement widens the crosshair.
        Input walk; walk.wishDir = glm::vec3(0, 0, 1); walk.lookDir = glm::vec3(0, 0, 1);
        for (int i = 0; i < 25; i++) { applyInput(reg, player, walk); qengine::stepSimulation(reg, jolt, level, dt); }
        float moveGap = hud.crosshairGap;
        for (int i = 0; i < 40; i++) idle();   // settle

        // Firing widens it (recoil kick), then it decays back.
        Input fire; fire.fire = true; fire.lookDir = glm::vec3(0, 0, -1);
        applyInput(reg, player, fire); qengine::stepSimulation(reg, jolt, level, dt);
        float fireGap = hud.crosshairGap;
        for (int i = 0; i < 40; i++) idle();

        // Low-ammo flag flips at the threshold.
        reg.get<Ammo>(player).shells = 2;  idle();  bool lowSet   = hud.lowAmmo;
        reg.get<Ammo>(player).shells = 25; idle();  bool lowClear = !hud.lowAmmo;
        // … shoot a dummy enemy dead for hit + kill markers …
        // … strike the player with an Enemy-faction bolt for damageDir …

        bool pass = moveGap > restGap + 0.5f && fireGap > restGap + 1.0f
                 && lowSet && lowClear && sawHit && sawKill && dmgDir;
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "gap rest=%.1f move=%.1f fire=%.1f; low set=%d clear=%d; hit=%d kill=%d; dmgDir=%d",
            restGap, moveGap, fireGap, lowSet?1:0, lowClear?1:0, sawHit?1:0, sawKill?1:0, dmgDir?1:0);
        return report("hud_signals", pass, buf);
    }
```

Every assertion is a *comparison of state values*, no rendering involved:

- **crosshair** — measure `crosshairGap` at rest, while walking, and the tick after firing; require
  `moveGap > restGap + 0.5` and `fireGap > restGap + 1.0` (movement and recoil each visibly widen it);
- **low-ammo** — drop shells to 2 and require `lowAmmo` set; raise to 25 and require it clear;
- **markers** — shoot a stationary dummy enemy repeatedly and require both `sawHit` and `sawKill` (the kill
  marker fires on the fatal shot);
- **damage direction** — strike the player with a hand-spawned `Enemy`-faction bolt and require
  `damageDirTimer > 0`.

It's registered in the dispatch beside the other new scenarios:

```cpp
    else if (scenario == "hud_signals")      pass = scenario_hud_signals(registry, jolt, level, dt);
```

> **Why is it worth a headless scenario for a HUD — isn't a HUD inherently a visual thing you check by eye?**
> Because the *logic* of a reactive HUD is where the bugs are, and that logic is now pure state. Whether the
> crosshair *looks* right is a visual judgement, but whether it *widens when you move and kick when you fire*
> is a numeric fact about `crosshairGap`, and a headless test pins it so a later refactor of `hudSignalSystem`
> can't silently break it. The same goes for "a fatal shot sets the kill marker" and "an enemy bolt sets the
> damage direction" — those are exactly the event-wiring paths (Steps 5–6) most likely to rot when the combat
> code changes. Moving the state into `HudSignals` (Step 1) is what *makes* this testable; the scenario is the
> payoff. The rendering itself stays a manual/visual check, which is fine — you can't assert pixels, but you
> can assert every decision behind them.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/ecs/components/core.h` | New `HudSignals` context struct: `crosshairGap`, `recoil`, three event `*Timer`s + `damageDir`, `lowAmmo`, `showDebug`, and the `kMarkerTime`/`kDamageDirTime` lifetimes. |
| `engine/ecs/systems/hud/hud_signal_system.{h,cpp}` | **New.** `hudSignalSystem`: decays the event timers, recomputes `crosshairGap` (`base + spread + movement + recoil`), derives `recoil` from the weapon cooldown, and sets `lowAmmo`. Runs last in `stepSimulation`. |
| `engine/app/simulation.cpp` | `buildWorld` emplaces `HudSignals` in the context; `stepSimulation` calls `hudSignalSystem` at the end of the tick. |
| `engine/ecs/systems/debug_hud/debug_hud_system.cpp` | Reads `HudSignals`: passes the live gap to `drawCrosshair`; draws hit/kill markers (kill wins); projects `damageDir` to a screen angle vs `CameraDirection` and draws the chevron; gates the FPS text on `showDebug`. |
| `engine/ecs/systems/debug_hud/draw_crosshair.cpp` | Takes a `gap` parameter — the crosshair arms open by the dynamic gap. |
| `engine/ecs/systems/debug_hud/draw_hit_marker.cpp` | **New.** A small diagonal "X" over the crosshair, coloured by the caller (white hit / red kill). |
| `engine/ecs/systems/debug_hud/draw_damage_arc.cpp` | **New.** A red chevron on a ring around the crosshair at a screen angle, fading with `alpha`. |
| `engine/ecs/systems/debug_hud/draw_ammo.cpp` | Turns the ammo text red when `HudSignals::lowAmmo` is set. |
| `engine/ecs/systems/debug_hud/debug_hud_internal.h` | Declares `drawCrosshair` (now with `gap`), `drawHitMarker`, `drawDamageArc`. |
| `engine/ecs/systems/combat/fire_hitscan.cpp` | Sets `hitMarkerTimer` (and `killMarkerTimer` on a fatal hit) when a player hitscan damages an enemy. |
| `engine/ecs/systems/combat/update_projectiles.cpp` | On impact: a player bolt → hit/kill marker; an enemy bolt on the player → `damageDir` + `damageDirTimer`. |
| `engine/ecs/systems/enemy/ai_system.cpp` | On a melee hit landing on the player, sets `damageDir` (toward the grunt) + `damageDirTimer`. |
| `harness/headless_main.cpp` | New `hud_signals` scenario asserting crosshair spread (move/fire), low-ammo toggle, hit/kill markers, and the damage-direction timer. |
| `CMakeLists.txt` | Adds `draw_hit_marker.cpp`, `draw_damage_arc.cpp`, `hud_signal_system.cpp` to `qengine_lib`. |

---

## What You Should See

Run `build/QEngine.exe` (the showcase):

1. **The crosshair breathes.** It's tight when you stand still, opens while you move, and kicks outward the
   instant you fire before settling back — wider on a high-spread weapon, near-still on the railgun.
2. **Markers confirm your shots.** A white "X" flashes over the crosshair when a shot damages an enemy, red on
   the killing blow.
3. **A chevron points at what hurt you.** Take a melee swipe or a ranged bolt and a red chevron appears around
   the crosshair aimed at the attacker; turn toward it and it swings to dead-ahead, then fades.
4. **Low ammo goes red.** The ammo readout turns red when the current weapon's pool runs low.
5. **The FPS text obeys `showDebug`.** It's on by default; the flag exists to hide it for a clean production
   HUD.

Headless:

6. **`hud_signals` passes** — printing the measured gaps and flags: movement and firing each widened the
   crosshair, the low-ammo flag toggled at the threshold, hit *and* kill markers fired, and the
   damage-direction timer was set by an enemy bolt.
7. **All 18 scenarios green** — the HUD signals slot in without disturbing combat, AI, or map tests.

---

## What's Next

The HUD now reflects the game the last two chapters built: enemies that chase and shoot are answered by a
crosshair that reads accuracy, markers that confirm hits, and a chevron that tells you where danger is. Because
the state lives in `HudSignals` and is recomputed each tick, it's all provable headless — the `hud_signals`
scenario pins the behaviour while the GL pass stays a dumb renderer of the context.

The plan that shipped this left clear follow-ons: a floating enemy **health bar** billboarded over aggroed
grunts (deferred — it needs the world→screen projection plumbing the damage chevron only half-introduces), and
a **key to flip `showDebug`** (the flag exists; wiring an input to it is a small follow-up, exactly like the
Chapter 30 F1 toggle for the trigger wireframes). Beyond those, the reactive-signal pattern generalises: any
new "the game wants to tell the player something" — a reload cue, a pickup pulse, an objective marker — is
another field in `HudSignals`, set at its event site and drawn from the context, testable before it's ever
seen on screen.
