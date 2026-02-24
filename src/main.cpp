#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/fixed_timestep.h"
#include "engine/ecs/components.h"
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/systems/demo_reset_system.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/collision_system.h"
#include "engine/ecs/systems/movement_system.h"
#include "engine/ecs/systems/physics_system.h"
#include "engine/ecs/systems/trigger_system.h"
#include "engine/ecs/systems/mover_system.h"
#include "engine/physics/spatial_hash.h"
#include "engine/physics/physics_config.h"
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
	Camera camera(glm::vec3(10.0f, 1.7f, 3.0f));

	// ─── ECS: Create the world ───────────────────────────────────
	entt::registry registry;

	auto& physicsConfig = registry.ctx().emplace<PhysicsConfig>();
	Level level = setupScene(registry, resources);
	SpatialHash spatialHash(4.0f);

	// ─── Game Loop ───────────────────────────────────────────────
	FixedTimestep fixedTimestep(physicsConfig.fixedDeltaTime);

	// enable depth testing (so closer things draw in front of further things)
	glEnable(GL_DEPTH_TEST);

	while (!window.shouldClose())
	{
		fixedTimestep.accumulate((float)glfwGetTime());
		float frameTime = fixedTimestep.getFrameTime();

		input.update();
		window.pollEvents();

		// ─── Input ───────────────────────────────────────────────
		if (input.isKeyPressed(GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window.getHandle(), true);

		if (input.isKeyPressed(GLFW_KEY_W))
			camera.processKeyboard(Camera::FORWARD, frameTime);
		if (input.isKeyPressed(GLFW_KEY_S))
			camera.processKeyboard(Camera::BACKWARD, frameTime);
		if (input.isKeyPressed(GLFW_KEY_A))
			camera.processKeyboard(Camera::LEFT, frameTime);
		if (input.isKeyPressed(GLFW_KEY_D))
			camera.processKeyboard(Camera::RIGHT, frameTime);

		camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

		// Sync player entity position to camera
		auto playerView = registry.view<Position, TagPlayer>();
		for (auto [entity, pos] : playerView.each()) {
			pos.value = camera.getPosition();
		}

		// ─── ECS Systems (tick order!) ───────────────────────────
		while (fixedTimestep.step())
		{
			physicsSystem(registry);
			moverSystem(registry);                         // update doors, lifts
			collisionSystem(registry, spatialHash, level); // adjust velocities
			movementSystem(registry); // apply velocities to positions
			groundDetectionSystem(registry, level);    // update OnGround for next frame
			triggerSystem(registry);                       // detect trigger overlaps (after final position)
			demoResetSystem(registry);
		}

		// ─── Render ──────────────────────────────────────────────
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
		renderSystem(registry, camera, aspectRatio); // draw everything

		window.swapBuffers();

	}

	resources.clear();
	return 0;
}