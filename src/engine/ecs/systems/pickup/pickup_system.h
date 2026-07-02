#pragma once

#include <entt/entt.hpp>

// Grants item pickups to overlapping TagTriggerable entities, then destroys the
// pickup. Mirrors triggerSystem's AABB overlap; runs after triggerSystem.
void pickupSystem(entt::registry& registry);
