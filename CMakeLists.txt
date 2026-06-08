cmake_minimum_required(VERSION 3.20)
project(QEngine VERSION 0.1.0 LANGUAGES C CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
# Jolt Physics — fetched from GitHub
# ──────────────────────────────────────────────
include(FetchContent)

# jolt configuration - set BEFORE FetContent_MakeAvailable
set(PHYSICS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
	JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

# ──────────────────────────────────────────────
# qengine_lib — all engine code (everything except entry points)
# ──────────────────────────────────────────────
# Shared by the windowed game (QEngine) and the headless harness
# (QEngineHeadless) so both run the exact same systems.
add_library(qengine_lib STATIC
	src/engine/app/simulation.cpp
	src/engine/core/input_manager.cpp
	src/engine/core/resource_manager.cpp
	src/engine/core/window.cpp
	src/engine/ecs/jolt_body_helpers.cpp
	src/engine/ecs/scene_setup.cpp
	src/engine/ecs/showcase_level.cpp
	src/engine/ecs/systems/combat_system.cpp
	src/engine/ecs/systems/debug_hud_system.cpp
	src/engine/ecs/systems/demo_reset_system.cpp
	src/engine/ecs/systems/lifetime_system.cpp
	src/engine/ecs/systems/jolt_sync_system.cpp
	src/engine/ecs/systems/player_character_system.cpp
	src/engine/ecs/systems/player_death_system.cpp
	src/engine/ecs/systems/mover_sync_system.cpp
	src/engine/ecs/systems/mover_system.cpp
	src/engine/ecs/systems/render_system.cpp
	src/engine/ecs/systems/trigger_system.cpp
	src/engine/level/level_loader.cpp
	src/engine/physics/jolt_world.cpp
	src/engine/physics/raycast.cpp
	src/engine/renderer/camera.cpp
	src/engine/renderer/mesh.cpp
	src/engine/renderer/obj_loader.cpp
	src/engine/renderer/shader.cpp
	src/engine/renderer/stb_image_impl.cpp
	src/engine/renderer/texture.cpp
)

target_include_directories(qengine_lib PUBLIC src)

target_link_libraries(qengine_lib PUBLIC
	glfw
	glad
	glm
	entt
	stb
	Jolt
)

# ──────────────────────────────────────────────
# QEngine executable — windowed game entry point
# ──────────────────────────────────────────────
add_executable(QEngine src/main.cpp)
target_link_libraries(QEngine PRIVATE qengine_lib)

# Static-link the C++ runtime so we don't depend on DLLs at all
# (avoids UCRT64 / MINGW64 mismatch on MSYS2)
if (MINGW)
	target_link_options(QEngine PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# ──────────────────────────────────────────────
# QEngineHeadless — windowless harness / regression tests
# ──────────────────────────────────────────────
add_executable(QEngineHeadless src/harness/headless_main.cpp)
target_link_libraries(QEngineHeadless PRIVATE qengine_lib)

if (MINGW)
	target_link_options(QEngineHeadless PRIVATE -static-libgcc -static-libstdc++ -static)
endif()