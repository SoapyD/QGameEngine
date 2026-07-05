#include "engine/app/scene_setup_map.h"

#include "engine/core/resource_manager.h"
#include "engine/ecs/components.h"
#include "engine/level/build_textured_meshes.h"
#include "engine/level/map_loader.h"
#include "engine/level/map_to_descriptors.h"
#include "engine/level/map_to_level.h"
#include "engine/level/spawn_scene.h"

#include <iostream>
#include <string>
#include <utility>

Level setupSceneFromMap
(
    entt::registry& registry,
    const ResourceManager& resources,
    const std::string& mapPath,
    bool headless
)
{
    auto litShader  = resources.getShader("lit");
    auto gridOrange = resources.getTexture("grid_orange");
    auto gridRed    = resources.getTexture("grid_red");
    auto cubeMesh   = resources.getMesh("cube");

    // ─── Parse the .map and build level geometry (worldspawn brushes) ──
    std::string err;
    qmap::MapData map = qmap::loadMapFile(mapPath, &err);
    if (!err.empty())
        std::cerr << "[setupSceneFromMap] " << mapPath << ": " << err << "\n";

    Level level = mapWorldspawnToLevel(map);

    // ─── Render entities: one per texture group (needs a GL context) ──
    // The level owns each Mesh (renderMeshes); the MeshRenderer only references
    // its VAO, which ~Mesh would free — so the Mesh must outlive the entity.
    if (!headless && !level.sectors.empty())
    {
        for (auto& [texName, mesh] : buildTexturedMeshes(level.sectors[0]))
        {
            unsigned int texId = resources.getTexture(texName)->getId();

            auto sectorEntity = registry.create();
            registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));
            registry.emplace<MeshRenderer>
            (
                sectorEntity,
                mesh->getVAO(), 0u,
                litShader->getId(), texId,
                true, mesh->getIndexCount()
            );

            level.renderMeshes.push_back(std::move(mesh));
        }
    }

    // ─── Scene entities: parsed .map entities → SpawnParams → dispatch ──
    // Same SpawnContext + two-pass spawnScene the showcase uses.
    factories::SpawnContext ctx;
    ctx.assets = factories::MeshAssets{ cubeMesh->getVAO(), cubeMesh->getIndexCount(),
                                        litShader->getId() };
    for (int i = 0; i < 7; ++i)
    {
        auto gun = resources.getMesh("gun_" + std::to_string(i));
        ctx.assets.gunVAO[i] = gun->getVAO();
        ctx.assets.gunIndexCount[i] = gun->getIndexCount();
    }
    ctx.texture = [&resources](std::string_view name)
    {
        return resources.getTexture(std::string(name))->getId();
    };
    factories::spawnScene(registry, ctx, mapEntitiesToDescriptors(map));

    // ─── Combat resources (registry context) ────────────────────
    auto& combatRes = registry.ctx().emplace<CombatResources>();
    combatRes.cubeVAO = cubeMesh->getVAO();
    combatRes.cubeIndexCount = cubeMesh->getIndexCount();
    combatRes.shaderId = litShader->getId();
    combatRes.projectileTextureId = gridRed->getId();
    combatRes.tracerTextureId = gridOrange->getId();

    return level;
}
