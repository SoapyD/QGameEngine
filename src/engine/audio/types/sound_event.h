#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// A request to play a sound, by manifest id. Simulation systems push these into
// the SoundQueue; the audio system drains and plays them (windowed build only).
struct SoundEvent
{
	std::string id;                 // manifest id, e.g. "weapon.shotgun"
	glm::vec3   pos{0.0f};          // world position (if positional)
	bool        positional = false; // 3D at pos, or 2D (UI/player)
};

// Registry context resource: a one-frame queue of sound requests.
struct SoundQueue
{
	std::vector<SoundEvent> events;
};
