# C++ Concepts Introduced Per Chapter

Since a goal of this tutorial is learning C++ as you go, this tracks which C++ features are introduced in each chapter. Earlier chapters use simpler C++; complexity builds gradually.

---

## Chapter 0: Dev Environment Setup
- Compiling and linking
- Header files vs source files
- `#include` and include guards / `#pragma once`
- CMake basics (`add_executable`, `target_link_libraries`)

## Chapter 1: Window & OpenGL Context
- `int main()` and return codes
- Basic types (`int`, `float`, `bool`)
- `while` loops
- Function calls
- Pointers (GLFW uses `GLFWwindow*`)
- Callbacks (function pointers for GLFW input)

## Chapter 2: Shader System
- `std::string` and `std::ifstream` (reading shader files)
- Error handling with return values
- `const` and `const char*`
- Basic class/struct: the `Shader` class
- Constructor / destructor
- Member functions

## Chapter 3: ECS Foundation
- Templates (EnTT uses heavy template syntax like `registry.emplace<Position>(entity)`)
- Structs as pure data
- `auto` keyword and structured bindings (`auto [pos, vel] = ...`)
- Range-based for loops
- References (`&`)
- Header-only libraries

## Chapter 4: 3D Transforms & Camera
- Matrices and vectors (via GLM)
- Operator overloading (GLM overloads `*`, `+`, etc.)
- `glm::mat4`, `glm::vec3` — working with math library types
- `const&` parameters (passing large types efficiently)
- Namespaces (`glm::`)

## Chapter 5: Textures & Materials
- `unsigned char*` and raw memory (stb_image returns raw pixel data)
- `unsigned int` for OpenGL handles
- Enums / `enum class`

## Chapter 6: Mesh & Model Loading
- `std::vector` — dynamic arrays
- `std::unordered_map` — hash maps for resource caching
- File parsing (reading OBJ text format)
- `std::stringstream` for parsing
- Move semantics basics (moving large vertex arrays)

## Chapter 7: Lighting
- Uniform buffer objects (passing data to GPU)
- Multiple shader uniforms
- Normalising vectors (math, not C++)

## Chapter 8: Level Geometry & BSP
- Recursive data structures (BSP tree is a binary tree)
- `std::unique_ptr` — smart pointers for tree ownership
- Recursion
- `std::variant` or tagged unions (BSP node types)

## Chapter 9: Collision Detection
- `std::optional` (raycast may or may not hit)
- Spatial data structures (grids or hash maps)
- `std::pair` / `std::tuple`

## Chapter 10: Physics & Movement
- Fixed-point timestep accumulator pattern
- `constexpr` for compile-time constants
- Bitwise operations (collision layers/masks)

## Chapter 11: Doors, Lifts & Triggers
- State machines (enum + switch)
- Lambda functions (for trigger callbacks, if used)
- `std::function` (if storing callbacks)

## Chapter 12: Weapons & Projectiles
- Factory functions (creating preconfigured entities)
- `std::chrono` (cooldown timers) or manual float timers

## Chapter 13: Items & Pickups
- Component composition patterns (same C++ concepts, deeper ECS understanding)

## Chapter 14: Enemy AI
- Finite state machines (expanded)
- `std::queue` or `std::stack` (for pathfinding open/closed lists)
- Algorithms: A* pathfinding

## Chapter 15: HUD & UI
- Orthographic projection (2D rendering in a 3D engine)
- Text rendering concepts

## Chapter 16: Audio
- Raw audio buffers
- Threading concepts (audio often runs on its own thread)
- `std::thread` or library-managed threading

## Chapter 17: Networking Foundation
- Sockets and ports (abstracted by ENet)
- Serialisation — converting structs to bytes
- `memcpy`, `reinterpret_cast` (low-level byte manipulation)
- Byte order / endianness

## Chapter 18: State Synchronisation
- Ring buffers / circular buffers
- Bitwise delta compression
- `std::deque` (for snapshot history)

## Chapter 19: Client-Side Prediction
- Command queues
- State rewinding and replaying
- Deeper understanding of determinism

## Chapter 20: Particles & Polish
- Object pools (`std::array` with free list)
- Random number generation (`std::mt19937`, `std::uniform_real_distribution`)
- Interpolation functions (lerp, smoothstep)
