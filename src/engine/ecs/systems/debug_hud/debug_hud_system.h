#pragma once

#include <entt/entt.hpp>

// Public entry point for the debug HUD overlay (FPS, health bar, ammo,
// crosshair, damage flash). Implementation split across systems/debug_hud/*.cpp;
// see debug_hud_internal.h for the drawing primitives.
void debugHudSystem(entt::registry& registry, int windowWidth, int windowHeight, float fps);
