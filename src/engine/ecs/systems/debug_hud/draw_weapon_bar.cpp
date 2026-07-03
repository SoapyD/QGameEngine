#include "engine/ecs/systems/debug_hud/debug_hud_internal.h"

#include "engine/ecs/components.h"        // WeaponInventory, TagPlayer
#include "engine/renderer/gun_mesh.h"     // weaponColor, weaponAbbrev

#include <cstdio>

// Top-of-screen weapon bar. One cell per weapon slot (1-7): the active weapon
// gets a bright coloured panel, owned weapons show their colour on a dark panel,
// and un-collected weapons are dimmed grey — so you can see what's available and
// which number selects it.
void drawWeaponBar(entt::registry& registry, int windowWidth, unsigned int shaderId,
                   const glm::mat4& projection, float scale)
{
    const WeaponInventory* inv = nullptr;
    for (auto [e, wi] : registry.view<WeaponInventory, TagPlayer>().each())
    {
        inv = &wi;
        break;
    }
    if (!inv) return;

    const int   N     = 7;
    const float cellW = 74.0f;
    const float cellH = 24.0f;
    const float gap   = 6.0f;
    const float total = N * cellW + (N - 1) * gap;
    const float x0    = (windowWidth - total) * 0.5f;
    const float y     = 8.0f;

    for (int i = 0; i < N; ++i)
    {
        float x = x0 + i * (cellW + gap);
        WeaponType t = static_cast<WeaponType>(i);
        bool owned   = inv->owned[i];
        bool current = owned && inv->currentWeapon == i;
        glm::vec3 wc = weaponColor(t);

        glm::vec3 panelColor;
        float     panelAlpha;
        glm::vec3 textColor;
        if (current)                       // active weapon: bright coloured cell
        {
            panelColor = wc;
            panelAlpha = 0.90f;
            textColor  = glm::vec3(0.05f);
        }
        else if (owned)                    // owned: colour text on a dark cell
        {
            panelColor = glm::vec3(0.08f);
            panelAlpha = 0.60f;
            textColor  = wc;
        }
        else                               // not collected: greyed out
        {
            panelColor = glm::vec3(0.12f);
            panelAlpha = 0.35f;
            textColor  = glm::vec3(0.40f);
        }

        drawPanel(x, y, cellW, cellH, shaderId, projection, panelColor, panelAlpha);

        char label[16];
        std::snprintf(label, sizeof(label), "%d %s", i + 1, weaponAbbrev(t));
        drawText(x + 6.0f, y + 5.0f, label, shaderId, projection, scale, textColor);
    }
}
