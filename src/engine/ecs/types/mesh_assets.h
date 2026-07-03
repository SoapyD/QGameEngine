#pragma once
// Shared render handles for the showcase's cube-based factory entities.
// (Relocated from level/factories.h per CODING_STANDARD §2.)

#include <array>

namespace factories
{
    struct MeshAssets
    {
        unsigned int cubeVAO = 0;
        unsigned int cubeIndexCount = 0;
        unsigned int litShader = 0;
        // Per-weapon gun meshes (indexed by WeaponType) for weapon pickups.
        std::array<unsigned int, 7> gunVAO{};
        std::array<unsigned int, 7> gunIndexCount{};
    };
}
