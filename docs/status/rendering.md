# Status: Rendering (Draw, Lighting, HUD)

**State:** ✅ working · _verified 2026-06-14_

## Works
- Mesh drawing for all `MeshRenderer` entities; optional `Scale`/`Colour` tint.
- Phong lighting: one directional light + point lights.
- Four shaders (basic/textured/lit/hud), name-cached via `ResourceManager`.
- `TagDebugWireframe` entities draw in line mode.
- Debug text HUD (FPS, position, health, weapon, ammo) via `stb_easy_font`.

## Known gaps / risks
- Debug HUD only — no crosshair, no production HUD/menus (🔴).
- No shadows, no transparency/blending pass, single directional light.
- HUD/render take params directly (camera/window/fps) rather than from ctx — fine,
  but inconsistent with the ctx-singleton pattern elsewhere.

## Next
- Add crosshair + a real HUD pass when gameplay needs it.

Process: [`../processes/rendering.md`](../processes/rendering.md)
