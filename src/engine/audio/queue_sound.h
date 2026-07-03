#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

#include "engine/audio/types/sound_event.h"

// Push a sound request onto the registry's SoundQueue, if one exists. Safe to
// call from any simulation system; a no-op when there's no queue (e.g. a test
// build with audio disabled). Capped so an undrained queue can't grow unbounded.

inline void queueSound(entt::registry& reg, const std::string& id)
{
	if (auto* q = reg.ctx().find<SoundQueue>())
		if (q->events.size() < 256)
			q->events.push_back(SoundEvent{ id, glm::vec3(0.0f), false });
}

inline void queueSoundAt(entt::registry& reg, const std::string& id, const glm::vec3& pos)
{
	if (auto* q = reg.ctx().find<SoundQueue>())
		if (q->events.size() < 256)
			q->events.push_back(SoundEvent{ id, pos, true });
}
