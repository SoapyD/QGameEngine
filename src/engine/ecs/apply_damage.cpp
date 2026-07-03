#include "engine/ecs/apply_damage.h"
#include "engine/ecs/components.h"
#include "engine/audio/queue_sound.h"

#include <algorithm>

bool applyDamage(entt::registry& registry, entt::entity target, float amount)
{
	if (amount <= 0.0f) return false;
	if (!registry.all_of<Health>(target)) return false;

	auto& health = registry.get<Health>(target);
	if (health.invulnerableTimer > 0.0f) return false;

	float remaining = amount;

	// Armour absorbs first (all of it, until depleted).
	if (Armor* armor = registry.try_get<Armor>(target))
	{
		float absorbed = std::min(armor->current, remaining);
		armor->current -= absorbed;
		remaining -= absorbed;
	}

	// Whatever armour didn't soak comes off health.
	health.current -= remaining;
	if (health.current < 0.0f) health.current = 0.0f;

	// The hit landed (on armour and/or health) — flash the screen. Note whether a
	// flash was already active so the pain voice fires once per flash, not every
	// tick (lava deals damage continuously).
	bool wasFlashing = false;
	if (registry.all_of<DamageFlash>(target))
	{
		auto& flash = registry.get<DamageFlash>(target);
		wasFlashing = flash.timer > 0.0f;
		flash.timer = flash.duration;
	}

	if (!wasFlashing && registry.all_of<TagPlayer>(target))
		queueSound(registry, "player.pain");

	return true;
}
