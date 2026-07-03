#pragma once

#include <entt/entt.hpp>

// Per-tick enemy upkeep that isn't behaviour: fades each enemy's hit-flash
// timer (the brief white blink when shot), and cleans up enemies whose Health
// has reached zero — plays a death sound, removes the Jolt body, destroys the
// entity. Runs after combat has applied damage. (Chase/attack behaviour is a
// separate system — see the AI behaviour plan.)
void enemyDeathSystem(entt::registry& registry);
