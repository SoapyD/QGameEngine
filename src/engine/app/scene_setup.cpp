#include "engine/app/scene_setup.h"
#include "engine/level/showcase_level.h"
#include "engine/level/spawn_scene.h"
#include "engine/level/showcase_descriptor.h"
#include "engine/ecs/components.h"
#include "engine/level/level.h"

#include <string>

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
    auto gridRed     = resources.getTexture("grid_red");
    auto cubeMesh    = resources.getMesh("cube");

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

    // ─── Scene entities: built from descriptors via classname dispatch ──
    // showcaseDescriptors() is the in-code stand-in for parsed .map data.
    // spawnScene runs a two-pass build (spawn all, then resolve door/lift/
    // teleporter target links) — before buildWorld's mover view, so movers
    // still get their kinematic bodies. The SpawnContext supplies the shared
    // cube handles and resolves texture names → GL ids.
    factories::SpawnContext ctx;
    ctx.assets = factories::MeshAssets{ cubeMesh->getVAO(), cubeMesh->getIndexCount(),
                                        litShader->getId() };
    ctx.texture = [&resources](std::string_view name)
    {
        return resources.getTexture(std::string(name))->getId();
    };
    factories::spawnScene(registry, ctx, showcaseDescriptors());

    // ─── Combat resources (registry context) ────────────────────
    auto& combatRes = registry.ctx().emplace<CombatResources>();
    combatRes.cubeVAO = cubeMesh->getVAO();
    combatRes.cubeIndexCount = cubeMesh->getIndexCount();
    combatRes.shaderId = litShader->getId();
    combatRes.projectileTextureId = gridRed->getId();    // red cubes for rockets
    combatRes.tracerTextureId = gridOrange->getId();      // orange lines for hitscan

    return level;
}
