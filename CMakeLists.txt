cmake_minimum_required(VERSION 3.20)
project(QEngine VERSION 0.1.0 LANGUAGES C CXX)

# Use C++20
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
# miniaudio — single-header audio backend (downloaded at configure time)
# ──────────────────────────────────────────────
set(MINIAUDIO_DIR ${CMAKE_BINARY_DIR}/miniaudio)
if(NOT EXISTS ${MINIAUDIO_DIR}/miniaudio.h)
	message(STATUS "Downloading miniaudio.h ...")
	file(DOWNLOAD
		https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h
		${MINIAUDIO_DIR}/miniaudio.h
		STATUS MA_DL_STATUS)
	list(GET MA_DL_STATUS 0 MA_DL_CODE)
	if(NOT MA_DL_CODE EQUAL 0)
		message(FATAL_ERROR "Failed to download miniaudio.h (${MA_DL_STATUS}). "
			"Place miniaudio.h at ${MINIAUDIO_DIR}/ manually and re-configure.")
	endif()
endif()
# stb_vorbis — gives miniaudio OGG/Vorbis decoding (our music is .ogg).
if(NOT EXISTS ${MINIAUDIO_DIR}/stb_vorbis.c)
	message(STATUS "Downloading stb_vorbis.c ...")
	file(DOWNLOAD
		https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c
		${MINIAUDIO_DIR}/stb_vorbis.c
		STATUS SV_DL_STATUS)
	list(GET SV_DL_STATUS 0 SV_DL_CODE)
	if(NOT SV_DL_CODE EQUAL 0)
		message(FATAL_ERROR "Failed to download stb_vorbis.c (${SV_DL_STATUS}).")
	endif()
endif()
add_library(miniaudio STATIC src/engine/audio/miniaudio_impl.cpp)
target_include_directories(miniaudio PUBLIC ${MINIAUDIO_DIR})
if(WIN32)
	target_link_libraries(miniaudio PUBLIC ole32 winmm)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

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
	src/engine/ai/build_nav_grid.cpp
	src/engine/ai/find_path.cpp
	src/engine/audio/audio_engine.cpp
	src/engine/ecs/systems/audio/audio_system.cpp
	src/engine/core/input_manager.cpp
	src/engine/core/resource_manager.cpp
	src/engine/core/window.cpp
	src/engine/ecs/apply_damage.cpp
	src/engine/level/factories.cpp
	src/engine/level/spawn_monster.cpp
	src/engine/level/classname_factory.cpp
	src/engine/level/classname_factory_items.cpp
	src/engine/level/spawn_scene.cpp
	src/engine/level/showcase_descriptor.cpp
	src/engine/level/map_loader.cpp
	src/engine/level/map_to_level.cpp
	src/engine/level/map_to_descriptors.cpp
	src/engine/level/build_textured_meshes.cpp
	src/engine/app/scene_setup_map.cpp
	src/engine/physics/bodies/create_dynamic_body.cpp
	src/engine/physics/bodies/create_kinematic_body.cpp
	src/engine/physics/bodies/create_level_bodies.cpp
	src/engine/physics/bodies/create_sensor_body.cpp
	src/engine/physics/bodies/create_static_body.cpp
	src/engine/app/scene_setup.cpp
	src/engine/level/showcase_level.cpp
	src/engine/ecs/systems/player/camera_follow_system.cpp
	src/engine/ecs/systems/combat/apply_spread.cpp
	src/engine/ecs/systems/combat/box_hits_level.cpp
	src/engine/ecs/systems/combat/combat_system.cpp
	src/engine/ecs/systems/combat/fire_hitscan.cpp
	src/engine/ecs/systems/combat/fire_projectile.cpp
	src/engine/ecs/systems/combat/raycast_entities.cpp
	src/engine/ecs/systems/combat/spawn_tracer.cpp
	src/engine/ecs/systems/combat/splash_damage.cpp
	src/engine/ecs/systems/combat/update_projectiles.cpp
	src/engine/ecs/systems/debug_hud/debug_hud_system.cpp
	src/engine/ecs/systems/debug_hud/draw_ammo.cpp
	src/engine/ecs/systems/debug_hud/draw_bar.cpp
	src/engine/ecs/systems/debug_hud/draw_crosshair.cpp
	src/engine/ecs/systems/debug_hud/draw_hit_marker.cpp
	src/engine/ecs/systems/debug_hud/draw_damage_arc.cpp
	src/engine/ecs/systems/debug_hud/draw_weapon_bar.cpp
	src/engine/ecs/systems/debug_hud/draw_flash_overlay.cpp
	src/engine/ecs/systems/debug_hud/draw_panel.cpp
	src/engine/ecs/systems/debug_hud/draw_text.cpp
	src/engine/ecs/systems/player/player_input_system.cpp
	src/engine/ecs/systems/demo/demo_reset_system.cpp
	src/engine/ecs/systems/lifetime/lifetime_system.cpp
	src/engine/ecs/systems/sync/jolt_sync_system.cpp
	src/engine/ecs/systems/player/init_player_character.cpp
	src/engine/ecs/systems/player/player_character_system.cpp
	src/engine/ecs/systems/player/player_death_system.cpp
	src/engine/ecs/systems/sync/mover_sync_system.cpp
	src/engine/ecs/systems/mover/mover_system.cpp
	src/engine/ecs/systems/render/render_system.cpp
	src/engine/ecs/systems/trigger/trigger_system.cpp
	src/engine/ecs/systems/pickup/pickup_system.cpp
	src/engine/ecs/systems/enemy/ai_system.cpp
	src/engine/ecs/systems/enemy/ai_step_character.cpp
	src/engine/ecs/systems/enemy/ai_line_of_sight.cpp
	src/engine/ecs/systems/enemy/ai_fire_bolt.cpp
	src/engine/ecs/systems/enemy/enemy_death_system.cpp
	src/engine/ecs/systems/enemy/init_enemy_characters.cpp
	src/engine/ecs/systems/hud/hud_signal_system.cpp
	src/engine/level/build_sector_meshes.cpp
	src/engine/physics/jolt_world.cpp
	src/engine/physics/raycast/ray_intersect_aabb.cpp
	src/engine/physics/raycast/ray_intersect_triangle.cpp
	src/engine/renderer/camera.cpp
	src/engine/renderer/gun_mesh.cpp
	src/engine/renderer/create_weapon_viewmodel.cpp
	src/engine/renderer/render_weapon_viewmodel.cpp
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
	miniaudio
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
add_executable(QEngineHeadless src/harness/headless_main.cpp src/harness/map_scenarios.cpp)
target_link_libraries(QEngineHeadless PRIVATE qengine_lib)

if (MINGW)
	target_link_options(QEngineHeadless PRIVATE -static-libgcc -static-libstdc++ -static)
endif()