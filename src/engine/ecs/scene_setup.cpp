#include "engine/ecs/scene_setup.h"
#include "engine/ecs/components.h"
#include "engine/level/level.h"
#include "engine/level/level_loader.h"

// ─── Helper: build a single large test room ─────────────────────
static Level createShowcaseLevel()
{
    Level level;

    // ═══════════════════════════════════════════════════════════
    // Single room: 30 x 30 x 6   (origin 0,0,0 to 30,6,30)
    // All grey walls, floor and ceiling
    // ═══════════════════════════════════════════════════════════
    {
        Sector room;
        room.id = 0;
        room.boundsMin = glm::vec3(0.0f, 0.0f, 0.0f);
        room.boundsMax = glm::vec3(30.0f, 6.0f, 30.0f);

        // Floor
        room.surfaces.push_back({
            {glm::vec3(0,0,30), glm::vec3(30,0,30), glm::vec3(30,0,0), glm::vec3(0,0,0)},
            glm::vec3(0, 1, 0),
            "grid_grey.png", 0
        });

        // Ceiling
        room.surfaces.push_back({
            {glm::vec3(0,6,0), glm::vec3(30,6,0), glm::vec3(30,6,30), glm::vec3(0,6,30)},
            glm::vec3(0, -1, 0),
            "grid_grey.png", 0
        });

        // Back wall (z=0, normal +z)
        room.surfaces.push_back({
            {glm::vec3(30,6,0), glm::vec3(0,6,0), glm::vec3(0,0,0), glm::vec3(30,0,0)},
            glm::vec3(0, 0, 1),
            "grid_grey.png", 0
        });

        // Front wall (z=30, normal -z)
        room.surfaces.push_back({
            {glm::vec3(0,6,30), glm::vec3(30,6,30), glm::vec3(30,0,30), glm::vec3(0,0,30)},
            glm::vec3(0, 0, -1),
            "grid_grey.png", 0
        });

        // Left wall (x=0, normal +x)
        room.surfaces.push_back({
            {glm::vec3(0,6,0), glm::vec3(0,6,30), glm::vec3(0,0,30), glm::vec3(0,0,0)},
            glm::vec3(1, 0, 0),
            "grid_grey.png", 0
        });

        // Right wall (x=30, normal -x)
        room.surfaces.push_back({
            {glm::vec3(30,6,30), glm::vec3(30,6,0), glm::vec3(30,0,0), glm::vec3(30,0,30)},
            glm::vec3(-1, 0, 0),
            "grid_grey.png", 0
        });

        level.sectors.push_back(std::move(room));
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
        registry.emplace<MeshRenderer>
        (
            sectorEntity,
            sector.mesh->getVAO(),
            0u,
            litShader->getId(),
            gridGrey->getId(),
            true,
            sector.mesh->getIndexCount()
        );
    }

    // ─── Player entity ────────────────────────────────────────
    auto player = registry.create();
    registry.emplace<Position>(player, glm::vec3(15.0f, 1.7f, 15.0f));
    registry.emplace<AABBCollider>(player, glm::vec3(0.3f, 0.85f, 0.3f), false);
    registry.emplace<TagPlayer>(player);

    // ═══════════════════════════════════════════════════════════
    // LIGHTING
    // ═══════════════════════════════════════════════════════════

    // Sun light (directional) — low ambient
    auto sun = registry.create();
    registry.emplace<DirectionalLight>
    (
        sun,
        glm::vec3(-0.2f, -1.0f, -0.3f),   // direction
        glm::vec3(1.0f, 1.0f, 1.0f),        // pure white
        0.08f                                // low ambient
    );

    // Bright ceiling light 1 (front half of room)
    auto ceilLight1 = registry.create();
    registry.emplace<Position>(ceilLight1, glm::vec3(15.0f, 5.5f, 10.0f));
    registry.emplace<PointLight>
    (
        ceilLight1,
        glm::vec3(2.0f, 2.0f, 2.0f),   // bright white
        0.05f, 0.09f, 0.032f            // wide range
    );
    { // debug cube for ceilLight1
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(15.0f, 5.5f, 10.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridGrey->getId(), true, cubeMesh->getIndexCount());
    }

    // Bright ceiling light 2 (back half of room)
    auto ceilLight2 = registry.create();
    registry.emplace<Position>(ceilLight2, glm::vec3(15.0f, 5.5f, 20.0f));
    registry.emplace<PointLight>
    (
        ceilLight2,
        glm::vec3(2.0f, 2.0f, 2.0f),   // bright white
        0.05f, 0.09f, 0.032f            // wide range
    );
    { // debug cube for ceilLight2
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(15.0f, 5.5f, 20.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridGrey->getId(), true, cubeMesh->getIndexCount());
    }

    // Red torch — left side
    auto redLight = registry.create();
    registry.emplace<Position>(redLight, glm::vec3(3.0f, 2.0f, 10.0f));
    registry.emplace<PointLight>
    (
        redLight,
        glm::vec3(3.0f, 0.2f, 0.2f),   // bright red
        0.01f, 0.35f, 0.44f             // tight pool
    );
    { // debug cube for redLight
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(3.0f, 2.0f, 10.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridRed->getId(), true, cubeMesh->getIndexCount());
    }

    // Green torch — left side, further back
    auto greenLight = registry.create();
    registry.emplace<Position>(greenLight, glm::vec3(3.0f, 2.0f, 15.0f));
    registry.emplace<PointLight>
    (
        greenLight,
        glm::vec3(0.2f, 3.0f, 0.2f),   // bright green
        0.01f, 0.35f, 0.44f             // tight pool
    );
    { // debug cube for greenLight
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(3.0f, 2.0f, 15.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridGreen->getId(), true, cubeMesh->getIndexCount());
    }

    // Blue torch — left side, further back still
    auto blueLight = registry.create();
    registry.emplace<Position>(blueLight, glm::vec3(3.0f, 2.0f, 20.0f));
    registry.emplace<PointLight>
    (
        blueLight,
        glm::vec3(0.2f, 0.2f, 3.0f),   // bright blue
        0.01f, 0.35f, 0.44f             // tight pool
    );
    { // debug cube for blueLight
        auto e = registry.create();
        registry.emplace<Position>(e, glm::vec3(3.0f, 2.0f, 20.0f));
        registry.emplace<Scale>(e, glm::vec3(0.2f));
        registry.emplace<MeshRenderer>(e, cubeMesh->getVAO(), 0u, litShader->getId(), gridBlue->getId(), true, cubeMesh->getIndexCount());
    }

    // ═══════════════════════════════════════════════════════════
    // PHYSICS DEMOS
    // ═══════════════════════════════════════════════════════════

    // A static cube (reference object for scale/collision)
    auto hallCube = registry.create();
    registry.emplace<Position>(hallCube, glm::vec3(15.0f, 0.5f, 15.0f));
    registry.emplace<AABBCollider>(hallCube, glm::vec3(0.5f), false);
    registry.emplace<MeshRenderer>
    (
        hallCube,
        cubeMesh->getVAO(), 0u,
        litShader->getId(), gridBlue->getId(),
        true, cubeMesh->getIndexCount()
    );

    // Shelf: a raised platform to slide cubes off
    // 4 wide, 2 tall, 4 deep — top surface at y=2
    auto shelf = registry.create();
    registry.emplace<Position>(shelf, glm::vec3(20.0f, 1.0f, 5.0f));
    registry.emplace<Scale>(shelf, glm::vec3(4.0f, 2.0f, 4.0f));
    registry.emplace<AABBCollider>(shelf, glm::vec3(2.0f, 1.0f, 2.0f), false);
    registry.emplace<MeshRenderer>
    (
        shelf,
        cubeMesh->getVAO(), 0u,
        litShader->getId(), gridBlue->getId(),
        true, cubeMesh->getIndexCount()
    );

    // Cube 1: on the shelf, nudged off the edge → falls to floor
    {
        glm::vec3 startPos(20.0f, 2.5f, 5.0f);
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
        glm::vec3 startPos(20.0f, 5.0f, 8.0f);
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
        glm::vec3 startPos(20.0f, 0.5f, 12.0f);
        glm::vec3 startVel(3.0f, 0.0f, 1.0f);

        auto cube = registry.create();
        registry.emplace<Position>(cube, startPos);
        registry.emplace<Velocity>(cube, startVel);
        registry.emplace<AABBCollider>(cube, glm::vec3(0.5f), false);
        registry.emplace<Gravity>(cube);
        registry.emplace<OnGround>(cube);
        auto& phys = registry.emplace<CharacterPhysics>(cube);
        phys.groundFriction = 1.5f;  // low friction so it slides visibly
        registry.emplace<DemoReset>(cube, startPos, startVel, 5.0f, 0.0f);
        registry.emplace<MeshRenderer>
        (
            cube,
            cubeMesh->getVAO(), 0u,
            litShader->getId(), gridOrange->getId(),
            true, cubeMesh->getIndexCount()
        );
    }

    // ═══════════════════════════════════════════════════════════
    // CHAPTER 11 — Doors, Lifts, Triggers
    // ═══════════════════════════════════════════════════════════

    // ─── Door: slides upward when player approaches ─────────
    // Placed against the right wall area as a freestanding demo
    auto door = registry.create();
    glm::vec3 closedPos(25.0f, 1.5f, 15.0f);
    glm::vec3 openPos(25.0f, 4.5f, 15.0f);

    registry.emplace<Position>(door, closedPos);
    registry.emplace<Scale>(door, glm::vec3(0.2f, 3.0f, 4.0f));
    registry.emplace<MeshRenderer>(door, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridOrange->getId(),
                                    true, cubeMesh->getIndexCount());
    registry.emplace<Mover>(door, closedPos, openPos, 3.0f, 4.0f, 0.0f, 0.0f,
                              MoverState::Idle, true);
    registry.emplace<AABBCollider>(door, glm::vec3(0.1f, 1.5f, 2.0f), false);

    // Trigger zone centred on the door
    auto doorTrigger = registry.create();
    registry.emplace<Position>(doorTrigger, glm::vec3(25.0f, 1.5f, 15.0f));
    registry.emplace<AABBCollider>(doorTrigger, glm::vec3(2.0f, 1.5f, 2.5f), true);
    registry.emplace<TriggerVolume>(doorTrigger,
        TriggerAction::ActivateMover,
        door,
        glm::vec3(0.0f), 0.0f, "",
        false, false, 1.0f, 0.0f);

    // Debug wireframe for door trigger
    auto debugDoor = registry.create();
    registry.emplace<Position>(debugDoor, glm::vec3(25.0f, 1.5f, 15.0f));
    registry.emplace<Scale>(debugDoor, glm::vec3(4.0f, 3.0f, 5.0f));
    registry.emplace<MeshRenderer>(debugDoor, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridGreen->getId(),
                                    true, cubeMesh->getIndexCount());
    registry.emplace<TagDebugWireframe>(debugDoor);

    // ─── Lift: rises when player steps on it ────────────────
    // Position y=0.1 so the top face (y=0.2) sits just above the floor
    auto lift = registry.create();
    glm::vec3 bottomPos(10.0f, 0.1f, 25.0f);
    glm::vec3 topPos(10.0f, 4.1f, 25.0f);

    registry.emplace<Position>(lift, bottomPos);
    registry.emplace<Scale>(lift, glm::vec3(3.0f, 0.2f, 3.0f));
    registry.emplace<MeshRenderer>(lift, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridGreen->getId(),
                                    true, cubeMesh->getIndexCount());
    registry.emplace<Mover>(lift, bottomPos, topPos, 2.0f, 2.0f, 0.0f, 0.0f,
                              MoverState::Idle, true);
    registry.emplace<AABBCollider>(lift, glm::vec3(1.5f, 0.1f, 1.5f), false);

    // Trigger on the lift platform
    auto liftTrigger = registry.create();
    registry.emplace<Position>(liftTrigger, glm::vec3(10.0f, 0.4f, 25.0f));
    registry.emplace<AABBCollider>(liftTrigger, glm::vec3(1.5f, 0.3f, 1.5f), true);
    registry.emplace<TriggerVolume>(liftTrigger,
        TriggerAction::ActivateMover, lift,
        glm::vec3(0.0f), 0.0f, "", false, false, 0.5f, 0.0f);

    // Debug wireframe for lift trigger
    auto debugLift = registry.create();
    registry.emplace<Position>(debugLift, glm::vec3(10.0f, 0.4f, 25.0f));
    registry.emplace<Scale>(debugLift, glm::vec3(3.0f, 0.6f, 3.0f));
    registry.emplace<MeshRenderer>(debugLift, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridGreen->getId(),
                                    true, cubeMesh->getIndexCount());
    registry.emplace<TagDebugWireframe>(debugLift);

    // ─── Teleporter ─────────────────────────────────────────
    auto teleportTrigger = registry.create();
    registry.emplace<Position>(teleportTrigger, glm::vec3(5.0f, 0.5f, 5.0f));
    registry.emplace<AABBCollider>(teleportTrigger, glm::vec3(1.0f, 1.5f, 1.0f), true);
    registry.emplace<TriggerVolume>(teleportTrigger,
        TriggerAction::Teleport,
        entt::null,
        glm::vec3(25.0f, 1.0f, 25.0f),    // destination: far corner
        0.0f, "", false, false, 1.0f, 0.0f);

    // Debug wireframe for teleporter
    auto debugTeleport = registry.create();
    registry.emplace<Position>(debugTeleport, glm::vec3(5.0f, 0.5f, 5.0f));
    registry.emplace<Scale>(debugTeleport, glm::vec3(2.0f, 3.0f, 2.0f));
    registry.emplace<MeshRenderer>(debugTeleport, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridGreen->getId(),
                                    true, cubeMesh->getIndexCount());
    registry.emplace<TagDebugWireframe>(debugTeleport);

    // Tall thin pole marking the teleporter centre
    auto teleportPole = registry.create();
    registry.emplace<Position>(teleportPole, glm::vec3(5.0f, 1.5f, 5.0f));
    registry.emplace<Scale>(teleportPole, glm::vec3(0.1f, 3.0f, 0.1f));
    registry.emplace<MeshRenderer>(teleportPole, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridBlue->getId(),
                                    true, cubeMesh->getIndexCount());

    // ─── Lava Pool (damage zone) ────────────────────────────
    // Visible lava surface — y=0.1 so top face sits at y=0.2, above the floor
    auto lavaSurface = registry.create();
    registry.emplace<Position>(lavaSurface, glm::vec3(20.0f, 0.1f, 25.0f));
    registry.emplace<Scale>(lavaSurface, glm::vec3(6.0f, 0.2f, 6.0f));
    registry.emplace<MeshRenderer>(lavaSurface, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridRed->getId(),
                                    true, cubeMesh->getIndexCount());

    // Damage trigger
    auto lava = registry.create();
    registry.emplace<Position>(lava, glm::vec3(20.0f, 0.5f, 25.0f));
    registry.emplace<AABBCollider>(lava, glm::vec3(3.0f, 0.5f, 3.0f), true);
    registry.emplace<TriggerVolume>(lava,
        TriggerAction::Damage,
        entt::null,
        glm::vec3(0.0f),
        25.0f,      // 25 damage per second
        "", false, false, 0.0f, 0.0f);

    // Debug wireframe for lava
    auto debugLava = registry.create();
    registry.emplace<Position>(debugLava, glm::vec3(20.0f, 0.5f, 25.0f));
    registry.emplace<Scale>(debugLava, glm::vec3(6.0f, 1.0f, 6.0f));
    registry.emplace<MeshRenderer>(debugLava, cubeMesh->getVAO(), 0u,
                                    litShader->getId(), gridRed->getId(),
                                    true, cubeMesh->getIndexCount());
    registry.emplace<TagDebugWireframe>(debugLava);

    return level;
};
