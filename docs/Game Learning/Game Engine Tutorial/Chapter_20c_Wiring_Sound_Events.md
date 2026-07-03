# Chapter 20c: Wiring Sound Events

## What You'll Learn
- Why the simulation must never call the audio backend directly — and the decoupled queue that fixes it
- The `SoundEvent` + `SoundQueue` pair: a one-frame mailbox the sim writes and the presentation layer reads
- `queueSound` / `queueSoundAt` — a header-only, no-op-when-absent API any system can call safely
- The `audioSystem` drainer: one pass per rendered frame, windowed build only
- Emplacing the `SoundQueue` in `buildWorld` so headless and windowed builds share the same code path
- Wiring real gameplay events: weapon fire, weapon switch, pickups, teleporters, doors, death, pain, and jumping
- Rate-limiting a continuous damage source so lava doesn't machine-gun the pain grunt
- Hooking it all into `main.cpp`: create, init, start music, per-frame listener + drain, shutdown
- Which sounds we deliberately left for later, and why they need edge/timer detection first

---

## Where We Are

Chapter 20a gave us the sound assets and a manifest that maps logical ids like
`"weapon.shotgun"` to files. Chapter 20b built the `AudioEngine` — a `miniaudio` wrapper that
turns those ids into actual sound. What we *don't* have yet is a single line of gameplay code
that makes a noise. The combat system fires, the pickup system grants items, the player jumps —
all in silence.

This chapter connects the two worlds. The obvious move — have `combatSystem` call
`audio.play("weapon.shotgun")` — is exactly the move we must not make, and understanding *why*
is the whole point of Step 1. Once the pattern is in place, wiring each event is a one-liner,
and we'll do eight of them.

---

## Step 1: Why a Queue, Not a Direct Call

Two hard constraints rule out calling the `AudioEngine` from a simulation system:

1. **The simulation must not depend on the audio backend.** `combatSystem`, `pickupSystem`,
   and friends live in the ECS/simulation layer. If they `#include "audio_engine.h"` and hold
   an `AudioEngine&`, then every system that might make a sound is welded to `miniaudio`. That
   is a layering inversion: gameplay logic should not know how sound is produced any more than
   it should know how triangles reach the screen.

2. **The headless harness has no audio device.** The regression harness from Chapter 10a runs
   the *exact same* `stepSimulation` with no window and no sound card. If `combatSystem`
   reached for an `AudioEngine`, there would be nothing to reach for — every test build would
   need to fake one, or the code would crash.

The solution is the same decoupling pattern we've used for knockback and pickup messages: the
simulation *records an intention* into a shared buffer, and a separate presentation-layer
system *acts on it later*. The sim writes "I fired a shotgun here"; something else — only in
the windowed build — decides that means playing a clip.

> **Why is this the right shape and not over-engineering?** The queue is the seam between two
> layers that genuinely have different lifetimes and dependencies. The simulation runs in both
> builds at a fixed timestep; audio runs only in the windowed build, once per rendered frame.
> A shared plain-data buffer is the minimum contract that lets each side ignore the other.

---

## Step 2: The `SoundEvent` and `SoundQueue`

The data is deliberately tiny. Create `engine/audio/types/sound_event.h`:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// A request to play a sound, by manifest id. Simulation systems push these into
// the SoundQueue; the audio system drains and plays them (windowed build only).
struct SoundEvent
{
	std::string id;                 // manifest id, e.g. "weapon.shotgun"
	glm::vec3   pos{0.0f};          // world position (if positional)
	bool        positional = false; // 3D at pos, or 2D (UI/player)
};

// Registry context resource: a one-frame queue of sound requests.
struct SoundQueue
{
	std::vector<SoundEvent> events;
};
```

A `SoundEvent` names a sound by its *manifest id* — never a filename. That keeps gameplay code
agnostic about where the audio comes from; the manifest (Chapter 20a) is the only place ids and
files meet. `positional` distinguishes a world sound (a shotgun going off at `pos`) from a 2D
sound (a UI weapon-switch click, or the player's own pain grunt, which has no meaningful
"where").

`SoundQueue` is a **registry context resource** — a singleton hung off the registry, exactly
like `PhysicsConfig` or `CameraDirection`. Any system with a `registry&` can find it without
threading it through function signatures.

> **Why store the id as a `std::string` rather than an enum?** Sound ids are data that lives in
> the manifest, not a fixed set baked into the engine. A designer can add `"world.teleport"` to
> the manifest and a system can queue it without touching any enum. The string is copied once
> per event into a queue that's cleared every frame — the cost is noise next to the audio
> device call it triggers.

---

## Step 3: The `queueSound` API

Simulation systems shouldn't poke the queue's internals directly — they'd have to look up the
context resource, null-check it, and respect the cap every time. Wrap that in two free
functions. Create `engine/audio/queue_sound.h`:

```cpp
#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

#include "engine/audio/types/sound_event.h"

// Push a sound request onto the registry's SoundQueue, if one exists. Safe to
// call from any simulation system; a no-op when there's no queue (e.g. a test
// build with audio disabled). Capped so an undrained queue can't grow unbounded.

inline void queueSound(entt::registry& reg, const std::string& id)
{
	if (auto* q = reg.ctx().find<SoundQueue>())
		if (q->events.size() < 256)
			q->events.push_back(SoundEvent{ id, glm::vec3(0.0f), false });
}

inline void queueSoundAt(entt::registry& reg, const std::string& id, const glm::vec3& pos)
{
	if (auto* q = reg.ctx().find<SoundQueue>())
		if (q->events.size() < 256)
			q->events.push_back(SoundEvent{ id, pos, true });
}
```

Two variants: `queueSound` for 2D sounds and `queueSoundAt` for positional ones. Three design
decisions are packed in here.

> **Why header-only `inline` functions instead of a `.cpp`?** These are two-line pushes with no
> state of their own. Made `inline` in a header, they compile straight into each caller with no
> link step and no new translation unit — and callers already `#include` this header to reach
> the API. There's nothing to put in a `.cpp` that wouldn't just be indirection.

> **Why the `find<SoundQueue>()` null-check — why not assume the queue exists?** This is what
> makes the same gameplay code run in both builds. If a build never emplaced a `SoundQueue`
> (imagine a future minimal test that skips `buildWorld`'s audio setup), `find` returns
> `nullptr` and the call quietly does nothing. Systems never branch on "are we headless?" — they
> just queue, and the absence of a drainer or a queue makes it a no-op.

> **Why the `< 256` cap?** In the windowed build the queue is drained and cleared every frame,
> so it never grows. But the headless harness emplaces the queue (Step 5) and *never drains it*
> — every shotgun blast across thousands of ticks would `push_back` forever. The cap makes an
> undrained queue bounded: it fills to 256 and then silently drops further events. Nothing reads
> them in that build, so dropping them costs nothing.

---

## Step 4: The `audioSystem` Drainer

Something has to turn queued events into sound. That's `audioSystem` — the one place that holds
both a `registry&` and an `AudioEngine&`, bridging the two layers. Declare it in
`engine/ecs/systems/audio/audio_system.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

class AudioEngine;

// Drain the SoundQueue and play each queued sound, then clear it. Called once per
// rendered frame in the windowed build; the headless harness never calls it.
void audioSystem(entt::registry& registry, AudioEngine& audio);
```

Note the **forward declaration** `class AudioEngine;` — the header never includes
`audio_engine.h`. Only the `.cpp` needs the full type. Implement it in
`engine/ecs/systems/audio/audio_system.cpp`:

```cpp
#include "engine/ecs/systems/audio/audio_system.h"

#include "engine/audio/audio_engine.h"
#include "engine/audio/types/sound_event.h"

void audioSystem(entt::registry& registry, AudioEngine& audio)
{
	auto* queue = registry.ctx().find<SoundQueue>();
	if (!queue) return;

	for (const SoundEvent& e : queue->events)
	{
		if (e.positional) audio.playAt(e.id, e.pos);
		else              audio.play(e.id);
	}
	queue->events.clear();
}
```

Drain, dispatch, clear. Positional events go to `AudioEngine::playAt` (which, per Chapter 20b,
is currently non-spatial and falls back to 2D — the position is captured now for when true 3D
falloff lands); everything else goes to `play`.

> **Why drain once per *rendered* frame, not once per fixed tick?** The simulation may step
> several fixed ticks per rendered frame (the accumulator in `main`). If we drained inside the
> tick loop we'd be fine too — but draining once per frame, after all ticks, means a weapon that
> somehow queued twice in one frame plays twice, and nothing is lost. Crucially the drain lives
> in `main`'s frame code, *not* in `stepSimulation`, so the headless harness — which only ever
> calls `stepSimulation` — never touches it.

> **Why clear the queue here rather than the producers clearing it?** The drainer is the single
> reader; making it the single point that empties the buffer means producers never coordinate.
> They push; the drainer consumes-and-resets. One writer role, one reader role.

---

## Step 5: Emplacing the Queue in `buildWorld`

The queue has to exist before any system pushes to it. It's created in `buildWorld` — the world
construction shared by *both* builds — in `engine/app/simulation.cpp`:

```cpp
Level buildWorld(entt::registry& registry, ResourceManager& resources,
                 JoltWorld& joltWorld, bool headless)
{
	Level level = setupScene(registry, resources, headless);

	// Sound-event queue: simulation systems push, the audio system drains it
	// (windowed build only; headless leaves it unread).
	registry.ctx().emplace<SoundQueue>();

	// Static bodies from level geometry
	createLevelBodies(registry, level);
	// … rest of world build …
```

with the include at the top:

```cpp
#include "engine/audio/types/sound_event.h"
```

> **Why emplace the queue in *both* builds, including headless?** Because it makes gameplay code
> unconditional. Every `queueSound` call finds a real queue in either build and pushes to it —
> no `if (!headless)` guards scattered through the systems. The windowed build then drains it
> each frame; the headless build simply never does (and the 256 cap from Step 3 keeps that
> undrained queue bounded). The producers are identical; only the presence of a *drainer*
> differs, and that difference lives entirely in `main.cpp`.

Nothing else in `stepSimulation` changes. The tick order is exactly as it was after Chapter 19
— `weaponSwitchSystem`, movers, physics, `playerCharacterSystem`, `combatSystem`, triggers,
pickups, death. Every one of those systems is about to gain a `queueSound` call, but none change
their *position* in the order.

---

## Step 6: Wiring Weapon Fire (Positional, Per Weapon)

Now the payoff. `combatSystem` already knows the firing weapon, the world position it fires
from, and gates the shot on ammo and cooldown. The sound is one line at the *successful-fire*
site. In `engine/ecs/systems/combat/combat_system.cpp`, add the include and a small id helper:

```cpp
#include "engine/audio/queue_sound.h"

namespace
{
	// The manifest sound id for a weapon's fire.
	const char* weaponSoundId(WeaponType type)
	{
		switch (type)
		{
			case WeaponType::Shotgun:         return "weapon.shotgun";
			case WeaponType::SuperShotgun:    return "weapon.supershotgun";
			case WeaponType::Nailgun:         return "weapon.nailgun";
			case WeaponType::RocketLauncher:  return "weapon.rocketlauncher";
			case WeaponType::GrenadeLauncher: return "weapon.grenadelauncher";
			case WeaponType::LighteningGun:   return "weapon.lightninggun";
			case WeaponType::Railgun:         return "weapon.railgun";
		}
		return "weapon.shotgun";
	}
}
```

Then queue it right after the shot is dispatched, using the same `fireOrigin` the projectile or
hitscan used:

```cpp
if (weapon.fireMode == FireMode::Hitscan)
{
	fireHitscan(registry, level, entity, weapon, fireOrigin, cameraDir, resources);
}
else
{
	fireProjectile(registry, entity, weapon, fireOrigin, cameraDir, resources);
}

queueSoundAt(registry, weaponSoundId(weapon.type), fireOrigin);
weapon.cooldownRemaining = weapon.fireRate;
```

Two things matter about *where* this line sits. It's **after** the ammo gate and cooldown
`continue`s from Chapter 19 — so a dry click (out of ammo, or still cooling down) makes no
sound, because control never reaches here. And it uses `queueSoundAt` with `fireOrigin`, the eye
position the shot came from, so the event carries a real world position.

> **Why a `weaponSoundId` helper that mirrors `ammoPool`?** Each weapon type maps to exactly one
> sound id, the same way it maps to one ammo pool (the `ammoPool` helper right below it). A
> `switch` returning a `const char*` is the cheapest possible lookup — no allocation, no table —
> and adding a weapon means adding one case here, symmetric with everywhere else `WeaponType` is
> dispatched. The `SuperShotgun`/`Shotgun` and `RocketLauncher`/`GrenadeLauncher` pairings echo
> the shared-ammo groupings.

---

## Step 7: Wiring Weapon Switch (Only on Change)

`weaponSwitchSystem` (a header-only inline system, `engine/ecs/systems/combat/weapon_switch_system.h`)
already contains the exact guard we need — it only reassigns `currentWeapon` when the requested
slot is valid *and different*. Drop the sound inside that same branch:

```cpp
#include "engine/audio/queue_sound.h"

inline void weaponSwitchSystem(entt::registry& registry)
{
	auto view = registry.view<PlayerInput, WeaponInventory>();

	for (auto [entity, input, inv] : view.each())
	{
		if (input.weaponSwitch >= 0 &&
		input.weaponSwitch < static_cast<int>(inv.weapons.size()) &&
		input.weaponSwitch != inv.currentWeapon)
		{
			inv.currentWeapon = input.weaponSwitch;
			queueSound(registry, "weapon.switch");
		}
	}
}
```

> **Why is `queueSound` (2D) right here, and why inside the `!=` check?** A weapon-switch click
> is a UI sound — it has no world position, so the non-positional `queueSound` is correct.
> Placing it inside the existing `input.weaponSwitch != inv.currentWeapon` guard means it fires
> **only when the weapon actually changes** — pressing "2" while already holding weapon 2 is
> silent, exactly as it should be. The system already computed "did this cause a change?"; we
> just piggyback the sound on that decision.

---

## Step 8: Wiring Pickups (By Type)

`pickupSystem` grants an item then shows a toast (Chapter 19). The sound slots in right beside
the toast, driven by pickup *type* rather than the specific item. In
`engine/ecs/systems/pickup/pickup_system.cpp`, add an id helper in the anonymous namespace:

```cpp
const char* pickupSoundId(PickupType type)
{
	switch (type)
	{
		case PickupType::Health: return "pickup.health";
		case PickupType::Armor:  return "pickup.armor";
		case PickupType::Weapon: return "pickup.weapon";
		default:                 return "pickup.ammo";
	}
}
```

and queue it in the overlap loop, right after the grant:

```cpp
applyPickup(registry, receiver, pickup);
queueSound(registry, pickupSoundId(pickup.type));

// Show a HUD toast on the receiver, if it displays one.
if (auto* msg = registry.try_get<PickupMessage>(receiver))
{
	msg->text = pickupMessage(pickup);
	msg->timer = msg->duration;
}
```

> **Why collapse all four ammo kinds to one `"pickup.ammo"` sound?** The `default:` catches
> Shells, Nails, Rockets, and Cells — a player doesn't need four distinct blips to tell them
> they grabbed ammo; the toast already says which kind. Health, armour, and weapon get their own
> distinct sounds because those are the meaningful "you got something good" moments. This is a
> `queueSound` (2D) rather than `queueSoundAt`: the pickup is at the player's feet by definition,
> so it's effectively a first-person confirmation, not a world sound.

---

## Step 9: Wiring Triggers (Teleport + Door/Lift Activate)

`triggerSystem` fires on AABB overlap and switches on the trigger's action. Two of those actions
deserve a sound, and both are **world** sounds (positional). In
`engine/ecs/systems/trigger/trigger_system.cpp`, add `#include "engine/audio/queue_sound.h"`,
then queue inside the relevant cases.

Door/lift activation — queued at the trigger's own position, only when the mover actually starts
moving from idle:

```cpp
auto& mover = registry.get<Mover>(trigger.target);
if (mover.state == MoverState::Idle)
{
	if (mover.startDelay > 0.0f)
	{
		mover.state = MoverState::StartDelay;
		mover.timer = mover.startDelay;
	}
	else
	{
		mover.state = MoverState::Moving;
	}
	queueSoundAt(registry, "world.door_open", trigPos.value);
}
```

Teleport — queued at the *destination*, after the player is moved and their velocity zeroed:

```cpp
// reset velocity to prevent flying out of the teleporter
if (registry.all_of<Velocity>(entity))
{
	registry.get<Velocity>(entity).value = glm::vec3(0.0f);
}
queueSoundAt(registry, "world.teleport", trigger.destination);
```

> **Why `trigPos.value` for the door but `trigger.destination` for the teleport?** A door sound
> should come from the door's trigger volume — where the player is standing when it opens. A
> teleport sound should come from where the player *arrives*, so it feels like it emanates from
> the exit pad. Each event carries the position that will make it feel right once 3D falloff is
> wired up. The door sound sits inside the `mover.state == MoverState::Idle` guard, so a lift
> that's already moving doesn't re-trigger the "open" sound each frame the player leans on the
> switch.

---

## Step 10: Wiring Death and Jump

Two player-state sounds, both 2D (they're about *you*, not a place).

**Death.** `playerDeathSystem` already detects the health-zero transition and respawns the
player. Queue the sound at that moment, in
`engine/ecs/systems/player/player_death_system.cpp` (with the `queue_sound.h` include):

```cpp
// reset health
health.current = health.max;
health.invulnerableTimer = 1.0f;  // 1 second of invulnerability

// move to spawn point
pos.value = spawn.position;

queueSound(registry, "player.death");
```

Because this sits after the `if (health.current > 0.0f) continue;` guard and immediately resets
health, it fires **once** per death — the next tick the player is back at full health and skips
the block.

**Jump.** `playerCharacterSystem` applies the jump impulse inside its `onGround` branch. Queue
there, so only a real (grounded) jump makes a sound:

```cpp
// jump
if (input.jump)
{
	desiredVel += JPH::Vec3(0.0f, physics.jumpForce, 0.0f);
	queueSound(registry, "player.jump");
}
```

> **Why is a mid-air jump press silent?** The whole `if (input.jump)` block lives inside
> `if (onGround)`, so holding jump against the ceiling or spamming it while falling never
> reaches this line — you can't queue a jump sound you didn't actually perform. The gate we
> already needed for the physics doubles as the gate for the sound.

---

## Step 11: Wiring Pain (Player Only, Rate-Limited)

Pain is the subtle one. Damage flows through the single `applyDamage` helper from Chapter 19,
which is called by lava, hitscan, projectiles, and splash. A naive `queueSound` there would work
for a single hit — but **lava deals damage every tick**. Standing in it would fire `player.pain`
sixty times a second, an ugly buzz instead of a grunt.

The fix rides on the `DamageFlash` we already trigger. A hit refreshes the flash timer; if a
flash was *already running*, this hit is a continuation of an ongoing damage stream (like lava),
not a fresh one. Queue the grunt only on the *rising edge* — when no flash was active. In
`engine/ecs/apply_damage.cpp` (add the `queue_sound.h` include):

```cpp
// The hit landed (on armour and/or health) — flash the screen. Note whether a
// flash was already active so the pain voice fires once per flash, not every
// tick (lava deals damage continuously).
bool wasFlashing = false;
if (registry.all_of<DamageFlash>(target))
{
	auto& flash = registry.get<DamageFlash>(target);
	wasFlashing = flash.timer > 0.0f;
	flash.timer = flash.duration;
}

if (!wasFlashing && registry.all_of<TagPlayer>(target))
	queueSound(registry, "player.pain");

return true;
```

> **Why gate on `!wasFlashing` instead of a dedicated cooldown timer?** The `DamageFlash`
> timer is *already* the "am I currently taking damage?" signal — reusing it means no new state
> to store or tick. Continuous lava keeps `wasFlashing == true` for as long as the flash lasts,
> so the grunt fires once when you step in and again only after the flash lapses (i.e. you've
> been out and stepped back). A discrete hit, arriving with the flash lapsed, always grunts.

> **Why `all_of<TagPlayer>(target)`?** `applyDamage` is generic — it'll damage enemies and props
> too, once those exist. Only the *player* has a first-person pain voice; an enemy taking a
> shotgun blast shouldn't grunt with the player's sound. The tag check scopes the grunt to the
> player without special-casing every call site.

---

## Step 12: Integrating in `main.cpp`

Everything above only produces *queued* events. The windowed build is where they become sound.
Four touch-points in `main.cpp`, in order.

**Create and initialise the engine**, after resources load:

```cpp
// ─── Audio ───────────────────────────────────────────────
AudioEngine audio;
audio.init("assets/sounds", "assets/sounds/manifest.json");
```

**Start the music** right after the world is built:

```cpp
Level level = qengine::buildWorld(registry, resources, joltWorld);

// Start background music (loops).
audio.playMusic("music.exploration");
```

**Per frame — set the listener, then drain the queue.** This lives in the frame body, *outside*
the fixed-timestep `while` loop, right after the camera follows the player:

```cpp
// ─── Camera follows player body (interpolated) ───────────
cameraFollowSystem(registry, camera, alpha);

// ─── Audio: update listener + play queued sounds ─────────
audio.setListener(camera.getPosition(), camera.getFront());
audioSystem(registry, audio);
```

**Shut down** at the end, before the other subsystems:

```cpp
audio.shutdown();
joltWorld.shutdown();
resources.clear();
return 0;
```

with the two new includes near the top:

```cpp
#include "engine/ecs/systems/audio/audio_system.h"
#include "engine/audio/audio_engine.h"
```

> **Why `setListener` every frame from the camera?** The listener is the player's ears. Feeding
> it the camera position and front vector each frame keeps it synced with the view — so when 3D
> falloff arrives (Chapter 20b flagged it as deferred), positional events already have a correct
> listener to attenuate against. Right now it's cheap bookkeeping that costs one call.

> **Why is the drain out here and not in `stepSimulation`?** This is the crux of the whole
> chapter. `stepSimulation` is shared with the headless harness; putting `audioSystem` in it
> would drag `AudioEngine` into a build that has no audio device. Keeping the drain in `main`'s
> frame loop means the *only* code that knows sound exists is `main.cpp` and `audio_system.cpp`.
> The simulation just queues.

No CMake change is needed for the header-only pieces (`queue_sound.h`, `sound_event.h`,
`weapon_switch_system.h`) — they compile into their includers. `audio_system.cpp` and
`apply_damage.cpp` are the only new/edited `.cpp` files, and both are already in
`CMakeLists.txt`.

---

## Step 13: What We Deliberately Left Out

A few sounds from the manifest aren't wired yet, on purpose. Each needs detection the current
systems don't yet do:

- **Dry-fire** (clicking with no ammo) needs to distinguish "player *tried* to fire but the
  ammo gate blocked it" from "player isn't firing." That's edge detection on the fire input, not
  just the successful-fire site we hooked in Step 6.
- **Footsteps** need a distance-travelled or step-timer accumulator on the moving player — a
  per-frame sound would be a drone.
- **Landing** needs a falling→grounded *transition* (a `wasOnGround` vs `onGround` edge), which
  `playerCharacterSystem` doesn't currently track.
- **Door-stop / door-close** needs the mover to signal when it *finishes* moving, a state
  transition `moverSystem` doesn't emit yet.

All four are the same shape: they need an *edge* or a *timer*, whereas everything we wired this
chapter fires at a moment the code already identifies (a successful shot, a granted pickup, a
state change). We'll add the transition detection when we build those systems out.

Similarly, **3D positional falloff** is still deferred in the `AudioEngine` itself (Chapter 20b):
`playAt` currently falls back to 2D. We're already capturing world positions in every
`queueSoundAt` call and feeding the listener each frame, so the day spatial audio lands, the
gameplay side needs no changes.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/audio/types/sound_event.h` | **New** — `SoundEvent` + `SoundQueue` context resource. |
| `engine/audio/queue_sound.h` | **New** — header-only `queueSound` / `queueSoundAt` (no-op when no queue, 256 cap). |
| `engine/ecs/systems/audio/audio_system.{h,cpp}` | **New** — per-frame drainer; forward-declares `AudioEngine`. |
| `app/simulation.cpp` | Emplace `SoundQueue` in `buildWorld` (both builds); include `sound_event.h`. |
| `combat_system.cpp` | `weaponSoundId` helper; `queueSoundAt` at the fire site (per `WeaponType`, positional). |
| `weapon_switch_system.h` | `queueSound("weapon.switch")` inside the change guard. |
| `pickup_system.cpp` | `pickupSoundId` helper; `queueSound` beside the toast (by type). |
| `trigger_system.cpp` | `queueSoundAt` for `world.door_open` (activate) and `world.teleport`. |
| `player_death_system.cpp` | `queueSound("player.death")` on respawn. |
| `player_character_system.cpp` | `queueSound("player.jump")` inside the grounded jump. |
| `apply_damage.cpp` | `queueSound("player.pain")` — player only, rising-edge of `DamageFlash`. |
| `main.cpp` | Create + init `AudioEngine`, start music, per-frame `setListener` + `audioSystem`, shutdown. |

---

## What You Should See

Run `build/QEngine.exe`:

1. **Background music** starts looping the moment the world loads.
2. **Firing** plays the current weapon's sound; switch weapons with 1–7 and each fires
   differently. A dry click (out of ammo, or mid-cooldown) is silent.
3. **Switching** weapons plays a click — but only when the weapon actually changes.
4. **Walking over a pickup** plays a confirmation: distinct sounds for health, armour, and
   weapons; a shared blip for any ammo.
5. **Stepping into lava** plays one pain grunt, not a continuous buzz — it grunts again only
   after you leave and re-enter. Discrete hits always grunt.
6. **Teleporters and door/lift switches** play their world sounds; **dying** plays a death sound
   as you respawn; **jumping** from the ground plays a jump sound (mid-air jump presses are
   silent).
7. The **headless harness** still runs unchanged and in silence — every `queueSound` call finds
   the queue, pushes, and is simply never drained.

---

## What's Next

The showcase is now a complete, audible FPS loop — but the *level* is still hand-built in
`createShowcaseLevel()` and `showcaseDescriptors()`. In **Chapter 21: The `.map` Parser & Brush
Geometry**, we finally retire the hard-coded world: we'll parse a TrenchBroom-authored `.map`
file, turn its brush geometry into meshes and colliders, and feed its entities straight into the
`spawnScene` pipeline from Chapter 18 — so every `item_*`, `weapon_*`, trigger, and mover you've
built becomes an FGD entity you can place by hand in the editor.
