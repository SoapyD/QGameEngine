#include "engine/audio/audio_engine.h"

#include "miniaudio.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// ─── pimpl ───────────────────────────────────────────────────────────────
struct AudioEngine::Impl
{
	ma_engine engine{};
	std::string root;                                  // e.g. "assets/sounds/"
	std::unordered_map<std::string, std::string> paths; // id -> full file path
	std::unordered_set<std::string> warned;            // ids warned-about once
	ma_sound music{};
	bool musicActive = false;
};

namespace
{
	// Minimal manifest reader: pulls every `"id": "path"` pair whose value ends
	// in a known audio extension out of our generated manifest.json. Avoids a
	// JSON dependency for a file we control the shape of.
	void parseManifest(const std::string& file, const std::string& root,
	                   std::unordered_map<std::string, std::string>& out)
	{
		std::ifstream in(file);
		if (!in)
		{
			std::cerr << "[audio] manifest not found: " << file << "\n";
			return;
		}
		std::string line;
		while (std::getline(in, line))
		{
			// find two double-quoted tokens on the line
			std::size_t a = line.find('"');
			if (a == std::string::npos) continue;
			std::size_t b = line.find('"', a + 1);
			if (b == std::string::npos) continue;
			std::string key = line.substr(a + 1, b - a - 1);

			std::size_t c = line.find('"', b + 1);
			if (c == std::string::npos) continue;
			std::size_t d = line.find('"', c + 1);
			if (d == std::string::npos) continue;
			std::string val = line.substr(c + 1, d - c - 1);

			bool audio = val.size() > 4 &&
				(val.rfind(".wav") == val.size() - 4 ||
				 val.rfind(".ogg") == val.size() - 4 ||
				 val.rfind(".mp3") == val.size() - 4);
			if (audio && key.find('.') != std::string::npos)
				out[key] = root + val;
		}
	}
}

// ─── lifecycle ───────────────────────────────────────────────────────────
AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(const std::string& soundsRoot, const std::string& manifestPath)
{
	m_impl->root = soundsRoot;
	if (!m_impl->root.empty() && m_impl->root.back() != '/' && m_impl->root.back() != '\\')
		m_impl->root += '/';

	if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS)
	{
		std::cerr << "[audio] failed to init device — running silent\n";
		m_valid = false;
		return false;
	}

	parseManifest(manifestPath, m_impl->root, m_impl->paths);
	std::cout << "[audio] " << m_impl->paths.size() << " sounds loaded from manifest\n";
	m_valid = true;
	return true;
}

void AudioEngine::shutdown()
{
	if (!m_valid) return;
	stopMusic();
	ma_engine_uninit(&m_impl->engine);
	m_valid = false;
}

// ─── playback ────────────────────────────────────────────────────────────
void AudioEngine::play(const std::string& id)
{
	if (!m_valid) return;
	auto it = m_impl->paths.find(id);
	if (it == m_impl->paths.end())
	{
		if (m_impl->warned.insert(id).second)
			std::cerr << "[audio] unknown sound id '" << id << "'\n";
		return;
	}
	ma_engine_play_sound(&m_impl->engine, it->second.c_str(), nullptr);
}

void AudioEngine::playAt(const std::string& id, const glm::vec3& /*pos*/)
{
	// Positional playback is a follow-up; for now play non-spatially.
	play(id);
}

void AudioEngine::playMusic(const std::string& id, float volume)
{
	if (!m_valid) return;
	auto it = m_impl->paths.find(id);
	if (it == m_impl->paths.end())
	{
		if (m_impl->warned.insert(id).second)
			std::cerr << "[audio] unknown music id '" << id << "'\n";
		return;
	}
	stopMusic();

	ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
	ma_result r = ma_sound_init_from_file(&m_impl->engine, it->second.c_str(), flags,
	                                      nullptr, nullptr, &m_impl->music);
	if (r != MA_SUCCESS)
	{
		std::cerr << "[audio] could not load music '" << it->second
		          << "' (ma_result " << r << ")\n";
		return;
	}

	ma_sound_set_looping(&m_impl->music, MA_TRUE);
	ma_sound_set_volume(&m_impl->music, volume);
	ma_sound_start(&m_impl->music);
	m_impl->musicActive = true;
	std::cout << "[audio] music playing: " << id << "\n";
}

void AudioEngine::stopMusic()
{
	if (m_valid && m_impl->musicActive)
	{
		ma_sound_uninit(&m_impl->music);
		m_impl->musicActive = false;
	}
}

void AudioEngine::setListener(const glm::vec3& pos, const glm::vec3& forward)
{
	if (!m_valid) return;
	ma_engine_listener_set_position(&m_impl->engine, 0, pos.x, pos.y, pos.z);
	ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
}
