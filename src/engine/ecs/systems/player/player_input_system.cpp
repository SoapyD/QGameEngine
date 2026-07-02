#include "engine/ecs/systems/player/player_input_system.h"

#include "engine/ecs/components.h"
#include "engine/core/input_manager.h"
#include "engine/renderer/camera.h"

void playerInputSystem(entt::registry& registry, const InputManager& input, const Camera& camera)
{
    // Build movement basis from the camera, flattened to the horizontal plane
    // so looking up/down never makes the player fly.
    glm::vec3 front = camera.getFront();
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
    front.y = 0.0f;
    front = glm::normalize(front);
    right.y = 0.0f;
    right = glm::normalize(right);

    glm::vec3 wishDir(0.0f);
    if (input.isKeyPressed(GLFW_KEY_W)) wishDir += front;
    if (input.isKeyPressed(GLFW_KEY_S)) wishDir -= front;
    if (input.isKeyPressed(GLFW_KEY_A)) wishDir -= right;
    if (input.isKeyPressed(GLFW_KEY_D)) wishDir += right;

    // Normalise so diagonal movement isn't faster.
    if (glm::length(wishDir) > 0.0f)
        wishDir = glm::normalize(wishDir);

    for (auto [entity, playerInput] : registry.view<PlayerInput>().each())
    {
        playerInput.wishDir = wishDir;
        playerInput.jump = input.isKeyPressed(GLFW_KEY_SPACE);
        playerInput.fire = input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
        playerInput.weaponSwitch = -1; // -1 = no switch
        if (input.isKeyPressed(GLFW_KEY_1)) playerInput.weaponSwitch = 0;
        if (input.isKeyPressed(GLFW_KEY_2)) playerInput.weaponSwitch = 1;
        if (input.isKeyPressed(GLFW_KEY_3)) playerInput.weaponSwitch = 2;
        if (input.isKeyPressed(GLFW_KEY_4)) playerInput.weaponSwitch = 3;
        if (input.isKeyPressed(GLFW_KEY_5)) playerInput.weaponSwitch = 4;
        if (input.isKeyPressed(GLFW_KEY_6)) playerInput.weaponSwitch = 5;
        if (input.isKeyPressed(GLFW_KEY_7)) playerInput.weaponSwitch = 6;
    }

    // Publish the aim direction for combatSystem (reads it from context).
    registry.ctx().insert_or_assign<CameraDirection>(CameraDirection{ camera.getFront() });
}
