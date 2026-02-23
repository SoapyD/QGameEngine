#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"
#include "engine/level/level.h"
#include "engine/level/level_loader.h"

// ─── Helper: build a test level programmatically ─────────────────
static Level createShowcaseLevel()
{
    Level level;

    // ═══════════════════════════════════════════════════════════
    // Sector 0: MAIN HALL (20 x 12 x 4)
    //   Origin: (0, 0, 0) to (20, 4, 12)
    //   Doorways:
    //     - Left wall (x=0): opening from z=4 to z=8 (to Light Room)
    //     - Front wall (z=12): opening from x=12 to x=16 (to Physics Lab)
    // ═══════════════════════════════════════════════════════════
    {
        Sector hall;
        hall.id = 0;
        hall.boundsMin = glm::vec3(0.0f, 0.0f, 0.0f);
        hall.boundsMax = glm::vec3(20.0f, 4.0f, 12.0f);

        // Floor
        hall.surfaces.push_back({
            {glm::vec3(0,0,12), glm::vec3(20,0,12), glm::vec3(20,0,0), glm::vec3(0,0,0)},
            glm::vec3(0, 1, 0),
            "grid_grey.png", 0
        });

        // Ceiling
        hall.surfaces.push_back({
            {glm::vec3(0,4,0), glm::vec3(20,4,0), glm::vec3(20,4,12), glm::vec3(0,4,12)},
            glm::vec3(0, -1, 0),
            "grid_grey.png", 0
        });

        // Back wall (z=0, full width, normal +z)
        hall.surfaces.push_back({
            {glm::vec3(20,4,0), glm::vec3(0,4,0), glm::vec3(0,0,0), glm::vec3(20,0,0)},
            glm::vec3(0, 0, 1),
            "grid_orange.png", 0
        });

        // Front wall (z=12, normal -z)
        // Split into two sections with a doorway gap at x=12 to x=16
        // Left section: x=0 to x=12
        hall.surfaces.push_back({
            {glm::vec3(0,4,12), glm::vec3(12,4,12), glm::vec3(12,0,12), glm::vec3(0,0,12)},
            glm::vec3(0, 0, -1),
            "grid_orange.png", 0
        });
        // Right section: x=16 to x=20
        hall.surfaces.push_back({
            {glm::vec3(16,4,12), glm::vec3(20,4,12), glm::vec3(20,0,12), glm::vec3(16,0,12)},
            glm::vec3(0, 0, -1),
            "grid_orange.png", 0
        });
        // Above doorway: x=12 to x=16, y=3 to y=4
        hall.surfaces.push_back({
            {glm::vec3(12,4,12), glm::vec3(16,4,12), glm::vec3(16,3,12), glm::vec3(12,3,12)},
            glm::vec3(0, 0, -1),
            "grid_orange.png", 0
        });

        // Left wall (x=0, normal +x)
        // Split with doorway from z=4 to z=8
        // Bottom section: z=0 to z=4
        hall.surfaces.push_back({
            {glm::vec3(0,4,0), glm::vec3(0,4,4), glm::vec3(0,0,4), glm::vec3(0,0,0)},
            glm::vec3(1, 0, 0),
            "grid_orange.png", 0
        });
        // Top section: z=8 to z=12
        hall.surfaces.push_back({
            {glm::vec3(0,4,8), glm::vec3(0,4,12), glm::vec3(0,0,12), glm::vec3(0,0,8)},
            glm::vec3(1, 0, 0),
            "grid_orange.png", 0
        });
        // Above doorway: z=4 to z=8, y=3 to y=4
        hall.surfaces.push_back({
            {glm::vec3(0,4,4), glm::vec3(0,4,8), glm::vec3(0,3,8), glm::vec3(0,3,4)},
            glm::vec3(1, 0, 0),
            "grid_orange.png", 0
        });

        // Right wall (x=20, full, normal -x)
        hall.surfaces.push_back({
            {glm::vec3(20,4,12), glm::vec3(20,4,0), glm::vec3(20,0,0), glm::vec3(20,0,12)},
            glm::vec3(-1, 0, 0),
            "grid_orange.png", 0
        });

        level.sectors.push_back(std::move(hall));
    }

    // ═══════════════════════════════════════════════════════════
    // Sector 1: LIGHT ROOM (8 x 8 x 4)
    //   Position: x = -8 to 0, z = 2 to 10, y = 0 to 4
    //   Doorway: right wall (x=0) from z=4 to z=8, matching hall
    // ═══════════════════════════════════════════════════════════
    {
        Sector lightRoom;
        lightRoom.id = 1;
        lightRoom.boundsMin = glm::vec3(-8.0f, 0.0f, 2.0f);
        lightRoom.boundsMax = glm::vec3(0.0f, 4.0f, 10.0f);

        // Floor
        lightRoom.surfaces.push_back({
            {glm::vec3(-8,0,10), glm::vec3(0,0,10), glm::vec3(0,0,2), glm::vec3(-8,0,2)},
            glm::vec3(0, 1, 0),
            "grid_grey.png", 1
        });

        // Ceiling
        lightRoom.surfaces.push_back({
            {glm::vec3(-8,4,2), glm::vec3(0,4,2), glm::vec3(0,4,10), glm::vec3(-8,4,10)},
            glm::vec3(0, -1, 0),
            "grid_grey.png", 1
        });

        // Back wall (z=2, normal +z)
        lightRoom.surfaces.push_back({
            {glm::vec3(0,4,2), glm::vec3(-8,4,2), glm::vec3(-8,0,2), glm::vec3(0,0,2)},
            glm::vec3(0, 0, 1),
            "grid_blue.png", 1
        });

        // Front wall (z=10, normal -z)
        lightRoom.surfaces.push_back({
            {glm::vec3(-8,4,10), glm::vec3(0,4,10), glm::vec3(0,0,10), glm::vec3(-8,0,10)},
            glm::vec3(0, 0, -1),
            "grid_blue.png", 1
        });

        // Left wall (x=-8, normal +x)
        lightRoom.surfaces.push_back({
            {glm::vec3(-8,4,2), glm::vec3(-8,4,10), glm::vec3(-8,0,10), glm::vec3(-8,0,2)},
            glm::vec3(1, 0, 0),
            "grid_blue.png", 1
        });

        // Right wall (x=0, normal -x)
        // Doorway from z=4 to z=8 (matching the hall's left wall opening)
        // Bottom section: z=2 to z=4
        lightRoom.surfaces.push_back({
            {glm::vec3(0,4,2), glm::vec3(0,4,4), glm::vec3(0,0,4), glm::vec3(0,0,2)},
            glm::vec3(-1, 0, 0),
            "grid_blue.png", 1
        });
        // Top section: z=8 to z=10
        lightRoom.surfaces.push_back({
            {glm::vec3(0,4,8), glm::vec3(0,4,10), glm::vec3(0,0,10), glm::vec3(0,0,8)},
            glm::vec3(-1, 0, 0),
            "grid_blue.png", 1
        });
        // Above doorway: z=4 to z=8, y=3 to y=4
        lightRoom.surfaces.push_back({
            {glm::vec3(0,4,4), glm::vec3(0,4,8), glm::vec3(0,3,8), glm::vec3(0,3,4)},
            glm::vec3(-1, 0, 0),
            "grid_blue.png", 1
        });

        level.sectors.push_back(std::move(lightRoom));
    }

    // ═══════════════════════════════════════════════════════════
    // Sector 2: PHYSICS LAB (10 x 10 x 4)
    //   Position: x = 12 to 22, z = 12 to 22, y = 0 to 4
    //   Doorway: back wall (z=12) from x=12 to x=16, matching hall
    // ═══════════════════════════════════════════════════════════
    {
        Sector physLab;
        physLab.id = 2;
        physLab.boundsMin = glm::vec3(12.0f, 0.0f, 12.0f);
        physLab.boundsMax = glm::vec3(22.0f, 4.0f, 22.0f);

        // Floor
        physLab.surfaces.push_back({
            {glm::vec3(12,0,22), glm::vec3(22,0,22), glm::vec3(22,0,12), glm::vec3(12,0,12)},
            glm::vec3(0, 1, 0),
            "grid_grey.png", 2
        });

        // Ceiling
        physLab.surfaces.push_back({
            {glm::vec3(12,4,12), glm::vec3(22,4,12), glm::vec3(22,4,22), glm::vec3(12,4,22)},
            glm::vec3(0, -1, 0),
            "grid_grey.png", 2
        });

        // Back wall (z=12, normal +z)
        // Doorway from x=12 to x=16 (matching hall)
        // Right section only: x=16 to x=22
        physLab.surfaces.push_back({
            {glm::vec3(22,4,12), glm::vec3(16,4,12), glm::vec3(16,0,12), glm::vec3(22,0,12)},
            glm::vec3(0, 0, 1),
            "grid_orange.png", 2
        });
        // Above doorway: x=12 to x=16, y=3 to y=4
        physLab.surfaces.push_back({
            {glm::vec3(16,4,12), glm::vec3(12,4,12), glm::vec3(12,3,12), glm::vec3(16,3,12)},
            glm::vec3(0, 0, 1),
            "grid_orange.png", 2
        });

        // Front wall (z=22, normal -z)
        physLab.surfaces.push_back({
            {glm::vec3(12,4,22), glm::vec3(22,4,22), glm::vec3(22,0,22), glm::vec3(12,0,22)},
            glm::vec3(0, 0, -1),
            "grid_orange.png", 2
        });

        // Left wall (x=12, normal +x)
        physLab.surfaces.push_back({
            {glm::vec3(12,4,12), glm::vec3(12,4,22), glm::vec3(12,0,22), glm::vec3(12,0,12)},
            glm::vec3(1, 0, 0),
            "grid_orange.png", 2
        });

        // Right wall (x=22, normal -x)
        physLab.surfaces.push_back({
            {glm::vec3(22,4,22), glm::vec3(22,4,12), glm::vec3(22,0,12), glm::vec3(22,0,22)},
            glm::vec3(-1, 0, 0),
            "grid_orange.png", 2
        });

        // ── Shelf: a raised platform to drop cubes from ──
        // A 4m x 4m platform at y=2 in the back-right corner
        physLab.surfaces.push_back({
            {glm::vec3(18,2,22), glm::vec3(22,2,22), glm::vec3(22,2,18), glm::vec3(18,2,18)},
            glm::vec3(0, 1, 0),
            "grid_grey.png", 2
        });

        // Shelf front face (z=18, y=0 to y=2, x=18 to x=22)
        physLab.surfaces.push_back({
            {glm::vec3(18,2,18), glm::vec3(22,2,18), glm::vec3(22,0,18), glm::vec3(18,0,18)},
            glm::vec3(0, 0, -1),
            "grid_orange.png", 2
        });

        // Shelf left face (x=18, y=0 to y=2, z=18 to z=22)
        physLab.surfaces.push_back({
            {glm::vec3(18,2,18), glm::vec3(18,2,22), glm::vec3(18,0,22), glm::vec3(18,0,18)},
            glm::vec3(-1, 0, 0),
            "grid_orange.png", 2
        });

        level.sectors.push_back(std::move(physLab));
    }

    buildSectorMeshes(level);
    return level;
}


Level setupScene
(
	entt::registry& registry, 
	const ResourceManager& resources
)
{
    auto litShader   = resources.getShader("lit");
    auto gridOrange  = resources.getTexture("grid_orange");
    auto gridGrey    = resources.getTexture("grid_grey");
    auto gridBlue    = resources.getTexture("grid_blue");
    auto cubeMesh    = resources.getMesh("cube");

    // ─── Create the showcase level ──────────────────────────────
    Level level = createShowcaseLevel();

    for (const auto& sector : level.sectors)
    {
        if (!sector.mesh) continue;

        auto sectorEntity = registry.create();
        registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));

        // Pick texture based on sector id
        unsigned int texId = gridOrange->getId(); // default
        if (sector.id == 1) texId = gridBlue->getId();  // light room

        registry.emplace<MeshRenderer>
        (
            sectorEntity,
            sector.mesh->getVAO(),
            0u,
            litShader->getId(),
            texId,
            true,
            sector.mesh->getIndexCount()
        );
    }

    // ═══════════════════════════════════════════════════════════
    // MAIN HALL — Sunlight + point light contrast
    // ═══════════════════════════════════════════════════════════

    // Sun light (directional) — low ambient so it doesn't wash out point lights
    auto sun = registry.create();
    registry.emplace<DirectionalLight>
    (
        sun,
        glm::vec3(-0.2f, -1.0f, -0.3f),   // direction
        glm::vec3(1.0f, 0.95f, 0.8f),      // warm white
        0.08f                                // low ambient
    );

    // Point light 1: warm torch near left wall
    auto hallLight1 = registry.create();
    registry.emplace<Position>(hallLight1, glm::vec3(4.0f, 2.5f, 6.0f));
    registry.emplace<PointLight>
    (
        hallLight1,
        glm::vec3(2.0f, 1.4f, 0.6f),   // warm orange
        0.05f, 0.045f, 0.0075f
    );

    // Point light 2: cool blue near right side
    auto hallLight2 = registry.create();
    registry.emplace<Position>(hallLight2, glm::vec3(16.0f, 2.5f, 6.0f));
    registry.emplace<PointLight>
    (
        hallLight2,
        glm::vec3(0.4f, 0.6f, 2.0f),   // cool blue
        0.05f, 0.045f, 0.0075f
    );

    // A static cube in the main hall (reference object for scale/collision)
    auto hallCube = registry.create();
    registry.emplace<Position>(hallCube, glm::vec3(10.0f, 0.5f, 6.0f));
    registry.emplace<AABBCollider>(hallCube, glm::vec3(0.5f), false);
    registry.emplace<MeshRenderer>
    (
        hallCube,
        cubeMesh->getVAO(), 0u,
        litShader->getId(), gridOrange->getId(),
        true, cubeMesh->getIndexCount()
    );

    // ═══════════════════════════════════════════════════════════
    // LIGHT ROOM — Coloured point lights in low ambient
    //
    // Without shadow mapping, the directional light still
    // contributes some illumination here. We counter this by
    // using bright point lights that visually dominate. The
    // coloured pools and their blending should still be clearly
    // visible against the blue grid walls.
    // ═══════════════════════════════════════════════════════════

    // Red light — back-left corner
    auto redLight = registry.create();
    registry.emplace<Position>(redLight, glm::vec3(-6.0f, 2.0f, 4.0f));
    registry.emplace<PointLight>
    (
        redLight,
        glm::vec3(3.0f, 0.2f, 0.2f),   // bright red
        0.02f, 0.09f, 0.032f
    );

    // Green light — back-right area
    auto greenLight = registry.create();
    registry.emplace<Position>(greenLight, glm::vec3(-2.0f, 2.0f, 4.0f));
    registry.emplace<PointLight>
    (
        greenLight,
        glm::vec3(0.2f, 3.0f, 0.2f),   // bright green
        0.02f, 0.09f, 0.032f
    );

    // Blue light — front-centre
    auto blueLight = registry.create();
    registry.emplace<Position>(blueLight, glm::vec3(-4.0f, 2.0f, 8.0f));
    registry.emplace<PointLight>
    (
        blueLight,
        glm::vec3(0.2f, 0.2f, 3.0f),   // bright blue
        0.02f, 0.09f, 0.032f
    );

    // White accent light — centre of room, shows how all three
    // blend together and provides a neutral reference
    auto whiteLight = registry.create();
    registry.emplace<Position>(whiteLight, glm::vec3(-4.0f, 3.0f, 6.0f));
    registry.emplace<PointLight>
    (
        whiteLight,
        glm::vec3(1.0f, 1.0f, 1.0f),   // neutral white
        0.02f, 0.14f, 0.07f             // shorter range (tighter pool)
    );

    // ═══════════════════════════════════════════════════════════
    // PHYSICS LAB — Gravity, collision, and friction demos
    //
    // All physics demo entities have a DemoReset component so
    // they loop automatically on a timer.
    // ═══════════════════════════════════════════════════════════

    // Cube 1: on the shelf, nudged off the edge → falls to floor
    {
        glm::vec3 startPos(19.0f, 3.5f, 19.0f);
        glm::vec3 startVel(-0.5f, 0.0f, 0.0f);

        auto cube = registry.create();
        registry.emplace<Position>(cube, startPos);
        registry.emplace<Velocity>(cube, startVel);
        registry.emplace<AABBCollider>(cube, glm::vec3(0.5f), false);
        registry.emplace<Gravity>(cube);
        registry.emplace<OnGround>(cube);
        registry.emplace<DemoReset>(cube, startPos, startVel, 6.0f, 0.0f);
        registry.emplace<MeshRenderer>
        (
            cube,
            cubeMesh->getVAO(), 0u,
            litShader->getId(), gridOrange->getId(),
            true, cubeMesh->getIndexCount()
        );
    }

    // Cube 2: dropped from near the ceiling (pure gravity test)
    {
        glm::vec3 startPos(15.0f, 3.5f, 17.0f);
        glm::vec3 startVel(0.0f);

        auto cube = registry.create();
        registry.emplace<Position>(cube, startPos);
        registry.emplace<Velocity>(cube, startVel);
        registry.emplace<AABBCollider>(cube, glm::vec3(0.5f), false);
        registry.emplace<Gravity>(cube);
        registry.emplace<OnGround>(cube);
        registry.emplace<DemoReset>(cube, startPos, startVel, 4.0f, 0.0f);
        registry.emplace<MeshRenderer>
        (
            cube,
            cubeMesh->getVAO(), 0u,
            litShader->getId(), gridOrange->getId(),
            true, cubeMesh->getIndexCount()
        );
    }

    // Cube 3: sliding across the floor (friction demo)
    {
        glm::vec3 startPos(14.0f, 0.5f, 14.0f);
        glm::vec3 startVel(3.0f, 0.0f, 1.0f);

        auto cube = registry.create();
        registry.emplace<Position>(cube, startPos);
        registry.emplace<Velocity>(cube, startVel);
        registry.emplace<AABBCollider>(cube, glm::vec3(0.5f), false);
        registry.emplace<Gravity>(cube);
        registry.emplace<OnGround>(cube);
        registry.emplace<CharacterPhysics>(cube); // provides friction values
        registry.emplace<DemoReset>(cube, startPos, startVel, 5.0f, 0.0f);
        registry.emplace<MeshRenderer>
        (
            cube,
            cubeMesh->getVAO(), 0u,
            litShader->getId(), gridOrange->getId(),
            true, cubeMesh->getIndexCount()
        );
    }

    // Point light in physics lab for visibility
    auto labLight = registry.create();
    registry.emplace<Position>(labLight, glm::vec3(17.0f, 3.0f, 17.0f));
    registry.emplace<PointLight>
    (
        labLight,
        glm::vec3(1.5f, 1.5f, 1.5f),   // bright white
        0.1f, 0.045f, 0.0075f
    );

    return level;
};