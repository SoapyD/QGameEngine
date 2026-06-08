# 04 — Renderer

**Status:** Evaluated.
**Scope:** `renderer/{shader,texture,mesh,obj_loader,camera,stb_image_impl}.*`, `ecs/systems/render_system.cpp`, `ecs/systems/debug_hud_system.cpp`.
**Part of:** [README.md](README.md).

## Findings

| # | Issue | Sev | Detail | Action | Location |
|---|-------|-----|--------|--------|----------|
| 4.1 | **Silent shader failure** | P1 | `checkErrors` logs compile/link failures but the ctor proceeds; an invalid/unlinked program is then used → black screen with no fatal error. Same "log and continue" pattern as the Window ctor. | Treat compile/link failure as fatal (throw / flag invalid) and surface it. | [shader.cpp:110-132](../../../src/engine/renderer/shader.cpp#L110-L132) |
| 4.2 | Missing-asset paths degrade silently | P1 | `Shader::readFile` returns `""`, `Texture` returns with `m_textureId==0`, `loadOBJ` returns an empty mesh — all on missing files, none propagate. Cascades into 4.1 / blank draws. | Propagate load failures from the resource layer. | [shader.cpp:83-95](../../../src/engine/renderer/shader.cpp#L83-L95), [texture.cpp:13-17](../../../src/engine/renderer/texture.cpp#L13-L17) |
| 4.3 | OBJ loader crash on malformed input | P2 | `std::stoi` (no try/catch) and `tempPositions[posIdx]` (no bounds check) → UB/throw on a bad OBJ. Negative (relative) OBJ indices unsupported. | Validate indices; guard `stoi`. | [obj_loader.cpp:83-118](../../../src/engine/renderer/obj_loader.cpp#L83-L118) |
| 4.4 | Per-entity uniform re-query | P2 | `renderSystem` calls `glGetUniformLocation` (string lookup) for **every** uniform on **every** entity **every** frame, and re-uploads all light data per entity. Fine now; a Phase-5 brush-heavy level makes it a hotspot. | Cache uniform locations; hoist light uploads; consider a UBO. | [render_system.cpp:84-136](../../../src/engine/ecs/systems/render_system.cpp#L84-L136) |
| 4.5 | `MAX_POINT_LIGHTS` divergence risk | P2 | Hardcoded 8 in C++; must match the shader array size or lights silently drop. | Single source of truth (shared constant / shader define). | [render_system.cpp:39](../../../src/engine/ecs/systems/render_system.cpp#L39) |
| 4.6 | No transparency support | P2 | `Colour` carries alpha but blending is never enabled and there's no depth sort. | Decide if needed; document if not. | render_system.cpp |
| 4.7 | Texture 2-channel format unhandled | P2 | `m_channels==2` falls through to `GL_RGB` (wrong). | Handle `GL_RG`. | [texture.cpp:20-23](../../../src/engine/renderer/texture.cpp#L20-L23) |
| 4.8 | `Camera::processKeyboard` dead | P2 | Movement now goes through the player system; this method is unused. | Remove. | [camera.cpp:24](../../../src/engine/renderer/camera.cpp#L24) |
| 4.9 | Mesh attribute comments wrong | P3 | Copy-paste: texCoords labelled "Normal (location 2)", strides commented "6 floats". Cosmetic. | Fix comments. | [mesh.cpp:96-104](../../../src/engine/renderer/mesh.cpp#L96-L104) |

**Good:** `Mesh` move semantics + `cleanup()` are correct (no GL leak on move/destroy); `Texture`/`Shader` dtors delete their objects.

## Graduates to a fix plan
4.1 + 4.2 + 4.3 = `docs/plans/renderer-robustness-fixes.md` (the silent-failure family — highest value: turns "black screen" into a clear error). 4.4 is a separate perf task, best deferred until Phase-5 load profiling.
