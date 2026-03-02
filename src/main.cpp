#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/fixed_timestep.h"
#include "engine/ecs/components.h"
#include "engine/ecs/jolt_body_helpers.h"
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/systems/combat_system.h"
#include "engine/ecs/systems/demo_reset_system.h"
#include "engine/ecs/systems/debug_hud_system.h"
#include "engine/ecs/systems/jolt_sync_system.h"
#include "engine/ecs/systems/lifetime_system.h"
#include "engine/ecs/systems/player_character_system.h"
#include "engine/ecs/systems/mover_sync_system.h"
#include "engine/ecs/systems/mover_system.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/trigger_system.h"
#include "engine/ecs/systems/weapon_switch_system.h"
#include "engine/physics/physics_config.h"
#include "engine/physics/jolt_world.h"
#include "engine/renderer/camera.h"

#include <entt/entt.hpp>

int main() 
{
	// ─── Core systems ────────────────────────────────────────
	Window window(1280, 720, "QEngine");

	InputManager input;
	input.init(window.getHandle());

	ResourceManager resources;

	// ─── Load resources ──────────────────────────────────────
	auto basicShader = resources.getShader("basic",
		"assets/shaders/basic.vert",
		"assets/shaders/basic.frag"
	);

	auto hudShader = resources.getShader("hud",
		"assets/shaders/hud.vert",
		"assets/shaders/hud.frag"
	);

	auto texturedShader = resources.getShader("textured",
		"assets/shaders/textured.vert",
		"assets/shaders/textured.frag"
	);

	auto litShader = resources.getShader("lit",
		"assets/shaders/lit.vert",
		"assets/shaders/lit.frag"
	);

	auto wallTexture = resources.getTexture("wall", "assets/textures/wall.png");
	auto gridGrey   = resources.getTexture("grid_grey",   "assets/textures/grid_grey.png");
	auto gridOrange = resources.getTexture("grid_orange", "assets/textures/grid_orange.png");
	auto gridBlue   = resources.getTexture("grid_blue",   "assets/textures/grid_blue.png");
	auto gridGreen = resources.getTexture("grid_green", "assets/textures/grid_green.png");
	auto gridRed   = resources.getTexture("grid_red",   "assets/textures/grid_red.png");

	// load the cube from the OBJ file we saved earlier
	auto cubeMesh = resources.getMesh("cube", "assets/models/cube.obj");

	// ─── Camera ──────────────────────────────────────────────────
	Camera camera(glm::vec3(15.0f, 1.7f, 15.0f));

	// ─── ECS: Create the world ───────────────────────────────────
	entt::registry registry;

	auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();
	FixedTimestep fixedTimestep(physicsConfig.fixedDeltaTime);

	auto& joltWorld = registry.ctx().emplace<JoltWorld>();
	joltWorld.init();

	Level level = setupScene(registry, resources);

	// create jolt bodies from the level geometry
	createLevelBodies(registry, level);

	joltWorld.physicsSystem->OptimizeBroadPhase();

	// Create Jolt bodies for movers (lifts, doors)
	auto moverView = registry.view<Position, AABBCollider, Mover>();
	for (auto [entity, pos, col, mover] : moverView.each())
	{
		createKinematicBody(registry, entity);
	}

	// Create sensor bodies for triggers
	auto triggerView = registry.view<Position, AABBCollider, TriggerVolume>();
	for (auto [entity, pos, col, trigger] : triggerView.each())
	{
		if (col.isTrigger)
		{
			createSensorBody(registry, entity);
		}
	}

	// Initialise the player's CharacterVirtual
	initPlayerCharacter(registry);

	// Re-optimise broad phase after adding more bodies
	joltWorld.physicsSystem->OptimizeBroadPhase();

	// ─── Game Loop ───────────────────────────────────────────────

	auto& HudeConfig = registry.ctx().emplace<HudConfig>();
	HudeConfig.shaderId = hudShader->getId();

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
			playerInput.fire = 
			(
				glfwGetMouseButton(window.getHandle(),
				GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS
			);
			playerInput.weaponSwitch = -1;
			if (input.isKeyPressed(GLFW_KEY_1)) playerInput.weaponSwitch = 0;
			if (input.isKeyPressed(GLFW_KEY_2)) playerInput.weaponSwitch = 1;
		}

		// ─── Write camera direction into registry context ────── NEW
		registry.ctx().insert_or_assign<glm::vec3>(camera.getFront());

		// ─── ECS Systems (tick order!) ───────────────────────────
		while (fixedTimestep.step())
		{
			weaponSwitchSystem(registry);
			playerCharacterSystem(registry);
			// velocity  
			moverSystem(registry);                         // update doors, lifts
			moverSyncSystem(registry);
			joltWorld.step(physicsConfig.fixedDeltaTime);
			joltSyncSystem(registry);
			// positions
			combatSystem(registry, level);
			lifetimeSystem(registry);
			triggerSystem(registry);                       // detect trigger overlaps (after final position)
			demoResetSystem(registry);
		}

		// ─── Camera follows player body ──────────────────────────
		auto playerView = registry.view<Position, AABBCollider, TagPlayer>();
		for (auto [entity, pos, col] : playerView.each())
		{
			// Camera sits near the top of the collider (eye height)
			glm::vec3 eyePos = pos.value;
			eyePos.y += col.halfExtents.y * 0.7f;  // 70% up from centre
			camera.setPosition(eyePos);
		}

		// Write camera direction and position into registry context
		registry.ctx().insert_or_assign<glm::vec3>(camera.getFront());

		// ─── Render ──────────────────────────────────────────────
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
		renderSystem(registry, camera, aspectRatio); // draw everything
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