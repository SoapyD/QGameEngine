#pragma once

#include <entt/entt.hpp>

// Recompute the player-facing HUD signals each fixed tick: crosshair spread
// (base + weapon spread + movement + recoil), the low-ammo flag, and the decay
// of the transient hit/kill/damage-direction timers. Runs in stepSimulation so
// the HUD *state* is exercised headless; the GL HUD (debugHudSystem) only draws
// what this leaves in the HudSignals context. Hit/kill/damage-dir timers are
// *set* at their event sites (combat / aiSystem); this only decays them.
void hudSignalSystem(entt::registry& registry);
