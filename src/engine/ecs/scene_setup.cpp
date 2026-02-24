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
            "grid_orange.png", 0
        });

        // Ceiling
        hall.surfaces.push_back({
            {glm::vec3(0,4,0), glm::vec3(20,4,0), glm::vec3(20,4,12), glm::vec3(0,4,12)},
            glm::vec3(0, -1, 0),
            "grid_orange.png", 0
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
            "grid_blue.png", 1
        });

        // Ceiling
        lightRoom.surfaces.push_back({
            {glm::vec3(-8,4,2), glm::vec3(0,4,2), glm::vec3(0,4,10), glm::vec3(-8,4,10)},
            glm::vec3(0, -1, 0),
            "grid_blue.png", 1
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
            {glm::vec3(0,4,4), glm::vec3(0,4,2), glm::vec3(0,0,2), glm::vec3(0,0,4)},
            glm::vec3(-1, 0, 0),
            "grid_blue.png", 1
        });
        // Top section: z=8 to z=10
        lightRoom.surfaces.push_back({
            {glm::vec3(0,4,10), glm::vec3(0,4,8), glm::vec3(0,0,8), glm::vec3(0,0,10)},
            glm::vec3(-1, 0, 0),
            "grid_blue.png", 1
        });
        // Above doorway: z=4 to z=8, y=3 to y=4
        lightRoom.surfaces.push_back({
            {glm::vec3(0,4,8), glm::vec3(0,4,4), glm::vec3(0,3,4), glm::vec3(0,3,8)},
            glm::vec3(-1, 0, 0),
            "grid_blue.png", 1
        });

        level.sectors.push_back(std::move(lightRoom));
    }

    // ═══════════════════════════════════════════════════════════
    // Sector 2: PHYSICS LAB (10 x 10 x 4)
    //   Bounds: (12, 0, 12) to (22, 4, 22)
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
            "grid_grey.png", 2
        });
        // Above doorway: x=12 to x=16, y=3 to y=4
        physLab.surfaces.push_back({
            {glm::vec3(16,4,12), glm::vec3(12,4,12), glm::vec3(12,3,12), glm::vec3(16,3,12)},
            glm::vec3(0, 0, 1),
            "grid_grey.png", 2
        });

        // Front wall (z=22, normal -z)
        physLab.surfaces.push_back({
            {glm::vec3(12,4,22), glm::vec3(22,4,22), glm::vec3(22,0,22), glm::vec3(12,0,22)},
            glm::vec3(0, 0, -1),
            "grid_grey.png", 2
        });

        // Left wall (x=12, normal +x)
        physLab.surfaces.push_back({
            {glm::vec3(12,4,12), glm::vec3(12,4,22), glm::vec3(12,0,22), glm::vec3(12,0,12)},
            glm::vec3(1, 0, 0),
            "grid_grey.png", 2
        });

        // Right wall (x=22, normal -x)
        physLab.surfaces.push_back({
            {glm::vec3(22,4,22), glm::vec3(22,4,12), glm::vec3(22,0,12), glm::vec3(22,0,22)},
            glm::vec3(-1, 0, 0),
            "grid_grey.png", 2
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
            "grid_grey.png", 2
        });

        // Shelf left face (x=18, y=0 to y=2, z=18 to z=22)
        physLab.surfaces.push_back({
            {glm::vec3(18,2,22), glm::vec3(18,2,18), glm::vec3(18,0,18), glm::vec3(18,0,22)},
            glm::vec3(-1, 0, 0),
            "grid_grey.png", 2
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
    auto gridGrey    = resources.getTexture("grid_grey");
    auto gridOrange  = resources.getTexture("grid_orange");
    auto gridBlue    = resources.getTexture("grid_blue");
    auto gridGreen  = resources.getTexture("grid_green");
    auto gridRed  = resources.getTexture("grid_red");
    auto cubeMesh    = resources.getMesh("cube");

    // ─── Create the showcase level ──────────────────────────────
    Level level = createShowcaseLevel();

    for (const auto& sector : level.sectors)
    {
        if (!sector.mesh) continue;

        auto sectorEntity = registry.create();
        registry.emplace<Position>(sectorEntity, glm::vec3(0.0f));

        // One texture per sector — the renderer does not support
        // per-surface textures, so each room gets a single colour.
        unsigned int texId = gridOrange->getId(); // sector 0: main hall
        if (sector.id == 1) texId = gridGrey->getId();  // light room
        if (sector.id == 2) texId = gridBlue->getId();   // physics lab

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

    // ─── Player entity ────────────────────────────────────────
    auto player = registry.create();
    registry.emplace<Position>(player, glm::vec3(10.0f, 1.7f, 3.0f));
    registry.emplace<AABBCollider>(player, glm::vec3(0.3f, 0.85f, 0.3f), false);
    registry.emplace<TagPlayer>(player);

    // ═══════════════════════════════════════════════════════════
    // MAIN HALL — Sunlight + point light contrast
    // ═══════════════════════════════════════════════════════════

    // Sun light (directional) — low ambient so it doesn't wash out point lights
    auto sun = registry.create();
    registry.emplace<DirectionalLight>
    (
        sun,
        glm::vec3(-0.2f, -1.0f, -0.3f),   // direction
        glm::vec3(1.0f, 1.0f, 1.0f),        // pure white (debug)
        0.08f                                // low ambient
    );

    // Point light 1: warm torch near left wall
    auto hallLight1 = registry.create();
    registry.emplace<Position>(hallLight1, glm::vec3(4.0f, 2.5f, 6.0f));
    registry.emplace<PointLight>
    (
        hallLight1,
        glm::vec3(1.5f, 1.5f, 1.5f),   // white
        0.01f, 0.7f, 1.8f              // tight range, lights only nearby
    );
    { // debug cube for hallLight1
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(4.0f, 2.5f, 6.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridGrey->getId(), true, cubeMesh->getIndexCount());
    }

    // Point light 2: white near right side
    auto hallLight2 = registry.create();
    registry.emplace<Position>(hallLight2, glm::vec3(16.0f, 2.5f, 6.0f));
    registry.emplace<PointLight>
    (
        hallLight2,
        glm::vec3(0.75f, 0.75f, 0.75f),   // dim white
        0.01f, 0.7f, 1.8f              // tight range
    );
    { // debug cube for hallLight2
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(16.0f, 2.5f, 6.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridGrey->getId(), true, cubeMesh->getIndexCount());
    }

    // A static cube in the main hall (reference object for scale/collision)
    auto hallCube = registry.create();
    registry.emplace<Position>(hallCube, glm::vec3(10.0f, 0.5f, 6.0f));
    registry.emplace<AABBCollider>(hallCube, glm::vec3(0.5f), false);
    registry.emplace<MeshRenderer>
    (
        hallCube,
        cubeMesh->getVAO(), 0u,
        litShader->getId(), gridBlue->getId(),
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

    // Red torch — back-left corner
    auto redLight = registry.create();
    registry.emplace<Position>(redLight, glm::vec3(-6.0f, 2.0f, 4.0f));
    registry.emplace<PointLight>
    (
        redLight,
        glm::vec3(3.0f, 0.2f, 0.2f),   // bright red
        0.01f, 0.35f, 0.44f             // tight pool
    );
    { // debug cube for redLight
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(-6.0f, 2.0f, 4.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridRed->getId(), true, cubeMesh->getIndexCount());
    }

    // Green torch — back-right area
    auto greenLight = registry.create();
    registry.emplace<Position>(greenLight, glm::vec3(-2.0f, 2.0f, 4.0f));
    registry.emplace<PointLight>
    (
        greenLight,
        glm::vec3(0.2f, 3.0f, 0.2f),   // bright green
        0.01f, 0.35f, 0.44f             // tight pool
    );
    { // debug cube for greenLight
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(-2.0f, 2.0f, 4.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridGreen->getId(), true, cubeMesh->getIndexCount());
    }

    // Blue torch — front-centre
    auto blueLight = registry.create();
    registry.emplace<Position>(blueLight, glm::vec3(-4.0f, 2.0f, 8.0f));
    registry.emplace<PointLight>
    (
        blueLight,
        glm::vec3(0.2f, 0.2f, 3.0f),   // bright blue
        0.01f, 0.35f, 0.44f             // tight pool
    );
    { // debug cube for blueLight
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(-4.0f, 2.0f, 8.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridBlue->getId(), true, cubeMesh->getIndexCount());
    }

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

	// ─── Door: Main Hall → Physics Lab ──────────────────────────
	// Doorway is at z=12, x=12..16, y=0..3 (lintel at y=3..4)
	// Door is a cube scaled to fill the opening: 4 wide, 3 tall, 0.2 deep
	auto door = registry.create();
	glm::vec3 closedPos(14.0f, 1.5f, 12.0f); // centred in the doorway
	glm::vec3 openPos(14.0f, 4.5f, 12.0f);   // slides up behind the lintel

	registry.emplace<Position>(door, closedPos);
	registry.emplace<Scale>(door, glm::vec3(4.0f, 3.0f, 0.2f));
	registry.emplace<MeshRenderer>(door, cubeMesh->getVAO(), 0u,
									litShader->getId(), gridGrey->getId(),
									true, cubeMesh->getIndexCount());
	registry.emplace<Mover>(door, closedPos, openPos, 3.0f, 4.0f, 0.0f, 0.0f,
							MoverState::Idle, true);
	registry.emplace<AABBCollider>(door, glm::vec3(2.0f, 1.5f, 0.1f), false);

	// Trigger zone in front of the door (hall side)
	auto doorTrigger = registry.create();
	registry.emplace<Position>(doorTrigger, glm::vec3(14.0f, 1.5f, 12.0f));
	registry.emplace<AABBCollider>(doorTrigger, glm::vec3(2.5f, 1.5f, 2.0f), true);
	registry.emplace<TriggerVolume>(doorTrigger,
		TriggerAction::ActivateMover,
		door,                      // target: the door entity
		glm::vec3(0.0f),           // destination (unused for mover)
		0.0f,                      // value (unused)
		"",                        // message (unused)
		false,                     // not once-only
		false,                     // not yet triggered
		1.0f,                      // 1 second cooldown between triggers
		0.0f                       // cooldown timer starts at 0
	);
	auto debugTrig = registry.create();
	registry.emplace<Position>(debugTrig, glm::vec3(14.0f, 1.5f, 12.0f));
	registry.emplace<Scale>(debugTrig, glm::vec3(5.0f, 3.0f, 4.0f)); // halfExtents * 2
	registry.emplace<MeshRenderer>(debugTrig, cubeMesh->getVAO(), 0u,
									litShader->getId(), gridGrey->getId(),
									true, cubeMesh->getIndexCount());
	registry.emplace<TagDebugWireframe>(debugTrig);

	// ─── Lift: Main Hall → Physics Lab ──────────────────────────
	auto lift = registry.create();
	glm::vec3 bottomPos(0.0f, 0.0f, -8.0f);
	glm::vec3 topPos(0.0f, 6.0f, -8.0f);

	registry.emplace<Position>(lift, bottomPos);
	registry.emplace<Scale>(lift, glm::vec3(3.0f, 0.2f, 3.0f));
	registry.emplace<MeshRenderer>(lift, cubeMesh->getVAO(), 0u,
									litShader->getId(), gridGrey->getId(),
									true, cubeMesh->getIndexCount());
	registry.emplace<Mover>(lift, bottomPos, topPos, 2.0f, 2.0f, 0.0f, 0.0f,
							MoverState::Idle, true);
	registry.emplace<AABBCollider>(lift, glm::vec3(1.5f, 0.1f, 1.5f), false);

	// Trigger on the lift platform itself (step on it to activate)
	auto liftTrigger = registry.create();
	registry.emplace<Position>(liftTrigger, glm::vec3(0.0f, 0.3f, -8.0f));
	registry.emplace<AABBCollider>(liftTrigger, glm::vec3(1.5f, 0.3f, 1.5f), true);
	registry.emplace<TriggerVolume>(liftTrigger,
		TriggerAction::ActivateMover, lift,
		glm::vec3(0.0f), 0.0f, "", false, false, 0.5f, 0.0f);

	// Debug wireframe cube showing the lift trigger zone
	auto debugLift = registry.create();
	registry.emplace<Position>(debugLift, glm::vec3(0.0f, 0.3f, -8.0f));
	registry.emplace<Scale>(debugLift, glm::vec3(3.0f, 0.6f, 3.0f));
	registry.emplace<MeshRenderer>(debugLift, cubeMesh->getVAO(), 0u,
									litShader->getId(), gridGreen->getId(),
									true, cubeMesh->getIndexCount());
	registry.emplace<TagDebugWireframe>(debugLift);

	// Teleporter ──────────────────────────
	auto teleportTrigger = registry.create();
	registry.emplace<Position>(teleportTrigger, glm::vec3(8.0f, 0.5f, 3.0f));
	registry.emplace<AABBCollider>(teleportTrigger, glm::vec3(1.0f, 1.5f, 1.0f), true);
	registry.emplace<TriggerVolume>(teleportTrigger,
		TriggerAction::Teleport,
		entt::null,                         // no target entity
		glm::vec3(-8.0f, 1.0f, -3.0f),    // destination
		0.0f, "", false, false, 1.0f, 0.0f);

	// Debug wireframe cube showing the teleporter trigger zone
	auto debugTeleport = registry.create();
	registry.emplace<Position>(debugTeleport, glm::vec3(8.0f, 0.5f, 3.0f));
	registry.emplace<Scale>(debugTeleport, glm::vec3(2.0f, 3.0f, 2.0f));
	registry.emplace<MeshRenderer>(debugTeleport, cubeMesh->getVAO(), 0u,
									litShader->getId(), gridGreen->getId(),
									true, cubeMesh->getIndexCount());
	registry.emplace<TagDebugWireframe>(debugTeleport);


	// Lava Pool ──────────────────────────
	// Visible lava surface — a flat red cube
	auto lavaSurface = registry.create();
	registry.emplace<Position>(lavaSurface, glm::vec3(0.0f, 0.0f, 10.0f));
	registry.emplace<Scale>(lavaSurface, glm::vec3(10.0f, 0.2f, 10.0f));
	registry.emplace<MeshRenderer>(lavaSurface, cubeMesh->getVAO(), 0u,
									litShader->getId(), gridRed->getId(),
									true, cubeMesh->getIndexCount());

	// Damage trigger — same position, slightly taller so it catches the player above the surface
	// auto lava = registry.create();
	// registry.emplace<Position>(lava, glm::vec3(0.0f, 0.0f, 10.0f));
	// registry.emplace<AABBCollider>(lava, glm::vec3(5.0f, 0.5f, 5.0f), true);
	// registry.emplace<TriggerVolume>(lava,
	// 	TriggerAction::Damage,
	// 	entt::null,
	// 	glm::vec3(0.0f),
	// 	25.0f,      // 25 damage per second
	// 	"", false, false, 0.0f, 0.0f);  // No cooldown — continuous damage

	// // Debug wireframe cube showing the damage zone (red to signal danger)
	// auto debugLava = registry.create();
	// registry.emplace<Position>(debugLava, glm::vec3(0.0f, -0.5f, 10.0f));
	// registry.emplace<Scale>(debugLava, glm::vec3(10.0f, 1.0f, 10.0f));
	// registry.emplace<MeshRenderer>(debugLava, cubeMesh->getVAO(), 0u,
	// 								litShader->getId(), gridRed->getId(),
	// 								true, cubeMesh->getIndexCount());
	// registry.emplace<TagDebugWireframe>(debugLava);

    // Point light in physics lab for visibility
    auto labLight = registry.create();
    registry.emplace<Position>(labLight, glm::vec3(17.0f, 3.0f, 17.0f));
    registry.emplace<PointLight>
    (
        labLight,
        glm::vec3(0.75f, 0.75f, 0.75f),   // dim white
        0.01f, 0.7f, 1.8f              // tight range
    );
    { // debug cube for labLight
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(17.0f, 3.0f, 17.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridGrey->getId(), true, cubeMesh->getIndexCount());
    }

    return level;
};