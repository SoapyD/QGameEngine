#pragma once
// Jolt body creation helpers. Each function is defined in its own file under
// physics/bodies/. (Relocated from ecs/jolt_body_helpers.h — these are
// physics-domain glue, ECS-aware but not ECS-owned. CODING_STANDARD §5.)

#include <entt/entt.hpp>
#include "engine/level/level.h"

void createLevelBodies(entt::registry& registry, const Level& level);
void createDynamicBody(entt::registry& registry, entt::entity entity);
void createKinematicBody(entt::registry& registry, entt::entity entity);
void createStaticBody(entt::registry& registry, entt::entity entity);
void createSensorBody(entt::registry& registry, entt::entity entity);
