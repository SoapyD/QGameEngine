#pragma once

#include <entt/entt.hpp>

class AudioEngine;

// Drain the SoundQueue and play each queued sound, then clear it. Called once per
// rendered frame in the windowed build; the headless harness never calls it.
void audioSystem(entt::registry& registry, AudioEngine& audio);
