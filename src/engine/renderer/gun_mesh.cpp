#include "engine/renderer/gun_mesh.h"

#include "engine/renderer/mesh.h"
#include "engine/ecs/components/core.h"   // Vertex

#include <vector>

namespace
{
    // Append an axis-aligned box (centre, halfExtents) to the vertex/index lists
    // with outward per-face normals and CCW winding (front-facing under back-face
    // culling). Six faces, four verts each.
    void appendBox(std::vector<Vertex>& v, std::vector<unsigned int>& idx,
                   glm::vec3 c, glm::vec3 h)
    {
        struct Face { glm::vec3 n, u, w; };
        const Face faces[6] = {
            {{ 0, 0, 1},{ 1,0,0},{0,1, 0}}, // +Z
            {{ 0, 0,-1},{-1,0,0},{0,1, 0}}, // -Z
            {{ 1, 0, 0},{ 0,0,-1},{0,1,0}}, // +X
            {{-1, 0, 0},{ 0,0, 1},{0,1,0}}, // -X
            {{ 0, 1, 0},{ 1,0,0},{0,0,-1}}, // +Y
            {{ 0,-1, 0},{ 1,0,0},{0,0, 1}}, // -Y
        };
        for (const Face& f : faces)
        {
            unsigned int base = (unsigned int)v.size();
            glm::vec3 uu = f.u * h;
            glm::vec3 ww = f.w * h;
            glm::vec3 centre = c + f.n * h;
            glm::vec3 corners[4] = {
                centre - uu - ww, centre + uu - ww,
                centre + uu + ww, centre - uu + ww,
            };
            for (int i = 0; i < 4; ++i)
                v.push_back(Vertex{ corners[i], f.n, glm::vec2(0.0f) });
            idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
    }
}

std::shared_ptr<Mesh> buildGunMesh(WeaponType type)
{
    std::vector<Vertex> v;
    std::vector<unsigned int> i;

    // Grip below/behind the body — common to every weapon.
    appendBox(v, i, { 0.00f, -0.06f,  0.05f }, { 0.030f, 0.060f, 0.040f });

    switch (type)
    {
        case WeaponType::Shotgun: // stubby side-by-side double barrel
            appendBox(v, i, { 0.00f, 0.00f, -0.05f }, { 0.050f, 0.050f, 0.120f });
            appendBox(v, i, {-0.030f, 0.00f, -0.28f }, { 0.025f, 0.030f, 0.140f });
            appendBox(v, i, { 0.030f, 0.00f, -0.28f }, { 0.025f, 0.030f, 0.140f });
            break;

        case WeaponType::SuperShotgun: // wider, shorter double barrel
            appendBox(v, i, { 0.00f, 0.00f, -0.05f }, { 0.060f, 0.055f, 0.120f });
            appendBox(v, i, {-0.045f, 0.00f, -0.24f }, { 0.035f, 0.040f, 0.120f });
            appendBox(v, i, { 0.045f, 0.00f, -0.24f }, { 0.035f, 0.040f, 0.120f });
            break;

        case WeaponType::Nailgun: // two thin stacked barrels
            appendBox(v, i, { 0.00f, 0.00f, -0.05f }, { 0.045f, 0.050f, 0.120f });
            appendBox(v, i, { 0.00f, 0.020f, -0.32f }, { 0.015f, 0.015f, 0.180f });
            appendBox(v, i, { 0.00f,-0.020f, -0.32f }, { 0.015f, 0.015f, 0.180f });
            break;

        case WeaponType::RocketLauncher: // one fat tube
            appendBox(v, i, { 0.00f, 0.00f, -0.10f }, { 0.070f, 0.070f, 0.280f });
            break;

        case WeaponType::GrenadeLauncher: // fat short barrel + a drum on top
            appendBox(v, i, { 0.00f, 0.00f, -0.05f }, { 0.050f, 0.050f, 0.100f });
            appendBox(v, i, { 0.00f, 0.00f, -0.24f }, { 0.060f, 0.060f, 0.120f });
            appendBox(v, i, { 0.00f, 0.070f, -0.05f }, { 0.040f, 0.040f, 0.050f });
            break;

        case WeaponType::LighteningGun: // thin barrel with two prongs at the tip
            appendBox(v, i, { 0.00f, 0.00f, -0.05f }, { 0.045f, 0.050f, 0.120f });
            appendBox(v, i, { 0.00f, 0.00f, -0.30f }, { 0.020f, 0.020f, 0.160f });
            appendBox(v, i, {-0.030f, 0.00f, -0.46f }, { 0.010f, 0.030f, 0.030f });
            appendBox(v, i, { 0.030f, 0.00f, -0.46f }, { 0.010f, 0.030f, 0.030f });
            break;

        case WeaponType::Railgun: // very long, thin barrel
            appendBox(v, i, { 0.00f, 0.00f, -0.05f }, { 0.045f, 0.050f, 0.120f });
            appendBox(v, i, { 0.00f, 0.010f, -0.42f }, { 0.018f, 0.018f, 0.300f });
            break;
    }

    return std::make_shared<Mesh>(v, i);
}

glm::vec3 weaponColor(WeaponType type)
{
    switch (type)
    {
        case WeaponType::Shotgun:         return { 0.85f, 0.22f, 0.20f }; // red
        case WeaponType::SuperShotgun:    return { 0.55f, 0.10f, 0.12f }; // crimson
        case WeaponType::Nailgun:         return { 0.90f, 0.80f, 0.20f }; // yellow
        case WeaponType::RocketLauncher:  return { 0.95f, 0.55f, 0.15f }; // orange
        case WeaponType::GrenadeLauncher: return { 0.30f, 0.72f, 0.28f }; // green
        case WeaponType::LighteningGun:   return { 0.30f, 0.78f, 0.95f }; // cyan
        case WeaponType::Railgun:         return { 0.72f, 0.32f, 0.88f }; // purple
    }
    return { 0.80f, 0.80f, 0.80f };
}

const char* weaponAbbrev(WeaponType type)
{
    switch (type)
    {
        case WeaponType::Shotgun:         return "SG";
        case WeaponType::SuperShotgun:    return "SSG";
        case WeaponType::Nailgun:         return "NG";
        case WeaponType::RocketLauncher:  return "RL";
        case WeaponType::GrenadeLauncher: return "GL";
        case WeaponType::LighteningGun:   return "LG";
        case WeaponType::Railgun:         return "RG";
    }
    return "?";
}
