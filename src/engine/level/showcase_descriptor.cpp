#include "engine/level/showcase_descriptor.h"

// Mirrors scene_setup.cpp's factory calls 1:1. Geometric `size` is the full
// render extent (box colliders take half). Textures are names, resolved by the
// SpawnContext at spawn time. Doors/lifts/teleporters are linked by targetname.

using factories::SpawnParams;

std::vector<SpawnParams> showcaseDescriptors()
{
    std::vector<SpawnParams> d;

    // ─── Player ──────────────────────────────────────────────────
    d.push_back({
        .classname = "info_player_start",
        .origin = glm::vec3(15.0f, 1.7f, 15.0f),
    });

    // ─── Lighting ────────────────────────────────────────────────
    d.push_back({
        .classname = "light_environment",
        .props = { {"direction", "-0.2 -1.0 -0.3"}, {"_color", "1 1 1"}, {"ambient", "0.08"} },
    });

    // Bright ceiling lights (front/back halves) — wide range.
    d.push_back({
        .classname = "light", .origin = glm::vec3(15.0f, 5.5f, 10.0f),
        .props = { {"_color", "2 2 2"}, {"ambient", "0.05"}, {"linear", "0.09"},
                   {"quadratic", "0.032"}, {"marker", "grid_grey"} },
    });
    d.push_back({
        .classname = "light", .origin = glm::vec3(15.0f, 5.5f, 20.0f),
        .props = { {"_color", "2 2 2"}, {"ambient", "0.05"}, {"linear", "0.09"},
                   {"quadratic", "0.032"}, {"marker", "grid_grey"} },
    });

    // Coloured torches down the left wall — tight pools.
    d.push_back({
        .classname = "light", .origin = glm::vec3(3.0f, 2.0f, 10.0f),
        .props = { {"_color", "3.0 0.2 0.2"}, {"ambient", "0.01"}, {"linear", "0.35"},
                   {"quadratic", "0.44"}, {"marker", "grid_red"} },
    });
    d.push_back({
        .classname = "light", .origin = glm::vec3(3.0f, 2.0f, 15.0f),
        .props = { {"_color", "0.2 3.0 0.2"}, {"ambient", "0.01"}, {"linear", "0.35"},
                   {"quadratic", "0.44"}, {"marker", "grid_green"} },
    });
    d.push_back({
        .classname = "light", .origin = glm::vec3(3.0f, 2.0f, 20.0f),
        .props = { {"_color", "0.2 0.2 3.0"}, {"ambient", "0.01"}, {"linear", "0.35"},
                   {"quadratic", "0.44"}, {"marker", "grid_blue"} },
    });

    // ─── Physics demos ───────────────────────────────────────────
    // Shelf: raised static platform to slide cubes off (top surface at y=2).
    d.push_back({
        .classname = "func_static", .origin = glm::vec3(20.0f, 1.0f, 5.0f),
        .size = glm::vec3(4.0f, 2.0f, 4.0f),
        .props = { {"texture", "grid_blue"} },
    });

    // Cube 1: nudged off the shelf edge (resets every 6s).
    d.push_back({
        .classname = "prop_dynamic", .origin = glm::vec3(20.5f, 4.0f, 5.0f),
        .props = { {"velocity", "-6.0 0.0 0.0"}, {"interval", "6.0"}, {"texture", "grid_orange"} },
    });
    // Cube 2: pure gravity drop (resets every 4s).
    d.push_back({
        .classname = "prop_dynamic", .origin = glm::vec3(20.0f, 5.0f, 8.0f),
        .props = { {"velocity", "0 0 0"}, {"interval", "4.0"}, {"texture", "grid_orange"} },
    });
    // Cube 3: slides across the floor, low friction (resets every 5s).
    d.push_back({
        .classname = "prop_dynamic", .origin = glm::vec3(20.0f, 0.5f, 12.0f),
        .props = { {"velocity", "3.0 0.0 1.0"}, {"interval", "5.0"}, {"texture", "grid_orange"} },
    });

    // ─── Door (slides upward when the player approaches) ──────────
    d.push_back({
        .classname = "func_door", .origin = glm::vec3(25.0f, 1.5f, 15.0f),
        .size = glm::vec3(0.2f, 3.0f, 4.0f), .targetname = "door1",
        .props = { {"endpos", "25.0 4.5 15.0"}, {"speed", "3.0"}, {"wait", "4.0"},
                   {"startdelay", "0.0"}, {"texture", "grid_orange"} },
    });
    d.push_back({
        .classname = "trigger_multiple", .origin = glm::vec3(25.0f, 1.5f, 15.0f),
        .size = glm::vec3(4.0f, 3.0f, 5.0f), .target = "door1",
        .props = { {"cooldown", "1.0"} },
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(25.0f, 1.5f, 15.0f),
        .size = glm::vec3(4.0f, 3.0f, 5.0f),
        .props = { {"texture", "grid_green"} },
    });

    // ─── Lift (rises when the player steps on it) ─────────────────
    d.push_back({
        .classname = "func_plat", .origin = glm::vec3(10.0f, 0.2f, 25.0f),
        .size = glm::vec3(3.0f, 0.2f, 3.0f), .targetname = "lift1",
        .props = { {"endpos", "10.0 4.2 25.0"}, {"speed", "2.0"}, {"wait", "2.0"},
                   {"startdelay", "2.0"}, {"texture", "grid_green"} },
    });
    d.push_back({
        .classname = "trigger_multiple", .origin = glm::vec3(10.0f, 0.5f, 25.0f),
        .size = glm::vec3(3.0f, 0.6f, 3.0f), .target = "lift1",
        .props = { {"cooldown", "0.5"} },
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(10.0f, 0.5f, 25.0f),
        .size = glm::vec3(3.0f, 0.6f, 3.0f),
        .props = { {"texture", "grid_green"} },
    });

    // ─── Teleporter → far corner ─────────────────────────────────
    d.push_back({
        .classname = "trigger_teleport", .origin = glm::vec3(5.0f, 0.5f, 5.0f),
        .size = glm::vec3(2.0f, 3.0f, 2.0f), .target = "tp_dest1",
        .props = { {"cooldown", "1.0"} },
    });
    d.push_back({
        .classname = "info_teleport_destination", .origin = glm::vec3(25.0f, 1.0f, 25.0f),
        .targetname = "tp_dest1",
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(5.0f, 0.5f, 5.0f),
        .size = glm::vec3(2.0f, 3.0f, 2.0f),
        .props = { {"texture", "grid_green"} },
    });
    d.push_back({  // centre pole
        .classname = "func_decor", .origin = glm::vec3(5.0f, 1.5f, 5.0f),
        .size = glm::vec3(0.1f, 3.0f, 0.1f),
        .props = { {"texture", "grid_blue"} },
    });

    // ─── Lava pool (visible surface + damage zone, 25/sec, no cooldown) ──
    d.push_back({
        .classname = "func_decor", .origin = glm::vec3(20.0f, 0.1f, 25.0f),
        .size = glm::vec3(6.0f, 0.2f, 6.0f),
        .props = { {"texture", "grid_red"} },
    });
    d.push_back({
        .classname = "trigger_hurt", .origin = glm::vec3(20.0f, 0.5f, 25.0f),
        .size = glm::vec3(6.0f, 1.0f, 6.0f),
        .props = { {"dmg", "25.0"}, {"cooldown", "0.0"} },
    });
    d.push_back({
        .classname = "_wireframe", .origin = glm::vec3(20.0f, 0.5f, 25.0f),
        .size = glm::vec3(6.0f, 1.0f, 6.0f),
        .props = { {"texture", "grid_red"} },
    });

    // ─── Item pickups (demo placement around the room) ───────────
    // Health near the lava so you can heal after getting burned.
    d.push_back({ .classname = "item_health",  .origin = glm::vec3(16.0f, 1.0f, 23.0f) });
    // Shells out on the open floor between spawn and the shelf.
    d.push_back({ .classname = "item_shells",  .origin = glm::vec3(17.0f, 1.0f, 9.0f) });
    // Rockets by the door.
    d.push_back({ .classname = "item_rockets", .origin = glm::vec3(24.0f, 1.0f, 12.0f) });
    // Armour shard mid-room.
    d.push_back({ .classname = "item_armor",   .origin = glm::vec3(12.0f, 1.0f, 20.0f) });
    // The Nailgun (a weapon the player doesn't start with) by the torch wall,
    // with a bumped ammo grant.
    d.push_back({
        .classname = "weapon_nailgun", .origin = glm::vec3(5.0f, 1.0f, 15.0f),
        .props = { {"amount", "50"} },
    });

    // ─── Weapon rack: the rest of the arsenal (player starts with shotgun +
    //     rocket launcher; these make all 7 weapons reachable) ─────────────
    d.push_back({ .classname = "weapon_supershotgun",    .origin = glm::vec3(8.0f, 1.0f, 6.0f)  });
    d.push_back({ .classname = "weapon_railgun",         .origin = glm::vec3(8.0f, 1.0f, 10.0f) });
    d.push_back({ .classname = "weapon_lightninggun",    .origin = glm::vec3(8.0f, 1.0f, 14.0f) });
    d.push_back({ .classname = "weapon_grenadelauncher", .origin = glm::vec3(8.0f, 1.0f, 18.0f) });

    // Ammo for the pools the player doesn't start stocked in (nails, cells).
    d.push_back({ .classname = "item_nails", .origin = glm::vec3(11.0f, 1.0f, 8.0f)  });
    d.push_back({ .classname = "item_cells", .origin = glm::vec3(11.0f, 1.0f, 12.0f) });

    return d;
}
