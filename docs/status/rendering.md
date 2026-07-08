# Status: Rendering (Draw, Lighting, HUD)

**State:** ✅ working · _verified 2026-07-08_

## Works
- Mesh drawing for all `MeshRenderer` entities; optional `Scale`/`Colour` tint.
- Phong lighting: one directional light + point lights.
- Four shaders (basic/textured/lit/hud), name-cached via `ResourceManager`.
- `TagDebugWireframe` entities draw in line mode.
- **HUD** (`debug_hud/`): health/armour bars, ammo (red when low), weapon bar,
  pickup toast, damage-flash overlay, and — new — a **dynamic crosshair** (gap =
  weapon spread + movement + recoil), **hit/kill markers**, and a
  **damage-direction chevron**. FPS/debug text gated behind `HudSignals::showDebug`.
- HUD *state* lives in `HudSignals` (registry context), recomputed each tick by
  `hudSignalSystem` and only *drawn* by the GL HUD — so it's exercised headless
  (`hud_signals` scenario) even though the draw needs a GL context.

## Known gaps / risks
- No shadows, no transparency/blending pass, single directional light.
- HUD draw takes params directly (camera/window/fps) rather than from ctx — fine,
  but inconsistent with the ctx-singleton pattern elsewhere.
- Deferred HUD: floating enemy health bar (needs world→screen projection), a
  debug-toggle key (the `showDebug` flag exists; nothing flips it yet), menus.

## Next
- Wire a key to toggle `showDebug`; add the enemy health bar when needed.

Process: [`../processes/rendering.md`](../processes/rendering.md)
