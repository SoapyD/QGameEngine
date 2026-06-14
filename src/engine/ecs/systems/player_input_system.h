#pragma once

#include <entt/entt.hpp>

class InputManager;
class Camera;

// Maps raw keyboard/mouse state (via InputManager) and the camera's facing
// direction into the player's PlayerInput component, and publishes the aim
// direction (CameraDirection) to the registry context for the combat system.
//
// Extracted from main.cpp so the input→PlayerInput mapping lives alongside the
// other systems rather than inline in the game loop. Mouse-look itself stays in
// the windowed loop (it mutates the camera); this reads the resulting facing.
void playerInputSystem(entt::registry& registry, const InputManager& input, const Camera& camera);
