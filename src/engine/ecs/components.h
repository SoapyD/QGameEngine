#pragma once

// Umbrella header for all ECS components. The definitions were split out of this
// single file (it had grown past a few hundred lines) into focused headers under
// components/; this barrel keeps every `#include "engine/ecs/components.h"`
// working unchanged. Include a specific components/<group>.h directly if you
// only need one group.

#include "engine/ecs/components/core.h"       // config/context, input, spatial transforms
#include "engine/ecs/components/physics.h"    // Jolt links, colliders, movement, spawn
#include "engine/ecs/components/combat.h"     // weapons, ammo, projectiles
#include "engine/ecs/components/gameplay.h"   // health, movers, triggers, lifetimes
#include "engine/ecs/components/rendering.h"  // lights, mesh renderer, colour
#include "engine/ecs/components/tags.h"       // empty marker tags
