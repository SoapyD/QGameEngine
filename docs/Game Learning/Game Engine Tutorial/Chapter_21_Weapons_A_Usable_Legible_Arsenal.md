# Chapter 21: Weapons — a Usable, Legible Arsenal

## What You'll Learn
- Why a **fixed 7-slot** weapon inventory (`std::array`, not `std::vector`) makes number keys 1-7 stable, and how `owned[]` separates "the slot exists" from "you can use it"
- Populating every slot's stats up-front in `spawnPlayer`, then letting pickups just flip an `owned` flag
- Teaching `weaponSwitchSystem`, `combatSystem`, and the ammo HUD to respect ownership
- Building **procedural first-person gun meshes** out of a handful of axis-aligned boxes, one silhouette per weapon
- A **flat-albedo shader path** (`useAlbedo` / `albedoColor`) that lights a solid colour with no texture — and how to set it per-entity so it can't leak
- Drawing an animated **weapon viewmodel** in view space with idle bob, fire recoil, and a raise-on-switch — windowed build only
- Rendering **weapon pickups as their actual gun models**, coloured by a `Colour` component through the same albedo path
- A top-of-screen **HUD weapon bar** that shows what you own, what's selected, and what's still out there to collect
- Wiring it all: shared meshes loaded once (real when windowed, GL-free stubs when headless), CMake, and the regression-harness updates

---

## Where We Are

After Chapter 19 the player could pick items up, and Chapter 20 gave the whole loop a voice.
But the *weapons* themselves were half-built. The inventory was a `std::vector<Weapon>` that
grew as you collected guns — which meant slot 1 was "the first weapon you happen to own", not "the
shotgun". Switching was positional, so the same number key selected different guns depending on
pickup order. And you never actually *saw* a weapon: no gun in your hands, and weapon pickups on
the floor were the same anonymous textured cube as a health box.

This chapter turns that into a real arsenal. We'll do it in four connected parts, in the order
that makes each one buildable on top of the last:

1. **A fixed 7-slot inventory** — the foundation everything else indexes into.
2. **A first-person viewmodel** — the gun in your hands, procedurally modelled and animated.
3. **Weapon pickups drawn as those same gun models** — so the floor shows you what you'd get.
4. **A HUD weapon bar** — a glanceable readout of the whole arsenal.

Parts 2-4 all lean on the same two ideas introduced in Part 2: seven procedural gun meshes shared
across the game, and a flat-albedo shader path that colours them without a texture. So we build
the inventory first, then the meshes, then everything that draws them.

---

## Part 1 — The Fixed 7-Slot Inventory

### Step 1: Index the Inventory by `WeaponType`

The `WeaponType` enum has always been the canonical list of weapons, in a fixed order:

```cpp
enum class WeaponType
{
	Shotgun,
	SuperShotgun,
	Nailgun,
	RocketLauncher,
	GrenadeLauncher,
	LighteningGun,
	Railgun
};
```

Seven values, `0..6`. The old `WeaponInventory` ignored that order and stored weapons in
collection order. The new one makes the enum *be* the index. In
`engine/ecs/components/combat.h`:

```cpp
// Fixed 7-slot inventory indexed by WeaponType (0..6). A slot is selectable and
// fireable only when owned[slot] is true, so number keys 1-7 always map to the
// same weapon type regardless of pickup order. spawnPlayer populates every slot's
// stats and marks the starting weapons owned; pickups just flip an owned flag.
struct WeaponInventory
{
	std::array<Weapon, 7> weapons{};
	std::array<bool, 7>   owned{};
	int currentWeapon = 0; // WeaponType index of the active weapon
};
```

The header now includes `<array>` instead of `<vector>`. `weapons[i]` is the stats for
`WeaponType(i)` whether or not you have it; `owned[i]` says whether you can select and fire it.
`currentWeapon` is a `WeaponType` index, not a position in a growing list.

> **Why a fixed array with a parallel `owned[]`, rather than a growing vector?** The whole point of
> a slot machine gun-select (press "3" for the nailgun) is that the mapping is *stable*. With a
> vector, "slot 3" meant "the third weapon I picked up" — press 3 and you got a different gun
> depending on the order you walked over pickups. Indexing by `WeaponType` makes "3" always the
> nailgun. Splitting "does the slot have stats" (`weapons`) from "can I use it" (`owned`) means we
> can fill in *every* weapon's stats once, up front, and a pickup becomes a single `owned[slot] =
> true` — no allocation, no search, no reordering.

### Step 2: Fill Every Slot in `spawnPlayer`

Because the array is always seven long, `spawnPlayer` populates all seven slots' stats at spawn,
then marks just the starting loadout as owned. In `engine/level/factories.cpp`:

```cpp
// Fixed 7-slot inventory: every slot carries its weapon's stats, but the
// player only *owns* (can select/fire) the shotgun + rocket launcher to
// start. Number keys 1-7 map straight to WeaponType; the rest are collected.
WeaponInventory inv;
for (int i = 0; i < 7; ++i)
	inv.weapons[i] = createWeapon(static_cast<WeaponType>(i));
inv.owned[static_cast<int>(WeaponType::Shotgun)] = true;
inv.owned[static_cast<int>(WeaponType::RocketLauncher)] = true;
inv.currentWeapon = static_cast<int>(WeaponType::Shotgun);
reg.emplace<WeaponInventory>(player, std::move(inv));
```

`createWeapon(WeaponType)` (from `engine/ecs/weapon_definitions.h`) returns the fully-configured
`Weapon` stats for a type — damage, fire rate, spread, pellet count, and so on. We call it for all
seven so a weapon you pick up later is *already* statted; the pickup only has to grant it.

> **Why stat every slot at spawn instead of when the weapon is collected?** It keeps the collect
> path trivial and branch-free. If stats were filled on pickup, `pickupSystem` would need the whole
> weapon-definition table and the logic to build a `Weapon`. By doing it once at spawn, ownership
> and stats are decoupled: the stats are constant data that's always present, and "owning" a weapon
> is purely a boolean. It also means `combatSystem` and the viewmodel can read `weapons[slot]`
> unconditionally — the stats are never missing, only possibly unowned.

### Step 3: Every Reader Now Checks `owned`

Three systems read the inventory, and each gains an ownership check so an unowned slot behaves as
if it isn't there.

**`weaponSwitchSystem`** (header-only, `engine/ecs/systems/combat/weapon_switch_system.h`) gains
one line in its guard — you can't switch *to* a weapon you don't have:

```cpp
if (input.weaponSwitch >= 0 &&
input.weaponSwitch < static_cast<int>(inv.weapons.size()) &&
inv.owned[input.weaponSwitch] &&               // can't select a weapon you don't have
input.weaponSwitch != inv.currentWeapon)
{
	inv.currentWeapon = input.weaponSwitch;
	queueSound(registry, "weapon.switch");
}
```

(The `queueSound` line is the weapon-switch audio from Chapter 20c; it rides inside the same guard,
so pressing a number for a gun you don't own is silent because the branch never runs.)

**`combatSystem`** (`engine/ecs/systems/combat/combat_system.cpp`) replaces the old
"is the vector empty?" guard with a range-and-ownership check on the *active* slot:

```cpp
if (!input.fire) continue;
if (inv.currentWeapon < 0 || inv.currentWeapon >= (int)inv.weapons.size()) continue;
if (!inv.owned[inv.currentWeapon]) continue;   // active slot must be a held weapon

Weapon& weapon = inv.weapons[inv.currentWeapon];
```

**The ammo HUD** (`engine/ecs/systems/debug_hud/draw_ammo.cpp`) skips drawing when the current slot
isn't owned:

```cpp
if (!inv.owned[inv.currentWeapon]) continue;
const Weapon& currentWeapon = inv.weapons[inv.currentWeapon];
```

> **Why does every reader need the `owned` check, when `spawnPlayer` guarantees a valid
> `currentWeapon`?** Because the array is always full — `weapons[currentWeapon]` is *never* a
> null/out-of-range access the way an empty vector was. That safety flips into a hazard: without the
> `owned` gate, you could fire or display a weapon whose stats exist but which you were never meant
> to have. The check restores the invariant the old `weapons.empty()` test used to give for free:
> "only act on a weapon you actually hold."

### Step 4: Pickups Flip a Flag

Granting a weapon is now the cheap operation the design was built around. In
`engine/ecs/systems/pickup/pickup_system.cpp`, the old "search the vector, push if absent" becomes
an index and a flag:

```cpp
if (reg.all_of<WeaponInventory>(receiver))
{
	auto& inv = reg.get<WeaponInventory>(receiver);
	const int slot = static_cast<int>(pickup.weaponType);
	if (!inv.owned[slot])
	{
		inv.weapons[slot] = createWeapon(pickup.weaponType);
		inv.owned[slot] = true;
	}
}
```

We re-`createWeapon` into the slot (harmless — it was already statted at spawn) and set
`owned[slot] = true`. No `std::any_of`, no `push_back`, no reordering.

> **Why not auto-switch to a weapon you just collected?** Notice the grant doesn't touch
> `currentWeapon`. Picking up the nailgun makes it *available* (its bar cell lights up, "3" now
> selects it) but doesn't yank it into your hands mid-fight. That's a deliberate FPS convention —
> you choose when to swap. The headless test in Step 15 asserts exactly this: `currentWeapon` is
> unchanged after collecting a weapon.

That's the whole foundation. The inventory is now a stable, index-addressable arsenal. Everything
that follows *draws* it.

---

## Part 2 — The First-Person Viewmodel

### Step 5: Procedural Gun Meshes

We don't have modelled gun art, and we don't need it — a recognisable *silhouette* per weapon is
enough to tell them apart in the hand and on the floor. So we build each gun from a few
axis-aligned boxes at load time. Create `engine/renderer/gun_mesh.h`:

```cpp
#pragma once

#include "engine/ecs/components/combat.h"   // WeaponType

#include <glm/glm.hpp>
#include <memory>

class Mesh;

// Procedurally build a rudimentary first-person gun mesh for a weapon type,
// composed from a few axis-aligned boxes (body / barrel(s) / grip) in gun-local
// space: origin at the grip, the barrel points toward -Z. Requires a live GL
// context (uploads to a VAO). Used by the weapon viewmodel.
std::shared_ptr<Mesh> buildGunMesh(WeaponType type);

// A distinct flat colour per weapon so they read apart at a glance.
glm::vec3 weaponColor(WeaponType type);

// A short 2-3 letter abbreviation for the HUD weapon bar (e.g. "SG", "RL").
const char* weaponAbbrev(WeaponType type);
```

Three functions, all keyed on `WeaponType`: the mesh, a colour, and an abbreviation. The colour and
abbreviation are used by the pickups and HUD later — we declare them here because they belong with
the gun's visual identity.

The implementation, `engine/renderer/gun_mesh.cpp`, starts with a helper that appends one box to a
growing vertex/index list, with correct outward normals so the boxes light properly:

```cpp
namespace
{
    // Append an axis-aligned box (centre, halfExtents) to the vertex/index lists
    // with outward per-face normals and CCW winding (front-facing under back-face
    // culling). Six faces, four verts each.
    void appendBox(std::vector<Vertex>& v, std::vector<unsigned int>& idx,
                   glm::vec3 c, glm::vec3 h)
    {
        struct Face { glm::vec3 n, u, w; };
        const Face faces[6] = {
            {{ 0, 0, 1},{ 1,0,0},{0,1, 0}}, // +Z
            {{ 0, 0,-1},{-1,0,0},{0,1, 0}}, // -Z
            {{ 1, 0, 0},{ 0,0,-1},{0,1,0}}, // +X
            {{-1, 0, 0},{ 0,0, 1},{0,1,0}}, // -X
            {{ 0, 1, 0},{ 1,0,0},{0,0,-1}}, // +Y
            {{ 0,-1, 0},{ 1,0,0},{0,0, 1}}, // -Y
        };
        for (const Face& f : faces)
        {
            unsigned int base = (unsigned int)v.size();
            glm::vec3 uu = f.u * h;
            glm::vec3 ww = f.w * h;
            glm::vec3 centre = c + f.n * h;
            glm::vec3 corners[4] = {
                centre - uu - ww, centre + uu - ww,
                centre + uu + ww, centre - uu + ww,
            };
            for (int i = 0; i < 4; ++i)
                v.push_back(Vertex{ corners[i], f.n, glm::vec2(0.0f) });
            idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
    }
}
```

Each face is defined by its normal `n` and two in-plane axes `u`, `w`; the four corners are the
face centre plus/minus the scaled axes, and the two triangles wind counter-clockwise so they survive
back-face culling. The texture coordinate is `(0,0)` for every vertex — these meshes are never
textured, they're flat-coloured (Step 7).

`buildGunMesh` then assembles each weapon from a shared grip box plus a distinctive body/barrel
arrangement:

```cpp
std::shared_ptr<Mesh> buildGunMesh(WeaponType type)
{
    std::vector<Vertex> v;
    std::vector<unsigned int> i;

    // Grip below/behind the body — common to every weapon.
    appendBox(v, i, { 0.00f, -0.06f,  0.05f }, { 0.030f, 0.060f, 0.040f });

    switch (type)
    {
        case WeaponType::Shotgun: // stubby side-by-side double barrel
            appendBox(v, i, { 0.00f, 0.00f, -0.05f }, { 0.050f, 0.050f, 0.120f });
            appendBox(v, i, {-0.030f, 0.00f, -0.28f }, { 0.025f, 0.030f, 0.140f });
            appendBox(v, i, { 0.030f, 0.00f, -0.28f }, { 0.025f, 0.030f, 0.140f });
            break;
        // … one case per WeaponType: super shotgun, nailgun, rocket launcher,
        //   grenade launcher, lightning gun, railgun …
    }

    return std::make_shared<Mesh>(v, i);
}
```

Every case shares the grip box (so all guns sit the same way in the hand) and adds boxes that give
a readable profile: the rocket launcher is a single fat tube, the nailgun two thin stacked barrels,
the railgun one very long thin barrel, the lightning gun a barrel with two prongs at the tip, and
so on. The barrel always points toward `-Z`, gun-local, with the grip at the origin — that
convention is what lets the viewmodel place the gun with a fixed offset (Step 9).

Alongside the mesh, `weaponColor` and `weaponAbbrev` give each weapon a colour and a label:

```cpp
glm::vec3 weaponColor(WeaponType type)
{
    switch (type)
    {
        case WeaponType::Shotgun:         return { 0.85f, 0.22f, 0.20f }; // red
        case WeaponType::SuperShotgun:    return { 0.55f, 0.10f, 0.12f }; // crimson
        case WeaponType::Nailgun:         return { 0.90f, 0.80f, 0.20f }; // yellow
        case WeaponType::RocketLauncher:  return { 0.95f, 0.55f, 0.15f }; // orange
        case WeaponType::GrenadeLauncher: return { 0.30f, 0.72f, 0.28f }; // green
        case WeaponType::LighteningGun:   return { 0.30f, 0.78f, 0.95f }; // cyan
        case WeaponType::Railgun:         return { 0.72f, 0.32f, 0.88f }; // purple
    }
    return { 0.80f, 0.80f, 0.80f };
}
```

`weaponAbbrev` returns `"SG"`, `"SSG"`, `"NG"`, `"RL"`, `"GL"`, `"LG"`, `"RG"` — the labels the
weapon bar prints in Step 14.

> **Why model guns procedurally out of boxes instead of loading `.obj` art?** It costs nothing and
> couples to nothing. We already have a `Mesh` that uploads a vertex/index list to a VAO; a gun is
> just a few boxes' worth of that list, built in a dozen lines. There are no asset files to ship,
> load, or keep in sync with the seven `WeaponType` values — add a weapon and you add a `case`, the
> same edit you already make everywhere else. It's placeholder art with a real purpose: seven
> silhouettes distinct enough that you can tell your weapon apart at a glance, in the hand *and* on
> the floor, because pickups reuse these exact meshes (Part 3).

### Step 6: Load the Seven Meshes Once, Shared

Both the viewmodel (your gun) and the pickups (guns on the floor) want the same seven meshes. Build
them once and store them in the `ResourceManager` under `"gun_0"`..`"gun_6"`. In
`engine/app/simulation.cpp`:

```cpp
// Store the 7 first-person / pickup gun meshes as "gun_0".."gun_6". Real
// meshes when we have a GL context; GL-free stubs for the headless harness.
static void loadGunMeshes(ResourceManager& resources, bool headless)
{
    for (int i = 0; i < 7; ++i)
    {
        std::string name = "gun_" + std::to_string(i);
        resources.storeMesh(name, headless
            ? std::make_shared<Mesh>(nullptr)
            : buildGunMesh(static_cast<WeaponType>(i)));
    }
}
```

`loadResources` calls it in both branches — headless stubs first:

```cpp
resources.storeMesh("cube", std::make_shared<Mesh>(nullptr));
loadGunMeshes(resources, /*headless=*/true);
return;
```

and real meshes in the windowed branch:

```cpp
resources.getMesh("cube", "assets/models/cube.obj");
loadGunMeshes(resources, /*headless=*/false);
```

`Mesh(nullptr)` is the GL-free stub constructor (`explicit Mesh(std::nullptr_t) noexcept {}`) — it
allocates no OpenGL objects. `buildGunMesh` uploads to a real VAO and so needs a live GL context,
which is exactly why it can only run in the windowed branch.

> **Why store the guns in the `ResourceManager` at all, rather than building them where they're
> used?** Two consumers need identical meshes — the viewmodel and the pickup factory. Building them
> once and handing out `shared_ptr`s means both draw the same VAOs; there's one upload, one owner,
> and no duplication. Storing them under stable names (`"gun_3"`) is the same pattern the cube mesh
> and textures already use, so `scene_setup` and the viewmodel can both look them up by name without
> a new plumbing path. And routing through the `headless` flag keeps the harness building the exact
> same resource set — just with stubs — so nothing downstream has to ask "are we headless?"

### Step 7: A Flat-Albedo Shader Path

The gun meshes carry no texture coordinates — they're meant to be a single lit colour. The lit
shader only knew how to sample a texture, so we add an opt-in albedo path. In
`assets/shaders/lit.frag`:

```glsl
// Flat-albedo path (used by the first-person weapon viewmodel): when useAlbedo
// is set, the surface colour is albedoColor instead of a texture sample, but it
// is still lit. Defaults to off, so world rendering is unchanged.
uniform bool useAlbedo;
uniform vec3 albedoColor;
```

and the one line in `main` that picks the surface colour:

```glsl
vec3 texColor = useAlbedo ? albedoColor : texture(textureSampler, TexCoord).rgb;
```

Everything downstream — the normal, the lighting maths, the point-light loop — is untouched. When
`useAlbedo` is `false` (its default), the shader behaves exactly as before; when it's `true`, the
"texture colour" is simply the flat `albedoColor`, and that solid colour is then lit like any other
surface.

> **Why light a flat colour instead of drawing it unlit?** An unlit solid-colour gun would look like
> a flat sticker pasted over the scene — no form, no depth. Feeding `albedoColor` in where the
> texture sample used to go means the gun still catches the directional light and shades across its
> faces, so the boxes read as a 3D object. It's the cheapest possible way to get a shaded,
> untextured surface: one branch on a uniform, no second shader, no new pipeline. And because it
> defaults off, the entire existing world renders bit-for-bit as before.

### Step 8: The `WeaponViewModel` and Its State

The viewmodel needs the seven meshes, their colours, and a little animation state that persists
between frames. Create the type in `engine/renderer/types/weapon_viewmodel.h`:

```cpp
// First-person weapon viewmodel state: the 7 gun meshes (shared with the weapon
// pickups) + colours, plus the small amount of animation state that persists
// across frames (bob phase, last-selected slot, and the switch-raise timer).
struct WeaponViewModel
{
    std::array<std::shared_ptr<Mesh>, 7> meshes;
    std::array<glm::vec3, 7>             colors;

    float bobPhase    = 0.0f; // accumulates for the idle/walk bob
    int   lastWeapon  = -1;   // to detect a switch
    float switchTimer = 0.0f; // counts down while the raise animation plays
};
```

The public interface lives in `engine/renderer/weapon_viewmodel.h`:

```cpp
// Assemble the viewmodel from the shared "gun_0".."gun_6" meshes already loaded
// into the ResourceManager (same meshes the weapon pickups use), plus colours.
WeaponViewModel createWeaponViewModel(const ResourceManager& resources);

// Draw the player's current weapon as an animated view-space model, using the
// lit shader. Advances animation by frameTime (real seconds). No-op if there is
// no player or the active slot isn't owned.
void renderWeaponViewModel(WeaponViewModel& vm, entt::registry& registry,
                           const Camera& camera, float aspectRatio,
                           unsigned int litShader, float frameTime);
```

`createWeaponViewModel` (`engine/renderer/create_weapon_viewmodel.cpp`) just pulls the shared meshes
by name and grabs each colour:

```cpp
WeaponViewModel createWeaponViewModel(const ResourceManager& resources)
{
    WeaponViewModel vm;
    for (int i = 0; i < 7; ++i)
    {
        vm.meshes[i] = resources.getMesh("gun_" + std::to_string(i));
        vm.colors[i] = weaponColor(static_cast<WeaponType>(i));
    }
    return vm;
}
```

> **Why keep `bobPhase`, `lastWeapon`, and `switchTimer` on the struct rather than recomputing them
> each frame?** They're animation *state* — they only mean anything relative to previous frames.
> `bobPhase` is an accumulating angle for the sway; `lastWeapon` lets us notice the exact frame you
> switch guns (so we can kick off the raise); `switchTimer` counts that raise down. None can be
> derived from the world at a single instant, so the viewmodel owns them. It's the presentation
> layer's private memory, which is why it lives in the renderer and never touches the ECS.

### Step 9: Animate and Draw It in View Space

`renderWeaponViewModel` (`engine/renderer/render_weapon_viewmodel.cpp`) is where the gun gets
placed, animated, and drawn. It opens with a block of tunables — all in *view space*, where `-Z`
points into the screen:

```cpp
namespace
{
    // ─── Tunables (all in view space; -Z is "into the screen") ───────
    constexpr glm::vec3 kBaseOffset{ 0.30f, -0.27f, -0.55f }; // right, down, forward
    constexpr float kScale       = 0.90f;
    constexpr float kYawDeg      = 8.0f;  // angle the gun slightly across screen
    constexpr float kPitchDeg    = 2.0f;
    constexpr float kSwitchTime  = 0.35f; // seconds for the raise animation
    constexpr float kSwitchDrop  = 0.40f; // how far the gun drops during a switch
    constexpr float kRecoilBack  = 0.06f; // kick toward the camera on fire
    constexpr float kRecoilPitch = 9.0f;  // muzzle-up kick (degrees) on fire
}
```

The function finds the player's active *owned* weapon, bails if there isn't one, then advances the
animation:

```cpp
int slot = inv.currentWeapon;
if (slot < 0 || slot >= 7 || !inv.owned[slot] || !vm.meshes[slot])
    return;

// ─── Advance animation state ─────────────────────────────
bool moving = glm::length(input.wishDir) > 0.1f;
vm.bobPhase += frameTime * (moving ? 9.0f : 2.5f);
float bobAmp = moving ? 0.010f : 0.0035f;
float bobX = std::sin(vm.bobPhase) * bobAmp;
float bobY = -std::fabs(std::sin(vm.bobPhase)) * bobAmp;

if (slot != vm.lastWeapon)
{
    vm.switchTimer = kSwitchTime;
    vm.lastWeapon = slot;
}
vm.switchTimer = std::max(0.0f, vm.switchTimer - frameTime);
float switchDrop = (vm.switchTimer / kSwitchTime) * kSwitchDrop;

const Weapon& w = inv.weapons[slot];
float recoil = (w.fireRate > 0.0f)
    ? std::clamp(w.cooldownRemaining / w.fireRate, 0.0f, 1.0f) : 0.0f;
```

Three animations, each derived from data that already exists:

- **Bob** — a sine wave off `bobPhase`, faster and wider when `input.wishDir` says you're moving.
  `bobX` swings side to side; `bobY` uses `-fabs(sin)` so the gun only ever dips *down*, never
  above rest.
- **Switch raise** — when `slot` differs from `lastWeapon`, we reset `switchTimer` to `kSwitchTime`;
  it counts down, and `switchDrop` drops the gun off-screen and lets it rise back as the timer
  lapses.
- **Recoil** — `cooldownRemaining / fireRate` is `1.0` the instant you fire and decays to `0` as the
  weapon cools, so it doubles as a recoil envelope with no new state at all.

Those feed a view-space model matrix:

```cpp
glm::vec3 p = kBaseOffset;
p.x += bobX;
p.y += bobY - switchDrop;
p.z += recoil * kRecoilBack;   // kick toward the camera

glm::mat4 model(1.0f);
model = glm::translate(model, p);
model = glm::rotate(model, glm::radians(kYawDeg), glm::vec3(0, 1, 0));
model = glm::rotate(model, glm::radians(kPitchDeg + recoil * kRecoilPitch),
                    glm::vec3(1, 0, 0));
model = glm::scale(model, glm::vec3(kScale));
```

Then the uniforms. The crucial trick is that **the view matrix is identity** — the model matrix is
already expressed in camera space, so the gun rides with the camera automatically:

```cpp
glUseProgram(litShader);
setMat4(litShader, "model", model);
setMat4(litShader, "view", glm::mat4(1.0f));
setMat4(litShader, "projection", camera.getProjectionMatrix(aspectRatio));
setVec3(litShader, "viewPos", glm::vec3(0.0f));
glUniform1f(glGetUniformLocation(litShader, "shininess"), 20.0f);

// Fixed viewmodel lighting so the gun looks the same regardless of the
// room. (renderSystem re-sets all of these next frame.)
glUniform1i(glGetUniformLocation(litShader, "hasDirLight"), 1);
setVec3(litShader, "dirLightDir", glm::normalize(glm::vec3(-0.5f, -0.7f, -0.6f)));
setVec3(litShader, "dirLightColor", glm::vec3(1.0f));
glUniform1f(glGetUniformLocation(litShader, "dirLightAmbient"), 0.45f);
glUniform1i(glGetUniformLocation(litShader, "numPointLights"), 0);
glUniform4f(glGetUniformLocation(litShader, "colorOverride"), 0, 0, 0, 0);

// Flat-albedo path: shaded per-weapon colour, no texture.
glUniform1i(glGetUniformLocation(litShader, "useAlbedo"), 1);
setVec3(litShader, "albedoColor", vm.colors[slot]);
```

Finally the draw — clearing depth first so the gun is never poked through by nearby geometry, but
keeping depth *testing* so the gun's own boxes occlude each other correctly — and a reset of
`useAlbedo` so the world pass next frame isn't tinted:

```cpp
// Draw over the world so the gun is never clipped by geometry, but keep
// depth testing so it self-occludes correctly.
glClear(GL_DEPTH_BUFFER_BIT);
glBindVertexArray(vm.meshes[slot]->getVAO());
glDrawElements(GL_TRIANGLES, vm.meshes[slot]->getIndexCount(), GL_UNSIGNED_INT, 0);

// Reset so the next frame's world pass isn't tinted (renderSystem never
// touches useAlbedo).
glUniform1i(glGetUniformLocation(litShader, "useAlbedo"), 0);
return;
```

> **Why identity view + a fresh depth clear, instead of drawing the gun as a world entity that
> follows the camera?** A viewmodel isn't *in* the world — it's painted on the lens. Setting the view
> matrix to identity means the model matrix is already camera-relative, so `kBaseOffset` is "down
> and to the right of where you're looking" no matter where you stand or face. Clearing the depth
> buffer just before the draw guarantees the gun sits on top of the scene — you never clip into a
> wall and see the room through your own weapon — while keeping depth *testing* on so the gun's
> barrel still correctly hides the grip behind it. It's the standard FPS viewmodel pass, done in one
> extra draw with no separate framebuffer.

> **Why derive recoil from `cooldownRemaining / fireRate`?** The combat system already tracks the
> cooldown for gameplay reasons — it's `fireRate` the instant you shoot and ticks to `0`. Normalised,
> that's a ready-made `1 → 0` envelope, which is exactly the shape of a recoil kick. Reusing it means
> the recoil is automatically tuned per weapon (a slow rocket launcher kicks longer than a fast
> nailgun, for free) and the viewmodel needs no fire event, no timer, and no coupling to the combat
> system beyond reading a float it already maintains.

### Step 10: Wire the Viewmodel into `main.cpp`

The viewmodel is a windowed-only render pass, so it's created and drawn in `main.cpp`, never in the
shared simulation. After the world is built (so a GL context exists and the shader is loaded):

```cpp
// First-person weapon viewmodel (needs a live GL context — build after load).
WeaponViewModel weaponViewModel = createWeaponViewModel(resources);
unsigned int litShaderId = resources.getShader("lit")->getId();
```

and in the frame body, straight after the world render, before the HUD:

```cpp
renderSystem(registry, camera, aspectRatio, alpha); // draw everything
renderWeaponViewModel(weaponViewModel, registry, camera, aspectRatio,
	litShaderId, frameTime);                        // first-person gun
```

with the include `#include "engine/renderer/weapon_viewmodel.h"` near the top.

> **Why draw the gun *between* the world and the HUD?** Order is deliberate. The world renders first
> and fills the depth buffer; the viewmodel then clears depth and paints the gun on top of the scene
> but *under* the 2D HUD, which is drawn last with depth off. So the crosshair, ammo counter, and
> weapon bar always sit over the gun — which is what you want, since they're the interface layer.
> Passing `frameTime` (real seconds, not the fixed physics `dt`) drives the animation at display
> rate, so the bob and recoil stay smooth however many physics ticks a frame contains.

---

## Part 3 — Weapon Pickups as Gun Models

We already have seven gun meshes and a way to draw a flat-lit colour. A weapon pickup on the floor
should just *be* its gun model, so you recognise from across the room what you'd be grabbing.

### Step 11: Carry the Gun VAOs in `MeshAssets`

The factories draw from a small bundle of shared render handles, `MeshAssets`. Extend it with the
seven guns' VAOs and index counts. In `engine/ecs/types/mesh_assets.h`:

```cpp
struct MeshAssets
{
    unsigned int cubeVAO = 0;
    unsigned int cubeIndexCount = 0;
    unsigned int litShader = 0;
    // Per-weapon gun meshes (indexed by WeaponType) for weapon pickups.
    std::array<unsigned int, 7> gunVAO{};
    std::array<unsigned int, 7> gunIndexCount{};
};
```

`scene_setup.cpp` fills them from the same `"gun_i"` resources loaded in Step 6, right after it sets
the cube handles:

```cpp
for (int i = 0; i < 7; ++i)
{
    auto gun = resources.getMesh("gun_" + std::to_string(i));
    ctx.assets.gunVAO[i] = gun->getVAO();
    ctx.assets.gunIndexCount[i] = gun->getIndexCount();
}
```

> **Why hand the factories raw VAOs/counts rather than the `Mesh` objects?** `MeshAssets` is a
> plain-data bundle of GL handles — it's what the ECS render path consumes. A `MeshRenderer`
> component stores a VAO and an index count, not a `shared_ptr<Mesh>`, so the factory needs exactly
> those integers. Pulling them out of the shared meshes here keeps the ownership in one place (the
> `ResourceManager`) while giving the pickups a cheap, copyable handle.

### Step 12: `spawnWeaponPickup` Draws the Gun, Coloured

A new factory renders a pickup as its gun mesh plus a `Colour` component for the flat albedo. In
`engine/level/factories.cpp`:

```cpp
entt::entity spawnWeaponPickup(entt::registry& reg, const MeshAssets& a, glm::vec3 pos,
                               const Pickup& pickup, WeaponType weapon)
{
    const int i = static_cast<int>(weapon);
    auto e = reg.create();
    reg.emplace<Position>(e, pos);
    reg.emplace<Rotation>(e, glm::vec3(0.0f, 40.0f, 0.0f));   // angled so the profile reads
    reg.emplace<Scale>(e, glm::vec3(1.6f));                   // guns are small — scale up
    reg.emplace<AABBCollider>(e, glm::vec3(0.6f), true);      // sensor: ECS overlap, no Jolt body
    reg.emplace<MeshRenderer>(e,
        MeshRenderer{ a.gunVAO[i], 0u, a.litShader, 0u, true, a.gunIndexCount[i] });
    reg.emplace<Colour>(e, glm::vec4(weaponColor(weapon), 1.0f)); // flat albedo via renderSystem
    reg.emplace<Pickup>(e, pickup);
    return e;
}
```

It mirrors the plain `spawnPickup` (a sensor AABB, a `Pickup`, so `pickupSystem` grants and destroys
it the same way) but swaps the textured cube for the weapon's gun mesh, rotates it 40° so its
silhouette reads, scales it up 1.6× (the guns are hand-sized), and attaches a `Colour` set from
`weaponColor`. The `Colour` component is the tiny RGBA struct in
`engine/ecs/components/rendering.h`:

```cpp
struct Colour {
	glm::vec4 value = glm::vec4(1.0f); // RGBA
};
```

For the mesh to actually render coloured, `renderSystem` has to honour `Colour` through the albedo
path. In `engine/ecs/systems/render/render_system.cpp`, per entity:

```cpp
// Flat lit albedo for Colour'd entities (e.g. weapon-pickup gun meshes); set per-entity so it can't leak.
bool hasColour = registry.all_of<Colour>(entity);
glUniform1i(glGetUniformLocation(mesh.shaderId, "useAlbedo"), hasColour ? 1 : 0);
if (hasColour)
	glUniform3fv(glGetUniformLocation(mesh.shaderId, "albedoColor"), 1,
		&registry.get<Colour>(entity).value[0]);
```

> **Why set `useAlbedo` on *every* entity (0 when it has no `Colour`) rather than only turning it on
> for the ones that do?** Uniforms are sticky — a program keeps the last value you set until you
> change it. If we only wrote `useAlbedo = 1` for the coloured pickups, the very next textured cube
> would still have it on and render as a flat block of the previous pickup's colour. Writing it
> explicitly for *each* entity — 1 with a colour, 0 without — means the flag can never leak from one
> draw into the next. It's the same discipline the viewmodel uses when it resets `useAlbedo` to 0
> after its pass.

### Step 13: Route Weapon Classnames Through `spawnWeaponPickup`

The `.map`/classname dispatch (Chapter 18) maps `weapon_shotgun`, `weapon_nailgun`, and friends to
factory functions. Those now call a `makeWeaponPickup` helper that builds a weapon `Pickup` and
spawns the gun model. In `engine/level/classname_factory_items.cpp`:

```cpp
// Weapon pickups render as the actual gun mesh (spawnWeaponPickup) rather
// than a textured cube.
entt::entity makeWeaponPickup(entt::registry& reg, const SpawnContext& ctx,
                              const SpawnParams& p, WeaponType weapon, int defaultAmount)
{
    Pickup pickup;
    pickup.type = PickupType::Weapon;
    pickup.amount = p.getInt("amount", defaultAmount);
    pickup.weaponType = weapon;
    return spawnWeaponPickup(reg, ctx.assets, p.origin, pickup, weapon);
}
```

Each `make_weapon_*` classname function now delegates to it instead of the generic cube `makePickup`:

```cpp
entt::entity make_weapon_shotgun (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
{ return makeWeaponPickup(r, c, p, WeaponType::Shotgun, 10); }
entt::entity make_weapon_nailgun (entt::registry& r, const SpawnContext& c, const SpawnParams& p)
{ return makeWeaponPickup(r, c, p, WeaponType::Nailgun, 25); }
// … and so on for supershotgun, rocketlauncher, grenadelauncher, lightninggun, railgun …
```

The `defaultAmount` is the ammo the weapon comes with (10 shells for the shotgun, 25 nails for the
nailgun, 5 rockets for the launcher), overridable per-placement with an `amount` map key. The plain
`item_*` pickups still route through `makePickup` and stay cubes — only weapons became models.

> **Why a separate `makeWeaponPickup` helper instead of teaching the generic `makePickup` to draw a
> gun?** They now build genuinely different entities — a textured cube versus a coloured gun mesh
> with a rotation and a different scale. Overloading one function on "is this a weapon?" would fill
> it with branches. A second small helper keeps each path linear and named for what it makes, and the
> classname table reads as a flat list of one-liners — exactly the declarative shape the dispatch
> layer wants.

---

## Part 4 — The HUD Weapon Bar

### Step 14: Draw Slots 1-7 at the Top of the Screen

The last piece is a glanceable readout: seven cells across the top, one per weapon, coloured by
ownership. Create `engine/ecs/systems/debug_hud/draw_weapon_bar.cpp`:

```cpp
// Top-of-screen weapon bar. One cell per weapon slot (1-7): the active weapon
// gets a bright coloured panel, owned weapons show their colour on a dark panel,
// and un-collected weapons are dimmed grey — so you can see what's available and
// which number selects it.
void drawWeaponBar(entt::registry& registry, int windowWidth, unsigned int shaderId,
                   const glm::mat4& projection, float scale)
{
    const WeaponInventory* inv = nullptr;
    for (auto [e, wi] : registry.view<WeaponInventory, TagPlayer>().each())
    {
        inv = &wi;
        break;
    }
    if (!inv) return;

    const int   N     = 7;
    const float cellW = 74.0f;
    const float cellH = 24.0f;
    const float gap   = 6.0f;
    const float total = N * cellW + (N - 1) * gap;
    const float x0    = (windowWidth - total) * 0.5f;
    const float y     = 8.0f;

    for (int i = 0; i < N; ++i)
    {
        float x = x0 + i * (cellW + gap);
        WeaponType t = static_cast<WeaponType>(i);
        bool owned   = inv->owned[i];
        bool current = owned && inv->currentWeapon == i;
        glm::vec3 wc = weaponColor(t);

        glm::vec3 panelColor;
        float     panelAlpha;
        glm::vec3 textColor;
        if (current)                       // active weapon: bright coloured cell
        {
            panelColor = wc;
            panelAlpha = 0.90f;
            textColor  = glm::vec3(0.05f);
        }
        else if (owned)                    // owned: colour text on a dark cell
        {
            panelColor = glm::vec3(0.08f);
            panelAlpha = 0.60f;
            textColor  = wc;
        }
        else                               // not collected: greyed out
        {
            panelColor = glm::vec3(0.12f);
            panelAlpha = 0.35f;
            textColor  = glm::vec3(0.40f);
        }

        drawPanel(x, y, cellW, cellH, shaderId, projection, panelColor, panelAlpha);

        char label[16];
        std::snprintf(label, sizeof(label), "%d %s", i + 1, weaponAbbrev(t));
        drawText(x + 6.0f, y + 5.0f, label, shaderId, projection, scale, textColor);
    }
}
```

The bar is centred horizontally (`x0` from the total width) and reuses the existing HUD primitives
`drawPanel` and `drawText` (Chapter 15/17c). Each cell is labelled `"<n> <abbrev>"` — e.g. `"1 SG"`,
`"4 RL"` — using the number key and the `weaponAbbrev` from Step 5. The three-way colouring encodes
ownership: bright weapon-colour for the one in your hands, the weapon colour as *text* on a dark
panel for other owned weapons, and dim grey for weapons you haven't collected yet.

Declare it in `engine/ecs/systems/debug_hud/debug_hud_internal.h`:

```cpp
// Draw the top-of-screen weapon bar: slots 1-7, coloured when owned (current
// highlighted), greyed out when not yet collected.
void drawWeaponBar(entt::registry& registry, int windowWidth, unsigned int shaderId,
                   const glm::mat4& projection, float scale);
```

and call it from `debug_hud_system.cpp`, before the crosshair:

```cpp
// Weapon bar (top centre): which weapons are owned + which number selects them.
drawWeaponBar(registry, windowWidth, shader, ortho, textScale);
```

> **Why show *un-collected* weapons greyed out instead of hiding them?** A blank slot tells you
> nothing; a greyed one tells you the weapon exists, which number will select it once you find it,
> and — by its absence of colour — that you don't have it yet. It turns the bar into a checklist of
> the whole arsenal, which is only possible *because* the inventory is a fixed seven slots (Part 1).
> With the old growing vector there was no "slot 5 that you don't own yet" to draw. The stable
> indexing is what makes a legible, complete weapon bar even expressible.

---

## Step 15: CMake and the Headless Harness

Three new `.cpp` files need adding to the `qengine_lib` target in `CMakeLists.txt`:

```cmake
	src/engine/ecs/systems/debug_hud/draw_weapon_bar.cpp
	…
	src/engine/renderer/gun_mesh.cpp
	src/engine/renderer/create_weapon_viewmodel.cpp
	src/engine/renderer/render_weapon_viewmodel.cpp
```

The header-only pieces (`weapon_viewmodel.h`, `types/weapon_viewmodel.h`, the changed
`weapon_switch_system.h`, `combat.h`) compile into their includers and need no CMake entry.

Because the inventory changed shape, two regression scenarios in `src/harness/headless_main.cpp`
had to move off the old vector API — and this is where we prove the new behaviour headlessly.

**`scenario_rocket_vs_floor`** used to switch to "slot 1" positionally. Now it selects the rocket
launcher by its stable `WeaponType` slot:

```cpp
// One tick: switch to the rocket launcher (its WeaponType slot) and fire down.
Input fire;
fire.weaponSwitch = static_cast<int>(WeaponType::RocketLauncher);
fire.fire = true;
fire.lookDir = glm::vec3(0.0f, -1.0f, 0.0f);
```

**`scenario_weapon_pickup`** used to count `weapons.size()`; a fixed array is always seven long, so
"how many weapons do I have?" is now "how many are owned?". A small lambda counts the `owned` flags,
and the assertions check exactly one new weapon is owned and there's no auto-switch:

```cpp
auto ownedCount = [](const WeaponInventory& v)
{ int n = 0; for (bool b : v.owned) if (b) n++; return n; };

int shellsBefore  = reg.get<Ammo>(player).shells;                 // 25
int nailsBefore   = reg.get<Ammo>(player).nails;                  // 0
int ownedBefore   = ownedCount(reg.get<WeaponInventory>(player)); // 2 (shotgun + RL)
int currentBefore = reg.get<WeaponInventory>(player).currentWeapon;       // 0
```

and after walking over the nailgun pickup:

```cpp
bool hasNailgun = inv.owned[static_cast<int>(WeaponType::Nailgun)];

bool pass = ammo.shells == shellsBefore          // shotgun ammo untouched
         && ammo.nails > nailsBefore             // nails granted
         && ownedCount(inv) == ownedBefore + 1   // exactly one new weapon owned
         && hasNailgun
         && inv.currentWeapon == currentBefore;  // no auto-switch
```

> **Why does the harness count `owned` flags now instead of `weapons.size()`?** With a fixed
> `std::array<Weapon,7>`, `weapons.size()` is *always* 7 — it no longer measures how many weapons you
> have. Ownership moved into the `owned[]` array, so the meaningful count is "how many flags are
> set". The test's intent is unchanged — pick up a weapon, own exactly one more, don't disturb the
> other ammo pools, don't auto-switch — but its *measurement* had to follow the data's new shape.
> This is the headless harness doing its job: the same six scenarios still run with no window and no
> audio device, and now they also pin the new inventory semantics.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/ecs/components/combat.h` | `WeaponInventory` is now `std::array<Weapon,7>` + `std::array<bool,7> owned`; `<array>` include. |
| `level/factories.cpp` | `spawnPlayer` stats all 7 slots + owns shotgun/RL; **new** `spawnWeaponPickup` (gun mesh + `Colour`). |
| `level/factories.h` | Declare `spawnWeaponPickup`. |
| `combat/weapon_switch_system.h` | Guard on `owned[slot]` — can't select an unowned weapon. |
| `combat/combat_system.cpp` | Fire gate checks `currentWeapon` range + `owned`, not `weapons.empty()`. |
| `debug_hud/draw_ammo.cpp` | Skip the ammo readout when the current slot isn't owned. |
| `pickup/pickup_system.cpp` | Grant a weapon by `owned[slot] = true` (+ re-stat the slot), no vector push. |
| `renderer/gun_mesh.{h,cpp}` | **New** — `buildGunMesh` (box silhouettes), `weaponColor`, `weaponAbbrev`. |
| `assets/shaders/lit.frag` | **New** `useAlbedo` / `albedoColor` uniforms — flat, still-lit surface colour. |
| `app/simulation.cpp` | `loadGunMeshes` stores `gun_0`..`gun_6` (real / GL-free stubs) in both builds. |
| `renderer/types/weapon_viewmodel.h` | **New** — `WeaponViewModel` state struct. |
| `renderer/weapon_viewmodel.h` | **New** — `createWeaponViewModel` / `renderWeaponViewModel` interface. |
| `renderer/create_weapon_viewmodel.cpp` | **New** — assemble the viewmodel from shared meshes + colours. |
| `renderer/render_weapon_viewmodel.cpp` | **New** — animated view-space gun draw (bob/recoil/switch). |
| `main.cpp` | Create the viewmodel after load; draw it between world and HUD each frame. |
| `ecs/types/mesh_assets.h` | Carry per-weapon `gunVAO` / `gunIndexCount`. |
| `app/scene_setup.cpp` | Fill the gun VAOs from the `gun_i` resources. |
| `render/render_system.cpp` | Honour `Colour` via the per-entity `useAlbedo` path. |
| `level/classname_factory_items.cpp` | `makeWeaponPickup` routes `weapon_*` classnames to gun-model pickups. |
| `debug_hud/draw_weapon_bar.cpp` | **New** — the top-of-screen weapon bar. |
| `debug_hud/debug_hud_internal.h` | Declare `drawWeaponBar`. |
| `debug_hud/debug_hud_system.cpp` | Call `drawWeaponBar` before the crosshair. |
| `CMakeLists.txt` | Add `gun_mesh.cpp`, the two viewmodel `.cpp`s, and `draw_weapon_bar.cpp`. |
| `harness/headless_main.cpp` | Rocket scenario selects by `WeaponType`; pickup scenario asserts on `owned` count. |

---

## What You Should See

Run `build/QEngine.exe`:

1. **A gun in your hands.** The shotgun (red) sits bottom-right, bobbing gently at idle and more
   as you walk, kicking up-and-back each time it fires.
2. **Number keys 1-7 are stable.** "1" is always the shotgun, "4" always the rocket launcher —
   whether or not you own the gun in between. Pressing a number for a weapon you don't own does
   nothing (and is silent).
3. **A weapon bar across the top.** Your current weapon's cell glows in its colour; other owned
   weapons show their colour as text on a dark cell; weapons you haven't found are greyed out with
   their number and abbreviation, so you know what's still out there.
4. **Weapon pickups look like their guns.** On the floor, a rocket-launcher pickup is an orange
   rocket tube, a railgun a long purple barrel — angled and scaled up so you recognise them across
   the room. Walk over one and its bar cell lights up (you don't auto-switch to it).
5. **The headless harness still passes all six scenarios** — silently, with no window — including the
   updated `weapon_pickup` (exactly one new weapon owned, no auto-switch) and `rocket_vs_floor`
   (rocket selected by its `WeaponType` slot).

---

## What's Next

The arsenal is now legible from every angle — in the hand, on the HUD, and on the floor — but every
gun still fires with the same basic hitscan-or-projectile logic from Chapter 12, and the *world*
they're fired in is still hand-assembled in the showcase level. The next step is to retire the
hard-coded level: parse a TrenchBroom-authored `.map` file into brush geometry and colliders, and
feed its `weapon_*`, `item_*`, trigger, and mover entities straight through the classname dispatch
you extended here — so the arsenal you just built becomes something a designer places by hand in an
editor.
