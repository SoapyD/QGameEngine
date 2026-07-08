# Plan — TrenchBroom: Texture Loading & UV Fidelity

**Group:** `trenchbroom`. **Status:** 📝 Proposed 2026-07-09. **Priority: MEDIUM** — needed for authored
levels to *look* right; do after brush-entities.

**Goal:** a loaded `.map` renders with **any** texture it references, **correctly aligned** — load
textures on demand, and project UVs from each face's Quake texture parameters (offset / rotation /
scale) instead of the current stretched planar UVs.

---

## Why (current limitation)
1. **Texture loading** — only textures the engine **preloads** by name (`wall`, `grid_*`) resolve.
   A map that references any other texture name gets a stub (`textureId 0`), so it renders untextured.
2. **UV fidelity** — `build_textured_meshes` computes UVs from **edge lengths** (planar stretch),
   ignoring the face's `offset`/`rotation`/`scale` (parsed into `MapFace` but unused). Textures don't
   tile or align — obvious on large faces and the new angled/ramp geometry.

## Scope
| # | Task | Notes |
|---|------|-------|
| 1 | **On-demand texture resolution** | When a surface's `textureName` isn't cached, load `assets/textures/<name>.png` via `ResourceManager` (cache it); fall back to a default (e.g. `grid_grey`) on miss, logged once. Wire into the `.map` render-mesh build. |
| 2 | **Standard Quake UV projection** | Pick the projection axis from the face normal (dominant-axis of the 6 axial planes), then `u = dot(p, uAxis)/scale.x + offset.x`, `v = …`, with `rotation` about the face. Replace the edge-length UVs in `build_textured_meshes` (and `build_sector_meshes` for parity). Compute in map space (texels), independent of the engine scale. |
| 3 | **Per-texture atlas/groups** | Already grouped per texture name in `build_textured_meshes` — keep; just feed real texture ids + real UVs. |

## Notes
- `MapFace` stores `offset`/`rotation`/`scale` only (Quake **Standard** format), so standard-axis
  projection is correct. If Valve-220 maps are ever authored (explicit U/V axes), extend the parser +
  `MapFace` first — out of scope here.
- Headless can't check pixels; assert the **computed UVs** for a known face against expected values,
  and eyeball alignment in the windowed build.

## Verification
Headless `map_uv` (or extend `map_scene`): build a face with a known texture + non-zero offset/scale,
assert the resulting `Vertex` UVs match the hand-computed projection. Windowed: load a textured map,
confirm textures tile + align on walls and the ramp.

## Docs to update on ship
`status/rendering.md`, `SYSTEMS.md` (render/build_textured_meshes), the
[engine-loader plan](2026-07-03_trenchbroom_engine-loader.md) deferred list.
