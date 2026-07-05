#pragma once

#include <entt/entt.hpp>
#include <string>

#include "engine/level/level.h"

class ResourceManager;

// Build the scene from a TrenchBroom `.map` file instead of the hard-coded
// showcase: worldspawn brushes → level geometry (+ per-texture render meshes),
// every other entity → SpawnParams → the same two-pass factories::spawnScene the
// showcase uses. Returns the Level (which owns its render meshes) so buildWorld's
// collision + nav grid work unchanged. Mirrors setupScene(); on a load/parse
// error the returned level is empty and the error is logged.
Level setupSceneFromMap(entt::registry& registry, const ResourceManager& resources,
                        const std::string& mapPath, bool headless);
