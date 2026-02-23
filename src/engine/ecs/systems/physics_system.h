#pragma once

#include <entt/entt.hpp>

struct Level;  // forward declaration (defined in engine/level/level.h)

void physicsSystem(entt::registry& registry, float dt);
void groundDetectionSystem(entt::registry& registry, const Level& level);