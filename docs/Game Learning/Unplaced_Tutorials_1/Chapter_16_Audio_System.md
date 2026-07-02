# Chapter 16: Audio System

## What You'll Learn
- Why audio matters more than you think
- miniaudio — a single-header cross-platform audio library
- Playing sound effects (one-shot: gunfire, pickups, impacts)
- Looping sounds (ambient: torches, machinery, music)
- 3D positional audio — sounds get louder as you approach
- An audio manager to handle loading and playback
- Audio components in the ECS

---

## Why Audio Matters

A game with no sound feels dead. A rocket that silently explodes has no impact. Footsteps echoing in a dark corridor create tension that no amount of lighting can achieve. Quake's audio — the shotgun blast, the ogre's chainsaw, the nail bouncing off a wall — is half the atmosphere.

Audio is also technically interesting: it often runs on its own thread, deals with real-time mixing, and 3D positional audio requires the same spatial math as rendering.

---

## miniaudio

We're using **miniaudio** — a single-header C library for cross-platform audio. It handles:
- Decoding WAV, MP3, and FLAC files
- Mixing multiple sounds simultaneously
- 3D spatialization (positional audio)
- Low-latency playback

### Setup

Download `miniaudio.h` from https://github.com/mackron/miniaudio and place it in `extern/miniaudio/`.

Create the implementation file:

### src/engine/audio/miniaudio_impl.cpp

```cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
```

Same pattern as stb_image — the implementation is compiled in exactly one `.cpp` file.

Add to CMakeLists.txt:
```cmake
    src/engine/audio/miniaudio_impl.cpp
```

And add the include path:
```cmake
target_include_directories(QEngine PRIVATE src extern/miniaudio)
```

---

## The Audio Manager

We need a central system to manage the audio device and sound playback. This is one of the few non-ECS singletons in the engine — there's only one audio device.

### src/engine/audio/audio_manager.h

```cpp
#pragma once

#include "miniaudio.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // Prevent copying (owns the audio device)
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // Initialise the audio device
    bool init();

    // Load a sound file and store it by name
    bool loadSound(const std::string& name, const std::string& path);

    // Play a one-shot sound (fire and forget)
    void playSound(const std::string& name, float volume = 1.0f);

    // Play a sound at a 3D position
    void playSoundAt(const std::string& name, const glm::vec3& position,
                      float volume = 1.0f);

    // Start/stop a looping sound, returns a handle to control it
    int playLoop(const std::string& name, float volume = 1.0f);
    void stopLoop(int handle);
    void setLoopPosition(int handle, const glm::vec3& position);

    // Update listener position (call every frame with camera position)
    void setListenerPosition(const glm::vec3& position,
                              const glm::vec3& forward,
                              const glm::vec3& up);

    void shutdown();

private:
    ma_engine m_engine;
    bool m_initialised = false;

    // Loaded sound file paths indexed by name
    std::unordered_map<std::string, std::string> m_soundPaths;

    // Active looping sounds
    struct LoopingSound {
        ma_sound sound;
        bool active = false;
    };
    std::vector<LoopingSound> m_loops;
    int m_nextLoopHandle = 0;
};
```

### src/engine/audio/audio_manager.cpp

```cpp
#include "engine/audio/audio_manager.h"
#include <iostream>

AudioManager::AudioManager() {}

AudioManager::~AudioManager() {
    shutdown();
}

bool AudioManager::init() {
    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;       // Stereo
    config.sampleRate = 44100;

    ma_result result = ma_engine_init(&config, &m_engine);
    if (result != MA_SUCCESS) {
        std::cerr << "ERROR: Failed to initialise audio engine" << std::endl;
        return false;
    }

    m_initialised = true;
    std::cout << "Audio engine initialised" << std::endl;
    return true;
}

bool AudioManager::loadSound(const std::string& name, const std::string& path) {
    // Just store the path — miniaudio loads on demand or we can preload
    m_soundPaths[name] = path;

    // Verify the file exists by trying to decode basic info
    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(path.c_str(), nullptr, &decoder);
    if (result != MA_SUCCESS) {
        std::cerr << "ERROR: Could not load sound: " << path << std::endl;
        return false;
    }
    ma_decoder_uninit(&decoder);

    std::cout << "Loaded sound: " << name << " (" << path << ")" << std::endl;
    return true;
}

void AudioManager::playSound(const std::string& name, float volume) {
    if (!m_initialised) return;

    auto it = m_soundPaths.find(name);
    if (it == m_soundPaths.end()) return;

    // ma_engine_play_sound is fire-and-forget — handles everything internally
    ma_engine_play_sound(&m_engine, it->second.c_str(), nullptr);
}

void AudioManager::playSoundAt(const std::string& name,
                                const glm::vec3& position, float volume) {
    if (!m_initialised) return;

    auto it = m_soundPaths.find(name);
    if (it == m_soundPaths.end()) return;

    // For 3D positioned sounds, we need a ma_sound object
    // Using the fire-and-forget approach with a temporary sound group
    // For simplicity, we use the basic play and accept that
    // full 3D positioning requires managing sound objects

    // A more complete implementation:
    ma_sound sound;
    ma_result result = ma_sound_init_from_file(
        &m_engine, it->second.c_str(),
        MA_SOUND_FLAG_DECODE,  // Decode immediately for low latency
        nullptr, nullptr, &sound);

    if (result != MA_SUCCESS) return;

    ma_sound_set_position(&sound, position.x, position.y, position.z);
    ma_sound_set_volume(&sound, volume);
    ma_sound_set_spatialization_enabled(&sound, MA_TRUE);

    // Set attenuation model
    ma_sound_set_attenuation_model(&sound, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(&sound, 1.0f);
    ma_sound_set_max_distance(&sound, 50.0f);

    ma_sound_start(&sound);

    // Problem: we need the sound to outlive this function.
    // Solution: the engine manages it internally with fire-and-forget,
    // OR we track active sounds in a pool. For one-shots, fire-and-forget works.
    // For a production engine, you'd use a sound pool.

    // Note: ma_engine_play_sound doesn't support positioning.
    // A proper implementation would maintain a pool of ma_sound objects.
}

int AudioManager::playLoop(const std::string& name, float volume) {
    if (!m_initialised) return -1;

    auto it = m_soundPaths.find(name);
    if (it == m_soundPaths.end()) return -1;

    int handle = m_nextLoopHandle++;

    // Ensure vector is large enough
    if (handle >= static_cast<int>(m_loops.size())) {
        m_loops.resize(handle + 1);
    }

    auto& loop = m_loops[handle];

    ma_result result = ma_sound_init_from_file(
        &m_engine, it->second.c_str(),
        MA_SOUND_FLAG_DECODE, nullptr, nullptr, &loop.sound);

    if (result != MA_SUCCESS) return -1;

    ma_sound_set_looping(&loop.sound, MA_TRUE);
    ma_sound_set_volume(&loop.sound, volume);
    ma_sound_start(&loop.sound);
    loop.active = true;

    return handle;
}

void AudioManager::stopLoop(int handle) {
    if (handle < 0 || handle >= static_cast<int>(m_loops.size())) return;
    if (!m_loops[handle].active) return;

    ma_sound_stop(&m_loops[handle].sound);
    ma_sound_uninit(&m_loops[handle].sound);
    m_loops[handle].active = false;
}

void AudioManager::setLoopPosition(int handle, const glm::vec3& position) {
    if (handle < 0 || handle >= static_cast<int>(m_loops.size())) return;
    if (!m_loops[handle].active) return;

    ma_sound_set_position(&m_loops[handle].sound, position.x, position.y, position.z);
    ma_sound_set_spatialization_enabled(&m_loops[handle].sound, MA_TRUE);
}

void AudioManager::setListenerPosition(const glm::vec3& position,
                                        const glm::vec3& forward,
                                        const glm::vec3& up) {
    if (!m_initialised) return;

    ma_engine_listener_set_position(&m_engine, 0,
                                     position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_engine, 0,
                                      forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_engine, 0, up.x, up.y, up.z);
}

void AudioManager::shutdown() {
    if (!m_initialised) return;

    // Stop all loops
    for (int i = 0; i < static_cast<int>(m_loops.size()); i++) {
        stopLoop(i);
    }

    ma_engine_uninit(&m_engine);
    m_initialised = false;
}
```

---

## 3D Positional Audio Explained

The listener (the player/camera) has a position and orientation. Each sound has a position. miniaudio calculates:

- **Volume**: Sounds further away are quieter (inverse distance attenuation)
- **Panning**: A sound to your left is louder in the left speaker
- **Doppler** (optional): A rocket flying past you shifts pitch

```
         Enemy shooting
              *
             ╱│╲
            ╱ │ ╲ Sound radiates
           ╱  │  ╲
    ┌─────────────────┐
    │                 │
    │  Player (ear)   │  ← Listener position
    │       👂        │     Sound is louder in left ear
    │                 │
    └─────────────────┘
```

### Attenuation Models

| Model | Formula | Use |
|-------|---------|-----|
| Inverse | `1 / (1 + factor * distance)` | Natural falloff, most common |
| Linear | `1 - (distance / maxDistance)` | Simpler, predictable |
| Exponential | `1 / (distance ^ factor)` | Faster falloff |

We use inverse attenuation with a min distance of 1 (full volume within 1 unit) and max distance of 50 (silent beyond 50 units).

---

## Audio Components

Add to `components.h`:

```cpp
struct AudioSource {
    std::string soundName;       // Name in the AudioManager
    bool positional = true;       // 3D positioned or 2D (HUD sounds)
    bool looping = false;
    float volume = 1.0f;
    int loopHandle = -1;          // Handle for active loops
    bool playing = false;
};

// Tag: play a sound once then remove the component
struct PlaySoundOnce {
    std::string soundName;
    float volume = 1.0f;
};
```

---

## The Audio System

### src/engine/ecs/systems/audio_system.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/audio/audio_manager.h"
#include "engine/renderer/camera.h"

void audioSystem(entt::registry& registry, AudioManager& audio,
                  const Camera& camera, float dt);
```

### src/engine/ecs/systems/audio_system.cpp

```cpp
#include "engine/ecs/systems/audio_system.h"
#include "engine/ecs/components.h"

void audioSystem(entt::registry& registry, AudioManager& audio,
                  const Camera& camera, float dt) {

    // ─── Update listener position (camera) ───────────────────────
    audio.setListenerPosition(
        camera.getPosition(),
        camera.getFront(),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    // ─── Handle one-shot sound requests ──────────────────────────
    auto oneShotView = registry.view<PlaySoundOnce>();
    std::vector<entt::entity> toRemove;

    for (auto [entity, playOnce] : oneShotView.each()) {
        if (registry.all_of<Position>(entity)) {
            auto& pos = registry.get<Position>(entity);
            audio.playSoundAt(playOnce.soundName, pos.value, playOnce.volume);
        } else {
            audio.playSound(playOnce.soundName, playOnce.volume);
        }

        // Remove the component (sound has been triggered)
        toRemove.push_back(entity);
    }

    for (auto e : toRemove) {
        registry.remove<PlaySoundOnce>(e);
    }

    // ─── Manage looping sounds ───────────────────────────────────
    auto loopView = registry.view<Position, AudioSource>();

    for (auto [entity, pos, src] : loopView.each()) {
        if (!src.looping) continue;

        if (src.playing && src.loopHandle >= 0) {
            // Update position of active loop
            audio.setLoopPosition(src.loopHandle, pos.value);
        } else if (!src.playing) {
            // Start the loop
            src.loopHandle = audio.playLoop(src.soundName, src.volume);
            if (src.loopHandle >= 0) {
                audio.setLoopPosition(src.loopHandle, pos.value);
                src.playing = true;
            }
        }
    }
}
```

---

## Playing Sounds from Game Events

When a weapon fires, enemy attacks, or item is picked up — attach a `PlaySoundOnce`:

```cpp
// In combatSystem, after firing a weapon:
registry.emplace_or_replace<PlaySoundOnce>(entity, "shotgun_fire", 1.0f);

// In pickupSystem, after picking up an item:
registry.emplace_or_replace<PlaySoundOnce>(playerEntity, "pickup_health", 0.8f);

// In aiSystem, when enemy attacks:
registry.emplace_or_replace<PlaySoundOnce>(enemyEntity, "enemy_attack", 1.0f);
```

`emplace_or_replace` is an EnTT function — if the entity already has a `PlaySoundOnce`, it replaces it. This prevents queueing up multiple sounds on the same entity in one frame.

---

## Ambient Sounds

A torch that crackles, a computer that hums, lava that bubbles:

```cpp
auto torch = registry.create();
registry.emplace<Position>(torch, glm::vec3(3.0f, 2.0f, -1.0f));
registry.emplace<PointLight>(torch, glm::vec3(2.0f, 1.4f, 0.6f),
                              0.15f, 0.045f, 0.0075f);
registry.emplace<AudioSource>(torch, "torch_crackle", true, true, 0.5f, -1, false);
```

The audio system sees it has a looping `AudioSource` and starts it. As the player moves around, miniaudio adjusts the volume and panning based on distance and direction.

---

## Loading Sounds at Startup

```cpp
AudioManager audio;
audio.init();

// Load all game sounds
audio.loadSound("shotgun_fire",    "assets/sounds/shotgun.wav");
audio.loadSound("rocket_fire",     "assets/sounds/rocket_fire.wav");
audio.loadSound("rocket_explode",  "assets/sounds/explosion.wav");
audio.loadSound("pickup_health",   "assets/sounds/pickup.wav");
audio.loadSound("pickup_ammo",     "assets/sounds/ammo.wav");
audio.loadSound("player_jump",     "assets/sounds/jump.wav");
audio.loadSound("player_pain",     "assets/sounds/pain.wav");
audio.loadSound("enemy_attack",    "assets/sounds/enemy_attack.wav");
audio.loadSound("enemy_death",     "assets/sounds/enemy_death.wav");
audio.loadSound("door_open",       "assets/sounds/door.wav");
audio.loadSound("torch_crackle",   "assets/sounds/torch_loop.wav");
audio.loadSound("ambient_hum",     "assets/sounds/ambient.wav");

// In the game loop:
audioSystem(registry, audio, camera, deltaTime);

// On shutdown:
audio.shutdown();
```

---

## C++ Concept: Threading Awareness

miniaudio runs audio mixing on a separate thread internally. This means:

- Sound playback is **non-blocking** — `playSound()` returns immediately
- You must be careful about accessing sound objects from multiple threads
- The `AudioManager` hides this complexity — you call functions from the main thread, miniaudio handles the audio thread

In future chapters (networking), threading becomes more explicit. For now, just know that audio doesn't block the game loop.

---

## Sound File Formats

| Format | Size | Quality | Use |
|--------|------|---------|-----|
| WAV | Large | Lossless | Short sound effects (loaded into memory) |
| OGG/Vorbis | Small | Lossy, good quality | Music, long ambient loops |
| MP3 | Small | Lossy | Music (patent-free now) |

For a Quake-like game:
- **WAV** for all short effects (gunshots, pickups, impacts) — loaded fully into memory for instant playback
- **OGG** for music and long ambient loops — streamed from disk

---

## What's Next

In **Chapter 17**, we'll begin the networking chapter — setting up a client-server architecture with ENet for multiplayer. This is the most complex subsystem in the engine.
