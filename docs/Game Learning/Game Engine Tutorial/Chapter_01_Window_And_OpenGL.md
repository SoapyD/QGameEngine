# Chapter 1: Window & OpenGL Context

## What You'll Learn
- How GLFW creates a window and an OpenGL context
- What a game loop is and why it exists
- How OpenGL renders to the screen (the swap chain)
- Handling keyboard input via callbacks
- Delta time — measuring frame duration

---

## The Game Loop

Every game in existence runs a variation of this:

```
while (game is running) {
    1. Process input     ← keyboard, mouse, controller
    2. Update game state ← move things, check collisions, run AI
    3. Render            ← draw everything to the screen
}
```

That's it. Every frame, forever, until the player quits. The entire engine we're building is just an increasingly sophisticated version of this loop.

---

## Creating a Window with GLFW

Replace the contents of `src/main.cpp` with:

```cpp
// src/main.cpp
#include <glad/glad.h>   // MUST be included before GLFW
#include <GLFW/glfw3.h>
#include <iostream>

// Called whenever the window is resized
void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

int main() {
    // ─── Initialise GLFW ─────────────────────────────────────────
    if (!glfwInit()) {
        std::cerr << "Failed to initialise GLFW" << std::endl;
        return -1;
    }

    // Tell GLFW we want OpenGL 4.6 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // ─── Create the window ───────────────────────────────────────
    GLFWwindow* window = glfwCreateWindow(1280, 720, "QEngine", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Make this window's OpenGL context current
    glfwMakeContextCurrent(window);

    // ─── Load OpenGL function pointers with GLAD ─────────────────
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialise GLAD" << std::endl;
        return -1;
    }

    // Set the viewport and register the resize callback
    glViewport(0, 0, 1280, 720);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // Print GPU info
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GPU: " << glGetString(GL_RENDERER) << std::endl;

    // ─── Game Loop ───────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        // Process input
        glfwPollEvents();

        // Close on Escape
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        // Clear the screen to dark grey
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Swap front and back buffers
        glfwSwapBuffers(window);
    }

    // ─── Cleanup ─────────────────────────────────────────────────
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

Build and run. You should see a dark grey window titled "QEngine" that closes when you press Escape.

---

## Breaking Down What Just Happened

### Include Order Matters

```cpp
#include <glad/glad.h>   // MUST come first
#include <GLFW/glfw3.h>  // After GLAD
```

GLAD defines the OpenGL function pointers. GLFW checks if they're already defined. If you flip the order, you get compilation errors. This is a classic OpenGL gotcha.

### Window Hints

```cpp
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
```

These tell GLFW what kind of OpenGL context we want **before** creating the window. "Core Profile" means we only get modern OpenGL — no deprecated legacy functions. This forces us to do things the right way.

> **Note:** If your GPU doesn't support OpenGL 4.6, change these to 3.3. Everything in this tutorial works on 3.3+.

### GLAD Loader

```cpp
gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
```

OpenGL is weird. The function pointers for OpenGL commands aren't available at link time — they're loaded at runtime from the GPU driver. GLAD handles this. After this call, all `gl*` functions (like `glClear`, `glViewport`) become usable.

### The Swap Chain (Double Buffering)

```cpp
glfwSwapBuffers(window);
```

The screen uses **double buffering**:
- **Back buffer** — where we draw (invisible to the user)
- **Front buffer** — what's currently displayed on screen

`glfwSwapBuffers` flips them. Without this, you'd see partial rendering (tearing) as pixels are drawn mid-frame.

```
Frame 1:  Draw to back ──▶ Swap ──▶ Back becomes front
Frame 2:  Draw to back ──▶ Swap ──▶ Back becomes front
...
```

### glClear

```cpp
glClearColor(0.1f, 0.1f, 0.1f, 1.0f);  // Set the clear colour (RGBA)
glClear(GL_COLOR_BUFFER_BIT);             // Actually clear
```

Every frame we wipe the back buffer clean before drawing. The colour values are 0.0 to 1.0 (not 0-255). `0.1, 0.1, 0.1` is dark grey.

---

## Adding Delta Time

Right now our loop runs as fast as possible. On a fast GPU, that might be thousands of frames per second. On a slow one, maybe 30. If we tie game logic to frame rate (e.g. "move 5 units per frame"), the game runs at different speeds on different machines.

**Delta time** solves this — it measures how long the last frame took, so we can say "move 5 units per **second**" instead.

Add this to `main.cpp`, just before the game loop:

```cpp
    float deltaTime = 0.0f;    // Time between current and last frame
    float lastFrame = 0.0f;    // Time of last frame

    while (!glfwWindowShouldClose(window)) {
        // ─── Calculate delta time ────────────────────────────────
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Process input
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        // Clear the screen
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
    }
```

`glfwGetTime()` returns seconds since GLFW was initialised. By subtracting last frame's time from the current time, we get the frame duration. A typical value at 60fps would be ~0.016 seconds.

Later, when we move things, we'll multiply speed by `deltaTime`:

```cpp
// Without delta time (broken — speed depends on frame rate):
position.x += 5.0f;

// With delta time (correct — always 5 units per second):
position.x += 5.0f * deltaTime;
```

---

## Callbacks — How GLFW Handles Events

GLFW uses **callbacks** — you give it a function pointer, and it calls your function when something happens. We already used one:

```cpp
// We wrote this function
void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// We told GLFW to call it when the window resizes
glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
```

### C++ Concept: Function Pointers

`framebufferSizeCallback` is the **name** of a function, but when used without `()`, it becomes a **pointer** to that function — an address in memory. GLFW stores that address and calls it later.

This is a pattern you'll see constantly in C and C++ libraries. The library says: "give me a function that matches this signature, and I'll call it when the event happens."

### Adding a Key Callback

Let's add a more structured key handler. Add this function above `main()`:

```cpp
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
        // Toggle fullscreen (we'll implement this properly later)
        std::cout << "F11 pressed — fullscreen toggle placeholder" << std::endl;
    }
}
```

Register it after creating the window:

```cpp
glfwSetKeyCallback(window, keyCallback);
```

Now you can remove the `glfwGetKey` escape check from the loop — the callback handles it.

The difference between `glfwGetKey` (polling) and the callback:

| `glfwGetKey` (polling) | Key callback |
|---|---|
| "Is this key down **right now**?" | "A key was just pressed/released" |
| Good for continuous actions (movement) | Good for one-shot actions (toggle fullscreen) |
| Called every frame | Called once per key event |

We'll use both throughout the engine. Movement will poll. Actions (jump, fire, menu) will use callbacks or per-frame edge detection.

---

## Extracting the Window Into Its Own Class

Our `main.cpp` is getting cluttered. Let's move the window logic into its own file. This is the first real C++ class we'll write for the engine.

### src/engine/core/window.h

```cpp
#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();

    GLFWwindow* getHandle() const { return m_window; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    GLFWwindow* m_window;
    int m_width;
    int m_height;

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
};
```

### src/engine/core/window.cpp

```cpp
#include "engine/core/window.h"
#include <iostream>

Window::Window(int width, int height, const std::string& title)
    : m_window(nullptr), m_width(width), m_height(height)
{
    if (!glfwInit()) {
        std::cerr << "Failed to initialise GLFW" << std::endl;
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialise GLAD" << std::endl;
        return;
    }

    glViewport(0, 0, width, height);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GPU: " << glGetString(GL_RENDERER) << std::endl;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::swapBuffers() {
    glfwSwapBuffers(m_window);
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}
```

### C++ Concepts: Classes

A few things to understand:

**Constructor (`Window::Window(...)`)** — runs when you create the object. We use an **initialiser list** (the `: m_window(nullptr), m_width(width), m_height(height)` part) to set member variables before the constructor body runs. This is more efficient than assigning them inside the body.

**Destructor (`Window::~Window()`)** — runs when the object is destroyed (goes out of scope or is deleted). We clean up GLFW resources here. This is C++'s version of "automatic cleanup" — no need to manually call a cleanup function.

**`const` member functions** — `shouldClose() const` means "this function doesn't modify the object." It's a promise to the compiler and to other programmers.

**`m_` prefix** — a convention meaning "member variable." Not required by C++, but widely used to distinguish members from local variables.

**`static` callback** — `framebufferSizeCallback` is `static` because GLFW callbacks are C-style function pointers — they can't be regular member functions (which have a hidden `this` parameter). Making it `static` removes the `this` requirement.

### Updated main.cpp

```cpp
// src/main.cpp
#include "engine/core/window.h"
#include <iostream>

int main() {
    Window window(1280, 720, "QEngine");

    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    while (!window.shouldClose()) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        window.pollEvents();

        if (glfwGetKey(window.getHandle(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window.getHandle(), true);
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        window.swapBuffers();
    }

    return 0;
}
```

### Update CMakeLists.txt

Add the new source file:

```cmake
add_executable(QEngine
    src/main.cpp
    src/engine/core/window.cpp
)
```

Build and run — same dark grey window, but now the code is organised.

---

## Understanding What We Built

```
main()
  │
  ├── Window(1280, 720, "QEngine")    ← Constructor: GLFW init, window, GLAD
  │
  ├── while (!window.shouldClose())   ← Game loop
  │   ├── Calculate deltaTime
  │   ├── window.pollEvents()         ← Process OS events
  │   ├── Check escape key
  │   ├── glClear()                   ← Clear screen
  │   └── window.swapBuffers()        ← Show the frame
  │
  └── return 0                         ← Window destructor runs: cleanup
```

This is the skeleton that every subsequent chapter builds on. The game loop is where all engine systems will eventually run.

---

## What's Next

In **Chapter 2**, we'll write our first OpenGL shaders — a vertex shader and a fragment shader — and render a coloured triangle. This will introduce the GPU rendering pipeline: how vertices go in one end and pixels come out the other.
