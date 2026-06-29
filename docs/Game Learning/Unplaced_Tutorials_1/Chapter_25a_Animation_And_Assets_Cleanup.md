# Chapter 25a: Animation & Assets Cleanup

> **Prerequisites:** Chapter 25 (Weapon Animations & View Models) completed. You should have a working view model system with keyframe animations (fire, lower, raise), procedural effects (view bob, recoil kick, idle sway), and a separate render pass for the weapon.

---

## Time for Another Cleanup

You know the rhythm by now. Chapters 5a, 10a, 15a, and 20a each followed the same pattern: the features work, the code does not scale. Chapter 25 added weapon animations and a view model system that looks and feels great in the game. But the code that drives it has the usual post-feature smell -- global animation objects, hardcoded magic numbers, and ad-hoc resource loading that does not go through the systems we built specifically to handle it.

Let us take inventory. Open your `view_model_system.cpp` and the files you touched in Chapters 21 through 25. You will find something like this:

```cpp
// view_model_system.cpp
#include "engine/ecs/systems/view_model_system.h"
#include "engine/ecs/components.h"

// Animation data (would normally be loaded from file or defined per weapon)
extern WeaponAnimation shotgunFire;
extern WeaponAnimation weaponLower;
extern WeaponAnimation weaponRaise;

void viewModelSystem(entt::registry& registry, float dt) {
    auto view = registry.view<ViewModel, Velocity, OnGround>();

    for (auto [entity, vm, vel, ground] : view.each()) {
        // ...
        switch (vm.state) {
            case ViewModelState::Firing:
                evaluateAnimation(shotgunFire, vm.stateTimer, animPos, animRot);
                // ...
        }
        // ...
    }
}
```

And somewhere in `main.cpp` or a setup function:

```cpp
// Hardcoded animation definitions
WeaponAnimation shotgunFire;
shotgunFire.frames = {
    { glm::vec3(0.0f),                glm::vec3(0.0f),           0.0f   },
    { glm::vec3(0.0f, 0.05f, 0.15f), glm::vec3(-10.0f, 0.0f, 0.0f), 0.05f  },
    { glm::vec3(0.0f, 0.02f, 0.05f), glm::vec3(-3.0f, 0.0f, 0.0f),  0.15f  },
    { glm::vec3(0.0f),                glm::vec3(0.0f),           0.35f  },
};

WeaponAnimation weaponLower;
weaponLower.frames = {
    { glm::vec3(0.0f),               glm::vec3(0.0f),            0.0f  },
    { glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(15.0f, 0.0f, 0.0f), 0.25f },
};

WeaponAnimation weaponRaise;
weaponRaise.frames = {
    { glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(15.0f, 0.0f, 0.0f), 0.0f  },
    { glm::vec3(0.0f),               glm::vec3(0.0f),            0.25f },
};
```

And the procedural effect constants buried inside functions:

```cpp
glm::vec3 calculateBob(float bobTimer, float moveSpeed) {
    float bobFrequency = 8.0f;         // magic number
    float bobAmplitude = 0.005f * moveSpeed;  // magic number
    float x = sin(bobTimer * bobFrequency) * bobAmplitude;
    float y = -abs(cos(bobTimer * bobFrequency)) * bobAmplitude * 0.5f;
    return glm::vec3(x, y, 0.0f);
}

void updateRecoil(float& recoilAmount, float& recoilVelocity, float dt) {
    float stiffness = 200.0f;   // magic number
    float damping = 15.0f;      // magic number
    float force = -stiffness * recoilAmount - damping * recoilVelocity;
    // ...
}

glm::vec3 calculateIdleSway(float swayTimer) {
    float swaySpeed = 1.2f;     // magic number
    float swayAmount = 0.002f;  // magic number
    // ...
}
```

Count the problems:

1. **`WeaponAnimation` objects are globals with `extern` declarations.** The view model system reaches across translation units to find animation data defined somewhere else. Adding a new weapon means adding another global, another `extern`, and another case in the switch statement. We solved this exact "global data scattered across files" problem in every previous cleanup chapter. Animations are data -- they should be loaded, cached, and looked up by name.

2. **Animation keyframes are hardcoded in C++.** Want to tweak the shotgun recoil timing? Recompile. Want an artist to adjust the weapon raise speed? They need a C++ compiler. The keyframe data should live in data files (JSON, in our case) and be loaded at runtime. This is the same data-driven philosophy we applied to `ParticleEmitterDef` in Chapter 20a and `PhysicsConfig` in Chapter 10a.

3. **Procedural effect parameters are hardcoded constants inside functions.** `bobFrequency = 8.0f`, `stiffness = 200.0f`, `swaySpeed = 1.2f` -- all baked into the code. Different weapons should have different bob amounts, different recoil springs, different sway patterns. A shotgun should kick harder than a pistol. A sniper rifle should sway more than an SMG. These parameters need to be per-weapon data, not per-function constants.

4. **`ResourceManager` does not cache meshes or animations.** Back in Chapter 5a, we built `ResourceManager` to cache textures and shaders by name. Since then, we have added meshes, particle emitter definitions, and now animations -- but none of them go through the cache. The view model renderer loads weapon meshes ad-hoc. Two entities with shotguns would load the mesh twice. We need to extend the asset cache to handle all resource types.

Here is our plan:

| Problem | Solution |
|---|---|
| Global `WeaponAnimation` objects with `extern` | `AnimationLibrary` class: load from JSON, look up by name |
| Hardcoded keyframe data in C++ | JSON animation files in `assets/animations/` |
| Hardcoded bob/sway/recoil constants | `WeaponEffectConfig` struct loaded per-weapon from data |
| `ResourceManager` only caches textures and shaders | Extend to cache meshes, animations, and effect configs |

---

## C++ Concept: `std::variant` and Type-Safe Resource Caching

When we extend `ResourceManager` to cache multiple resource types (meshes, animations, textures, shaders), we face a design question: do we add a separate `std::unordered_map` for each type, or do we build a more generic cache?

In Chapter 5a, we used separate maps:

```cpp
std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;
std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
```

This is straightforward but requires adding a new map, a new getter, and a new clear call for every resource type. With four types it is manageable. With ten it gets tedious.

An alternative is **`std::variant`**, introduced in C++17. A `variant` holds one of several types at a time, and you can query or visit it safely:

```cpp
using Resource = std::variant<
    std::shared_ptr<Shader>,
    std::shared_ptr<Texture>,
    std::shared_ptr<MeshData>,
    std::shared_ptr<WeaponAnimation>
>;

std::unordered_map<std::string, Resource> m_cache;

// Retrieving a specific type:
template<typename T>
std::shared_ptr<T> get(const std::string& name) {
    auto it = m_cache.find(name);
    if (it == m_cache.end()) return nullptr;
    auto* ptr = std::get_if<std::shared_ptr<T>>(&it->second);
    return ptr ? *ptr : nullptr;
}
```

`std::get_if` returns a pointer to the contained value if the variant currently holds that type, or `nullptr` otherwise. This is a compile-time type check -- you cannot accidentally retrieve a `Texture` from a slot that holds a `Shader`. The compiler enforces the types.

However, `variant` adds complexity that is not justified for our four resource types. We will stick with separate maps in this chapter -- the explicit approach is easier to read and debug. But `variant` is worth knowing for projects where the number of cached types grows large, or where you want a single generic `get<T>()` interface.

The takeaway: C++ gives you the tools to build generic systems, but "generic" is not always better. Use the simplest solution that handles your actual requirements.

---

## Step 1: AnimationLibrary

The `AnimationLibrary` loads `WeaponAnimation` data from JSON files and stores them in a name-keyed cache. Systems retrieve animations by name instead of through `extern` globals.

### Why JSON?

JSON is human-readable, trivially editable in any text editor, and has excellent C++ libraries. We will use [nlohmann/json](https://github.com/nlohmann/json), a single-header library. If your project does not already include it, drop `json.hpp` into your `external/` or `include/` directory.

For a production engine, you would eventually compile JSON to a binary format for faster loading. But during development, the ability to tweak a number in a text file and reload without recompiling is worth the tiny parsing overhead.

### The JSON Format

Each animation file defines a named set of keyframes:

```json
{
    "name": "shotgun_fire",
    "frames": [
        {
            "position": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "time": 0.0
        },
        {
            "position": [0.0, 0.05, 0.15],
            "rotation": [-10.0, 0.0, 0.0],
            "time": 0.05
        },
        {
            "position": [0.0, 0.02, 0.05],
            "rotation": [-3.0, 0.0, 0.0],
            "time": 0.15
        },
        {
            "position": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "time": 0.35
        }
    ]
}
```

Compare this to the C++ definition from Chapter 25. The numbers are identical -- we have just moved them from code to data. An artist or designer can now tweak the recoil peak timing (0.05 seconds), the kick-back distance (0.15 units on Z), or the recovery time (0.35 seconds) without touching C++.

### assets/animations/weapon_lower.json

```json
{
    "name": "weapon_lower",
    "frames": [
        {
            "position": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "time": 0.0
        },
        {
            "position": [0.0, -0.5, 0.0],
            "rotation": [15.0, 0.0, 0.0],
            "time": 0.25
        }
    ]
}
```

### assets/animations/weapon_raise.json

```json
{
    "name": "weapon_raise",
    "frames": [
        {
            "position": [0.0, -0.5, 0.0],
            "rotation": [15.0, 0.0, 0.0],
            "time": 0.0
        },
        {
            "position": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "time": 0.25
        }
    ]
}
```

### engine/animation/animation_library.h

```cpp
// engine/animation/animation_library.h
#pragma once

#include "engine/ecs/components.h"  // for WeaponAnimation, Keyframe

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <fstream>
#include <iostream>

// ─── AnimationLibrary ────────────────────────────────────────────
// Loads WeaponAnimation data from JSON files and caches them by
// name. Systems look up animations by name instead of through
// global extern declarations.
//
// Usage:
//   AnimationLibrary animLib;
//   animLib.load("assets/animations/shotgun_fire.json");
//   const auto* anim = animLib.getAnimation("shotgun_fire");

class AnimationLibrary
{
public:
	// ─── Load a single animation from a JSON file ────────────
	// Parses the file, builds a WeaponAnimation, and stores it
	// under the name specified in the JSON "name" field.
	// Returns true on success, false on failure.

	bool load(const std::string& filePath)
	{
		std::ifstream file(filePath);
		if (!file.is_open())
		{
			std::cerr << "AnimationLibrary: failed to open '"
			          << filePath << "'" << std::endl;
			return false;
		}

		nlohmann::json j;
		try
		{
			file >> j;
		}
		catch (const nlohmann::json::parse_error& e)
		{
			std::cerr << "AnimationLibrary: parse error in '"
			          << filePath << "': " << e.what() << std::endl;
			return false;
		}

		std::string name = j.at("name").get<std::string>();

		WeaponAnimation anim;
		for (const auto& frame : j.at("frames"))
		{
			Keyframe kf;
			auto pos = frame.at("position");
			kf.position = glm::vec3(
				pos[0].get<float>(),
				pos[1].get<float>(),
				pos[2].get<float>()
			);

			auto rot = frame.at("rotation");
			kf.rotation = glm::vec3(
				rot[0].get<float>(),
				rot[1].get<float>(),
				rot[2].get<float>()
			);

			kf.time = frame.at("time").get<float>();
			anim.frames.push_back(kf);
		}

		std::cout << "AnimationLibrary: loaded '" << name
		          << "' (" << anim.frames.size() << " frames)" << std::endl;

		m_animations[name] = std::move(anim);
		return true;
	}

	// ─── Load all animation files from a directory ───────────
	// Convenience method that loads every .json file in the
	// given directory. Uses std::filesystem.

	void loadDirectory(const std::string& directoryPath)
	{
		namespace fs = std::filesystem;

		if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath))
		{
			std::cerr << "AnimationLibrary: directory not found '"
			          << directoryPath << "'" << std::endl;
			return;
		}

		for (const auto& entry : fs::directory_iterator(directoryPath))
		{
			if (entry.path().extension() == ".json")
			{
				load(entry.path().string());
			}
		}
	}

	// ─── Retrieve an animation by name ───────────────────────
	// Returns a pointer to the cached animation, or nullptr if
	// the name is not found. The pointer remains valid as long
	// as the AnimationLibrary is alive and the animation is not
	// removed.

	const WeaponAnimation* getAnimation(const std::string& name) const
	{
		auto it = m_animations.find(name);
		if (it != m_animations.end())
		{
			return &it->second;
		}

		std::cerr << "AnimationLibrary: animation '" << name
		          << "' not found" << std::endl;
		return nullptr;
	}

	// ─── Check if an animation exists ────────────────────────
	bool hasAnimation(const std::string& name) const
	{
		return m_animations.find(name) != m_animations.end();
	}

	// ─── Clear all cached animations ─────────────────────────
	void clear()
	{
		m_animations.clear();
		std::cout << "AnimationLibrary: all animations cleared" << std::endl;
	}

private:
	std::unordered_map<std::string, WeaponAnimation> m_animations;
};
```

### What Changed

The three global `WeaponAnimation` objects (`shotgunFire`, `weaponLower`, `weaponRaise`) and their `extern` declarations are gone. In their place, a single `AnimationLibrary` instance loads animation data from JSON files and provides named lookup. The `view_model_system` no longer reaches across translation units -- it queries the library by name.

Notice that `AnimationLibrary` is header-only. The class is small, the methods are straightforward, and inlining the `load()` function (which does file I/O) is not a performance concern -- it runs once at startup. If the library grows more complex in a future chapter, we can split it into a `.h` and `.cpp`.

### Why `const WeaponAnimation*` and not `shared_ptr`?

The `AnimationLibrary` owns all animations for their entire lifetime. No system needs to extend an animation's lifetime beyond the library. A raw const pointer is the correct tool here -- it communicates "observe, do not own." This matches the Core Guidelines: use raw pointers for non-owning references, smart pointers for ownership.

---

## Step 2: AssetCache -- Extending ResourceManager

Chapter 5a's `ResourceManager` caches textures and shaders. Since then, we have accumulated meshes (from `MeshFactory`) and now animations (from `AnimationLibrary`) that are loaded ad-hoc with no caching. Two entities referencing the same weapon mesh each trigger a separate load. We need to extend the cache.

### The Approach

Rather than building a single generic cache (tempting, but over-engineered for four types), we add two new maps to `ResourceManager`: one for meshes and one for the `AnimationLibrary` itself. The `AnimationLibrary` already caches animations internally by name, so `ResourceManager` just needs to hold the library instance and provide access to it.

For meshes, we follow the same `shared_ptr` pattern established for textures and shaders in Chapter 5a.

### engine/core/resource_manager.h (updated)

```cpp
// engine/core/resource_manager.h
#pragma once

#include "engine/renderer/shader.h"
#include "engine/renderer/texture.h"
#include "engine/core/mesh_factory.h"
#include "engine/animation/animation_library.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

class ResourceManager
{
public:
	// ─── Shaders ─────────────────────────────────────────────
	// Load a shader (or return the cached version if already loaded)
	std::shared_ptr<Shader> getShader(
		const std::string& name,
		const std::string& vertexPath,
		const std::string& fragmentPath);

	// Retrieve a previously loaded shader by name
	std::shared_ptr<Shader> getShader(const std::string& name) const;

	// ─── Textures ────────────────────────────────────────────
	// Load a texture (or return the cached version if already loaded)
	std::shared_ptr<Texture> getTexture(
		const std::string& name,
		const std::string& path);

	// Retrieve a previously loaded texture by name
	std::shared_ptr<Texture> getTexture(const std::string& name) const;

	// ─── Meshes (NEW) ────────────────────────────────────────
	// Cache a mesh under a name. Unlike shaders and textures,
	// meshes are created by MeshFactory or loaded from model
	// files, so we provide a "store" method rather than a
	// "load" method. The ResourceManager does not know how to
	// create meshes -- it just caches them.

	void storeMesh(const std::string& name,
	               std::shared_ptr<MeshData> mesh);

	std::shared_ptr<MeshData> getMesh(const std::string& name) const;

	// ─── Animations (NEW) ────────────────────────────────────
	// Provides access to the AnimationLibrary, which handles
	// its own loading and caching internally. This keeps
	// animation loading consistent with the resource pipeline
	// without duplicating the AnimationLibrary's cache logic.

	AnimationLibrary& getAnimationLibrary() { return m_animationLibrary; }
	const AnimationLibrary& getAnimationLibrary() const { return m_animationLibrary; }

	// ─── Cleanup ─────────────────────────────────────────────
	// Drop all cached resources.
	// Actual GPU cleanup happens when the last shared_ptr is released.
	void clear();

private:
	std::unordered_map<std::string, std::shared_ptr<Shader>>  m_shaders;
	std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
	std::unordered_map<std::string, std::shared_ptr<MeshData>> m_meshes;

	AnimationLibrary m_animationLibrary;
};
```

### engine/core/resource_manager.cpp (updated)

```cpp
// engine/core/resource_manager.cpp
#include "engine/core/resource_manager.h"

// ─── Shaders ─────────────────────────────────────────────────────

std::shared_ptr<Shader> ResourceManager::getShader(
	const std::string& name,
	const std::string& vertexPath,
	const std::string& fragmentPath)
{
	auto it = m_shaders.find(name);
	if (it != m_shaders.end())
	{
		return it->second;
	}

	auto shader = std::make_shared<Shader>(vertexPath, fragmentPath);
	m_shaders[name] = shader;
	std::cout << "ResourceManager: cached shader '" << name << "'" << std::endl;
	return shader;
}

std::shared_ptr<Shader> ResourceManager::getShader(const std::string& name) const
{
	auto it = m_shaders.find(name);
	if (it != m_shaders.end())
	{
		return it->second;
	}

	std::cerr << "ERROR: Shader '" << name << "' not found in cache" << std::endl;
	return nullptr;
}

// ─── Textures ────────────────────────────────────────────────────

std::shared_ptr<Texture> ResourceManager::getTexture(
	const std::string& name,
	const std::string& path)
{
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}

	auto texture = std::make_shared<Texture>(path);
	m_textures[name] = texture;
	std::cout << "ResourceManager: cached texture '" << name << "'" << std::endl;
	return texture;
}

std::shared_ptr<Texture> ResourceManager::getTexture(const std::string& name) const
{
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}

	std::cerr << "ERROR: Texture '" << name << "' not found in cache" << std::endl;
	return nullptr;
}

// ─── Meshes ──────────────────────────────────────────────────────

void ResourceManager::storeMesh(const std::string& name,
                                 std::shared_ptr<MeshData> mesh)
{
	m_meshes[name] = std::move(mesh);
	std::cout << "ResourceManager: cached mesh '" << name << "'" << std::endl;
}

std::shared_ptr<MeshData> ResourceManager::getMesh(const std::string& name) const
{
	auto it = m_meshes.find(name);
	if (it != m_meshes.end())
	{
		return it->second;
	}

	std::cerr << "ERROR: Mesh '" << name << "' not found in cache" << std::endl;
	return nullptr;
}

// ─── Cleanup ─────────────────────────────────────────────────────

void ResourceManager::clear()
{
	m_shaders.clear();
	m_textures.clear();
	m_meshes.clear();
	m_animationLibrary.clear();
	std::cout << "ResourceManager: all resources cleared" << std::endl;
}
```

### Why `storeMesh()` Instead of a Load Overload?

Textures and shaders have a simple contract: give me a file path, I give you a GPU resource. Meshes do not work that way. A mesh might come from `MeshFactory::createQuadMesh()`, from a model loader, or from procedural generation. The `ResourceManager` should not know about all those creation methods. Instead, the caller creates the mesh however it needs to, then stores it in the cache. Retrieval is always the same: `getMesh("shotgun_viewmodel")`.

This is the **repository pattern** -- the cache accepts pre-built resources and provides lookup, without coupling to resource creation logic.

### Why Embed AnimationLibrary Instead of Duplicating Its Cache?

The `AnimationLibrary` already has its own `std::unordered_map` for name-keyed lookup. Wrapping it inside another map in `ResourceManager` would mean double caching. Instead, `ResourceManager` owns the `AnimationLibrary` instance and exposes it directly. This keeps animation loading consistent with the resource pipeline (everything goes through `ResourceManager`) without duplicating storage.

---

## Step 3: WeaponEffectConfig

The procedural effects from Chapter 25 -- view bob, recoil kick, and idle sway -- all use hardcoded constants. Different weapons should feel different. A shotgun should kick harder than a pistol. A sniper rifle should sway more than an SMG. We need a per-weapon configuration struct.

### The Config Struct

```cpp
// engine/animation/weapon_effect_config.h
#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <string>
#include <fstream>
#include <iostream>

// ─── WeaponEffectConfig ──────────────────────────────────────────
// Per-weapon parameters for procedural view model effects.
// Loaded from JSON so designers can tune weapon feel without
// recompiling.
//
// Each weapon type (shotgun, pistol, rifle, etc.) has its own
// config file. The ViewModel component stores the name of its
// active config. The view model system looks it up each frame.

struct WeaponEffectConfig
{
	// ─── Identity ────────────────────────────────────────────
	std::string name;

	// ─── View Bob ────────────────────────────────────────────
	// Weapon sway while moving. Frequency controls oscillation
	// speed, amplitude controls how far the weapon moves.
	float bobFrequency  = 8.0f;
	float bobAmplitudeX = 0.005f;
	float bobAmplitudeY = 0.0025f;

	// ─── Idle Sway ───────────────────────────────────────────
	// Subtle drift when standing still. Makes the weapon feel
	// alive. Different frequencies on X/Y create a Lissajous
	// curve (figure-8 pattern).
	float swaySpeed     = 1.2f;
	float swayAmount    = 0.002f;
	float swayRatioY    = 0.7f;   // Y frequency as fraction of X

	// ─── Recoil Spring ───────────────────────────────────────
	// Damped spring for the recoil kick. Stiffness controls
	// how fast the weapon returns, damping controls how quickly
	// oscillation dies. Higher stiffness = snappier return.
	// Higher damping = less bounce.
	float recoilStiffness = 200.0f;
	float recoilDamping   = 15.0f;
	float recoilKickImpulse = 3.0f;  // Initial velocity on fire

	// ─── Recoil Rotation ─────────────────────────────────────
	// How much the weapon pitches up during recoil.
	float recoilPitchMultiplier = 15.0f;

	// ─── Animation Names ─────────────────────────────────────
	// Which animations to use for each state. These are looked
	// up in the AnimationLibrary by name.
	std::string fireAnimation   = "shotgun_fire";
	std::string lowerAnimation  = "weapon_lower";
	std::string raiseAnimation  = "weapon_raise";
	std::string reloadAnimation = "";  // Empty = no reload anim
};
```

### Loading from JSON

```cpp
// engine/animation/weapon_effect_config.h (continued)

// ─── Load a WeaponEffectConfig from a JSON file ──────────────────
// Returns a default-constructed config on failure.

inline WeaponEffectConfig loadWeaponEffectConfig(const std::string& filePath)
{
	WeaponEffectConfig config;

	std::ifstream file(filePath);
	if (!file.is_open())
	{
		std::cerr << "WeaponEffectConfig: failed to open '"
		          << filePath << "'" << std::endl;
		return config;
	}

	nlohmann::json j;
	try
	{
		file >> j;
	}
	catch (const nlohmann::json::parse_error& e)
	{
		std::cerr << "WeaponEffectConfig: parse error in '"
		          << filePath << "': " << e.what() << std::endl;
		return config;
	}

	// Use .value() with defaults so missing fields fall back to
	// the struct's default values. This makes the JSON format
	// forward-compatible -- adding a new field to the struct
	// does not break existing config files.

	config.name = j.value("name", "unnamed");

	config.bobFrequency  = j.value("bobFrequency",  config.bobFrequency);
	config.bobAmplitudeX = j.value("bobAmplitudeX", config.bobAmplitudeX);
	config.bobAmplitudeY = j.value("bobAmplitudeY", config.bobAmplitudeY);

	config.swaySpeed   = j.value("swaySpeed",   config.swaySpeed);
	config.swayAmount  = j.value("swayAmount",  config.swayAmount);
	config.swayRatioY  = j.value("swayRatioY",  config.swayRatioY);

	config.recoilStiffness      = j.value("recoilStiffness",      config.recoilStiffness);
	config.recoilDamping        = j.value("recoilDamping",        config.recoilDamping);
	config.recoilKickImpulse    = j.value("recoilKickImpulse",    config.recoilKickImpulse);
	config.recoilPitchMultiplier = j.value("recoilPitchMultiplier", config.recoilPitchMultiplier);

	config.fireAnimation   = j.value("fireAnimation",   config.fireAnimation);
	config.lowerAnimation  = j.value("lowerAnimation",  config.lowerAnimation);
	config.raiseAnimation  = j.value("raiseAnimation",  config.raiseAnimation);
	config.reloadAnimation = j.value("reloadAnimation", config.reloadAnimation);

	std::cout << "WeaponEffectConfig: loaded '" << config.name << "'" << std::endl;
	return config;
}
```

### Example Config Files

**assets/configs/shotgun_effects.json:**

```json
{
    "name": "shotgun",

    "bobFrequency": 8.0,
    "bobAmplitudeX": 0.006,
    "bobAmplitudeY": 0.003,

    "swaySpeed": 1.0,
    "swayAmount": 0.002,
    "swayRatioY": 0.7,

    "recoilStiffness": 180.0,
    "recoilDamping": 12.0,
    "recoilKickImpulse": 4.0,
    "recoilPitchMultiplier": 18.0,

    "fireAnimation": "shotgun_fire",
    "lowerAnimation": "weapon_lower",
    "raiseAnimation": "weapon_raise"
}
```

**assets/configs/pistol_effects.json:**

```json
{
    "name": "pistol",

    "bobFrequency": 10.0,
    "bobAmplitudeX": 0.003,
    "bobAmplitudeY": 0.0015,

    "swaySpeed": 1.5,
    "swayAmount": 0.001,
    "swayRatioY": 0.6,

    "recoilStiffness": 250.0,
    "recoilDamping": 18.0,
    "recoilKickImpulse": 1.5,
    "recoilPitchMultiplier": 10.0,

    "fireAnimation": "pistol_fire",
    "lowerAnimation": "weapon_lower",
    "raiseAnimation": "weapon_raise"
}
```

Notice how the pistol has faster bob (lighter weapon), less sway (shorter barrel), stiffer recoil spring (snappier return), and a weaker kick impulse (less punch). These differences are now tunable data, not buried code constants.

### Storing Configs in ResourceManager

The `WeaponEffectConfig` is small and plain. We store configs in a simple map on `ResourceManager`, following the same pattern as meshes:

Add these to `ResourceManager`:

```cpp
// In resource_manager.h, add to the public section:

void storeWeaponEffectConfig(const std::string& name,
                              const WeaponEffectConfig& config);

const WeaponEffectConfig* getWeaponEffectConfig(
    const std::string& name) const;

// In resource_manager.h, add to the private section:

std::unordered_map<std::string, WeaponEffectConfig> m_weaponEffectConfigs;
```

```cpp
// In resource_manager.cpp:

void ResourceManager::storeWeaponEffectConfig(
    const std::string& name,
    const WeaponEffectConfig& config)
{
	m_weaponEffectConfigs[name] = config;
	std::cout << "ResourceManager: cached weapon effect config '"
	          << name << "'" << std::endl;
}

const WeaponEffectConfig* ResourceManager::getWeaponEffectConfig(
    const std::string& name) const
{
	auto it = m_weaponEffectConfigs.find(name);
	if (it != m_weaponEffectConfigs.end())
	{
		return &it->second;
	}

	std::cerr << "ERROR: WeaponEffectConfig '" << name
	          << "' not found in cache" << std::endl;
	return nullptr;
}
```

And update `clear()`:

```cpp
void ResourceManager::clear()
{
	m_shaders.clear();
	m_textures.clear();
	m_meshes.clear();
	m_weaponEffectConfigs.clear();
	m_animationLibrary.clear();
	std::cout << "ResourceManager: all resources cleared" << std::endl;
}
```

---

## Step 4: Updated ViewModel Component

The `ViewModel` component needs a small update: instead of directly storing animation references, it stores the name of its active `WeaponEffectConfig`. The view model system uses that name to look up both the config (for procedural parameters) and the animation names (for keyframe playback).

```cpp
// In components.h — updated ViewModel component

enum class ViewModelState {
	Idle,
	Firing,
	Reloading,
	SwitchingOut,
	SwitchingIn
};

struct ViewModel {
	// Visual
	std::string meshName;
	std::string textureName;

	// Base position (rest position, bottom-right of screen)
	glm::vec3 basePosition = glm::vec3(0.3f, -0.3f, -0.5f);
	glm::vec3 baseRotation = glm::vec3(0.0f);

	// Current animated position/rotation (base + all effects combined)
	glm::vec3 currentPosition = glm::vec3(0.3f, -0.3f, -0.5f);
	glm::vec3 currentRotation = glm::vec3(0.0f);

	// Animation state
	ViewModelState state = ViewModelState::Idle;
	float stateTimer = 0.0f;

	// Procedural effects
	float bobTimer = 0.0f;
	float recoilAmount = 0.0f;
	float recoilVelocity = 0.0f;
	float swayTimer = 0.0f;

	// Weapon switch
	std::string pendingWeaponMesh;
	std::string pendingWeaponTexture;
	std::string pendingEffectConfig;  // NEW: pending config name

	// Effect config reference (NEW)
	// Name of the active WeaponEffectConfig in ResourceManager.
	// The view model system looks this up each frame to get
	// the current weapon's bob/sway/recoil parameters and
	// animation names.
	std::string effectConfigName;
};
```

The only additions are `effectConfigName` and `pendingEffectConfig`. Everything else is unchanged from Chapter 25.

---

## Step 5: Updated view_model_system.cpp

This is the payoff. The view model system no longer has `extern` declarations, hardcoded constants, or standalone procedural functions. It reads everything from the `AnimationLibrary` and `WeaponEffectConfig` through the `ResourceManager`.

### Before (Chapter 25)

For reference, here is what the system looked like with all the problems:

```cpp
// view_model_system.cpp — BEFORE (Chapter 25)
#include "engine/ecs/systems/view_model_system.h"
#include "engine/ecs/components.h"

// Global animation data via extern
extern WeaponAnimation shotgunFire;
extern WeaponAnimation weaponLower;
extern WeaponAnimation weaponRaise;

// Standalone procedural functions with hardcoded constants
glm::vec3 calculateBob(float bobTimer, float moveSpeed) {
    float bobFrequency = 8.0f;
    float bobAmplitude = 0.005f * moveSpeed;
    float x = sin(bobTimer * bobFrequency) * bobAmplitude;
    float y = -abs(cos(bobTimer * bobFrequency)) * bobAmplitude * 0.5f;
    return glm::vec3(x, y, 0.0f);
}

void updateRecoil(float& recoilAmount, float& recoilVelocity, float dt) {
    float stiffness = 200.0f;
    float damping = 15.0f;
    float force = -stiffness * recoilAmount - damping * recoilVelocity;
    recoilVelocity += force * dt;
    recoilAmount += recoilVelocity * dt;
    if (abs(recoilAmount) < 0.001f && abs(recoilVelocity) < 0.01f) {
        recoilAmount = 0.0f;
        recoilVelocity = 0.0f;
    }
}

glm::vec3 calculateIdleSway(float swayTimer) {
    float swaySpeed = 1.2f;
    float swayAmount = 0.002f;
    float x = sin(swayTimer * swaySpeed) * swayAmount;
    float y = sin(swayTimer * swaySpeed * 0.7f) * swayAmount * 0.5f;
    return glm::vec3(x, y, 0.0f);
}

void viewModelSystem(entt::registry& registry, float dt) {
    auto view = registry.view<ViewModel, Velocity, OnGround>();

    for (auto [entity, vm, vel, ground] : view.each()) {
        vm.stateTimer += dt;

        glm::vec3 animPos(0.0f);
        glm::vec3 animRot(0.0f);

        switch (vm.state) {
            case ViewModelState::Idle:
                break;
            case ViewModelState::Firing:
                evaluateAnimation(shotgunFire, vm.stateTimer, animPos, animRot);
                if (vm.stateTimer >= shotgunFire.getDuration()) {
                    vm.state = ViewModelState::Idle;
                    vm.stateTimer = 0.0f;
                }
                break;
            case ViewModelState::SwitchingOut:
                evaluateAnimation(weaponLower, vm.stateTimer, animPos, animRot);
                if (vm.stateTimer >= weaponLower.getDuration()) {
                    vm.meshName = vm.pendingWeaponMesh;
                    vm.textureName = vm.pendingWeaponTexture;
                    vm.state = ViewModelState::SwitchingIn;
                    vm.stateTimer = 0.0f;
                }
                break;
            case ViewModelState::SwitchingIn:
                evaluateAnimation(weaponRaise, vm.stateTimer, animPos, animRot);
                if (vm.stateTimer >= weaponRaise.getDuration()) {
                    vm.state = ViewModelState::Idle;
                    vm.stateTimer = 0.0f;
                }
                break;
            case ViewModelState::Reloading:
                break;
        }

        float speed = glm::length(glm::vec2(vel.value.x, vel.value.z));
        glm::vec3 bob(0.0f);
        if (ground.value && speed > 0.5f) {
            vm.bobTimer += dt * speed * 0.5f;
            bob = calculateBob(vm.bobTimer, speed);
        } else {
            vm.bobTimer = 0.0f;
        }

        updateRecoil(vm.recoilAmount, vm.recoilVelocity, dt);
        glm::vec3 recoilOffset(0.0f, 0.0f, vm.recoilAmount);
        glm::vec3 recoilRotation(-vm.recoilAmount * 15.0f, 0.0f, 0.0f);

        vm.swayTimer += dt;
        glm::vec3 sway = calculateIdleSway(vm.swayTimer);

        vm.currentPosition = vm.basePosition + animPos + bob + recoilOffset + sway;
        vm.currentRotation = vm.baseRotation + animRot + recoilRotation;
    }
}
```

### After (Chapter 25a)

```cpp
// engine/ecs/systems/view_model_system.cpp — AFTER (Chapter 25a)
#include "engine/ecs/systems/view_model_system.h"
#include "engine/ecs/components.h"
#include "engine/core/resource_manager.h"
#include "engine/core/math_utils.h"

#include <cmath>

// ─── Helper: evaluate keyframe animation ─────────────────────────
// Unchanged from Chapter 25. Interpolates between keyframes using
// smoothstep for organic weapon motion.

static void evaluateAnimation(const WeaponAnimation& anim, float time,
                               glm::vec3& outPosition, glm::vec3& outRotation)
{
	if (anim.frames.empty()) return;

	time = glm::clamp(time, 0.0f, anim.getDuration());

	for (size_t i = 0; i < anim.frames.size() - 1; i++)
	{
		const auto& a = anim.frames[i];
		const auto& b = anim.frames[i + 1];

		if (time >= a.time && time <= b.time)
		{
			float range = b.time - a.time;
			float t = (range > 0.001f) ? (time - a.time) / range : 0.0f;

			// Smoothstep for organic feel
			t = MathUtils::smoothstep(0.0f, 1.0f, t);

			outPosition = glm::mix(a.position, b.position, t);
			outRotation = glm::mix(a.rotation, b.rotation, t);
			return;
		}
	}

	outPosition = anim.frames.back().position;
	outRotation = anim.frames.back().rotation;
}

// ─── Helper: look up an animation safely ─────────────────────────
// Returns a pointer to the animation, or nullptr if not found.
// Avoids repeating the ResourceManager lookup pattern.

static const WeaponAnimation* findAnimation(
    const ResourceManager& resources,
    const std::string& name)
{
	if (name.empty()) return nullptr;
	return resources.getAnimationLibrary().getAnimation(name);
}

// ─── Helper: process a keyframe animation state ──────────────────
// Evaluates the named animation and checks for completion.
// Returns true if the animation is still playing, false if it ended.

static bool processAnimation(
    const ResourceManager& resources,
    const std::string& animName,
    float timer,
    glm::vec3& outPos,
    glm::vec3& outRot)
{
	const auto* anim = findAnimation(resources, animName);
	if (!anim) return false;

	evaluateAnimation(*anim, timer, outPos, outRot);
	return timer < anim->getDuration();
}

// ─── Helper: calculate view bob from config ──────────────────────

static glm::vec3 calculateBob(float bobTimer, float moveSpeed,
                                const WeaponEffectConfig& config)
{
	float amplitudeX = config.bobAmplitudeX * moveSpeed;
	float amplitudeY = config.bobAmplitudeY * moveSpeed;

	float x = std::sin(bobTimer * config.bobFrequency) * amplitudeX;
	float y = -std::abs(std::cos(bobTimer * config.bobFrequency)) * amplitudeY;

	return glm::vec3(x, y, 0.0f);
}

// ─── Helper: update recoil spring from config ────────────────────

static void updateRecoil(float& recoilAmount, float& recoilVelocity,
                          float dt, const WeaponEffectConfig& config)
{
	float force = -config.recoilStiffness * recoilAmount
	            - config.recoilDamping * recoilVelocity;
	recoilVelocity += force * dt;
	recoilAmount += recoilVelocity * dt;

	// Kill tiny oscillations
	if (std::abs(recoilAmount) < 0.001f
	    && std::abs(recoilVelocity) < 0.01f)
	{
		recoilAmount = 0.0f;
		recoilVelocity = 0.0f;
	}
}

// ─── Helper: calculate idle sway from config ─────────────────────

static glm::vec3 calculateIdleSway(float swayTimer,
                                     const WeaponEffectConfig& config)
{
	float x = std::sin(swayTimer * config.swaySpeed) * config.swayAmount;
	float y = std::sin(swayTimer * config.swaySpeed * config.swayRatioY)
	        * config.swayAmount * 0.5f;

	return glm::vec3(x, y, 0.0f);
}

// ─── The System ──────────────────────────────────────────────────

void viewModelSystem(entt::registry& registry, float dt)
{
	// The ResourceManager is stored as a registry context variable
	// (set up during initialisation). This gives every system access
	// to cached resources without parameter passing.
	auto* resources = registry.ctx().find<ResourceManager*>();
	if (!resources || !*resources) return;
	ResourceManager& res = **resources;

	auto view = registry.view<ViewModel, Velocity, OnGround>();

	for (auto [entity, vm, vel, ground] : view.each())
	{
		// ─── Look up the weapon's effect config ──────────────
		// Default config used if the name is empty or not found.
		static const WeaponEffectConfig defaultConfig;
		const WeaponEffectConfig& config =
			vm.effectConfigName.empty()
				? defaultConfig
				: [&]() -> const WeaponEffectConfig& {
					const auto* c = res.getWeaponEffectConfig(
						vm.effectConfigName);
					return c ? *c : defaultConfig;
				}();

		// ─── Update state timer ──────────────────────────────
		vm.stateTimer += dt;

		// ─── Keyframe animation based on state ───────────────
		glm::vec3 animPos(0.0f);
		glm::vec3 animRot(0.0f);

		switch (vm.state)
		{
			case ViewModelState::Idle:
				// No keyframe animation — just procedural
				break;

			case ViewModelState::Firing:
			{
				if (!processAnimation(res, config.fireAnimation,
				                       vm.stateTimer, animPos, animRot))
				{
					vm.state = ViewModelState::Idle;
					vm.stateTimer = 0.0f;
				}
				break;
			}

			case ViewModelState::SwitchingOut:
			{
				if (!processAnimation(res, config.lowerAnimation,
				                       vm.stateTimer, animPos, animRot))
				{
					// Swap the weapon mesh and config
					vm.meshName = vm.pendingWeaponMesh;
					vm.textureName = vm.pendingWeaponTexture;
					vm.effectConfigName = vm.pendingEffectConfig;
					vm.state = ViewModelState::SwitchingIn;
					vm.stateTimer = 0.0f;
				}
				break;
			}

			case ViewModelState::SwitchingIn:
			{
				// Use the NEW weapon's raise animation
				const WeaponEffectConfig& newConfig =
					vm.effectConfigName.empty()
						? defaultConfig
						: [&]() -> const WeaponEffectConfig& {
							const auto* c = res.getWeaponEffectConfig(
								vm.effectConfigName);
							return c ? *c : defaultConfig;
						}();

				if (!processAnimation(res, newConfig.raiseAnimation,
				                       vm.stateTimer, animPos, animRot))
				{
					vm.state = ViewModelState::Idle;
					vm.stateTimer = 0.0f;
				}
				break;
			}

			case ViewModelState::Reloading:
			{
				if (!config.reloadAnimation.empty())
				{
					if (!processAnimation(res, config.reloadAnimation,
					                       vm.stateTimer, animPos, animRot))
					{
						vm.state = ViewModelState::Idle;
						vm.stateTimer = 0.0f;
					}
				}
				break;
			}
		}

		// ─── Procedural: view bob ────────────────────────────
		float speed = glm::length(glm::vec2(vel.value.x, vel.value.z));
		glm::vec3 bob(0.0f);

		if (ground.value && speed > 0.5f)
		{
			vm.bobTimer += dt * speed * 0.5f;
			bob = calculateBob(vm.bobTimer, speed, config);
		}
		else
		{
			// Smoothly reduce bob when not moving
			vm.bobTimer = MathUtils::lerp(vm.bobTimer, 0.0f, dt * 5.0f);
		}

		// ─── Procedural: recoil ──────────────────────────────
		updateRecoil(vm.recoilAmount, vm.recoilVelocity, dt, config);
		glm::vec3 recoilOffset(0.0f, 0.0f, vm.recoilAmount);
		glm::vec3 recoilRotation(
			-vm.recoilAmount * config.recoilPitchMultiplier,
			0.0f, 0.0f);

		// ─── Procedural: idle sway ───────────────────────────
		vm.swayTimer += dt;
		glm::vec3 sway = calculateIdleSway(vm.swayTimer, config);

		// ─── Combine all effects ─────────────────────────────
		vm.currentPosition = vm.basePosition + animPos + bob
		                   + recoilOffset + sway;
		vm.currentRotation = vm.baseRotation + animRot + recoilRotation;
	}
}
```

### What Changed

Let us walk through the differences:

1. **No more `extern` declarations.** The system gets animations from `AnimationLibrary` via `ResourceManager`, looked up by the names stored in `WeaponEffectConfig`. Adding a new weapon means adding a new config file and animation files -- no code changes in the system.

2. **No more hardcoded procedural constants.** `calculateBob()`, `updateRecoil()`, and `calculateIdleSway()` now take a `const WeaponEffectConfig&` parameter. The numbers come from data, not from the function body.

3. **`ResourceManager` accessed through registry context.** We store a pointer to `ResourceManager` in the registry's context (just like `PhysicsConfig` in Chapter 10a). This means the system signature does not change -- it still takes `(registry, dt)` -- but it has access to all cached resources.

4. **Smooth bob decay.** When the player stops moving, instead of snapping `bobTimer` to zero (which caused an abrupt weapon jump), we use `MathUtils::lerp()` to smoothly decay the timer. This was a subtle bug in Chapter 25 that the cleanup naturally fixes.

5. **Animation lookup is a helper function.** The `processAnimation()` helper encapsulates the "look up animation, evaluate it, check if done" pattern that was repeated in every switch case. Each case is now three lines instead of six.

---

## Step 6: Updated Setup and Game Loop

### Loading Resources

Here is how the new loading flow looks in `setupScene()` or your initialisation code:

```cpp
// ─── In setupScene() or initialisation ───────────────────────────

void setupScene(entt::registry& registry, ResourceManager& resources)
{
	// ─── Load animations ─────────────────────────────────────
	// Load all animation JSON files from the animations directory.
	// Each file defines a named animation that can be referenced
	// by WeaponEffectConfig.

	resources.getAnimationLibrary().loadDirectory("assets/animations");

	// ─── Load weapon effect configs ──────────────────────────
	// Each config defines the procedural parameters and animation
	// names for one weapon type.

	auto shotgunConfig = loadWeaponEffectConfig(
		"assets/configs/shotgun_effects.json");
	resources.storeWeaponEffectConfig(shotgunConfig.name, shotgunConfig);

	auto pistolConfig = loadWeaponEffectConfig(
		"assets/configs/pistol_effects.json");
	resources.storeWeaponEffectConfig(pistolConfig.name, pistolConfig);

	// ─── Cache weapon meshes ─────────────────────────────────
	// Meshes are created by MeshFactory or loaded from model files,
	// then stored in ResourceManager for shared access.

	auto shotgunMesh = std::make_shared<MeshData>(
		MeshFactory::createWeaponMesh("assets/models/shotgun.obj"));
	resources.storeMesh("shotgun_viewmodel", shotgunMesh);

	auto pistolMesh = std::make_shared<MeshData>(
		MeshFactory::createWeaponMesh("assets/models/pistol.obj"));
	resources.storeMesh("pistol_viewmodel", pistolMesh);

	// ─── Load weapon textures through ResourceManager ────────
	resources.getTexture("shotgun_tex", "assets/textures/shotgun.png");
	resources.getTexture("pistol_tex", "assets/textures/pistol.png");

	// ─── Store ResourceManager pointer in registry context ───
	// This gives every ECS system access to cached resources
	// without threading ResourceManager& through every function.
	// Same pattern as PhysicsConfig from Chapter 10a.

	ResourceManager* resPtr = &resources;
	registry.ctx().emplace<ResourceManager*>(resPtr);

	// ─── Create player entity with ViewModel ─────────────────
	auto player = registry.create();
	// ... other player components (Position, Velocity, etc.) ...

	auto& vm = registry.emplace<ViewModel>(player);
	vm.meshName = "shotgun_viewmodel";
	vm.textureName = "shotgun_tex";
	vm.effectConfigName = "shotgun";  // References the config by name

	// ... HUD components, camera effects, etc. from previous chapters ...
}
```

### Updated Game Loop

The game loop itself does not change at all. The `viewModelSystem` signature is the same as Chapter 25:

```cpp
while (!window.shouldClose())
{
	fixedTimestep.accumulate();

	// -- Phase: Input --
	input.update();
	window.pollEvents();
	inputSystem(registry);

	// -- Phase: Physics (fixed timestep) --
	while (fixedTimestep.step())
	{
		gravitySystem(registry);
		movementSystem(registry);
		collisionSystem(registry);
		jumpSystem(registry);
	}

	// -- Phase: GameLogic --
	float dt = fixedTimestep.getFrameDelta();
	hudUpdateSystem(registry, dt);
	cameraEffectsUpdateSystem(registry, dt);
	viewModelSystem(registry, dt);           // No change to signature

	// -- Phase: LateUpdate --
	cameraEffectsApplySystem(registry);
	cameraFollowSystem(registry);

	// -- Phase: Render --
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	float aspectRatio = static_cast<float>(window.getWidth())
		/ static_cast<float>(window.getHeight());

	auto& offset = registry.get<CameraEffectOffset>(player);
	glm::mat4 view = camera.getViewMatrix();
	view = glm::translate(view, offset.positionOffset);

	renderSystem(registry, view, projection);
	renderParticles(particlePool, particleShader, quadVAO,
	                camera, aspectRatio);

	renderViewModel(registry, viewModelShader, aspectRatio);

	hudRenderer.render(registry,
		static_cast<float>(window.getWidth()),
		static_cast<float>(window.getHeight()));

	window.swapBuffers();
}
```

The only difference from Chapter 25 is that the three global `WeaponAnimation` definitions that used to sit before the loop are gone. They now live in JSON files loaded during `setupScene()`.

### Triggering Weapon Fire (Updated)

When the combat system fires the weapon, it now uses the config's kick impulse instead of a hardcoded value:

```cpp
// In combatSystem, after firing:
if (registry.all_of<ViewModel>(entity))
{
	auto& vm = registry.get<ViewModel>(entity);
	vm.state = ViewModelState::Firing;
	vm.stateTimer = 0.0f;

	// Use the weapon's configured kick impulse
	auto* resources = registry.ctx().find<ResourceManager*>();
	if (resources && *resources)
	{
		const auto* config = (*resources)->getWeaponEffectConfig(
			vm.effectConfigName);
		if (config)
		{
			vm.recoilVelocity = config->recoilKickImpulse;
		}
	}
}
```

### Triggering Weapon Switch (Updated)

The switch function now also transfers the effect config:

```cpp
void switchWeapon(entt::registry& registry, entt::entity player,
                   const std::string& newMesh,
                   const std::string& newTexture,
                   const std::string& newEffectConfig)
{
	auto& vm = registry.get<ViewModel>(player);

	if (vm.state != ViewModelState::Idle) return;

	vm.pendingWeaponMesh = newMesh;
	vm.pendingWeaponTexture = newTexture;
	vm.pendingEffectConfig = newEffectConfig;
	vm.state = ViewModelState::SwitchingOut;
	vm.stateTimer = 0.0f;
}

// Usage:
switchWeapon(registry, player,
    "pistol_viewmodel", "pistol_tex", "pistol");
```

---

## Updated File Structure

After this chapter, your project tree has these new and modified files:

```
src/
  engine/
    animation/
      animation_library.h          <- NEW: AnimationLibrary class
      weapon_effect_config.h       <- NEW: WeaponEffectConfig struct + loader
    core/
      math_utils.h                 <- UNCHANGED (from 20a)
      fixed_timestep.h             <- UNCHANGED (from 10a)
      mesh_factory.h               <- UNCHANGED
      resource_manager.h           <- MODIFIED: +storeMesh, +getMesh,
                                               +storeWeaponEffectConfig,
                                               +getWeaponEffectConfig,
                                               +getAnimationLibrary
      resource_manager.cpp         <- MODIFIED: new method implementations
    ecs/
      components.h                 <- MODIFIED: ViewModel gains effectConfigName
      polish_components.h          <- UNCHANGED (from 20a)
      hud_components.h             <- UNCHANGED (from 15a)
      systems/
        view_model_system.h        <- UNCHANGED (same signature)
        view_model_system.cpp      <- MODIFIED: uses AnimationLibrary + configs
        camera_effects_system.h    <- UNCHANGED (from 20a)
        camera_effects_system.cpp  <- UNCHANGED (from 20a)
    particles/
      particle_emitter_def.h       <- UNCHANGED (from 20a)
      particle_emitter_defs.h      <- UNCHANGED (from 20a)
      particle_emitter.h           <- UNCHANGED (from 20a)
  main.cpp                         <- MODIFIED: setup loads JSON configs
assets/
  animations/
    shotgun_fire.json              <- NEW: keyframe data
    weapon_lower.json              <- NEW: keyframe data
    weapon_raise.json              <- NEW: keyframe data
  configs/
    shotgun_effects.json           <- NEW: per-weapon procedural parameters
    pistol_effects.json            <- NEW: per-weapon procedural parameters
```

Note that both new engine headers (`animation_library.h` and `weapon_effect_config.h`) are header-only. No new `.cpp` compilation units were added -- only `resource_manager.cpp` and `view_model_system.cpp` were modified. The only build system change is ensuring `nlohmann/json.hpp` is on the include path, if it was not already.

---

## Build and Test

Rebuild the project:

```bash
cmake --build build
```

You should see the exact same weapon animations as before -- the shotgun fires with the same recoil kick, the weapon lowers and raises with the same timing, the view bob and idle sway feel identical. The behaviour is unchanged. The architecture is fundamentally better.

If something does not work:

1. **Check that JSON files exist.** If `AnimationLibrary::load()` fails, you will see an error in the console. Make sure the `assets/animations/` and `assets/configs/` directories contain the JSON files with the correct format.

2. **Check the animation names.** The `WeaponEffectConfig` references animations by the name in the JSON `"name"` field. If the config says `"fireAnimation": "shotgun_fire"` but the animation JSON says `"name": "shotgun_fire_anim"`, the lookup will fail. The names must match exactly.

3. **Check the ResourceManager context.** If the view model system cannot find `ResourceManager*` in the registry context, it returns early and does nothing. Make sure `registry.ctx().emplace<ResourceManager*>(&resources)` runs before the game loop starts.

4. **Check the nlohmann/json include.** If you get compile errors about `nlohmann::json`, make sure the library header is available. It is a single header -- drop `json.hpp` into your include path.

5. **Compare numbers.** The JSON keyframe values should be identical to the C++ values from Chapter 25. If the animation looks different, compare the JSON numbers against the original hardcoded arrays. A common mistake is swapping X and Y in the rotation values.

---

## Before vs After: Summary

| Aspect | Before (Chapter 25) | After (Chapter 25a) |
|---|---|---|
| **Animation data** | Global `WeaponAnimation` objects with `extern` | JSON files loaded into `AnimationLibrary` |
| **Animation lookup** | Direct global variable reference | `animLib.getAnimation("shotgun_fire")` |
| **Adding a new weapon animation** | New global, new `extern`, new switch case | New JSON file, new config entry |
| **Bob frequency** | `8.0f` hardcoded in `calculateBob()` | `config.bobFrequency` from JSON |
| **Recoil stiffness** | `200.0f` hardcoded in `updateRecoil()` | `config.recoilStiffness` from JSON |
| **Sway speed** | `1.2f` hardcoded in `calculateIdleSway()` | `config.swaySpeed` from JSON |
| **Per-weapon feel** | All weapons use the same constants | Each weapon has its own `WeaponEffectConfig` |
| **Mesh caching** | Ad-hoc, not in ResourceManager | `resources.storeMesh() / getMesh()` |
| **Recompile to tweak** | Yes, for any parameter change | No -- edit JSON, restart |
| **New .cpp files** | 0 | 0 (only modified existing files) |
| **New headers** | 0 | 2 (`animation_library.h`, `weapon_effect_config.h`) |
| **New data files** | 0 | 5 (3 animation JSONs, 2 config JSONs) |

---

## What We Accomplished

No new features. No new visual output. The game looks and feels exactly the same. Here is what changed underneath:

1. **Animations are data, not code.** `WeaponAnimation` keyframes live in JSON files loaded by `AnimationLibrary`. Adding a new animation means creating a new JSON file, not writing C++ code. Artists and designers can tweak timing and positions without a compiler. The `extern` globals and their scattered definitions are gone.

2. **Procedural effects are configurable per weapon.** The `WeaponEffectConfig` struct holds all bob, sway, and recoil parameters. Each weapon type has its own config loaded from JSON. A shotgun kicks harder than a pistol. A sniper rifle sways more than an SMG. These differences are expressed in data, not in code branches or duplicated functions.

3. **ResourceManager caches all asset types.** Textures, shaders, meshes, animations, and weapon configs all go through one system. Reference counting via `shared_ptr` for GPU resources ensures nothing is loaded twice and nothing is freed prematurely. The ad-hoc resource loading from earlier chapters is replaced with a consistent pipeline.

4. **The view model system is decoupled from data sources.** It does not know where animations come from (files, procedural generation, a network stream). It asks `AnimationLibrary` for an animation by name and gets a `const WeaponAnimation*` back. It asks `ResourceManager` for a config by name and gets a `const WeaponEffectConfig*` back. The sources can change without touching the system.

The pattern across all our cleanup chapters is consistent: identify hardcoded data, move it to external definitions; identify ad-hoc resource handling, route it through the cache; identify scattered code, consolidate it behind clean interfaces. Chapters 5a, 10a, 15a, 20a, and now 25a all follow this same discipline. Each pass, the codebase becomes more data-driven and more uniform.

---

## Exercises

1. **Reload animation.** Create a `shotgun_reload.json` animation file with 4-5 keyframes (weapon tilts, shells insert, weapon returns). Add `"reloadAnimation": "shotgun_reload"` to the shotgun config. Trigger it when the player presses R. This exercises the full pipeline: JSON data, AnimationLibrary lookup, state machine transition.

2. **Heavy weapon config.** Create a `minigun_effects.json` config with very low bob amplitude (heavy weapon barely moves), high sway (hard to hold steady), low recoil stiffness (slow spring recovery), and strong kick impulse. Attach it to a test entity and compare the feel against the shotgun. This exercises per-weapon differentiation through data alone.

3. **Runtime config reloading.** Add a debug key (F5) that calls `resources.getAnimationLibrary().clear()` followed by `loadDirectory()` again, and reloads all weapon effect configs. This lets you edit JSON files and see changes without restarting the game. This exercises the hot-reload workflow that data-driven design enables.

4. **Animation blending.** When transitioning from `Firing` back to `Idle`, the weapon snaps to the rest position. Implement a blend: store the last animation output, and over 0.1 seconds, interpolate from that output to the idle position using `MathUtils::smoothstep()`. This is a preview of more advanced animation concepts we will revisit in later chapters.

5. **Config validation.** Write a `validateWeaponEffectConfig()` function that checks for obviously wrong values (negative frequencies, zero stiffness, missing animation names) and prints warnings. Run it after loading each config. This exercises defensive programming for data-driven systems -- user-editable files will eventually contain errors.

---

*Next up: **Chapter 26 -- Boss Fights & Arenas**, where we will build a multi-phase boss encounter that combines combat, animation, and state management. The data-driven animation system from this chapter will let us define unique boss attack patterns without hardcoding them in C++.*
