#pragma once

#include "engine/renderer/types/weapon_viewmodel.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>

class ResourceManager;

// First-person weapon viewmodel: a small coloured gun drawn in view space over
// the world, with idle bob, fire recoil, and a drop/raise on weapon switch.
// Windowed build only (the headless harness never renders).

// Assemble the viewmodel from the shared "gun_0".."gun_6" meshes already loaded
// into the ResourceManager (same meshes the weapon pickups use), plus colours.
WeaponViewModel createWeaponViewModel(const ResourceManager& resources);

// Draw the player's current weapon as an animated view-space model, using the
// lit shader. Advances animation by frameTime (real seconds). No-op if there is
// no player or the active slot isn't owned.
void renderWeaponViewModel(WeaponViewModel& vm, entt::registry& registry,
                           const Camera& camera, float aspectRatio,
                           unsigned int litShader, float frameTime);
