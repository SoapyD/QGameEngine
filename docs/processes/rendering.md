# Process: Rendering (Draw, Lighting, HUD)

**Purpose:** Draw all visible entities with Phong lighting, and overlay a debug
text HUD. Runs once per frame (not per fixed tick).

**Systems:** `renderSystem` + `debugHudSystem`.
`src/engine/ecs/systems/render_system.{h,cpp}`, `debug_hud_system.{h,cpp}`.

## Flow (per frame)

```
renderSystem(camera, aspectRatio):
   build view + projection from Camera
   upload light uniforms (DirectionalLight + PointLights)
   for each entity with MeshRenderer:
       model = translate(Position) · scale(Scale?)
       bind shader/VAO/texture; apply Colour tint if present
       TagDebugWireframe → draw GL_LINE, else GL_TRIANGLES

debugHudSystem(w, h, fps):
   stb_easy_font text → FPS, player Position, Health, weapon, Ammo
   drawn with the HUD shader (orthographic)
```

## Shaders

`basic` (unlit solid), `textured` (unlit textured), `lit` (textured + Phong),
`hud` (2D orthographic). Loaded/cached by `ResourceManager`; components store raw
GL handles, not objects.

## Components

| Component | System | Access |
|-----------|--------|--------|
| `MeshRenderer` | render | read (VAO, shader, texture, index count) |
| `Position`, `Scale`, `Colour` | render | read |
| `DirectionalLight`, `PointLight` | render | read |
| `TagDebugWireframe` | render | read (line mode) |
| `Health`, `WeaponInventory`, `Ammo`, `TagPlayer` | hud | read |

**Context:** `HudConfig` (HUD shader id). Both systems take camera/window/fps
params directly rather than from the registry.

See also: [`../architecture/SYSTEMS.md`](../architecture/SYSTEMS.md#10-rendersystem),
[status](../status/rendering.md).
