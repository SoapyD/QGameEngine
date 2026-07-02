#pragma once

#include <entt/entt.hpp>

class Camera;

// Moves the camera to the player's eye position each frame, interpolating
// between the previous and current fixed-tick positions by `alpha` so motion is
// smooth above the 60 Hz tick rate. A large jump (teleport/respawn) snaps
// instead of lerping to avoid a streak across the room.
//
// Extracted from main.cpp's game loop. Runs once per frame, after the fixed
// ticks, with `alpha` = FixedTimestep::getAlpha().
void cameraFollowSystem(entt::registry& registry, Camera& camera, float alpha);
