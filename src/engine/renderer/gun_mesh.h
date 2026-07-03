#pragma once

#include "engine/ecs/components/combat.h"   // WeaponType

#include <glm/glm.hpp>
#include <memory>

class Mesh;

// Procedurally build a rudimentary first-person gun mesh for a weapon type,
// composed from a few axis-aligned boxes (body / barrel(s) / grip) in gun-local
// space: origin at the grip, the barrel points toward -Z. Requires a live GL
// context (uploads to a VAO). Used by the weapon viewmodel.
std::shared_ptr<Mesh> buildGunMesh(WeaponType type);

// A distinct flat colour per weapon so they read apart at a glance.
glm::vec3 weaponColor(WeaponType type);

// A short 2-3 letter abbreviation for the HUD weapon bar (e.g. "SG", "RL").
const char* weaponAbbrev(WeaponType type);
