#include "harness/map_scenarios.h"

#include "engine/level/map_loader.h"
#include "engine/level/map_to_level.h"
#include "engine/level/map_to_descriptors.h"
#include "engine/level/map_transform.h"
#include "engine/level/brush_geometry.h"
#include "engine/level/level.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

// Scenario definitions are indented inside the namespace (like headless_main's
// scenarios) so each is a cohesive test, not a file-scope free function.
namespace mapscenarios
{
    namespace
    {
        bool report(const std::string& name, bool pass, const std::string& detail)
        {
            std::cout << "[" << (pass ? "PASS" : "FAIL") << "] " << name
                      << " — " << detail << std::endl;
            return pass;
        }
    }

    bool scenarioMapParse()
    {
        // One worldspawn cube (6 faces), one point spawn, one light. Includes a
        // `//` comment, a negative coordinate, and a decimal to exercise the
        // tokenizer/number parser.
        const std::string src =
            "// sample map fixture\n"
            "{\n"
            "\"classname\" \"worldspawn\"\n"
            "{\n"
            "( -64 -64 -64 ) ( -64 -63 -64 ) ( -64 -64 -63 ) grid_grey 0 0 0 1 1\n"
            "(  64 -64 -64 ) (  64 -64 -63 ) (  64 -63 -64 ) grid_grey 0 0 0 1 1\n"
            "( -64 -64 -64 ) ( -63 -64 -64 ) ( -64 -64 -63 ) grid_grey 0 0 0 1 1\n"
            "( -64  64 -64 ) ( -64  64 -63 ) ( -63  64 -64 ) grid_grey 0 0 0 1 1\n"
            "( -64 -64 -64 ) ( -64 -63 -64 ) ( -63 -64 -64 ) floor 8 16 90 1.5 1\n"
            "( -64 -64  64 ) ( -63 -64  64 ) ( -64 -63  64 ) grid_grey 0 0 0 1 1\n"
            "}\n"
            "}\n"
            "{\n"
            "\"classname\" \"info_player_start\"\n"
            "\"origin\" \"16 16 32\"\n"
            "\"angle\" \"90\"\n"
            "}\n"
            "{\n"
            "\"classname\" \"light\"\n"
            "\"origin\" \"32 32 48\"\n"
            "}\n";

        std::string err;
        qmap::MapData map = qmap::parseMapString(src, &err);

        bool ok = err.empty() && map.entities.size() == 3;

        // worldspawn: 1 brush, 6 faces; check a point, a texture, and UV parsing.
        if (ok)
        {
            const auto& world = map.entities[0];
            ok = world.classname() == "worldspawn"
              && world.brushes.size() == 1
              && world.brushes[0].faces.size() == 6
              && world.brushes[0].faces[0].points[0] == glm::vec3(-64, -64, -64)
              && world.brushes[0].faces[0].texture == "grid_grey"
              && world.brushes[0].faces[4].texture == "floor"
              && world.brushes[0].faces[4].rotation == 90.0f
              && world.brushes[0].faces[4].scale.x == 1.5f;
        }

        // Point entities: no brushes, props parsed.
        if (ok)
        {
            const auto& spawn = map.entities[1];
            const auto& light = map.entities[2];
            ok = spawn.classname() == "info_player_start"
              && spawn.isPointEntity()
              && spawn.getString("origin") == "16 16 32"
              && spawn.getString("angle") == "90"
              && light.classname() == "light"
              && light.isPointEntity();
        }

        // Negative test: a truncated brush must fail with a message, not crash.
        std::string err2;
        qmap::MapData bad = qmap::parseMapString("{\n\"classname\" \"worldspawn\"\n{\n", &err2);
        bool rejectsBad = !err2.empty() && bad.entities.empty();

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "entities=%zu (want 3), parse err=\"%s\"; malformed rejected=%d",
            map.entities.size(), err.c_str(), rejectsBad ? 1 : 0);
        return report("map_parse", ok && rejectsBad, buf);
    }

    bool scenarioMapFile(const std::string& path)
    {
        std::string err;
        qmap::MapData map = qmap::loadMapFile(path, &err);

        std::size_t worldBrushes = 0, totalFaces = 0;
        bool hasWorldspawn = false, hasPlayerStart = false;
        for (const auto& e : map.entities)
        {
            if (e.classname() == "worldspawn")        hasWorldspawn = true;
            if (e.classname() == "info_player_start") hasPlayerStart = true;
            for (const auto& b : e.brushes)
            {
                if (e.classname() == "worldspawn") ++worldBrushes;
                totalFaces += b.faces.size();
            }
        }

        std::cout << "  file: " << path << "\n";
        if (!err.empty())
            std::cout << "  parse error: " << err << "\n";
        std::cout << "  entities: " << map.entities.size()
                  << "  worldspawn brushes: " << worldBrushes
                  << "  total faces: " << totalFaces << "\n";
        for (const auto& e : map.entities)
        {
            std::cout << "    - " << e.classname();
            if (e.isPointEntity())
                std::cout << "  origin=\"" << e.getString("origin") << "\"";
            else
                std::cout << "  brushes=" << e.brushes.size();
            std::cout << "\n";
        }

        bool ok = err.empty() && hasWorldspawn && worldBrushes > 0 && hasPlayerStart;

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "entities=%zu, worldspawn brushes=%zu, player_start=%d, err=\"%s\"",
            map.entities.size(), worldBrushes, hasPlayerStart ? 1 : 0, err.c_str());
        return report("map_file", ok, buf);
    }

    bool scenarioMapScene()
    {
        std::string err;
        qmap::MapData map = qmap::loadMapFile("assets/maps/smoke.map", &err);

        Level lvl = mapWorldspawnToLevel(map);
        std::size_t surfaces = lvl.sectors.empty() ? 0 : lvl.sectors[0].surfaces.size();

        auto descriptors = mapEntitiesToDescriptors(map);
        int players = 0, lights = 0;
        for (const auto& d : descriptors)
        {
            if (d.classname == "info_player_start") ++players;
            if (d.classname == "light")             ++lights;
        }

        bool ok = err.empty()
               && lvl.sectors.size() == 1
               && surfaces == 36
               && descriptors.size() == 2
               && players == 1
               && lights == 1;

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "sectors=%zu surfaces=%zu descriptors=%zu (player=%d light=%d) err=\"%s\"",
            lvl.sectors.size(), surfaces, descriptors.size(), players, lights, err.c_str());
        return report("map_scene", ok, buf);
    }

    bool scenarioMapGroundPlacement(const std::string& path)
    {
        std::string err;
        qmap::MapData map = qmap::loadMapFile(path, &err);
        auto descriptors = mapEntitiesToDescriptors(map);

        // Mirror of spawnMonsterGrunt's AABBCollider half-Y and the loader's lift.
        constexpr float kGruntHalfY = 0.9f;
        constexpr float kEps        = 0.01f;

        int grunts = 0, sunk = 0;
        float worstFeetGap = 0.0f;   // signed: negative = feet below authored floor

        for (const auto& e : map.entities)
        {
            if (e.classname() != "monster_grunt" || !e.has("origin")) continue;
            ++grunts;

            // Authored ground point (feet target) in engine space.
            std::istringstream ss(e.getString("origin"));
            glm::vec3 m(0.0f);
            ss >> m.x >> m.y >> m.z;
            const float floorY = qmap::mapPointToEngine(m).y;

            // The matching descriptor must place the body CENTRE a half-height above
            // that ground, so the feet (centre − halfY) land exactly on the floor.
            const glm::vec3 wantCentre = qmap::mapPointToEngine(m) + glm::vec3(0.0f, kGruntHalfY, 0.0f);
            bool matched = false;
            for (const auto& d : descriptors)
            {
                if (d.classname != "monster_grunt") continue;
                if (glm::length(d.origin - wantCentre) < kEps) { matched = true; break; }
            }

            const float feetY = matched ? (wantCentre.y - kGruntHalfY) : (floorY - kGruntHalfY);
            const float gap   = feetY - floorY;
            if (std::fabs(gap) > kEps) { ++sunk; }
            if (std::fabs(gap) > std::fabs(worstFeetGap)) worstFeetGap = gap;
        }

        bool ok = err.empty() && grunts > 0 && sunk == 0;

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "grunts=%d, sunk/misplaced=%d, worst feet gap=%.3f (want ~0), err=\"%s\"",
            grunts, sunk, worstFeetGap, err.c_str());
        return report("map_ground", ok, buf);
    }

    namespace
    {
        // Count face/surface normals that are NOT axis-aligned (≥2 significant
        // components) — i.e. genuinely slanted geometry (a ramp).
        bool isSlanted(const glm::vec3& n)
        {
            int big = (std::fabs(n.x) > 0.3f) + (std::fabs(n.y) > 0.3f) + (std::fabs(n.z) > 0.3f);
            return big >= 2;
        }
    }

    bool scenarioBrushGeometry()
    {
        // A face from 3 points, wound so cross(p2-p0, p1-p0) points along `outward`
        // (the Quake/TrenchBroom CW-from-front convention buildBrushGeometry expects).
        auto face = [](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 outward)
        {
            if (glm::dot(glm::cross(c - a, b - a), outward) < 0.0f) std::swap(b, c);
            qmap::MapFace f;
            f.points[0] = a; f.points[1] = b; f.points[2] = c;
            f.texture = "wall";
            return f;
        };

        // ── Axis-aligned box, 0..64 map units → 6 faces, 8 corners ──
        qmap::MapBrush box;
        box.faces = {
            face({ 0, 0, 0}, { 0,64, 0}, { 0, 0,64}, {-1, 0, 0}),
            face({64, 0, 0}, {64,64, 0}, {64, 0,64}, { 1, 0, 0}),
            face({ 0, 0, 0}, {64, 0, 0}, { 0, 0,64}, { 0,-1, 0}),
            face({ 0,64, 0}, {64,64, 0}, { 0,64,64}, { 0, 1, 0}),
            face({ 0, 0, 0}, {64, 0, 0}, { 0,64, 0}, { 0, 0,-1}),
            face({ 0, 0,64}, {64, 0,64}, { 0,64,64}, { 0, 0, 1}),
        };
        qmap::BrushGeometry bg = qmap::buildBrushGeometry(box);

        // ── Triangular prism (wedge) extruded along Y → 5 faces, 6 corners, one
        //    slanted normal (the hypotenuse x+z=64). ──
        qmap::MapBrush wedge;
        wedge.faces = {
            face({ 0, 0, 0}, {64, 0, 0}, { 0, 0,64}, { 0,-1, 0}),   // y=0 end
            face({ 0,64, 0}, {64,64, 0}, { 0,64,64}, { 0, 1, 0}),   // y=64 end
            face({ 0, 0, 0}, {64, 0, 0}, { 0,64, 0}, { 0, 0,-1}),   // z=0 bottom
            face({ 0, 0, 0}, { 0,64, 0}, { 0, 0,64}, {-1, 0, 0}),   // x=0 side
            face({64, 0, 0}, { 0, 0,64}, {64,64, 0}, { 1, 0, 1}),   // hypotenuse (slanted)
        };
        qmap::BrushGeometry wg = qmap::buildBrushGeometry(wedge);

        int wedgeSlanted = 0;
        for (const auto& f : wg.faces) if (isSlanted(f.normal)) ++wedgeSlanted;

        bool boxOk   = bg.faces.size() == 6 && bg.vertices.size() == 8;
        bool wedgeOk = wg.faces.size() == 5 && wg.vertices.size() == 6 && wedgeSlanted >= 1;

        // ── Real data: the authored showcase.map contains a ramp brush. Load it and
        //    assert the loader produces a slanted surface + convex hulls (the failure
        //    mode the synthetic brushes above missed: real TrenchBroom faces are tiny
        //    plane-defining triangles, so the winding convention — not a face-point
        //    average — must drive the half-space test). ──
        std::string err;
        qmap::MapData realMap = qmap::loadMapFile("assets/maps/showcase.map", &err);
        Level level = mapWorldspawnToLevel(realMap);
        int slantedSurfaces = 0;
        for (const auto& sec : level.sectors)
            for (const auto& s : sec.surfaces) if (isSlanted(s.normal)) ++slantedSurfaces;
        bool realOk = err.empty() && !level.sectors.empty()
                   && !level.collisionHulls.empty() && slantedSurfaces > 0;

        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "box %zu/%zu (want 6/8); wedge %zu/%zu/%d slanted (want 5/6/>=1); showcase.map: "
            "%zu hulls, %d slanted surfaces (want >0 each)",
            bg.faces.size(), bg.vertices.size(), wg.faces.size(), wg.vertices.size(), wedgeSlanted,
            level.collisionHulls.size(), slantedSurfaces);
        return report("brush_geometry", boxOk && wedgeOk && realOk, buf);
    }
}
