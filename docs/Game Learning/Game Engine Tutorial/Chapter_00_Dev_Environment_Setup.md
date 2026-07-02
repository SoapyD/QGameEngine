# Chapter 0: Dev Environment Setup

## What You'll Learn
- How C++ projects are compiled and linked
- How CMake manages the build process
- How to set up the project structure and dependencies for QEngine

---

## Prerequisites

Install the following before starting:

1. **A C++ compiler** — one of:
   - **MSVC** (comes with Visual Studio 2022, Community Edition is free)
   - **MinGW-w64** (GCC for Windows)
   - On Linux: `sudo apt install build-essential` (GCC) or install Clang

2. **CMake** (3.20 or later)
   - Download from https://cmake.org/download/
   - Or install via: `winget install Kitware.CMake` / `sudo apt install cmake`
   - Verify: `cmake --version`

3. **Git**
   - Download from https://git-scm.com/
   - Verify: `git --version`

4. **An IDE or editor** — recommended:
   - **Visual Studio 2022** (full IDE, built-in CMake support)
   - **VS Code** with the C/C++ and CMake Tools extensions
   - **CLion** (paid, excellent CMake integration)

---

## C++ Build Basics

Before touching game engines, you need to understand how C++ code becomes a running program. This section explains the pipeline.

### The Compilation Pipeline

```
Source Files (.cpp)
       │
       ▼
   Preprocessor     ← Handles #include, #define, #ifdef
       │
       ▼
    Compiler         ← Turns C++ into machine code (object files, .o / .obj)
       │
       ▼
     Linker          ← Combines object files + libraries into an executable
       │
       ▼
  Executable (.exe)
```

### Header Files vs Source Files

C++ splits code into two types of file:

- **Header files** (`.h` or `.hpp`) — declarations. "Here's what exists."
- **Source files** (`.cpp`) — definitions. "Here's how it works."

```cpp
// shader.h — DECLARATION
// Tells other files "this class exists and has these members"
#pragma once  // prevents this file being included twice

class Shader {
public:
    Shader(const char* vertexPath, const char* fragmentPath);
    void use();
private:
    unsigned int programId;
};
```

```cpp
// shader.cpp — DEFINITION
// The actual implementation
#include "shader.h"
#include <fstream>
#include <iostream>

Shader::Shader(const char* vertexPath, const char* fragmentPath) {
    // ... actual code to compile shaders
}

void Shader::use() {
    glUseProgram(programId);
}
```

**Why the split?** When `main.cpp` wants to use `Shader`, it only needs to `#include "shader.h"` to know the class exists. The compiler compiles `main.cpp` and `shader.cpp` separately, then the linker connects them.

### `#pragma once` vs Include Guards

Both prevent a header from being included twice (which causes "redefinition" errors):

```cpp
// Modern way (what we'll use)
#pragma once

// Old way (still common in existing code)
#ifndef SHADER_H
#define SHADER_H
// ... header contents ...
#endif
```

We'll use `#pragma once` throughout — it's simpler and supported by every modern compiler.

---

## Project Structure

Create the following directory structure. You can do this manually or via the terminal:

```bash
mkdir -p QEngine/{extern,assets/{shaders,textures,models,levels,sounds},src/{engine/{core,ecs/systems,renderer,physics,audio,network,level},game},tests}
```

The result:

```
QEngine/
├── CMakeLists.txt          ← We'll create this next
├── extern/                 ← Third-party libraries go here
├── assets/
│   ├── shaders/
│   ├── textures/
│   ├── models/
│   ├── levels/
│   └── sounds/
├── src/
│   ├── main.cpp            ← Entry point
│   ├── engine/
│   │   ├── core/
│   │   ├── ecs/
│   │   │   └── systems/
│   │   ├── renderer/
│   │   ├── physics/
│   │   ├── audio/
│   │   ├── network/
│   │   └── level/
│   └── game/
└── tests/
```

---

## Setting Up Dependencies

We need to get our libraries into the `extern/` folder. We'll add them as Git submodules so they're version-tracked.

### GLFW (Windowing & Input)

```bash
cd QEngine
git init
git submodule add https://github.com/glfw/glfw.git extern/glfw
```

### GLM (Math Library)

```bash
git submodule add https://github.com/g-truc/glm.git extern/glm
```

### EnTT (ECS Library)

```bash
git submodule add https://github.com/skypjack/entt.git extern/entt
```

### GLAD (OpenGL Loader)

GLAD is generated for your specific OpenGL version. Go to:
https://glad.dav1d.de/

Configure:
- Language: **C/C++**
- Specification: **OpenGL**
- API gl: **Version 4.6** (or 3.3 for maximum compatibility)
- Profile: **Core**
- Click **Generate**

Download and extract into `extern/glad/`. You should have:

```
extern/glad/
├── include/
│   ├── glad/
│   │   └── glad.h
│   └── KHR/
│       └── khrplatform.h
└── src/
    └── glad.c
```

### stb_image (Image Loading)

```bash
mkdir -p extern/stb
```

Download `stb_image.h` from https://github.com/nothings/stb and place it in `extern/stb/`.

---

## CMakeLists.txt

This is the build configuration file. CMake reads this to know how to compile your project.

Create `QEngine/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(QEngine VERSION 0.1.0 LANGUAGES C CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ──────────────────────────────────────────────
# GLFW — builds as a CMake subproject
# ──────────────────────────────────────────────
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/glfw)

# ──────────────────────────────────────────────
# GLAD — compiled as a small static library
# ──────────────────────────────────────────────
add_library(glad STATIC extern/glad/src/glad.c)
target_include_directories(glad PUBLIC extern/glad/include)

# ──────────────────────────────────────────────
# GLM — header-only, just needs include path
# ──────────────────────────────────────────────
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE extern/glm)

# ──────────────────────────────────────────────
# EnTT — header-only
# ──────────────────────────────────────────────
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE extern/entt/src)

# ──────────────────────────────────────────────
# stb — header-only (we'll define the implementation in one .cpp)
# ──────────────────────────────────────────────
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE extern/stb)

# ──────────────────────────────────────────────
# QEngine executable
# ──────────────────────────────────────────────
# For now, just main.cpp. We'll add more files as we create them.
add_executable(QEngine
    src/main.cpp
)

target_include_directories(QEngine PRIVATE src)

target_link_libraries(QEngine PRIVATE
    glfw
    glad
    glm
    entt
    stb
)
```

### What's Happening Here

- `cmake_minimum_required` — minimum CMake version needed
- `project()` — names the project and sets the language
- `set(CMAKE_CXX_STANDARD 17)` — we use C++17 features
- `add_subdirectory(extern/glfw)` — GLFW has its own CMakeLists.txt, this builds it as part of our project
- `add_library(glad STATIC ...)` — compiles GLAD's single `.c` file into a static library
- `add_library(glm INTERFACE)` — GLM is header-only, so `INTERFACE` means "no compiled code, just include paths"
- `add_executable(QEngine src/main.cpp)` — our program
- `target_link_libraries(...)` — connect everything together

---

## A Minimal main.cpp

Create `src/main.cpp` to verify everything compiles:

```cpp
#include <iostream>

int main() {
    std::cout << "QEngine starting..." << std::endl;

    // Verify GLFW links
    #include <GLFW/glfw3.h>  // normally at top of file — here just for test

    // Verify GLM links
    #include <glm/glm.hpp>

    // Verify EnTT links
    #include <entt/entt.hpp>

    std::cout << "All dependencies linked successfully!" << std::endl;
    return 0;
}
```

Actually — `#include` inside a function is bad practice. Here's the proper version:

```cpp
// src/main.cpp
#include <iostream>

// Third-party libraries — verify they're found
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

int main() {
    std::cout << "QEngine starting..." << std::endl;

    // Quick test: create a GLM vector
    glm::vec3 position(1.0f, 2.0f, 3.0f);
    std::cout << "Position: " << position.x << ", "
              << position.y << ", " << position.z << std::endl;

    // Quick test: create an EnTT registry
    entt::registry registry;
    auto entity = registry.create();
    std::cout << "Created entity: " << (uint32_t)entity << std::endl;

    std::cout << "All systems go!" << std::endl;
    return 0;
}
```

---

## Building the Project

### Option A: Command Line

```bash
cd QEngine

# Create a build directory (keeps compiled files separate from source)
mkdir build
cd build

# Generate build files
cmake ..

# Compile
cmake --build .
```

If successful, you'll find `QEngine.exe` (or `QEngine` on Linux) in the build directory.

### Option B: Visual Studio 2022

1. Open Visual Studio
2. Choose "Open a local folder"
3. Select the `QEngine` directory
4. VS will auto-detect the CMakeLists.txt
5. Select `QEngine.exe` as the startup item in the toolbar
6. Press F5 to build and run

### Option C: VS Code

1. Install the "CMake Tools" extension
2. Open the `QEngine` folder
3. Press `Ctrl+Shift+P` → "CMake: Configure"
4. Select your compiler (e.g. "Visual Studio Community 2022 Release - amd64")
5. Press `Ctrl+Shift+P` → "CMake: Build"
6. Press `Ctrl+Shift+P` → "CMake: Run Without Debugging"

---

## Expected Output

```
QEngine starting...
Position: 1, 2, 3
Created entity: 0
All systems go!
```

If you see this, your development environment is fully set up and all dependencies are linking correctly.

---

## Troubleshooting

**"GLFW/glfw3.h: No such file or directory"**
- The GLFW submodule didn't clone. Run: `git submodule update --init --recursive`

**"Cannot find -lglfw3"**
- On Linux you may need: `sudo apt install libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libwayland-dev libxkbcommon-dev`

**"glad.h: No such file or directory"**
- Check that `extern/glad/include/glad/glad.h` exists. Re-download from the GLAD generator if needed.

**CMake errors about C++17**
- Ensure your compiler supports C++17. MSVC 2017+, GCC 7+, and Clang 5+ all support it.

---

## What's Next

In **Chapter 1**, we'll replace the console output with an actual window — using GLFW to create an OpenGL context and get a blank screen rendering. That blank screen is the foundation everything else draws on.
