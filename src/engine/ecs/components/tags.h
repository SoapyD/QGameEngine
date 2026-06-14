#pragma once

// Tags are empty structs — they mark entities without adding data.

struct TagPlayer {};
struct TagDebugWireframe {};

// Marks an entity that can activate TriggerVolumes (lava, teleporters, mover
// pads). Currently only the player carries it, but triggerSystem keys off this
// tag rather than TagPlayer so enemies/props can be made triggerable without
// touching the trigger logic.
struct TagTriggerable {};
