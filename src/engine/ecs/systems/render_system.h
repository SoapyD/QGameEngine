#pragma once

#include <entt/entt.hpp>
#include "engine/renderer/camera.h"
#include "engine/renderer/shader.h"

// alpha is the fixed-timestep interpolation factor [0,1]; rendered positions
// are lerped between PrevPosition and Position by it for smooth motion.
void renderSystem(entt::registry& registry, const Camera& camera, float aspectRatio, float alpha = 1.0f);