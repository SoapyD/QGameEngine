#pragma once

#include <glm/glm.hpp>

#include <array>
#include <memory>

class Mesh;

// First-person weapon viewmodel state: the 7 gun meshes (shared with the weapon
// pickups) + colours, plus the small amount of animation state that persists
// across frames (bob phase, last-selected slot, and the switch-raise timer).
struct WeaponViewModel
{
    std::array<std::shared_ptr<Mesh>, 7> meshes;
    std::array<glm::vec3, 7>             colors;

    float bobPhase    = 0.0f; // accumulates for the idle/walk bob
    int   lastWeapon  = -1;   // to detect a switch
    float switchTimer = 0.0f; // counts down while the raise animation plays
};
