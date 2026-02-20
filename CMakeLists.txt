cmake_minimum_required(VERSION 3.20)
project(QEngine VERSION 0.1.0 LANGUAGES C CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 20)
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
# for now, just main.cpp. We'll add more files as we create them
add_executable(QEngine
	src/main.cpp
	src/engine/core/input_manager.cpp
	src/engine/core/resource_manager.cpp
	src/engine/core/window.cpp
	src/engine/ecs/scene_setup.cpp
	src/engine/ecs/systems/movement_system.cpp
	src/engine/ecs/systems/render_system.cpp
	src/engine/level/level_loader.cpp
	src/engine/renderer/camera.cpp	
	src/engine/renderer/mesh.cpp	
	src/engine/renderer/obj_loader.cpp
	src/engine/renderer/shader.cpp
	src/engine/renderer/stb_image_impl.cpp
	src/engine/renderer/texture.cpp
)

target_include_directories(QEngine PRIVATE src)

target_link_libraries(QEngine PRIVATE
	glfw
	glad
	glm
	entt
	stb
)