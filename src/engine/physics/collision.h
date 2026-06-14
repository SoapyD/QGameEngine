#pragma once

// ─── LEGACY / DEAD CODE — not compiled, kept for tutorial reference ──────────
// `collision.cpp` is NOT in the CMake build and this header has no live
// includer. The swept-AABB collision was replaced by Jolt (see
// docs/processes/physics.md → "Legacy & retained code"). Do not extend; the
// `.map`/Jolt path supersedes it.
// NOTE: `aabb.h` (below) is the opposite — it is LIVE (combat + trigger systems).
// ────────────────────────────────────────────────────────────────────────────

#include "engine/physics/aabb.h"

struct SweepResult
{
	float time; // 0.0 to 1.0 - how along the movement
	glm::vec3 normal; // surface normal of what was hit
	bool hit;
};

// test is a moving AABB hits a static AABB
SweepResult sweepAABB
(
	const AABB& moving, 
	const glm::vec3& velocity,
	const AABB& stationary
);

