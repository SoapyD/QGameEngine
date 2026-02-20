#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/ecs/components.h"
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/movement_system.h"
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

	// load the cube from the OBJ file we saved earlier
	auto cubeMesh = resources.getMesh("cube", "assets/models/cube.obj");

	// ─── Camera ──────────────────────────────────────────────────
	Camera camera(glm::vec3(0.0f, 1.7f, 3.0f));

	// ─── ECS: Create the world ───────────────────────────────────
	entt::registry registry;
	Level level = setupScene(registry, resources);
	
	// ─── Game Loop ───────────────────────────────────────────────
	float deltaTime = 0.0f;
	float lastFrame = 0.0f;

	// enable depth testing (so closer things draw in front of further things)
	glEnable(GL_DEPTH_TEST);

	while (!window.shouldClose())
	{
		float currentFrame = (float)glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		input.update();
		window.pollEvents();

		// ─── Input ───────────────────────────────────────────────
		if (input.isKeyPressed(GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window.getHandle(), true);

		if (input.isKeyPressed(GLFW_KEY_W))
			camera.processKeyboard(Camera::FORWARD, deltaTime);
		if (input.isKeyPressed(GLFW_KEY_S))
			camera.processKeyboard(Camera::BACKWARD, deltaTime);
		if (input.isKeyPressed(GLFW_KEY_A))
			camera.processKeyboard(Camera::LEFT, deltaTime);
		if (input.isKeyPressed(GLFW_KEY_D))
			camera.processKeyboard(Camera::RIGHT, deltaTime);

		camera.processMouse(input.getMouseXOffset(), input.getMouseYOffset());

		// ─── ECS Systems (tick order!) ───────────────────────────
		movementSystem(registry, deltaTime); // update positions
		// ... future systems go here ...

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