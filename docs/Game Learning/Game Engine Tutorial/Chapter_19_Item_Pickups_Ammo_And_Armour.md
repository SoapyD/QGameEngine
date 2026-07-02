# Chapter 19: Item Pickups, Ammo & Armour

## What You'll Learn
- A `Pickup` component + `pickupSystem` that reuses the trigger-overlap pattern
- Spawning pickups by `classname` through the data-driven dispatch from Chapter 18
- On-screen "Picked up X" toast messages
- Making ammo *matter* — consuming it when you fire, so pickups can replenish it
- An `Armor` stat and a single `applyDamage` helper that makes armour absorb damage before health
- HUD polish: an armour bar, weapon-selection keys, and legibility panels behind text
- Growing the headless harness with gameplay-logic scenarios

---

## Where We Are

You can lose health to lava and spend… well, nothing yet — firing a weapon doesn't
actually cost ammo. There's no way to *gain* anything back either: no items exist in the
world. This chapter closes the core FPS loop — pick things up, spend ammo, soak hits with
armour — and it leans hard on the data-driven spawning we built in Chapter 18. Adding a new
kind of item will turn out to be *one factory + one table row*.

We'll build the feature first (Steps 1–5), then fix two things that testing immediately
exposes: ammo that's never consumed (Step 6) and armour that doesn't actually protect you
(Step 7), before polishing the HUD (Step 8) and locking it all down with tests (Step 9).

---

## Step 1: The `Pickup` and `Armor` Components

An item needs to describe *what it gives*. Add to `components/gameplay.h`:

```cpp
// WeaponType is defined in components/combat.h; a Weapon pickup only stores it
// by value, so an opaque declaration keeps this header light.
enum class WeaponType;
```

(put that near the top, after the includes) and then, with the other gameplay components:

```cpp
// ─── Item pickups ────────────────────────────────────────────────
enum class PickupType
{
	Health,   // restores Health (capped at max)
	Shells,   // Ammo.shells
	Nails,    // Ammo.nails
	Rockets,  // Ammo.rockets
	Cells,    // Ammo.cells
	Armor,    // restores Armor (capped at max)
	Weapon    // grants weaponType if not held; tops up its ammo
};

// A sensor entity that grants an effect to a TagTriggerable toucher, then is
// consumed. pickupSystem does the overlap + grant + destroy.
struct Pickup
{
	PickupType type = PickupType::Health;
	int amount = 0;                              // health/armour/ammo granted
	WeaponType weaponType = static_cast<WeaponType>(0); // used only when type == Weapon
};
```

> **Why forward-declare `WeaponType` instead of including `combat.h`?** `Pickup` stores a
> `WeaponType` *by value* but never names any of its enumerators — only the pickup *factory*
> (a `.cpp`) does. An opaque enum declaration (`enum class WeaponType;`) is enough for the
> compiler to size the member, and it keeps this header from dragging `combat.h` into every
> translation unit that sees a `Pickup`. Narrow includes keep rebuilds fast (Chapter 17's
> header-discipline rule).

Armour is a brand-new stat. Add it to `components/combat.h`, next to `Ammo`:

```cpp
// Secondary damage buffer. Filled by item_armor; absorbs damage before health.
struct Armor
{
	float current = 0.0f;
	float max = 100.0f;
};
```

---

## Step 2: The `pickupSystem`

The trigger system already does everything we need for detection: build an AABB from a
volume, build one per `TagTriggerable` entity, test for overlap. A pickup is that same test
plus "grant an effect and delete myself." Create
`ecs/systems/pickup/pickup_system.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

// Grants item pickups to overlapping TagTriggerable entities, then destroys the
// pickup. Mirrors triggerSystem's AABB overlap; runs after triggerSystem.
void pickupSystem(entt::registry& registry);
```

The implementation, `ecs/systems/pickup/pickup_system.cpp`, splits into small helpers.
First, granting the effect:

```cpp
#include "engine/ecs/systems/pickup/pickup_system.h"
#include "engine/ecs/components.h"
#include "engine/ecs/weapon_definitions.h"   // createWeapon
#include "engine/physics/types/aabb.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	// Add ammo of the given kind to an Ammo component.
	void addAmmo(Ammo& ammo, PickupType type, int amount)
	{
		switch (type)
		{
			case PickupType::Shells:  ammo.shells  += amount; break;
			case PickupType::Nails:   ammo.nails   += amount; break;
			case PickupType::Rockets: ammo.rockets += amount; break;
			case PickupType::Cells:   ammo.cells   += amount; break;
			default: break;
		}
	}

	// The ammo kind a weapon draws from — a weapon pickup tops this up.
	PickupType ammoKindFor(WeaponType weapon)
	{
		switch (weapon)
		{
			case WeaponType::Shotgun:
			case WeaponType::SuperShotgun:    return PickupType::Shells;
			case WeaponType::Nailgun:         return PickupType::Nails;
			case WeaponType::RocketLauncher:
			case WeaponType::GrenadeLauncher: return PickupType::Rockets;
			case WeaponType::LighteningGun:
			case WeaponType::Railgun:         return PickupType::Cells;
		}
		return PickupType::Shells;
	}

	// Grant a weapon: add it if not already held, then top up its ammo pool.
	void grantWeapon(entt::registry& reg, entt::entity receiver, const Pickup& pickup)
	{
		if (reg.all_of<WeaponInventory>(receiver))
		{
			auto& inv = reg.get<WeaponInventory>(receiver);
			bool held = std::any_of(inv.weapons.begin(), inv.weapons.end(),
				[&](const Weapon& w) { return w.type == pickup.weaponType; });
			if (!held) inv.weapons.push_back(createWeapon(pickup.weaponType));
		}
		if (reg.all_of<Ammo>(receiver))
			addAmmo(reg.get<Ammo>(receiver), ammoKindFor(pickup.weaponType), pickup.amount);
	}

	void applyPickup(entt::registry& reg, entt::entity receiver, const Pickup& pickup)
	{
		switch (pickup.type)
		{
			case PickupType::Health:
				if (reg.all_of<Health>(receiver))
				{
					auto& h = reg.get<Health>(receiver);
					h.current = std::min(h.current + (float)pickup.amount, h.max);
				}
				break;
			case PickupType::Armor:
				if (reg.all_of<Armor>(receiver))
				{
					auto& a = reg.get<Armor>(receiver);
					a.current = std::min(a.current + (float)pickup.amount, a.max);
				}
				break;
			case PickupType::Weapon:
				grantWeapon(reg, receiver, pickup);
				break;
			default: // ammo kinds
				if (reg.all_of<Ammo>(receiver))
					addAmmo(reg.get<Ammo>(receiver), pickup.type, pickup.amount);
				break;
		}
	}
}
```

Then the system itself — the overlap loop:

```cpp
void pickupSystem(entt::registry& registry)
{
	auto pickupView   = registry.view<Position, AABBCollider, Pickup>();
	auto receiverView = registry.view<Position, AABBCollider, TagTriggerable>();

	// Collect consumed pickups and destroy after iterating (don't invalidate
	// the view mid-loop).
	std::vector<entt::entity> consumed;

	for (auto [pickupEntity, pickupPos, pickupCol, pickup] : pickupView.each())
	{
		AABB pickupBox = AABB::fromCentreSize(pickupPos.value, pickupCol.halfExtents);

		for (auto [receiver, recvPos, recvCol] : receiverView.each())
		{
			AABB recvBox = AABB::fromCentreSize(recvPos.value, recvCol.halfExtents);
			if (!pickupBox.intersects(recvBox)) continue;

			applyPickup(registry, receiver, pickup);
			consumed.push_back(pickupEntity);
			break; // one toucher consumes it
		}
	}

	for (entt::entity e : consumed)
		registry.destroy(e);
}
```

> **Why collect into `consumed` instead of destroying inline?** Calling `registry.destroy()`
> while iterating the view you're destroying from invalidates the iteration. We record the
> victims and delete them after the loop — a standard ECS safety pattern.

> **Why a separate component instead of a new `TriggerAction`?** A trigger *acts on the
> toucher and stays*; a pickup *is consumed*. They share only the overlap maths, not the
> lifecycle — and a `Pickup` carries its own data (amount, weapon type). Keeping them
> separate stops the trigger switch from growing a "sometimes delete myself" branch.

---

## Step 3: Spawning Pickups

### The factory
A pickup is a small cube with a *sensor* collider — it overlaps but has no physics body, so
you walk through it. Add to `level/factories.h` (and include `gameplay.h` for `Pickup`):

```cpp
// Item pickup: a small rendered cube with a sensor AABB (ECS overlap only,
// no Jolt body) carrying the given Pickup. pickupSystem grants + destroys it.
entt::entity spawnPickup(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                         const Pickup& pickup, unsigned int textureId);
```

Implementation in `level/factories.cpp`:

```cpp
entt::entity spawnPickup(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                         const Pickup& pickup, unsigned int textureId)
{
    auto e = reg.create();
    reg.emplace<Position>(e, pos);
    reg.emplace<Scale>(e, glm::vec3(0.4f));            // small floating cube
    reg.emplace<AABBCollider>(e, glm::vec3(0.5f), true); // sensor: ECS overlap, no Jolt body
    reg.emplace<MeshRenderer>(e, cubeRenderer(a, textureId));
    reg.emplace<Pickup>(e, pickup);
    return e;
}
```

Note the collider (`0.5` half-extents) is slightly larger than the visible cube (`0.4`
scale) so it's forgiving to walk into. The `true` flag marks it a sensor — no Jolt body is
created, so it's pure ECS overlap, exactly like a trigger volume.

### The classname factories
Here's the payoff from Chapter 18. In `level/classname_factory.cpp`, one helper covers every
item, and each `item_*` / `weapon_*` differs only in type, default amount, and texture:

```cpp
entt::entity makePickup(entt::registry& reg, const SpawnContext& ctx, const SpawnParams& p,
                        PickupType type, int defaultAmount, const char* defaultTexture,
                        WeaponType weapon = static_cast<WeaponType>(0))
{
    Pickup pickup;
    pickup.type = type;
    pickup.amount = p.getInt("amount", defaultAmount);
    pickup.weaponType = weapon;
    return spawnPickup(reg, ctx.assets, p.origin, pickup,
        ctx.texture(p.getString("texture", defaultTexture)));
}

entt::entity make_item_health (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
{ return makePickup(r, c, p, PickupType::Health,  25, "grid_green"); }
entt::entity make_item_shells (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
{ return makePickup(r, c, p, PickupType::Shells,  10, "grid_orange"); }
// … nails, rockets, cells, armor …
entt::entity make_weapon_nailgun (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
{ return makePickup(r, c, p, PickupType::Weapon, 25, "grid_grey", WeaponType::Nailgun); }
// … shotgun, rocketlauncher, railgun …
```

Then register the classnames in the dispatch table:

```cpp
{ "item_health",           &make_item_health },
{ "item_shells",           &make_item_shells },
{ "item_nails",            &make_item_nails },
{ "item_rockets",          &make_item_rockets },
{ "item_cells",            &make_item_cells },
{ "item_armor",            &make_item_armor },
{ "weapon_shotgun",        &make_weapon_shotgun },
{ "weapon_nailgun",        &make_weapon_nailgun },
{ "weapon_rocketlauncher", &make_weapon_rocket },
{ "weapon_railgun",        &make_weapon_railgun },
```

### Placing them in the showcase
Because entities are now data (Chapter 18), adding items is a few lines in
`level/showcase_descriptor.cpp`, before the `return`:

```cpp
// ─── Item pickups (demo placement around the room) ───────────
d.push_back({ .classname = "item_health",  .origin = glm::vec3(16.0f, 1.0f, 23.0f) });
d.push_back({ .classname = "item_shells",  .origin = glm::vec3(17.0f, 1.0f, 9.0f) });
d.push_back({ .classname = "item_rockets", .origin = glm::vec3(24.0f, 1.0f, 12.0f) });
d.push_back({ .classname = "item_armor",   .origin = glm::vec3(12.0f, 1.0f, 20.0f) });
d.push_back({
    .classname = "weapon_nailgun", .origin = glm::vec3(5.0f, 1.0f, 15.0f),
    .props = { {"amount", "50"} },
});
```

The player also needs an `Armor` component to receive armour pickups. In
`factories::spawnPlayer`, after the `Ammo`:

```cpp
reg.emplace<Armor>(player, 0.0f, 100.0f); // starts empty; item_armor fills it
```

---

## Step 4: Wire It Up

Add `pickupSystem` to the tick order in `simulation.cpp`, right after `triggerSystem`:

```cpp
triggerSystem(registry);
pickupSystem(registry);         // grant + consume items on touch
playerDeathSystem(registry);
demoResetSystem(registry);
```

> **Why here?** After `triggerSystem` (and well after `playerCharacterSystem`), the player's
> `Position` is current, so overlap tests are accurate; and a health/armour grant lands
> before `playerDeathSystem`'s death check.

Add the source files to `CMakeLists.txt` (`pickup_system.cpp`), then build. At this point you
can walk over the items and they vanish — but nothing tells you what you got. Next.

---

## Step 5: On-Screen Pickup Messages

We want a "Picked up Health +25" toast. Store the message on the receiver so the game logic
decides *what* to say and the HUD decides *how* to draw it. Add to `components/gameplay.h`:

```cpp
// Transient on-screen toast ("Picked up …"). pickupSystem sets text + timer on
// the receiver; the HUD draws it centred and fades it out.
struct PickupMessage
{
	std::string text;
	float timer = 0.0f;    // remaining display time (seconds)
	float duration = 2.5f; // total display length
};
```

Give the player one in `spawnPlayer` (`reg.emplace<PickupMessage>(player);`).

In `pickup_system.cpp`, add helpers that turn a pickup into text:

```cpp
const char* weaponName(WeaponType w)
{
	switch (w)
	{
		case WeaponType::Shotgun:         return "Shotgun";
		case WeaponType::Nailgun:         return "Nailgun";
		case WeaponType::RocketLauncher:  return "Rocket Launcher";
		// … the rest …
	}
	return "Weapon";
}

std::string pickupMessage(const Pickup& p)
{
	const int n = p.amount;
	switch (p.type)
	{
		case PickupType::Health:  return "Picked up Health +" + std::to_string(n);
		case PickupType::Shells:  return "Picked up " + std::to_string(n) + " Shells";
		// … nails / rockets / cells …
		case PickupType::Armor:   return "Picked up Armor +" + std::to_string(n);
		case PickupType::Weapon:  return std::string("Got the ") + weaponName(p.weaponType);
	}
	return "Picked up item";
}
```

Set it in the overlap loop, right after `applyPickup`:

```cpp
applyPickup(registry, receiver, pickup);

// Show a HUD toast on the receiver, if it displays one.
if (auto* msg = registry.try_get<PickupMessage>(receiver))
{
    msg->text = pickupMessage(pickup);
    msg->timer = msg->duration;
}

consumed.push_back(pickupEntity);
```

Draw it in `debug_hud_system.cpp` (before the crosshair). It's ticked here just like the
damage flash:

```cpp
// Pickup toast (upper-centre, fades out).
{
	float dt = registry.ctx().get<PhysicsConfig>().fixedDeltaTime;
	for (auto [entity, msg] : registry.view<PickupMessage, TagPlayer>().each())
	{
		if (msg.timer <= 0.0f) continue;

		float msgScale = 2.5f;
		float textWidth = msg.text.size() * 6.0f * msgScale;  // ~6px/glyph before scaling
		float x = windowWidth * 0.5f - textWidth * 0.5f;
		float y = windowHeight * 0.30f;
		drawText(x, y, msg.text.c_str(), shader, ortho, msgScale, glm::vec3(1.0f, 0.9f, 0.4f));

		msg.timer -= dt;
		if (msg.timer < 0.0f) msg.timer = 0.0f;
	}
}
```

---

## Step 6: Make Ammo Matter

Testing the shells pickup reveals a problem: firing never *spends* ammo, so there's nothing
to replenish. The combat system ticks cooldowns and fires, but never touches `Ammo`. Fix it
in `combat_system.cpp` with a small helper and an "ammo gate."

Add the pool lookup (an anonymous-namespace helper at the top of the file):

```cpp
namespace
{
	int& ammoPool(Ammo& ammo, WeaponType type)
	{
		switch (type)
		{
			case WeaponType::Shotgun:
			case WeaponType::SuperShotgun:    return ammo.shells;
			case WeaponType::Nailgun:         return ammo.nails;
			case WeaponType::RocketLauncher:
			case WeaponType::GrenadeLauncher: return ammo.rockets;
			case WeaponType::LighteningGun:
			case WeaponType::Railgun:         return ammo.cells;
		}
		return ammo.shells;
	}
}
```

Then gate firing on it, right after the cooldown check:

```cpp
Weapon& weapon = inv.weapons[inv.currentWeapon];
if (weapon.cooldownRemaining > 0.0f) continue;

// Ammo gate: a shooter that tracks Ammo must have enough, and firing
// consumes it. Shooters with no Ammo component (e.g. enemies) fire freely.
if (Ammo* ammo = registry.try_get<Ammo>(entity))
{
	int& pool = ammoPool(*ammo, weapon.type);
	if (pool < weapon.ammoPerShot) continue;   // out of ammo — no shot
	pool -= weapon.ammoPerShot;
}
```

> **Why gate on `try_get<Ammo>` rather than requiring it?** Future enemies will fire weapons
> but may not track ammo. Making the gate opt-in (only entities *with* an `Ammo` component are
> limited) keeps the player honest without forcing an ammo economy on every shooter. And
> "not enough ammo → `continue`" means a dry click costs nothing — no shot, no cooldown.

Now the shells pickup is observable: fire the shotgun, watch the count drop; grab shells,
watch it climb.

---

## Step 7: Armour That Absorbs Damage

Armour is useless if it doesn't protect you. Testing the `item_armor` pickup shows the blue
number goes up but lava still eats your health directly. We want **armour to soak damage
first, health to take the overflow** — and we want that everywhere, not just for lava.

Right now four places apply damage — lava (`trigger_system`), hitscan (`fire_hitscan`),
projectile impact (`update_projectiles`), and splash (`splash_damage`) — and each has its own
copy of the same invulnerability-check + clamp + flash logic. That duplication is the smell
that says "extract a function." Create a single shared helper.

`ecs/apply_damage.h`:

```cpp
#pragma once
#include <entt/entt.hpp>

// Deal `amount` damage to `target`. No-op (returns false) if the target has no
// Health or is invulnerable. Armour (if present) absorbs first; the remainder
// comes off Health, clamped at 0. Triggers the target's DamageFlash. Returns
// true if the hit landed — callers gate knockback on this.
bool applyDamage(entt::registry& registry, entt::entity target, float amount);
```

`ecs/apply_damage.cpp`:

```cpp
#include "engine/ecs/apply_damage.h"
#include "engine/ecs/components.h"
#include <algorithm>

bool applyDamage(entt::registry& registry, entt::entity target, float amount)
{
	if (amount <= 0.0f) return false;
	if (!registry.all_of<Health>(target)) return false;

	auto& health = registry.get<Health>(target);
	if (health.invulnerableTimer > 0.0f) return false;

	float remaining = amount;

	// Armour absorbs first (all of it, until depleted).
	if (Armor* armor = registry.try_get<Armor>(target))
	{
		float absorbed = std::min(armor->current, remaining);
		armor->current -= absorbed;
		remaining -= absorbed;
	}

	// Whatever armour didn't soak comes off health.
	health.current -= remaining;
	if (health.current < 0.0f) health.current = 0.0f;

	// The hit landed (on armour and/or health) — flash the screen.
	if (registry.all_of<DamageFlash>(target))
	{
		auto& flash = registry.get<DamageFlash>(target);
		flash.timer = flash.duration;
	}
	return true;
}
```

Now every damage site collapses to a call. Lava, in `trigger_system.cpp`:

```cpp
case TriggerAction::Damage:
{
	// Armour absorbs first, then health (see applyDamage).
	if (applyDamage(registry, entity, trigger.value * dt))
	{
		// Knockback: push player upward out of lava
		if (registry.all_of<PendingKnockback>(entity))
			registry.get<PendingKnockback>(entity).impulse += glm::vec3(0.0f, 1.0f, 0.0f);
	}
	break;
}
```

Hitscan, in `fire_hitscan.cpp`:

```cpp
// apply damage (armour-aware; flashes internally)
if (applyDamage(registry, entityHit->entity, weapon.damage))
{
	if (registry.all_of<PendingKnockback>(entityHit->entity))
	{
		glm::vec3 knockDir = glm::normalize(direction);
		registry.get<PendingKnockback>(entityHit->entity).impulse += knockDir * 1.0f;
	}
}
```

The projectile-impact and splash sites change the same way — replace the whole
guard/clamp/flash block with an `applyDamage` call (splash keeps its separate
velocity-based knockback). Add `#include "engine/ecs/apply_damage.h"` to each of the four
files and add `apply_damage.cpp` to `CMakeLists.txt`.

> **The win is twofold.** Armour now protects against *every* damage source for free, and ~20
> lines of copy-pasted invuln/flash logic collapse into one tested function. When enemies
> arrive and add new damage sources, they get armour handling automatically by calling the
> same helper.

---

## Step 8: HUD Polish

### Armour bar
The player has `Armor` but the HUD never showed it. Gather it alongside health in
`debug_hud_system.cpp`:

```cpp
float armor = 0.0f, maxArmor = 0.0f;
for (auto [entity, ap] : registry.view<Armor, TagPlayer>().each())
{
	armor = ap.current;
	maxArmor = ap.max;
}
```

and draw a blue bar just above the health bar, reusing `drawBar`:

```cpp
float armorY = barY - (barHeight + barGap);          // row above health
float armorPercent = (maxArmor > 0.0f) ? armor / maxArmor : 0.0f;
drawBar(barX, armorY, barWidth, barHeight, armorPercent, shader, ortho,
	glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.2f, 0.5f, 1.0f));
```

### Weapon-selection keys
The Nailgun pickup lands in inventory slot 2 — but only keys 1 and 2 were bound, so it was
unreachable. Bind the rest in `player_input_system.cpp`:

```cpp
if (input.isKeyPressed(GLFW_KEY_3)) playerInput.weaponSwitch = 2;
if (input.isKeyPressed(GLFW_KEY_4)) playerInput.weaponSwitch = 3;
if (input.isKeyPressed(GLFW_KEY_5)) playerInput.weaponSwitch = 4;
if (input.isKeyPressed(GLFW_KEY_6)) playerInput.weaponSwitch = 5;
if (input.isKeyPressed(GLFW_KEY_7)) playerInput.weaponSwitch = 6;
```

`weaponSwitchSystem` already clamps the slot to the inventory size, so pressing 3 before you
own a third weapon safely does nothing.

### Legibility panels
White text over a bright 3D scene is hard to read. Add a semi-transparent backing quad. This
reuses the `alpha` uniform the HUD shader gained in Chapter 16. New primitive
`debug_hud/draw_panel.cpp`:

```cpp
void drawPanel(float x, float y, float width, float height,
               unsigned int shaderId, const glm::mat4& projection,
               const glm::vec3& color, float alpha)
{
	if (alpha <= 0.0f) return;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// … build + draw a quad with textColor=color, alpha=alpha …
	glDisable(GL_BLEND);
}
```

Declare it in `debug_hud_internal.h`, add it to CMake, then draw a panel *before* each text
group — the FPS readout, the bottom-left status cluster (armour + health bars + ammo), and
the pickup toast. `drawText` and `drawBar` force `alpha = 1.0`, so text and bars stay crisp
on top of the dimmed backing.

---

## Step 9: Regression Scenarios

The headless harness (Chapter 10a) tests *behaviour*, and these are pure gameplay-logic
checks. Add four to `harness/headless_main.cpp`:

- **`pickup_health`** — wound the player, stand them on the health item, assert health rose,
  the pickup entity is gone, and the toast text was set.
- **`ammo_shells`** — fire once (shells −1), walk onto the shells item (+10). Proves both
  consumption and replenishment.
- **`armor_absorb`** — give 10 armour, pin the player in lava for ~1s (~25 damage). Assert
  armour drains to 0 *and* health takes the ~15 overflow — proving absorb-before-health.
- **`weapon_pickup`** — walk onto the Nailgun. Assert it's added, nails are granted, and
  **shells are unchanged** and the weapon didn't auto-switch (this is the "did the nailgun
  replace my shotgun ammo?" test — it doesn't; separate pools).

The `armor_absorb` loop is worth seeing — it re-pins the player each tick so the lava's
upward knockback can't float them out of the volume:

```cpp
reg.get<Armor>(player).current = 10.0f;
reg.get<Health>(player).current = 100.0f;
for (int i = 0; i < 60; i++)  // ~1s → ~25 damage
{
	teleportPlayer(reg, player, glm::vec3(20.0f, halfY, 25.0f));  // stay in the lava
	applyInput(reg, player, idle);
	qengine::stepSimulation(reg, jolt, level, dt);
}
// expect armor == 0, health ~85
```

Register each in `main`'s scenario dispatch. Running the suite:

```
[PASS] pickup_health — health 50→75, pickups 5→4, toast="Picked up Health +25"
[PASS] ammo_shells   — shells 25→24 (fire -1) →34 (pickup +10)
[PASS] armor_absorb  — after ~25 lava dmg: armor=0.0 (expect 0), health=85.0 (expect ~85)
[PASS] weapon_pickup — shells 25→25 (unchanged), nails 0→50, weapons 2→3, current=0
```

---

## What Changed — Summary

| File | Change |
|------|--------|
| `components/gameplay.h` | **New** `PickupType`, `Pickup`, `PickupMessage`; opaque `WeaponType` fwd-decl. |
| `components/combat.h` | **New** `Armor`. |
| `ecs/systems/pickup/pickup_system.{h,cpp}` | **New** — overlap → grant → destroy, message text. |
| `ecs/apply_damage.{h,cpp}` | **New** — shared armour-aware damage helper. |
| `level/factories.{h,cpp}` | **New** `spawnPickup`; player gets `Armor` + `PickupMessage`. |
| `level/classname_factory.cpp` | `item_*` / `weapon_*` factories + table entries. |
| `level/showcase_descriptor.cpp` | Five demo pickups placed in the room. |
| `combat_system.cpp` | Ammo gate: firing checks + consumes ammo (`ammoPool`). |
| `trigger_system.cpp`, `fire_hitscan.cpp`, `update_projectiles.cpp`, `splash_damage.cpp` | Route damage through `applyDamage` (removes duplicated invuln/flash logic). |
| `player_input_system.cpp` | Bind weapon keys 3–7. |
| `debug_hud/draw_panel.cpp` | **New** legibility panel primitive. |
| `debug_hud_system.cpp` | Armour bar, pickup toast, panels behind text. |
| `app/simulation.cpp` | `pickupSystem` in the tick order. |
| `harness/headless_main.cpp` | 4 new scenarios. |
| `CMakeLists.txt` | `pickup_system.cpp`, `apply_damage.cpp`, `draw_panel.cpp`. |

---

## What You Should See

Run `build/QEngine.exe`:

1. **Five items** float around the room (health, shells, rockets, armour, and a Nailgun).
2. **Walking over one** removes it and shows a gold **"Picked up …"** toast near the top.
3. **Firing the shotgun** drops the ammo counter; grabbing shells refills it.
4. **The armour shard** fills a blue bar above the health bar; standing in lava now drains
   **armour first**, health only once armour is gone.
5. **Press 3** to select the picked-up Nailgun (keys 1–2 for the starting weapons); your
   shotgun shells are untouched.
6. **HUD text** sits on subtle dark panels, readable over any scene.

---

## What's Next

The showcase is now a small but complete FPS loop: things to shoot with, things to pick up,
and stats that matter. The remaining hard-coded piece is the *level itself*. In **Chapter 20:
The `.map` Parser & Brush Geometry**, we finally replace `createShowcaseLevel()` +
`showcaseDescriptors()` with a TrenchBroom-authored `.map` file — parsing brush geometry into
meshes and colliders, and feeding its entities straight into the `spawnScene` pipeline from
Chapter 18. All the `item_*` / `weapon_*` classnames you just built become FGD entities you
can place by hand in the editor.
```
