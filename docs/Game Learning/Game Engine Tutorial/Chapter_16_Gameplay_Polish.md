# Chapter 16: Gameplay Polish

## What You'll Learn
- Drawing a crosshair at screen centre using raw GL_LINES
- Rendering health bars with coloured quads via the HUD shader
- Implementing death detection and respawn with invulnerability
- Flashing the screen red on damage using a full-screen overlay
- Applying knockback impulses when the player takes hits
- Wiring new systems into the existing tick order

---

## Step 1: Crosshair

The player needs a visual indicator of where they are aiming. A simple crosshair drawn at the centre of the screen does the job. We will use GL_LINES with the existing HUD shader, which already provides an orthographic projection and a `textColor` uniform.

### Why GL_LINES?

A crosshair is just two short lines. GL_LINES is the simplest way to draw them — no index buffer, no triangles, just 4 vertices. The HUD shader already accepts 2D screen-space positions through an orthographic projection, so we can reuse it directly.

### The crosshair helper function

Add this static function at the top of `debug_hud_system.cpp`, after the existing `drawText` function:

```cpp
static void drawCrosshair
(
	float centreX, float centreY,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& color
)
{
	// Crosshair: two lines with a 2px gap in the centre
	// Total width/height = 20px (10px each side, minus 2px gap)
	float gap = 2.0f;
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

	unsigned int vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

	glUseProgram(shaderId);

	GLint loc = glGetUniformLocation(shaderId, "projection");
	glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

	loc = glGetUniformLocation(shaderId, "textColor");
	glUniform3fv(loc, 1, &color[0]);

	// Draw as 4 separate line segments (GL_LINES draws pairs)
	glDrawArrays(GL_LINES, 0, 8);

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
```

The vertices define four line segments: left arm, right arm, top arm, bottom arm. `GL_LINES` interprets each pair of vertices as one line segment, so 8 vertices produce 4 lines. The gap in the centre keeps the exact aim point visible.

### Call it from the HUD system

At the bottom of `debugHudSystem`, just before the `glEnable(GL_DEPTH_TEST)` call, add:

```cpp
	// ─── Crosshair at screen centre ─────────────────────────
	drawCrosshair
	(
		windowWidth * 0.5f, windowHeight * 0.5f,
		shader, ortho, glm::vec3(1.0f)  // white
	);
```

The crosshair draws after text so it is always on top.

---

## Step 2: Health & Ammo Bars

Text-only health works for debugging, but a coloured bar gives instant visual feedback. The bar changes colour based on the health percentage: green when healthy, yellow when hurt, red when critical.

### Why quads through the HUD shader?

The HUD shader accepts screen-space positions and a `textColor` uniform. A filled rectangle is just two triangles — 6 vertices. We build the vertices on the CPU each frame (the bar width changes as health drops) and draw with `GL_TRIANGLES`.

### The bar helper function

Add this static function in `debug_hud_system.cpp`, after `drawCrosshair`:

```cpp
static void drawBar
(
	float x, float y, float width, float height,
	float fillPercent,
	unsigned int shaderId, const glm::mat4& projection,
	const glm::vec3& bgColor, const glm::vec3& fgColor
)
{
	// Background quad (full width)
	float bgVertices[] =
	{
		x,         y,          0.0f,
		x + width, y,          0.0f,
		x + width, y + height, 0.0f,
		x,         y,          0.0f,
		x + width, y + height, 0.0f,
		x,         y + height, 0.0f
	};

	// Foreground quad (partial width based on fillPercent)
	float fw = width * fillPercent;
	float fgVertices[] =
	{
		x,      y,          0.0f,
		x + fw, y,          0.0f,
		x + fw, y + height, 0.0f,
		x,      y,          0.0f,
		x + fw, y + height, 0.0f,
		x,      y + height, 0.0f
	};

	unsigned int vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

	glUseProgram(shaderId);
	GLint loc = glGetUniformLocation(shaderId, "projection");
	glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

	// Draw background
	glBufferData(GL_ARRAY_BUFFER, sizeof(bgVertices), bgVertices, GL_DYNAMIC_DRAW);
	loc = glGetUniformLocation(shaderId, "textColor");
	glUniform3fv(loc, 1, &bgColor[0]);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	// Draw foreground
	glBufferData(GL_ARRAY_BUFFER, sizeof(fgVertices), fgVertices, GL_DYNAMIC_DRAW);
	glUniform3fv(loc, 1, &fgColor[0]);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
```

We reuse the same VAO/VBO for both quads — just swap the vertex data and colour between draws. This avoids allocating two separate GPU objects for something this simple.

### Pick the health bar colour

Add this helper above the HUD system function:

```cpp
static glm::vec3 healthBarColor(float percent)
{
	if (percent > 0.5f)  return glm::vec3(0.0f, 0.8f, 0.0f);  // green
	if (percent > 0.25f) return glm::vec3(0.9f, 0.9f, 0.0f);  // yellow
	return glm::vec3(0.9f, 0.1f, 0.1f);                        // red
}
```

### Integrate into the HUD system

Replace the existing health text rendering in `debugHudSystem` with the health bar plus a condensed text label. Find the section that draws `healthText` and replace it with:

```cpp
	// ─── Health bar (bottom-left) ────────────────────────────
	float healthPercent = (maxHealth > 0.0f) ? health / maxHealth : 0.0f;
	float barX = 10.0f;
	float barY = (float)windowHeight - 30.0f;  // 30px from bottom
	float barWidth = 200.0f;
	float barHeight = 16.0f;

	drawBar
	(
		barX, barY, barWidth, barHeight,
		healthPercent, shader, ortho,
		glm::vec3(0.2f, 0.2f, 0.2f),           // dark grey background
		healthBarColor(healthPercent)             // colour based on %
	);

	// Health text on top of the bar
	char healthText[64];
	snprintf(healthText, sizeof(healthText), "HP: %.0f / %.0f", health, maxHealth);
	drawText
	(
		barX + 4.0f, barY + 2.0f, healthText,
		shader, ortho, textScale, glm::vec3(1.0f)
	);
```

### Ammo display

Add the ammo counter as text to the right of the health bar. After the health bar code:

```cpp
	// ─── Ammo counter (to the right of health bar) ──────────
	auto ammoView = registry.view<Ammo, WeaponInventory, TagPlayer>();
	for (auto [entity, ammo, inv] : ammoView.each())
	{
		if (inv.weapons.empty()) continue;
		const Weapon& currentWeapon = inv.weapons[inv.currentWeapon];

		const char* weaponName = "Unknown";
		int ammoCount = 0;
		switch (currentWeapon.type)
		{
			case WeaponType::Shotgun:
			case WeaponType::SuperShotgun:
				weaponName = "Shotgun";
				ammoCount = ammo.shells;
				break;
			case WeaponType::Nailgun:
				weaponName = "Nailgun";
				ammoCount = ammo.nails;
				break;
			case WeaponType::RocketLauncher:
			case WeaponType::GrenadeLauncher:
				weaponName = "Rockets";
				ammoCount = ammo.rockets;
				break;
			case WeaponType::LighteningGun:
			case WeaponType::Railgun:
				weaponName = "Cells";
				ammoCount = ammo.cells;
				break;
		}

		char ammoText[64];
		snprintf(ammoText, sizeof(ammoText), "%s: %d", weaponName, ammoCount);
		drawText
		(
			barX + barWidth + 20.0f, barY + 2.0f, ammoText,
			shader, ortho, textScale, glm::vec3(1.0f)
		);
	}
```

The weapon name and ammo count stay as text — a visual bar for ammo would add complexity without much benefit at this stage.

---

## Step 3: Death & Respawn

When the player's health hits zero, we need to reset them to a known starting position with full health. This requires two things: knowing *where* to respawn (a spawn point), and a system that detects death and performs the reset.

### New component: `SpawnPoint`

Add to `components.h`, in the State Components section:

```cpp
// ─── Spawn / Respawn ─────────────────────────────────────────
struct SpawnPoint
{
	glm::vec3 position = glm::vec3(0.0f);
	float yaw = 0.0f;  // facing direction on respawn (degrees)
};
```

> **Why a dedicated component instead of reusing Position?** The player's Position changes every frame. We need the *original* spawn location preserved, independent of movement. Storing it separately follows the ECS principle: each piece of data has one clear owner.

### Add invulnerability to `Health`

We need a brief grace period after respawning so the player doesn't immediately die again if they spawn near a hazard. Add a timer field to the existing `Health` component:

```cpp
struct Health
{
	float current;
	float max;
	float invulnerableTimer = 0.0f;  // seconds of remaining invulnerability
};
```

This is simpler than a separate component because invulnerability is tightly coupled to damage — every system that reduces health already has access to the `Health` component and can check this timer.

### New system: `player_death_system.h`

Create `src/engine/ecs/systems/player_death_system.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

void playerDeathSystem(entt::registry& registry);
```

### New system: `player_death_system.cpp`

Create `src/engine/ecs/systems/player_death_system.cpp`:

```cpp
#include "engine/ecs/systems/player_death_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/jolt_world.h"
#include "engine/physics/physics_config.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

void playerDeathSystem(entt::registry& registry)
{
	const auto& config = registry.ctx().get<PhysicsConfig>();
	float dt = config.fixedDeltaTime;

	auto view = registry.view<Health, Position, SpawnPoint, JoltCharacter, TagPlayer>();
	for (auto [entity, health, pos, spawn, joltChar] : view.each())
	{
		// ─── Tick down invulnerability ───────────────────────
		if (health.invulnerableTimer > 0.0f)
		{
			health.invulnerableTimer -= dt;
			if (health.invulnerableTimer < 0.0f)
				health.invulnerableTimer = 0.0f;
		}

		// ─── Check for death ────────────────────────────────
		if (health.current > 0.0f) continue;

		// Reset health
		health.current = health.max;
		health.invulnerableTimer = 1.0f;  // 1 second of invulnerability

		// Move to spawn point
		pos.value = spawn.position;

		// Teleport the Jolt CharacterVirtual to the spawn position
		auto& character = joltChar.character;
		character->SetPosition(JPH::RVec3(spawn.position.x, spawn.position.y, spawn.position.z));
		character->SetLinearVelocity(JPH::Vec3::sZero());
	}
}
```

The system does three things each tick:

1. **Ticks down invulnerability** on all players, regardless of health state.
2. **Checks for death** (health <= 0).
3. **Resets everything**: health to max, position to spawn, Jolt character teleported and velocity zeroed.

Teleporting the `CharacterVirtual` is essential — without it, the player's physics body stays at the death location and snaps back next frame.

### Guard damage with invulnerability

Every system that reduces health must check the invulnerability timer. Update the damage paths:

**In `trigger_system.cpp`**, in the `TriggerAction::Damage` case:

```cpp
case TriggerAction::Damage:
{
	if (registry.all_of<Health>(entity))
	{
		auto& health = registry.get<Health>(entity);
		if (health.invulnerableTimer <= 0.0f)  // NEW: guard
		{
			health.current -= trigger.value * dt;
			if (health.current < 0.0f) health.current = 0.0f;
		}
	}
	break;
}
```

**In `combat_system.cpp`**, in the hitscan damage application (inside `fireHitscan`):

```cpp
// apply damage
if (registry.all_of<Health>(entityHit->entity))
{
	auto& health = registry.get<Health>(entityHit->entity);
	if (health.invulnerableTimer <= 0.0f)  // NEW: guard
	{
		health.current -= weapon.damage;
		if (health.current < 0.0f) health.current = 0.0f;
	}
}
```

**In `combat_system.cpp`**, in the projectile collision damage:

```cpp
// apply damage if the target has Health
if (registry.all_of<Health>(target))
{
	auto& health = registry.get<Health>(target);
	if (health.invulnerableTimer <= 0.0f)  // NEW: guard
	{
		health.current -= proj.damage;
		if (health.current < 0.0f) health.current = 0.0f;
	}
}
```

**In `combat_system.cpp`**, in `applySplashDamage`:

```cpp
float damage = maxDamage * scale;
if (health.invulnerableTimer <= 0.0f)  // NEW: guard
{
	health.current -= damage;
	if (health.current < 0.0f) health.current = 0.0f;
}
```

---

## Step 4: Damage Feedback

When the player takes a hit, the screen should briefly flash red. This gives immediate feedback that damage occurred, even when the health bar is in peripheral vision.

### New component: `DamageFlash`

Add to `components.h`:

```cpp
struct DamageFlash
{
	float timer = 0.0f;       // remaining flash time
	float duration = 0.3f;    // total flash length (seconds)
};
```

### Trigger the flash on damage

We need to set `timer = duration` whenever health decreases. The cleanest approach is to snapshot the previous health and compare after damage.

**In `trigger_system.cpp`**, update the `TriggerAction::Damage` case:

```cpp
case TriggerAction::Damage:
{
	if (registry.all_of<Health>(entity))
	{
		auto& health = registry.get<Health>(entity);
		if (health.invulnerableTimer <= 0.0f)
		{
			float before = health.current;
			health.current -= trigger.value * dt;
			if (health.current < 0.0f) health.current = 0.0f;

			// Trigger damage flash if health actually decreased
			if (health.current < before && registry.all_of<DamageFlash>(entity))
			{
				auto& flash = registry.get<DamageFlash>(entity);
				flash.timer = flash.duration;
			}
		}
	}
	break;
}
```

**In `combat_system.cpp`**, in the hitscan damage section of `fireHitscan`:

```cpp
if (registry.all_of<Health>(entityHit->entity))
{
	auto& health = registry.get<Health>(entityHit->entity);
	if (health.invulnerableTimer <= 0.0f)
	{
		float before = health.current;
		health.current -= weapon.damage;
		if (health.current < 0.0f) health.current = 0.0f;

		if (health.current < before && registry.all_of<DamageFlash>(entityHit->entity))
		{
			auto& flash = registry.get<DamageFlash>(entityHit->entity);
			flash.timer = flash.duration;
		}
	}
}
```

Apply the same pattern to projectile direct damage and splash damage in `combat_system.cpp`. The key is: capture `before`, apply damage, compare, trigger flash.

### Update the HUD shader for alpha blending

The damage overlay needs to be semi-transparent. The current HUD fragment shader outputs `vec4(textColor, 1.0)` — always fully opaque. Add an alpha uniform so we can control transparency.

**Update `assets/shaders/hud.frag`:**

```glsl
#version 460 core

out vec4 FragColor;

uniform vec3 textColor;
uniform float alpha;

void main()
{
	FragColor = vec4(textColor, alpha);
}
```

**Update every existing helper function** to set `alpha = 1.0` so nothing changes visually. Add these two lines after each `glUniform3fv(loc, 1, &color[0])` call that sets `textColor`:

```cpp
GLint alphaLoc = glGetUniformLocation(shaderId, "alpha");
glUniform1f(alphaLoc, 1.0f);
```

There are three places — one per helper:

1. **`drawText`** — after the `textColor` uniform is set (just before `glDrawElements`)
2. **`drawCrosshair`** — after the `textColor` uniform is set (just before `glDrawArrays`)
3. **`drawBar`** — after the **first** `textColor` uniform is set (before drawing the background quad). You only need it once here — `alpha` stays `1.0` for both the background and foreground draws

Existing rendering stays fully opaque. The `alpha` uniform only matters for the damage flash overlay we're about to add.

### Tick down the flash timer

In `debugHudSystem`, after gathering player data but before drawing anything, add the timer countdown. We read `dt` from the physics config:

```cpp
	// ─── Tick damage flash ──────────────────────────────────
	float flashAlpha = 0.0f;
	{
		const auto& config = registry.ctx().get<PhysicsConfig>();
		float dt = config.fixedDeltaTime;

		auto flashView = registry.view<DamageFlash, TagPlayer>();
		for (auto [entity, flash] : flashView.each())
		{
			if (flash.timer > 0.0f)
			{
				flashAlpha = (flash.timer / flash.duration) * 0.4f;
				flash.timer -= dt;
				if (flash.timer < 0.0f) flash.timer = 0.0f;
			}
		}
	}
```

> **Why tick the timer in the HUD system?** The flash is purely visual feedback — it has no gameplay effect. Ticking it in the rendering path keeps the visual concern out of the game logic systems. If we later add a dedicated "effects system," we can move it there.

### Draw the damage overlay

At the bottom of `debugHudSystem`, just before re-enabling depth testing, draw a full-screen red quad when `flashAlpha > 0`:

```cpp
	// ─── Damage flash overlay ───────────────────────────────
	if (flashAlpha > 0.0f)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		float overlayVerts[] =
		{
			0.0f,                  0.0f,                   0.0f,
			(float)windowWidth,    0.0f,                   0.0f,
			(float)windowWidth,    (float)windowHeight,    0.0f,
			0.0f,                  0.0f,                   0.0f,
			(float)windowWidth,    (float)windowHeight,    0.0f,
			0.0f,                  (float)windowHeight,    0.0f
		};

		unsigned int oVao, oVbo;
		glGenVertexArrays(1, &oVao);
		glGenBuffers(1, &oVbo);
		glBindVertexArray(oVao);
		glBindBuffer(GL_ARRAY_BUFFER, oVbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(overlayVerts), overlayVerts, GL_DYNAMIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

		glUseProgram(shader);
		GLint loc = glGetUniformLocation(shader, "projection");
		glUniformMatrix4fv(loc, 1, GL_FALSE, &ortho[0][0]);

		loc = glGetUniformLocation(shader, "textColor");
		glm::vec3 red(1.0f, 0.0f, 0.0f);
		glUniform3fv(loc, 1, &red[0]);

		GLint alphaLoc = glGetUniformLocation(shader, "alpha");
		glUniform1f(alphaLoc, flashAlpha);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glBindVertexArray(0);
		glDeleteBuffers(1, &oVbo);
		glDeleteVertexArrays(1, &oVao);

		glDisable(GL_BLEND);
	}
```

The overlay starts at 40% opacity (`0.4f`) and fades to zero over 0.3 seconds. `GL_BLEND` is enabled only for this draw and disabled immediately after.

### Add the include for PhysicsConfig

At the top of `debug_hud_system.cpp`, add:

```cpp
#include "engine/physics/physics_config.h"
```

---

## Step 5: Knockback

Damage should push the player away from the source. Lava pushes you upward (the floor is hot, jump!). Hitscan and projectile damage pushes you in the direction of the shot. This makes combat feel physical.

### New component: `PendingKnockback`

Add to `components.h`:

```cpp
struct PendingKnockback
{
	glm::vec3 impulse = glm::vec3(0.0f);
};
```

This is a one-frame buffer. Damage systems write an impulse, the player movement system reads and clears it. The separation prevents damage systems from needing to know about Jolt's character controller.

### Apply knockback from lava

In `trigger_system.cpp`, in the `TriggerAction::Damage` case, after reducing health:

```cpp
// Knockback: push player upward out of lava
if (health.current < before && registry.all_of<PendingKnockback>(entity))
{
	registry.get<PendingKnockback>(entity).impulse += glm::vec3(0.0f, 5.0f, 0.0f);
}
```

This adds a vertical kick every time lava deals damage. The `+=` means multiple damage sources in the same tick stack their knockback.

### Apply knockback from hitscan

In `combat_system.cpp`, in the hitscan damage section of `fireHitscan`, after reducing health:

```cpp
// Knockback: push target in the direction of the shot
if (health.current < before && registry.all_of<PendingKnockback>(entityHit->entity))
{
	glm::vec3 knockDir = glm::normalize(direction);
	registry.get<PendingKnockback>(entityHit->entity).impulse += knockDir * 5.0f;
}
```

### Apply knockback from projectile impact

In `combat_system.cpp`, in the projectile collision section, after reducing health:

```cpp
// Knockback: push target away from projectile
if (health.current < before && registry.all_of<PendingKnockback>(target))
{
	glm::vec3 knockDir = glm::normalize(vel.value);
	registry.get<PendingKnockback>(target).impulse += knockDir * 8.0f;
}
```

Projectiles use a stronger knockback (8.0 vs 5.0) because rockets should feel impactful.

### Consume knockback in the player movement system

In `player_character_system.cpp`, at the point where we set the desired velocity on the character (just before `character->SetLinearVelocity(desiredVel)`), add the knockback impulse:

```cpp
		// ─── Apply pending knockback ────────────────────────
		if (registry.all_of<PendingKnockback>(entity))
		{
			auto& kb = registry.get<PendingKnockback>(entity);
			if (kb.impulse.x != 0.0f || kb.impulse.y != 0.0f || kb.impulse.z != 0.0f)
			{
				desiredVel += JPH::Vec3(kb.impulse.x, kb.impulse.y, kb.impulse.z);
				kb.impulse = glm::vec3(0.0f);  // consumed
			}
		}

		character->SetLinearVelocity(desiredVel);
```

The impulse is added directly to the velocity and zeroed. On the next tick, if no new damage occurs, `PendingKnockback::impulse` stays at zero and has no effect.

> **Why add to velocity instead of using Jolt's `AddImpulse`?** `CharacterVirtual` is not a rigid body — it doesn't have mass-based impulse handling. We control its velocity directly each frame, so we simply add the knockback to the desired velocity before passing it to `ExtendedUpdate`.

---

## Step 6: Wire Everything Up

### Update `components.h`

The complete set of new components added to `components.h`:

```cpp
// ─── Spawn / Respawn ─────────────────────────────────────────
struct SpawnPoint
{
	glm::vec3 position = glm::vec3(0.0f);
	float yaw = 0.0f;
};

// ─── Damage Feedback ─────────────────────────────────────────
struct DamageFlash
{
	float timer = 0.0f;
	float duration = 0.3f;
};

struct PendingKnockback
{
	glm::vec3 impulse = glm::vec3(0.0f);
};
```

And the updated `Health` component:

```cpp
struct Health
{
	float current;
	float max;
	float invulnerableTimer = 0.0f;
};
```

### Update `scene_setup.cpp`

Add the new components to the player entity. After the existing `registry.emplace<Health>(player, 100.0f, 100.0f);` line:

```cpp
	registry.emplace<Health>(player, 100.0f, 100.0f, 0.0f);
	registry.emplace<SpawnPoint>(player, glm::vec3(15.0f, 1.7f, 15.0f), 0.0f);
	registry.emplace<DamageFlash>(player);
	registry.emplace<PendingKnockback>(player);
```

Note that `Health` now takes three arguments (current, max, invulnerableTimer). Remove the old `registry.emplace<Health>(player, 100.0f, 100.0f);` line and use the one above.

### Update `CMakeLists.txt`

Add the new source file:

```cmake
	src/engine/ecs/systems/player_death_system.cpp
```

Add it alongside the other system `.cpp` files in the `add_executable` list.

### Update `main.cpp`

Add the include at the top:

```cpp
#include "engine/ecs/systems/player_death_system.h"
```

Add `playerDeathSystem` to the tick order, right after `triggerSystem`:

```cpp
		while (fixedTimestep.step())
		{
			weaponSwitchSystem(registry);
			playerCharacterSystem(registry);
			moverSystem(registry);
			moverSyncSystem(registry);
			joltWorld.step(physicsConfig.fixedDeltaTime);
			joltSyncSystem(registry);
			combatSystem(registry, level);
			lifetimeSystem(registry);
			triggerSystem(registry);
			playerDeathSystem(registry);              // NEW
			demoResetSystem(registry);
		}
```

### Complete updated tick order

```
 #  System                    Phase         Purpose
────────────────────────────────────────────────────────────────────
 1  weaponSwitchSystem        Input         Handle weapon swap input
 2  playerCharacterSystem     Movement      Apply input + knockback -> CharacterVirtual
 3  moverSystem               Animation     Animate door/lift positions
 4  moverSyncSystem           Physics Prep  Push mover positions to Jolt
 5  joltWorld.step()          Physics       Simulate all rigid bodies
 6  joltSyncSystem            Physics Sync  Read Jolt transforms -> ECS
 7  combatSystem              Game Logic    Hitscan/projectile weapons + damage flash + knockback
 8  lifetimeSystem            Cleanup       Auto-destroy timed entities
 9  triggerSystem             Game Logic    Trigger overlaps + damage flash + knockback
10  playerDeathSystem         Respawn       Detect death, respawn, tick invulnerability
11  demoResetSystem           Cleanup       Reset physics demo objects
```

`playerDeathSystem` runs after `triggerSystem` so that lava damage (trigger) has a chance to kill the player before the death check runs. It runs before `demoResetSystem` because demo resets are independent cleanup.

---

## What Changed -- Summary

| File | Change |
|------|--------|
| `components.h` | Added `SpawnPoint`, `DamageFlash`, `PendingKnockback` components. Added `invulnerableTimer` to `Health`. |
| `debug_hud_system.cpp` | Added `drawCrosshair()`, `drawBar()`, `healthBarColor()`. Replaced text-only health with health bar. Added ammo counter text. Added crosshair rendering. Added damage flash overlay with alpha blending. Added `PhysicsConfig` include. |
| `player_death_system.h` | **New file** -- header for death/respawn system. |
| `player_death_system.cpp` | **New file** -- death detection, respawn, invulnerability timer. |
| `player_character_system.cpp` | Reads `PendingKnockback` and adds impulse to desired velocity before `SetLinearVelocity`. |
| `combat_system.cpp` | Guards damage with `invulnerableTimer`. Sets `DamageFlash::timer` and `PendingKnockback::impulse` on hit. |
| `trigger_system.cpp` | Guards damage with `invulnerableTimer`. Sets `DamageFlash::timer` and `PendingKnockback::impulse` on lava damage. |
| `scene_setup.cpp` | Adds `SpawnPoint`, `DamageFlash`, `PendingKnockback` to player entity. Updated `Health` to include `invulnerableTimer`. |
| `hud.frag` | Added `uniform float alpha` for semi-transparent overlay rendering. |
| `main.cpp` | Added `#include` for `player_death_system.h`. Added `playerDeathSystem(registry)` after `triggerSystem` in tick order. |
| `CMakeLists.txt` | Added `player_death_system.cpp` to source list. |

---

## What You Should See

After building and running:

1. **White crosshair** at the exact centre of the screen, visible at all times
2. **Green health bar** at bottom-left that transitions to yellow below 50%, red below 25%
3. **Ammo counter** showing the current weapon name and ammo count to the right of the health bar
4. **Walking into lava** drains health, flashes the screen red, and gives a slight upward kick
5. **Health reaching zero** resets the player to the spawn point with full health
6. **1 second of invulnerability** after respawning (lava damage has no effect during this time)
7. **Taking hitscan/projectile damage** pushes the player in the shot direction and flashes red

---

## What's Next

The hardcoded showcase level has served us well, but real levels need real tools. In **Chapter 17: .map Parser & Brush Rendering**, we will replace the procedural level with TrenchBroom-authored maps -- parsing the `.map` file format, converting brush geometry into renderable meshes, and loading arbitrary levels from disk.
