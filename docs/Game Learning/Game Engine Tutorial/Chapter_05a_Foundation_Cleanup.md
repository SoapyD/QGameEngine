# Chapter 5a: Foundation Cleanup

## Time to Take Out the Trash

If you have been following along, congratulations -- you have a working 3D engine. You can render coloured triangles, textured quads, move a camera through the world, and you have an ECS driving the whole thing. That is legitimately impressive for five chapters of work.

But let us be honest: `main.cpp` is becoming a mess.

Right now it contains global mouse state, inline vertex data, raw OpenGL setup calls, entity creation, the game loop, and manual cleanup code. It is doing the job of at least five different systems. In the Quake days, Carmack could hold the entire codebase in his head. We are not Carmack. We need to organize.

This chapter adds **zero new features**. Instead, we are going to rip out the scaffolding and replace it with proper foundations. By the end, `main.cpp` will be short, readable, and ready for the real work ahead.

---

## What We Are Refactoring (and Why)

Here is what currently lives in `main.cpp` that should not:

| Problem | Current Location | New Home |
|---|---|---|
| Mouse state globals (`lastMouseX`, `firstMouse`, etc.) | Top of `main.cpp` | `InputManager` |
| Mouse callback function | Top of `main.cpp` | `InputManager` |
| Key polling (`glfwGetKey` calls) | Inside the game loop | `InputManager` |
| Inline vertex arrays and VAO/VBO setup | Middle of `main()` | `MeshFactory` |
| Shader and texture creation with no caching | Middle of `main()` | `ResourceManager` |
| Entity creation code | Middle of `main()` | `setupScene()` |
| Manual `glDelete` calls at the end | Bottom of `main()` | RAII handles it |

### Why bother?

Three reasons:

1. **Maintainability.** When something breaks, you want to look in one place, not scroll through 200 lines of `main.cpp` trying to find the relevant section.

2. **Bug prevention.** Those global mouse variables? Any function in the entire program can modify them. That is a bug waiting to happen. Encapsulation restricts access to the code that actually needs it.

3. **Scaling.** We are about to add lighting, model loading, audio, and more. If `main.cpp` already has 235 lines with three entities, imagine what it would look like with fifty. The answer is: unmaintainable.

---

## Before: The Current main.cpp

For reference, here is what `main.cpp` looks like right now, in its entirety:

```cpp
#include "engine/core/window.h"
#include "engine/ecs/components.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/movement_system.h"
#include "engine/renderer/camera.h"
#include "engine/renderer/shader.h"
#include "engine/renderer/texture.h"

#include <entt/entt.hpp>
#include <iostream>

// ─── Mouse state (temporary globals) ─────────────────────────────
float lastMouseX = 640.0f;
float lastMouseY = 360.0f;
float mouseXOffset = 0.0f;
float mouseYOffset = 0.0f;
bool firstMouse = true;

void mouseCallback(GLFWwindow* window, double xpos, double ypos)
{
	float x = static_cast<float>(xpos);
	float y = static_cast<float>(ypos);

	if (firstMouse)
	{
		lastMouseX = x;
		lastMouseY = y;
		firstMouse = false;
	}

	mouseXOffset = x - lastMouseX;
	mouseYOffset = lastMouseY - y; // reversed: y goes bottom-to-top in OpenGL
	lastMouseX = x;
	lastMouseY = y;
}


int main()
{
	Window window(1280, 720, "QEngine");

	glfwSetInputMode(window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPosCallback(window.getHandle(), mouseCallback);

	// ─── Shaders ──────────────────────────────────────────────────
	Shader basicShader(
		"assets/shaders/basic.vert",
		"assets/shaders/basic.frag"
	);

	Shader texturedShader(
		"assets/shaders/textured.vert",
		"assets/shaders/textured.frag"
	);

	// ─── Textures ────────────────────────────────────────────────
	Texture wallTexture("assets/textures/wall.png");

	// ─── Triangle vertex data ────────────────────────────────────
	float vertices[] =
	{
		// positions     // colours
		-0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
		 0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f
	};

	// ─── Quad vertex data (textured) ─────────────────────────────
	float quadVertices[] = {
		// Positions          // UV coords
		-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
		 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,

		-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
		 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
		-0.5f,  0.5f, 0.0f,  0.0f, 1.0f
	};

	// ─── Create VAO and VBO ──────────────────────────────────────
	unsigned int VAO, VBO;

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
		(void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	// ─── Create quad VAO and VBO ─────────────────────────────────
	unsigned int quadVAO, quadVBO;

	glGenVertexArrays(1, &quadVAO);
	glGenBuffers(1, &quadVBO);

	glBindVertexArray(quadVAO);

	glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
		(void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	// ─── Camera ──────────────────────────────────────────────────
	Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

	// ─── ECS: Create the world ───────────────────────────────────
	entt::registry registry;

	auto triangle = registry.create();
	registry.emplace<Position>(triangle, glm::vec3(0.0f, 0.0f, 0.0f));
	registry.emplace<MeshRenderer>(triangle, VAO, 3u, basicShader.getId());

	auto triangle2 = registry.create();
	registry.emplace<Position>(triangle2, glm::vec3(2.0f, 0.0f, -1.0f));
	registry.emplace<Rotation>(triangle2, glm::vec3(0.0f, 45.0f, 0.0f));
	registry.emplace<MeshRenderer>(triangle2, VAO, 3u, basicShader.getId());

	auto wall = registry.create();
	registry.emplace<Position>(wall, glm::vec3(0.0f, 0.0f, -2.0f));
	registry.emplace<MeshRenderer>(wall, quadVAO, 6u, texturedShader.getId(),
								wallTexture.getId(), false, 0u);

	// ─── Game Loop ───────────────────────────────────────────────
	float deltaTime = 0.0f;
	float lastFrame = 0.0f;

	glEnable(GL_DEPTH_TEST);

	while (!window.shouldClose())
	{
		float currentFrame = (float)glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		window.pollEvents();

		// ─── Input ───────────────────────────────────────────────
		if (glfwGetKey(window.getHandle(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(window.getHandle(), true);

		if (glfwGetKey(window.getHandle(), GLFW_KEY_W) == GLFW_PRESS)
			camera.processKeyboard(Camera::FORWARD, deltaTime);
		if (glfwGetKey(window.getHandle(), GLFW_KEY_S) == GLFW_PRESS)
			camera.processKeyboard(Camera::BACKWARD, deltaTime);
		if (glfwGetKey(window.getHandle(), GLFW_KEY_A) == GLFW_PRESS)
			camera.processKeyboard(Camera::LEFT, deltaTime);
		if (glfwGetKey(window.getHandle(), GLFW_KEY_D) == GLFW_PRESS)
			camera.processKeyboard(Camera::RIGHT, deltaTime);

		camera.processMouse(mouseXOffset, mouseYOffset);
		mouseXOffset = 0.0f;
		mouseYOffset = 0.0f;

		// ─── ECS Systems ─────────────────────────────────────────
		movementSystem(registry, deltaTime);

		// ─── Render ──────────────────────────────────────────────
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
		renderSystem(registry, camera, aspectRatio);

		window.swapBuffers();
	}

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteVertexArrays(1, &quadVAO);
	glDeleteBuffers(1, &quadVBO);
	return 0;
}
```

That is 150+ lines, and it is only going to get worse. Let us fix it.

---

## C++ Concept: Encapsulation

Before we start writing new classes, let us talk about **encapsulation** -- one of the core principles of object-oriented programming.

Encapsulation means bundling data together with the functions that operate on that data, and restricting direct access from outside code. You have already seen this with the `Window` class: the `GLFWwindow*` pointer is private, and the outside world interacts with it through methods like `shouldClose()` and `swapBuffers()`.

**Why does this matter?**

Consider our current mouse state. Right now, `mouseXOffset` and `mouseYOffset` are global variables. Any function in the entire program can read or modify them at any time. If a bug appears in mouse input, you have to search the entire codebase to find what might be touching those variables.

When we move this state into an `InputManager` class with private members, we know *exactly* what code can modify the mouse offsets: only the methods of `InputManager`. If the mouse input breaks, you look in one file. That is the power of encapsulation.

```cpp
// BAD: anyone can modify these at any time
float mouseXOffset = 0.0f;  // global
float mouseYOffset = 0.0f;  // global

// GOOD: access controlled through a class interface
class InputManager
{
public:
	float getMouseXOffset() const { return m_mouseXOffset; }
private:
	float m_mouseXOffset = 0.0f;  // only InputManager can modify this
};
```

---

## Step 1: InputManager

The `InputManager` will own all mouse state and provide clean key-query methods. No more globals, no more raw `glfwGetKey` calls scattered through the game loop.

### Why are we doing this?

Right now, if you want to check whether the W key is pressed, you write:

```cpp
if (glfwGetKey(window.getHandle(), GLFW_KEY_W) == GLFW_PRESS)
```

That is verbose and directly couples your game logic to GLFW. If we ever switch windowing libraries (unlikely but possible), or want to add input remapping, rebinding, or an input replay system, we would have to change every single key check. With an `InputManager`, we change it in one place.

The mouse callback is trickier. GLFW uses C-style function pointer callbacks, which means they cannot be regular member functions (member functions have a hidden `this` parameter). The standard solution is to use a **static member function** as the callback and store the `InputManager` instance pointer in GLFW's user pointer system, which is exactly what we will do.

### engine/core/input_manager.h

Create the file `src/engine/core/input_manager.h`:

```cpp
#pragma once

#include <GLFW/glfw3.h>

class InputManager
{
public:
	// Initialise the input manager and register GLFW callbacks
	void init(GLFWwindow* window);

	// Call once per frame to reset per-frame state (mouse deltas)
	void update();

	// ─── Key queries ─────────────────────────────────────────
	bool isKeyPressed(int key) const;
	bool isKeyReleased(int key) const;

	// ─── Mouse queries ───────────────────────────────────────
	float getMouseXOffset() const { return m_mouseXOffset; }
	float getMouseYOffset() const { return m_mouseYOffset; }
	float getMouseX() const { return m_lastMouseX; }
	float getMouseY() const { return m_lastMouseY; }

private:
	GLFWwindow* m_window = nullptr;

	// ─── Mouse state ─────────────────────────────────────────
	float m_lastMouseX = 640.0f;
	float m_lastMouseY = 360.0f;
	float m_mouseXOffset = 0.0f;
	float m_mouseYOffset = 0.0f;
	bool m_firstMouse = true;

	// GLFW callback — must be static (C function pointer requirement)
	static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
};
```

Notice that every piece of mouse state that was previously global is now a private member. The only way to read the mouse offsets is through `getMouseXOffset()` and `getMouseYOffset()`. The only way to modify them is through the GLFW callback, which is a private static function.

### engine/core/input_manager.cpp

Create the file `src/engine/core/input_manager.cpp`:

```cpp
#include "engine/core/input_manager.h"

void InputManager::init(GLFWwindow* window)
{
	m_window = window;

	// Capture the mouse cursor — essential for FPS-style camera
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Store a pointer to this InputManager inside the GLFW window.
	// This is how the static callback can find our instance.
	// GLFW provides this "user pointer" slot specifically for this purpose.
	glfwSetWindowUserPointer(window, this);

	// Register our static callback function
	glfwSetCursorPosCallback(window, mouseCallback);
}

void InputManager::update()
{
	// Reset per-frame mouse deltas.
	// If the mouse did not move this frame, the offsets should be zero,
	// not whatever they were last frame.
	m_mouseXOffset = 0.0f;
	m_mouseYOffset = 0.0f;
}

bool InputManager::isKeyPressed(int key) const
{
	return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool InputManager::isKeyReleased(int key) const
{
	return glfwGetKey(m_window, key) == GLFW_RELEASE;
}

// ─── Static callback ─────────────────────────────────────────────
// GLFW calls this whenever the mouse moves.
// We retrieve our InputManager instance from the user pointer.
void InputManager::mouseCallback(GLFWwindow* window, double xpos, double ypos)
{
	// Retrieve the InputManager instance we stored earlier
	auto* input = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
	if (!input) return;

	float x = static_cast<float>(xpos);
	float y = static_cast<float>(ypos);

	if (input->m_firstMouse)
	{
		input->m_lastMouseX = x;
		input->m_lastMouseY = y;
		input->m_firstMouse = false;
	}

	input->m_mouseXOffset = x - input->m_lastMouseX;
	input->m_mouseYOffset = input->m_lastMouseY - y; // reversed for OpenGL
	input->m_lastMouseX = x;
	input->m_lastMouseY = y;
}
```

**Key design decision:** We call `update()` at the beginning of each frame to reset mouse deltas. The GLFW callback writes new values whenever the mouse moves. If it does not move, the deltas stay at zero. This replaces the manual `mouseXOffset = 0.0f` reset we had in the game loop.

**About `glfwSetWindowUserPointer`:** GLFW lets you attach one `void*` to each window. We store `this` (our `InputManager` pointer) so the static callback can access instance data. This is the standard pattern for bridging C callbacks with C++ objects.

---

## C++ Concept: RAII (Resource Acquisition Is Initialization)

Before we build the next class, let us talk about **RAII** -- arguably the most important idiom in C++.

RAII means: when you create an object, it acquires the resources it needs (in the constructor). When the object is destroyed, it releases those resources (in the destructor). You have already been using this pattern -- `Window`, `Shader`, and `Texture` all clean up after themselves in their destructors.

But look at the bottom of our current `main.cpp`:

```cpp
glDeleteVertexArrays(1, &VAO);
glDeleteBuffers(1, &VBO);
glDeleteVertexArrays(1, &quadVAO);
glDeleteBuffers(1, &quadVBO);
```

This is manual cleanup. If we add a new mesh and forget to add the corresponding `glDelete` call, we leak GPU memory. If an exception is thrown before we reach these lines, we leak GPU memory. RAII eliminates both problems.

When our `MeshFactory` creates mesh data, the VAO and VBO handles will be stored in a struct. When we later build a proper `Mesh` class (in a future chapter), the destructor will handle cleanup. For now, our `MeshData` struct is a simple data holder, but the important thing is that we have centralized ownership -- the handles live in one known place, not scattered as local variables in `main()`.

---

## C++ Concept: std::shared_ptr

When multiple entities share the same texture (imagine ten walls all using `wall.png`), who owns that texture? Who is responsible for deleting it? If entity A deletes the texture and entity B tries to use it, you get a crash or corrupted rendering.

`std::shared_ptr` solves this with **reference counting**. Every time you copy a `shared_ptr`, the reference count increments. Every time a `shared_ptr` is destroyed, the count decrements. When it hits zero, the resource is freed.

```cpp
// Two entities share the same texture — neither "owns" it exclusively
std::shared_ptr<Texture> wallTex = std::make_shared<Texture>("assets/textures/wall.png");

// wallTex reference count: 1
auto copy = wallTex;    // reference count: 2
copy.reset();           // reference count: 1
wallTex.reset();        // reference count: 0 → Texture destructor runs
```

This is exactly what our `ResourceManager` will use. Load a texture once, hand out `shared_ptr`s, and the texture lives as long as anyone needs it.

---

## C++ Concept: std::unordered_map

A `std::unordered_map` is a hash table -- it maps keys to values with O(1) average lookup time. We will use `std::unordered_map<std::string, std::shared_ptr<Shader>>` to cache shaders by name.

```cpp
std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;

// First call: creates and caches the shader
auto shader = getShader("basic", "assets/shaders/basic.vert", "assets/shaders/basic.frag");

// Second call with same name: returns the cached version instantly
auto sameShader = getShader("basic", "assets/shaders/basic.vert", "assets/shaders/basic.frag");
// shader and sameShader point to the same Shader object
```

Why `unordered_map` instead of `map`? Because we do not need sorted keys. We just want fast lookup by name, and hash tables are faster than binary search trees for that.

---

## Step 2: ResourceManager

The `ResourceManager` caches shaders and textures so we never load the same asset twice. It also provides a single point of access for all loaded resources.

### Why are we doing this?

Right now, if you create two `Shader` objects with the same file paths, you compile the same shader twice and waste GPU memory storing two identical programs. With a `ResourceManager`, the second request returns the already-loaded shader.

More importantly, it gives us a central place to manage resource lifetimes. When we eventually need to unload a level and free its resources, we call one function instead of hunting through the code for every texture and shader.

### engine/core/resource_manager.h

Create the file `src/engine/core/resource_manager.h`:

```cpp
#pragma once

#include "engine/renderer/shader.h"
#include "engine/renderer/texture.h"

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

	// ─── Cleanup ─────────────────────────────────────────────
	// Drop all cached resources.
	// Actual GPU cleanup happens when the last shared_ptr is released.
	void clear();

private:
	std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;
	std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
};
```

Two overloads for each `get` function: one that loads (with file paths), and one that retrieves by name only. The loading overload checks the cache first and only creates a new resource if it does not already exist.

### engine/core/resource_manager.cpp

Create the file `src/engine/core/resource_manager.cpp`:

```cpp
#include "engine/core/resource_manager.h"

// ─── Shaders ─────────────────────────────────────────────────────

std::shared_ptr<Shader> ResourceManager::getShader(
	const std::string& name,
	const std::string& vertexPath,
	const std::string& fragmentPath)
{
	// Check if we already have this shader cached
	auto it = m_shaders.find(name);
	if (it != m_shaders.end())
	{
		return it->second;
	}

	// Not cached — load it, store it, return it
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
	// Check if we already have this texture cached
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}

	// Not cached — load it, store it, return it
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

// ─── Cleanup ─────────────────────────────────────────────────────

void ResourceManager::clear()
{
	m_shaders.clear();
	m_textures.clear();
	std::cout << "ResourceManager: all resources cleared" << std::endl;
}
```

**Note about `clear()`:** Calling `clear()` on the maps drops our references, but the actual GPU resources (shader programs, texture handles) are only freed when the last `shared_ptr` to each resource goes out of scope. If some entity component is still holding a `shared_ptr`, the resource stays alive. That is reference counting doing its job.

---

## Step 3: MeshFactory

The `MeshFactory` provides helper functions that create mesh data (VAO, VBO, vertex count) and return it as a simple struct. This gets all of that inline OpenGL setup code out of `main.cpp`.

### Why are we doing this?

Every time we wanted a new mesh shape in `main.cpp`, we were copying and pasting 20+ lines of `glGen` / `glBind` / `glBufferData` / `glVertexAttribPointer` calls. That is error-prone -- get one stride or offset wrong and you get silent rendering corruption. By centralizing mesh creation, we define each mesh layout once and reuse it cleanly.

### The MeshData Struct

First, we need a simple struct to hold the result of mesh creation:

```cpp
struct MeshData
{
	unsigned int vao = 0;
	unsigned int vbo = 0;
	unsigned int vertexCount = 0;
};
```

We keep the `vbo` handle around so we can clean it up later. The `vao` is what the renderer actually binds, and `vertexCount` tells `glDrawArrays` how many vertices to draw.

### engine/core/mesh_factory.h

Create the file `src/engine/core/mesh_factory.h`:

```cpp
#pragma once

#include <glad/glad.h>

// ─── MeshData ────────────────────────────────────────────────────
// Simple container for the GPU handles returned by mesh creation.
// Stores the VAO (for rendering), VBO (for cleanup), and vertex count.

struct MeshData
{
	unsigned int vao = 0;
	unsigned int vbo = 0;
	unsigned int vertexCount = 0;

	// Cleanup GPU resources. Call this when the mesh is no longer needed.
	void destroy()
	{
		if (vao) glDeleteVertexArrays(1, &vao);
		if (vbo) glDeleteBuffers(1, &vbo);
		vao = 0;
		vbo = 0;
	}
};

// ─── MeshFactory ─────────────────────────────────────────────────
// Free functions that create common mesh shapes.
// Each returns a MeshData with the GPU handles ready to use.

namespace MeshFactory
{
	// Create a coloured triangle (position + colour per vertex)
	// Layout: location 0 = vec3 position, location 1 = vec3 colour
	MeshData createTriangleMesh();

	// Create a textured quad (two triangles, position + UV per vertex)
	// Layout: location 0 = vec3 position, location 1 = vec2 texcoord
	MeshData createQuadMesh();
}
```

### engine/core/mesh_factory.cpp

Create the file `src/engine/core/mesh_factory.cpp`:

```cpp
#include "engine/core/mesh_factory.h"

namespace MeshFactory
{

MeshData createTriangleMesh()
{
	// ─── Vertex data: position (vec3) + colour (vec3) ────────
	float vertices[] =
	{
		// positions          // colours
		-0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // Bottom-left  (red)
		 0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // Bottom-right (green)
		 0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // Top          (blue)
	};

	MeshData mesh;
	mesh.vertexCount = 3;

	glGenVertexArrays(1, &mesh.vao);
	glGenBuffers(1, &mesh.vbo);

	glBindVertexArray(mesh.vao);

	glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	// Attribute 0: Position (3 floats)
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
		6 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Attribute 1: Colour (3 floats, offset by 3 floats)
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
		6 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	return mesh;
}

MeshData createQuadMesh()
{
	// ─── Vertex data: position (vec3) + UV (vec2) ────────────
	// Two triangles forming a quad
	float vertices[] =
	{
		// positions          // UV coords
		-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  // Bottom-left
		 0.5f, -0.5f, 0.0f,  1.0f, 0.0f,  // Bottom-right
		 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  // Top-right

		-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  // Bottom-left
		 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  // Top-right
		-0.5f,  0.5f, 0.0f,  0.0f, 1.0f   // Top-left
	};

	MeshData mesh;
	mesh.vertexCount = 6;

	glGenVertexArrays(1, &mesh.vao);
	glGenBuffers(1, &mesh.vbo);

	glBindVertexArray(mesh.vao);

	glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	// Attribute 0: Position (3 floats)
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
		5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Attribute 1: UV / Texcoord (2 floats, offset by 3 floats)
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
		5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	return mesh;
}

} // namespace MeshFactory
```

Compare this to the original `main.cpp`. The actual OpenGL calls are identical -- we have not changed any rendering logic. We have just moved the code into named functions so `main.cpp` can say `createTriangleMesh()` instead of 20 lines of raw GL calls.

---

## Step 4: Scene Setup Function

The last piece is extracting entity creation into its own function. Right now, creating three entities takes 12 lines in `main()`. When we have 20 or 50 entities, that becomes unmanageable. We will create a simple `setupScene()` function that takes the registry and the resources it needs.

### Why are we doing this?

Scene setup is logically separate from engine initialization. Your engine code (window, input, renderer) should not care what entities exist. By isolating scene creation, we prepare for a future where scenes can be loaded from files, swapped at runtime, or defined in a level editor.

For now, we will keep it simple: a free function in its own file.

### engine/ecs/scene_setup.h

Create the file `src/engine/ecs/scene_setup.h`:

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/core/mesh_factory.h"
#include "engine/renderer/shader.h"
#include "engine/renderer/texture.h"

#include <memory>

// Set up the initial scene entities.
// This replaces the inline entity creation that was in main().
void setupScene(
	entt::registry& registry,
	const MeshData& triangleMesh,
	const MeshData& quadMesh,
	std::shared_ptr<Shader> basicShader,
	std::shared_ptr<Shader> texturedShader,
	std::shared_ptr<Texture> wallTexture);
```

### engine/ecs/scene_setup.cpp

Create the file `src/engine/ecs/scene_setup.cpp`:

```cpp
#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"

void setupScene(
	entt::registry& registry,
	const MeshData& triangleMesh,
	const MeshData& quadMesh,
	std::shared_ptr<Shader> basicShader,
	std::shared_ptr<Shader> texturedShader,
	std::shared_ptr<Texture> wallTexture)
{
	// ─── Coloured triangle at the origin ─────────────────────
	auto triangle = registry.create();
	registry.emplace<Position>(triangle, glm::vec3(0.0f, 0.0f, 0.0f));
	registry.emplace<MeshRenderer>(triangle,
		triangleMesh.vao, triangleMesh.vertexCount, basicShader->getId());

	// ─── Second triangle, offset and rotated ─────────────────
	auto triangle2 = registry.create();
	registry.emplace<Position>(triangle2, glm::vec3(2.0f, 0.0f, -1.0f));
	registry.emplace<Rotation>(triangle2, glm::vec3(0.0f, 45.0f, 0.0f));
	registry.emplace<MeshRenderer>(triangle2,
		triangleMesh.vao, triangleMesh.vertexCount, basicShader->getId());

	// ─── Textured wall quad ──────────────────────────────────
	auto wall = registry.create();
	registry.emplace<Position>(wall, glm::vec3(0.0f, 0.0f, -2.0f));
	registry.emplace<MeshRenderer>(wall,
		quadMesh.vao, quadMesh.vertexCount, texturedShader->getId(),
		wallTexture->getId(), false, 0u);
}
```

Notice how this function does exactly what the old inline code did, but it is self-contained and clearly named. When you read `main.cpp` and see `setupScene(...)`, you know what it does without reading the implementation.

---

## Step 5: Updating CMakeLists.txt

We need to tell CMake about our new source files. Add the new `.cpp` files to the `add_executable` list:

```cmake
add_executable(QEngine
	src/main.cpp
	src/engine/core/input_manager.cpp
	src/engine/core/mesh_factory.cpp
	src/engine/core/resource_manager.cpp
	src/engine/core/window.cpp
	src/engine/ecs/scene_setup.cpp
	src/engine/ecs/systems/movement_system.cpp
	src/engine/ecs/systems/render_system.cpp
	src/engine/renderer/camera.cpp
	src/engine/renderer/shader.cpp
	src/engine/renderer/stb_image_impl.cpp
	src/engine/renderer/texture.cpp
)
```

---

## Step 6: The Clean main.cpp

Now for the payoff. Here is the refactored `main.cpp`:

```cpp
#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/mesh_factory.h"
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
		"assets/shaders/basic.frag");

	auto texturedShader = resources.getShader("textured",
		"assets/shaders/textured.vert",
		"assets/shaders/textured.frag");

	auto wallTexture = resources.getTexture("wall",
		"assets/textures/wall.png");

	// ─── Create meshes ───────────────────────────────────────
	MeshData triangleMesh = MeshFactory::createTriangleMesh();
	MeshData quadMesh = MeshFactory::createQuadMesh();

	// ─── Camera ──────────────────────────────────────────────
	Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

	// ─── ECS: Create the world ───────────────────────────────
	entt::registry registry;
	setupScene(registry, triangleMesh, quadMesh,
		basicShader, texturedShader, wallTexture);

	// ─── Game loop ───────────────────────────────────────────
	float deltaTime = 0.0f;
	float lastFrame = 0.0f;

	glEnable(GL_DEPTH_TEST);

	while (!window.shouldClose())
	{
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		input.update();
		window.pollEvents();

		// ─── Input ───────────────────────────────────────────
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

		// ─── ECS Systems ─────────────────────────────────────
		movementSystem(registry, deltaTime);

		// ─── Render ──────────────────────────────────────────
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float aspectRatio = static_cast<float>(window.getWidth())
			/ static_cast<float>(window.getHeight());
		renderSystem(registry, camera, aspectRatio);

		window.swapBuffers();
	}

	// ─── Cleanup ─────────────────────────────────────────────
	triangleMesh.destroy();
	quadMesh.destroy();
	resources.clear();

	return 0;
}
```

### What Changed?

Let us compare:

| Aspect | Before | After |
|---|---|---|
| **Global variables** | 5 mouse-state globals + callback function | Zero globals. `InputManager` owns everything |
| **Vertex data** | 30+ lines of inline arrays and GL calls | `MeshFactory::createTriangleMesh()` -- one line |
| **Resource loading** | Raw `Shader` and `Texture` construction | `ResourceManager` with caching and named lookup |
| **Entity creation** | 12 lines of inline registry calls | `setupScene()` -- one line |
| **Mouse offset reset** | Manual `= 0.0f` in the game loop | `input.update()` handles it |
| **Key queries** | `glfwGetKey(window.getHandle(), GLFW_KEY_W) == GLFW_PRESS` | `input.isKeyPressed(GLFW_KEY_W)` |
| **Cleanup** | Manual `glDelete` for each VAO/VBO | `MeshData::destroy()` + `resources.clear()` |
| **Total lines** | ~150 | ~80 |

The game loop is now almost entirely *intent* -- you can read it top to bottom and understand what it does without parsing raw OpenGL calls.

---

## New File Structure

After this chapter, your `src/` directory should look like this:

```
src/
  main.cpp
  engine/
    core/
      input_manager.h
      input_manager.cpp
      mesh_factory.h
      mesh_factory.cpp
      resource_manager.h
      resource_manager.cpp
      window.h
      window.cpp
    ecs/
      components.h
      scene_setup.h
      scene_setup.cpp
      systems/
        movement_system.h
        movement_system.cpp
        render_system.h
        render_system.cpp
    renderer/
      camera.h
      camera.cpp
      shader.h
      shader.cpp
      stb_image_impl.cpp
      texture.h
      texture.cpp
```

The `core/` directory is becoming the backbone of the engine. `InputManager`, `ResourceManager`, and `MeshFactory` are the kinds of systems that every game engine needs, regardless of what game you are building on top of it.

---

## Build and Test

Rebuild the project:

```bash
cmake --build build
```

Run it. You should see the exact same scene as before -- two coloured triangles and a textured wall quad. The camera should move with WASD and look around with the mouse. Nothing has changed in behaviour. Everything has changed in structure.

If something does not render, double-check:
1. The `MeshFactory` vertex data matches the original arrays exactly
2. The shader attribute locations (0 and 1) match your shader files
3. `setupScene()` is passing the correct shader and texture IDs
4. `input.update()` is called *before* you read mouse offsets (not after)

---

## What We Accomplished

No new features. No new visual output. And yet this might be the most important chapter so far.

We took a monolithic `main.cpp` and broke it into focused, single-responsibility modules:

- **InputManager** -- handles all input state and GLFW callbacks
- **ResourceManager** -- caches shaders and textures, prevents duplicate loads
- **MeshFactory** -- creates mesh geometry in clean, reusable functions
- **Scene setup** -- defines what entities exist, separate from engine code

We applied core C++ principles: **encapsulation** (private state with public interfaces), **RAII** (resources tied to object lifetimes), **smart pointers** (shared ownership without manual memory management), and **hash maps** (fast cached lookups).

The engine is now ready for real work. In the next chapter, when we add lighting, we will not be fighting `main.cpp` to do it. We will create a new shader, register it with the `ResourceManager`, create a mesh with `MeshFactory`, and set up entities in `setupScene()`. Each piece goes in its designated place.

That is what good architecture buys you: the ability to add complexity without adding chaos.

---

*Next up: Chapter 6 -- Lighting. We will implement Phong shading and watch our flat-coloured world come to life.*
