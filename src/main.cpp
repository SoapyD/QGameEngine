#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/fixed_timestep.h"
#include "engine/app/simulation.h"
#include "engine/ecs/components.h"
#include "engine/ecs/systems/debug_hud/debug_hud_system.h"
#include "engine/ecs/systems/render/render_system.h"
#include "engine/renderer/weapon_viewmodel.h"
#include "engine/ecs/systems/player/player_input_system.h"
#include "engine/ecs/systems/player/camera_follow_system.h"
#include "engine/ecs/systems/audio/audio_system.h"
#include "engine/audio/audio_engine.h"
#include "engine/physics/physics_config.h"
#include "engine/physics/jolt_world.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	std::string mapPath = (argc > 1) ? argv[1] : "assets/maps/smoke.map";  // ""/"showcase" → showcase
	if (mapPath == "showcase") mapPath.clear();
	// ─── Core systems ────────────────────────────────────────
	Window window(1280, 720, "QEngine");
	if (!window.isValid())
	{
		std::cerr << "Fatal: window/GL initialisation failed" << std::endl;
		return 1;
	}

	InputManager input;
	input.init(window.getHandle());

	ResourceManager resources;

	qengine::loadResources(resources);

	AudioEngine audio;
	audio.init("assets/sounds", "assets/sounds/manifest.json");

	Camera camera(glm::vec3(15.0f, 1.7f, 15.0f));

	// ─── ECS: Create the world ───────────────────────────────────
	entt::registry registry;

	auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();
	FixedTimestep fixedTimestep(physicsConfig.fixedDeltaTime);

	auto& joltWorld = registry.ctx().emplace<JoltWorld>();
	joltWorld.init(false, physicsConfig.gravity);

	Level level = qengine::buildWorld(registry, resources, joltWorld, false, mapPath);

	// First-person weapon viewmodel (needs a live GL context — build after load).
	WeaponViewModel weaponViewModel = createWeaponViewModel(resources);
	unsigned int litShaderId = resources.getShader("lit")->getId();

	audio.playMusic("music.exploration");  // loops

	// ─── Game Loop ───────────────────────────────────────────────

	auto& hudConfig = registry.ctx().emplace<HudConfig>();
	hudConfig.shaderId = resources.getShader("hud")->getId();

	// F1 toggles trigger-volume wireframes; default on (visible on load).
	registry.ctx().emplace<DebugRenderConfig>();
	bool prevToggleKey = false;

	glEnable(GL_DEPTH_TEST);  // closer fragments occlude further ones

	float fpsTimer = 0.0f;
	int frameCount = 0;
	float currentFps = 0.0f;

	while (!window.shouldClose())
	{
		fixedTimestep.accumulate((float)glfwGetTime());
		float frameTime = fixedTimestep.getFrameTime();
		
		input.update();
		window.pollEvents();

		// ─── FPS counter ─────────────────────────────────────────
		frameCount++;
		fpsTimer += frameTime;
		if (fpsTimer >= 1.0f)
		{
			currentFps = (float)frameCount / fpsTimer;
			frameCount = 0;
			fpsTimer = 0.0f;
		}

		// ─── Input ───────────────────────────────────────────────
		if (input.isKeyPressed(GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window.getHandle(), true);

		// F1 (edge-detected): toggle trigger-volume debug wireframes.
		bool toggleKey = input.isKeyPressed(GLFW_KEY_F1);
		if (toggleKey && !prevToggleKey)
			registry.ctx().get<DebugRenderConfig>().showTriggerVolumes ^= true;
		prevToggleKey = toggleKey;

		camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

		// ─── Map input → PlayerInput + publish aim direction ─────
		playerInputSystem(registry, input, camera);

		// ─── ECS Systems (tick order!) ───────────────────────────
		while (fixedTimestep.step())
		{
			// Snapshot positions so the renderer can interpolate (prev =
			// state at the start of this tick).
			for (auto [e, pos] : registry.view<Position>().each())
				registry.emplace_or_replace<PrevPosition>(e, pos.value);

			qengine::stepSimulation(registry, joltWorld, level, physicsConfig.fixedDeltaTime);
		}

		// Interpolation factor between the last two ticks for this frame.
		float alpha = fixedTimestep.getAlpha();

		// ─── Camera follows player body (interpolated) ───────────
		cameraFollowSystem(registry, camera, alpha);

		// ─── Audio: update listener + play queued sounds ─────────
		audio.setListener(camera.getPosition(), camera.getFront());
		audioSystem(registry, audio);

		// ─── Render ──────────────────────────────────────────────
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
		renderSystem(registry, camera, aspectRatio, alpha); // draw everything
		renderWeaponViewModel(weaponViewModel, registry, camera, aspectRatio,
			litShaderId, frameTime);                        // first-person gun
		debugHudSystem(registry, window.getWidth(), window.getHeight(), currentFps);

		window.swapBuffers();
	}
	audio.shutdown();
	// Destroy entities before shutdown — enemy ~CharacterVirtual removes its inner Jolt body via joltWorld.
	registry.clear();
	joltWorld.shutdown();
	resources.clear();
	return 0;
}