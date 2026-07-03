#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>

// Thin wrapper over miniaudio: loads the sound manifest and plays clips by their
// logical id (e.g. "weapon.shotgun"). Pimpl keeps miniaudio.h out of this header,
// so gameplay code can queue sounds without pulling the whole backend in.
//
// Only the windowed build creates one; the headless harness never does (it just
// queues SoundEvents that are never drained). See ecs/systems/audio/audio_system.
class AudioEngine
{
	public:
		AudioEngine();
		~AudioEngine();

		// Start the device and load `manifestPath` (paths are resolved relative
		// to `soundsRoot`). Returns false if the audio device can't be opened.
		bool init(const std::string& soundsRoot, const std::string& manifestPath);
		void shutdown();
		bool isValid() const { return m_valid; }

		// Fire-and-forget one-shot by manifest id. `playAt` is the positional
		// variant (currently non-spatial; falls back to 2D).
		void play(const std::string& id);
		void playAt(const std::string& id, const glm::vec3& pos);

		// Streamed, looping background track (stops any current track first).
		void playMusic(const std::string& id, float volume = 0.6f);
		void stopMusic();

		// Position/orientation of the listener (the camera) for future 3D audio.
		void setListener(const glm::vec3& pos, const glm::vec3& forward);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
		bool m_valid = false;
};
