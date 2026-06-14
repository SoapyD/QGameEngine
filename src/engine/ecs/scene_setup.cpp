#include "engine/ecs/scene_setup.h"
#include "engine/ecs/factories.h"
#include "engine/ecs/showcase_level.h"
#include "engine/ecs/components.h"
#include "engine/level/level.h"

using factories::MeshAssets;

Level setupScene
(
	entt::registry& registry,
	const ResourceManager& resources,
	bool headless
)
{
    auto litShader   = resources.getShader("lit");
    auto gridGrey    = resources.getTexture("grid_grey");
    auto gridOrange  = resources.getTexture("grid_orange");
    auto gridBlue    = resources.getTexture("grid_blue");
    auto gridGreen   = resources.getTexture("grid_green");
    auto gridRed     = resources.getTexture("grid_red");
    auto cubeMesh    = resources.getMesh("cube");

    // Shared render handles passed to the cube-based factories.
    MeshAssets assets{ cubeMesh->getVAO(), cubeMesh->getIndexCount(), litShader->getId() };

    // ─── Showcase level geometry ────────────────────────────────
    Level level = createShowcaseLevel(headless);
    for (const auto& sector : level.sectors)
    {
        if (!sector.mesh) continue;

        auto sectorEntity = registry.create();
        registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));
        registry.emplace<MeshRenderer>
        (
            sectorEntity,
            sector.mesh->getVAO(), 0u,
            litShader->getId(), gridGrey->getId(),
            true, sector.mesh->getIndexCount()
        );
    }

    // ─── Player ─────────────────────────────────────────────────
    factories::spawnPlayer(registry, glm::vec3(15.0f, 1.7f, 15.0f));

    // ═══════════════════════════════════════════════════════════
    // LIGHTING
    // ═══════════════════════════════════════════════════════════
    factories::spawnDirectionalLight(registry,
        glm::vec3(-0.2f, -1.0f, -0.3f), glm::vec3(1.0f), 0.08f);

    // Bright ceiling lights (front and back halves of the room) — wide range.
    factories::spawnPointLight(registry, assets, glm::vec3(15.0f, 5.5f, 10.0f),
        glm::vec3(2.0f), 0.05f, 0.09f, 0.032f, gridGrey->getId());
    factories::spawnPointLight(registry, assets, glm::vec3(15.0f, 5.5f, 20.0f),
        glm::vec3(2.0f), 0.05f, 0.09f, 0.032f, gridGrey->getId());

    // Coloured torches down the left wall — tight pools.
    factories::spawnPointLight(registry, assets, glm::vec3(3.0f, 2.0f, 10.0f),
        glm::vec3(3.0f, 0.2f, 0.2f), 0.01f, 0.35f, 0.44f, gridRed->getId());
    factories::spawnPointLight(registry, assets, glm::vec3(3.0f, 2.0f, 15.0f),
        glm::vec3(0.2f, 3.0f, 0.2f), 0.01f, 0.35f, 0.44f, gridGreen->getId());
    factories::spawnPointLight(registry, assets, glm::vec3(3.0f, 2.0f, 20.0f),
        glm::vec3(0.2f, 0.2f, 3.0f), 0.01f, 0.35f, 0.44f, gridBlue->getId());

    // ═══════════════════════════════════════════════════════════
    // PHYSICS DEMOS
    // ═══════════════════════════════════════════════════════════
    // Shelf: raised static platform to slide cubes off (top surface at y=2).
    factories::spawnStaticBox(registry, assets, glm::vec3(20.0f, 1.0f, 5.0f),
        glm::vec3(4.0f, 2.0f, 4.0f), glm::vec3(2.0f, 1.0f, 2.0f), gridBlue->getId());

    // Cube 1: nudged off the shelf edge → falls to floor (resets every 6s).
    factories::spawnDemoCube(registry, assets, glm::vec3(20.5f, 4.0f, 5.0f),
        glm::vec3(-6.0f, 0.0f, 0.0f), 6.0f, gridOrange->getId());
    // Cube 2: pure gravity drop from near the ceiling (resets every 4s).
    factories::spawnDemoCube(registry, assets, glm::vec3(20.0f, 5.0f, 8.0f),
        glm::vec3(0.0f), 4.0f, gridOrange->getId());
    // Cube 3: slides across the floor, low friction (resets every 5s).
    factories::spawnDemoCube(registry, assets, glm::vec3(20.0f, 0.5f, 12.0f),
        glm::vec3(3.0f, 0.0f, 1.0f), 5.0f, gridOrange->getId());

    // ═══════════════════════════════════════════════════════════
    // DOORS, LIFTS, TRIGGERS
    // ═══════════════════════════════════════════════════════════
    // Door: slides upward when the player approaches.
    auto door = factories::spawnMover(registry, assets,
        glm::vec3(25.0f, 1.5f, 15.0f), glm::vec3(25.0f, 4.5f, 15.0f),
        glm::vec3(0.2f, 3.0f, 4.0f), glm::vec3(0.1f, 1.5f, 2.0f),
        3.0f, 4.0f, 0.0f, gridOrange->getId());
    factories::spawnTrigger(registry, glm::vec3(25.0f, 1.5f, 15.0f),
        glm::vec3(2.0f, 1.5f, 2.5f), TriggerAction::ActivateMover, door,
        glm::vec3(0.0f), 0.0f, 1.0f);
    factories::spawnDebugWireframe(registry, assets, glm::vec3(25.0f, 1.5f, 15.0f),
        glm::vec3(4.0f, 3.0f, 5.0f), gridGreen->getId());

    // Lift: rises when the player steps on it. Bottom y=0.2 so the body
    // (bottom at y=0.1) clears the floor body (top at y=0.1).
    auto lift = factories::spawnMover(registry, assets,
        glm::vec3(10.0f, 0.2f, 25.0f), glm::vec3(10.0f, 4.2f, 25.0f),
        glm::vec3(3.0f, 0.2f, 3.0f), glm::vec3(1.5f, 0.1f, 1.5f),
        2.0f, 2.0f, 2.0f, gridGreen->getId());
    factories::spawnTrigger(registry, glm::vec3(10.0f, 0.5f, 25.0f),
        glm::vec3(1.5f, 0.3f, 1.5f), TriggerAction::ActivateMover, lift,
        glm::vec3(0.0f), 0.0f, 0.5f);
    factories::spawnDebugWireframe(registry, assets, glm::vec3(10.0f, 0.5f, 25.0f),
        glm::vec3(3.0f, 0.6f, 3.0f), gridGreen->getId());

    // Teleporter → far corner.
    factories::spawnTrigger(registry, glm::vec3(5.0f, 0.5f, 5.0f),
        glm::vec3(1.0f, 1.5f, 1.0f), TriggerAction::Teleport, entt::null,
        glm::vec3(25.0f, 1.0f, 25.0f), 0.0f, 1.0f);
    factories::spawnDebugWireframe(registry, assets, glm::vec3(5.0f, 0.5f, 5.0f),
        glm::vec3(2.0f, 3.0f, 2.0f), gridGreen->getId());
    factories::spawnDecorBox(registry, assets, glm::vec3(5.0f, 1.5f, 5.0f),
        glm::vec3(0.1f, 3.0f, 0.1f), gridBlue->getId());  // centre pole

    // Lava pool: visible surface (y=0.1 so the top face sits at y=0.2) plus a
    // damage trigger dealing 25/sec with no cooldown.
    factories::spawnDecorBox(registry, assets, glm::vec3(20.0f, 0.1f, 25.0f),
        glm::vec3(6.0f, 0.2f, 6.0f), gridRed->getId());
    factories::spawnTrigger(registry, glm::vec3(20.0f, 0.5f, 25.0f),
        glm::vec3(3.0f, 0.5f, 3.0f), TriggerAction::Damage, entt::null,
        glm::vec3(0.0f), 25.0f, 0.0f);
    factories::spawnDebugWireframe(registry, assets, glm::vec3(20.0f, 0.5f, 25.0f),
        glm::vec3(6.0f, 1.0f, 6.0f), gridRed->getId());

    // ─── Combat resources (registry context) ────────────────────
    auto& combatRes = registry.ctx().emplace<CombatResources>();
    combatRes.cubeVAO = cubeMesh->getVAO();
    combatRes.cubeIndexCount = cubeMesh->getIndexCount();
    combatRes.shaderId = litShader->getId();
    combatRes.projectileTextureId = gridRed->getId();    // red cubes for rockets
    combatRes.tracerTextureId = gridOrange->getId();      // orange lines for hitscan

    return level;
}
