#pragma once

#include <entt/entt.hpp>
#include "engine/level/level.h"

void createLevelBodies(entt::registry& registry, const Level& level);
void createDynamicBody(entt::registry& registry, entt::entity entity);
void createKinematicBody(entt::registry& registry, entt::entity entity);
void createStaticBody(entt::registry& registry, entt::entity entity);
void createSensorBody(entt::registry& registry, entt::entity entity);