#include "engine/ecs/systems/audio/audio_system.h"

#include "engine/audio/audio_engine.h"
#include "engine/audio/types/sound_event.h"

void audioSystem(entt::registry& registry, AudioEngine& audio)
{
	auto* queue = registry.ctx().find<SoundQueue>();
	if (!queue) return;

	for (const SoundEvent& e : queue->events)
	{
		if (e.positional) audio.playAt(e.id, e.pos);
		else              audio.play(e.id);
	}
	queue->events.clear();
}
