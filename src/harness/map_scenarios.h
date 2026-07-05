#pragma once

#include <string>

// TrenchBroom `.map` headless scenarios, split out of headless_main.cpp so that
// file stays within its size budget as scenarios accrue. Each returns true on
// PASS (and prints a one-line result), matching the harness's other scenarios.
namespace mapscenarios
{
    // Parser check (step 2.1): parse an embedded Standard-format fixture and
    // assert the entities → brushes → faces structure + a malformed-input reject.
    bool scenarioMapParse();

    // Load an on-disk `.map` and report its parsed structure (entities, worldspawn
    // brushes, faces). Passes if it parses and holds a worldspawn + a player start.
    bool scenarioMapFile(const std::string& path);

    // Loader-conversion check (steps 2.2–2.3): parse smoke.map, build the Level
    // geometry + SpawnParams, and assert the MVP conversion (36 surfaces, player +
    // light descriptors). Pure data — no GL, registry, or physics.
    bool scenarioMapScene();
}
