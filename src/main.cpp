#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/fixed_timestep.h"
#include "engine/app/simulation.h"
#include "engine/ecs/components.h"
#include "engine/ecs/systems/debug_hud_system.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/physics/physics_config.h"
#include "engine/physics/jolt_world.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>
#include <iostream>

int main()
{
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

	// ─── Load resources ──────────────────────────────────────
	qengine::loadResources(resources);

	// ─── Camera ──────────────────────────────────────────────────
	Camera camera(glm::vec3(15.0f, 1.7f, 15.0f));

	// ─── ECS: Create the world ───────────────────────────────────
	entt::registry registry;

	auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();
	FixedTimestep fixedTimestep(physicsConfig.fixedDeltaTime);

	auto& joltWorld = registry.ctx().emplace<JoltWorld>();
	joltWorld.init();

	Level level = qengine::buildWorld(registry, resources, joltWorld);

	// ─── Game Loop ───────────────────────────────────────────────

	auto& hudConfig = registry.ctx().emplace<HudConfig>();
	hudConfig.shaderId = resources.getShader("hud")->getId();

	// enable depth testing (so closer things draw in front of further things)
	glEnable(GL_DEPTH_TEST);

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

		// mouse look - camera still handles this directly
		camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

		glm::vec3 front =camera.getFront();
		glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));

		// flatten to horizontal plane (don't fly when looking up/down)
		front.y = 0.0f;
		front = glm::normalize(front);
		right.y = 0.0f;
		right = glm::normalize(right);

		glm::vec3 wishDir(0.0f);
		if (input.isKeyPressed(GLFW_KEY_W)) wishDir += front;
		if (input.isKeyPressed(GLFW_KEY_S)) wishDir -= front;
		if (input.isKeyPressed(GLFW_KEY_A)) wishDir -= right;
		if (input.isKeyPressed(GLFW_KEY_D)) wishDir += right;

		// normalise to prevent faster diagonal movement
		if (glm::length(wishDir) > 0.0f)
			wishDir = glm::normalize(wishDir);

		// ─── Populate PlayerInput from GLFW ──────────────────────
		auto inputView = registry.view<PlayerInput>();
		for (auto [entity, playerInput] : inputView.each()) {
			playerInput.wishDir = wishDir;
			playerInput.jump = input.isKeyPressed(GLFW_KEY_SPACE);
			playerInput.fire = input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
			playerInput.weaponSwitch = -1;
			if (input.isKeyPressed(GLFW_KEY_1)) playerInput.weaponSwitch = 0;
			if (input.isKeyPressed(GLFW_KEY_2)) playerInput.weaponSwitch = 1;
		}

		// ─── Write camera direction into registry context ────── NEW
		registry.ctx().insert_or_assign<CameraDirection>(CameraDirection{camera.getFront()});

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
		auto playerView = registry.view<Position, AABBCollider, TagPlayer>();
		for (auto [entity, pos, col] : playerView.each())
		{
			glm::vec3 followPos = pos.value;
			if (registry.all_of<PrevPosition>(entity))
			{
				const glm::vec3& prev = registry.get<PrevPosition>(entity).value;
				if (glm::distance(prev, pos.value) < 3.0f)
					followPos = glm::mix(prev, pos.value, alpha);
			}
			// Camera sits near the top of the collider (eye height)
			glm::vec3 eyePos = followPos;
			eyePos.y += col.halfExtents.y * kEyeHeightFraction;
			camera.setPosition(eyePos);
		}

		// Write camera direction and position into registry context
		registry.ctx().insert_or_assign<CameraDirection>(CameraDirection{camera.getFront()});

		// ─── Render ──────────────────────────────────────────────
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
		renderSystem(registry, camera, aspectRatio, alpha); // draw everything
		debugHudSystem
		(
			registry, window.getWidth(), 
			window.getHeight(), currentFps
		);

		window.swapBuffers();

	}

	joltWorld.shutdown();
	resources.clear();
	return 0;
}