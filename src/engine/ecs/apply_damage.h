#pragma once

#include <entt/entt.hpp>

// Deal `amount` damage to `target`. No-op (returns false) if the target has no
// Health or is invulnerable. Armour (if present) absorbs first; the remainder
// comes off Health, clamped at 0. Triggers the target's DamageFlash. Returns
// true if the hit landed — callers gate knockback on this.
bool applyDamage(entt::registry& registry, entt::entity target, float amount);
