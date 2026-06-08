# QEngine — Nice-to-Haves Roadmap

Features that turn the engine into a more complete game. None of these are blocking — the core 21 chapters give you a playable FPS. These are ordered by impact and difficulty.

---

## Phase A: Game States & Menus (Chapters 21-22)

The engine currently drops straight into gameplay. A real game needs state management.

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 21 | Game State Machine | State stack (Menu, Playing, Paused, GameOver), state transitions, input routing per state | Ch 15 (HUD) |
| 22 | Main Menu & Pause Screen | Menu rendering reusing HUD system, button highlighting, settings (resolution, volume, sensitivity) | Ch 21 |

### What You'll Build
- A `GameState` base with `enter()`, `update()`, `render()`, `exit()`
- State stack so Pause overlays Playing (doesn't destroy it)
- Main menu with New Game, Settings, Quit
- Pause menu with Resume, Settings, Quit to Menu
- Settings screen controlling audio volume (Ch 16) and mouse sensitivity

### ECS Approach
States aren't entities — they're the context that decides which systems run. When paused, physics/AI systems stop ticking but the render system still draws.

```
GameStateStack:
┌──────────────┐
│  PauseState  │  ← Receives input, renders overlay
├──────────────┤
│ PlayingState  │  ← Still renders (frozen), systems paused
└──────────────┘
```

---

## Phase B: Save/Load (Chapter 23)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 23 | Save & Load System | ECS serialisation, component visitors, JSON/binary formats | Ch 3 (ECS), Ch 21 (states) |

### What You'll Build
- Serialise the entire `entt::registry` to disk
- Save: iterate all entities, write each component's data
- Load: clear registry, recreate entities from saved data
- Save slots with metadata (timestamp, level name, screenshot)

### Key Challenges
- EnTT doesn't have built-in serialisation — you need a visitor pattern or manual per-component serialise/deserialise functions
- Pointers and handles (mesh IDs, sound handles) need to be saved as names/paths, then re-resolved on load
- Entity references (e.g. a trigger targeting a door) need stable IDs that survive save/load

### Suggested Format
JSON for readability during development, with an option to switch to binary later for speed:
```json
{
  "level": "e1m1",
  "entities": [
    {
      "components": {
        "Position": { "x": 3.0, "y": 1.0, "z": -5.0 },
        "Health": { "current": 75, "max": 100 },
        "TagPlayer": {}
      }
    }
  ]
}
```

---

## Phase C: Skybox (Chapter 24)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 24 | Skybox | Cubemap textures, skybox shader, depth trick, environment mapping | Ch 5 (Textures), Ch 7 (Lighting) |

### What You'll Build
- Load 6 textures into a GL cubemap
- A cube rendered at maximum depth (drawn first, depth test set to `GL_LEQUAL`)
- Strip translation from the view matrix so the skybox never moves relative to the camera
- Optional: sample the cubemap for ambient lighting (environment mapping)

### Render Order Update
```
1. Skybox (depth write off, drawn first)
2. Opaque level geometry
3. Opaque entities
4. Transparent/additive (particles, effects)
5. HUD (orthographic, depth test off)
```

### Size
This is a small feature — roughly half a chapter. The shader is ~15 lines, the loader reuses stb_image, and the rendering is a single draw call.

---

## Phase D: Extended Content (Chapters 25-26)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 25 | Weapon Animations & View Models | View model rendering, keyframe animation, procedural recoil/sway | Ch 12 (Weapons), Ch 20 (Polish) |
| 26 | Boss Fights & Arenas | Multi-phase AI, arena triggers, spawn waves, health gates | Ch 14 (AI), Ch 11 (Triggers) |

### Chapter 25: View Models
The player's weapon visible on screen (the "view model"):
- Rendered in a separate pass with a narrower FOV (prevents clipping into walls)
- Keyframe animation for reload, fire, idle sway
- Procedural additions: recoil kick (Ch 20), movement bob (Ch 20)
- Weapon switching animation (lower old, raise new)

This builds heavily on the interpolation functions from Chapter 20.

### Chapter 26: Boss Fights
Not a new system — it's a demonstration of combining existing systems:
- Multi-phase `AIBrain` with health-gated state transitions
- Arena triggers that lock doors and start spawn waves
- Custom attack patterns using projectile spawning (Ch 12)
- Death sequence using particles (Ch 20) and audio (Ch 16)

The point is to show that the ECS architecture handles complex gameplay without new engine features — just new component data and system logic.

---

## Phase E: Quality of Life (Chapter 27)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 27 | Developer Console & Debug Tools | In-game console, command parsing, debug rendering (wireframe AABBs, nav graph overlay), FPS counter | Ch 15 (HUD), Ch 9 (Collision) |

### What You'll Build
- Toggle console with ~ key
- Text input using GLFW character callbacks
- Command parser: `god`, `noclip`, `give health 100`, `map e1m2`, `spawn grunt`
- Debug rendering: wireframe AABBs, trigger volumes, AI sight lines, nav graph
- FPS/frame time counter in corner

### Why It Matters
Debug tools pay for themselves immediately. Being able to type `noclip` and fly through walls, or see collision boxes rendered, makes every other feature faster to develop and debug.

---

---

## Phase F: Rendering Upgrades (Chapters 28-29)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 28 | Framebuffers & Post-Processing | Render-to-texture, FBO setup, full-screen quad, bloom, vignette, damage flash, colour grading | Ch 4 (Transforms), Ch 7 (Lighting) |
| 29 | Shadow Mapping | Depth-only pass from light's POV, shadow map FBO, PCF filtering, shadow acne/peter-panning, cascaded shadow maps | Ch 28 (Framebuffers), Ch 7 (Lighting) |

### Chapter 28: Post-Processing
Render the scene to a texture instead of directly to the screen, then draw a full-screen quad with effects applied:
- Framebuffer objects (FBOs) — the foundational concept
- Bloom (bright-pass filter + Gaussian blur + additive blend)
- Vignette, colour grading, damage flash overlay
- Retro CRT shader as a fun optional effect

This chapter is prerequisite for shadows (Ch 29) since shadow mapping also uses render-to-texture.

### Chapter 29: Shadow Mapping
A single directional light shadow map:
- Render scene depth from the light's perspective into an FBO
- Sample shadow map in the main fragment shader to determine if a pixel is in shadow
- Fixes: shadow acne (bias), peter-panning, percentage-closer filtering (PCF) for soft edges
- Optional: cascaded shadow maps (CSM) for large outdoor levels

---

## Phase G: Text & Visual Effects (Chapters 30-31)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 30 | Font Rendering | Bitmap font atlas, glyph metrics, FreeType loading, text batching, screen-space text | Ch 5 (Textures), Ch 15 (HUD) |
| 31 | Decals | Projected quads onto surfaces, decal volume intersection, texture atlas, fade/lifetime | Ch 5 (Textures), Ch 9 (Collision) |

### Chapter 30: Font Rendering
Replace hardcoded HUD rendering with a proper text system:
- Bitmap font atlas approach (generate offline or with FreeType at startup)
- Glyph metrics: advance, bearing, kerning
- Text batching — one draw call for all text per frame
- Supports the dev console (Ch 27) and menus (Ch 22) with proper text rendering

### Chapter 31: Decals
Bullet holes, blood splatter, scorch marks:
- Project a textured quad onto the nearest surface using the hit normal
- Decal texture atlas for different impact types
- Lifetime and fade-out so decals don't accumulate forever
- ECS integration: Decal component with position, normal, timer

---

## Phase H: Performance (Chapter 32)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 32 | Frustum Culling | View frustum extraction from VP matrix, plane-AABB test, culling before draw calls | Ch 4 (Transforms), Ch 9 (Collision/AABBs) |

### What You'll Build
- Extract 6 frustum planes from the view-projection matrix
- Test each entity's AABB against the frustum before submitting draw calls
- Skip rendering for anything entirely outside the camera's view
- Debug visualisation: render the frustum wireframe with the dev console (Ch 27)

### Why It Matters
Without culling, the renderer draws everything every frame regardless of visibility. For large levels this is a significant performance cost. Frustum culling is the simplest and most impactful optimisation.

---

## Phase I: Animation & World (Chapters 33-34)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 33 | Skeletal Animation | Bone hierarchy, joint transforms, skinning shader, animation clips, blending | Ch 6 (Meshes), Ch 4 (Transforms) |
| 34 | Level Transitions | Map change triggers, registry clearing, persistent player state, loading screens | Ch 8 (Levels), Ch 21 (Game States), Ch 23 (Save/Load) |

### Chapter 33: Skeletal Animation
Give enemies walk cycles, attack animations, and death animations:
- Bone hierarchy as a tree of transforms
- Skinning: each vertex is influenced by up to 4 bones (weights)
- Animation clips: arrays of bone poses at keyframes (extending Ch 25's keyframe concept)
- Animation blending: cross-fade between idle → walk → attack
- GPU skinning in the vertex shader
- Loading from a simple format (glTF subset or custom)

### Chapter 34: Level Transitions
Moving between maps (e1m1 → e1m2):
- Trigger volume at level exit (reusing Ch 11 triggers)
- What to preserve: player health, ammo, weapons, score
- What to destroy: all other entities, level geometry
- Loading screen state (reusing Ch 22's LoadingState)
- The `map` console command (Ch 27) also triggers this flow

---

---

## Phase J: Surface Detail (Chapter 35)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 35 | Normal Mapping | Tangent space, TBN matrix, normal map textures, per-pixel lighting detail | Ch 5 (Textures), Ch 7 (Lighting) |

### Chapter 35: Normal Mapping
Add per-pixel surface detail without extra geometry:
- Normal map textures (blue-ish images encoding surface normals)
- Tangent space and the TBN (Tangent, Bitangent, Normal) matrix
- Modified fragment shader: sample normal from texture, transform to world space, use in lighting calculation
- Huge visual upgrade — bricks look like bricks, metal looks like metal
- Optional: parallax mapping for even more depth illusion

---

## Phase K: Asset Pipeline (Chapter 36)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 36 | Model Loading (OBJ & glTF) | OBJ format parsing, glTF binary format, mesh/material extraction, asset caching | Ch 6 (Meshes), Ch 33 (Skeletal Animation) |

### Chapter 36: Model Loading
Load artist-created 3D models into the engine:
- OBJ format: simple text parser for vertices, normals, UVs, faces (~150 lines)
- MTL material files: diffuse/specular textures, colours
- glTF overview: JSON + binary buffers, meshes, materials, skeleton, animations
- Asset caching: load each model once, share across entities
- Integration: MeshRenderer component references loaded model by name

---

## Phase L: AI Navigation (Chapter 37)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 37 | Pathfinding (A* & Nav Mesh) | A* algorithm, nav mesh generation, path smoothing, steering behaviours | Ch 14 (AI), Ch 9 (Collision) |

### Chapter 37: Pathfinding
Give enemies the ability to navigate around obstacles:
- A* search algorithm on a grid or nav mesh
- Nav mesh: walkable polygons generated from level geometry
- Path smoothing: funnel algorithm to remove unnecessary waypoints
- Steering: following a path with velocity and avoidance
- Integration with AI system: enemies request paths, follow waypoints

---

## Phase M: Rendering Performance (Chapter 38)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 38 | Instanced Rendering | glDrawElementsInstanced, instance buffers, per-instance data, vegetation/debris | Ch 6 (Meshes), Ch 32 (Frustum Culling) |

### Chapter 38: Instanced Rendering
Draw thousands of similar objects in a single draw call:
- The problem: many draw calls for identical meshes (grass, debris, columns, crates)
- glDrawElementsInstanced: one draw call, many copies
- Instance buffer: per-instance model matrices via vertex attributes (divisor = 1)
- Combining with frustum culling: only instance visible objects
- Use case: vegetation, debris fields, repeated architecture

---

## Phase N: Environmental Effects (Chapter 39)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 39 | Water & Liquid Rendering | Planar reflections, refraction with distortion, underwater fog, swimming mechanics | Ch 28 (Post-Processing), Ch 24 (Skybox) |

### Chapter 39: Water & Liquid Rendering
Render convincing water surfaces and underwater effects:
- Water plane with animated UV scrolling for wave motion
- Planar reflection: render scene flipped, clip plane, reflection FBO
- Refraction: render scene below water into FBO, distort UVs with a dudv map
- Fresnel effect: blend reflection/refraction based on view angle
- Underwater: tint screen blue-green, add fog, distort view (post-processing from Ch 28)
- Swimming trigger: modify player physics when submerged (reduced gravity, slower movement)

---

## Phase O: Animation Polish (Chapters 40-42)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 40 | Animation Events & Notifies | Frame-triggered callbacks, event channels, footstep sounds, muzzle flash sync, damage windows | Ch 33 (Skeletal Animation), Ch 16 (Audio) |
| 41 | Ragdoll Physics | Bone-to-rigidbody mapping, joint constraints, animation-to-ragdoll transition, impulse forces | Ch 33 (Skeletal Animation), Ch 10 (Physics) |
| 42 | Animation Layers & Partial Body | Bone masks, upper/lower body split, additive blending, layer priorities | Ch 33 (Skeletal Animation) |

### Chapter 40: Animation Events & Notifies
Trigger gameplay actions at specific animation frames:
- Event data embedded in animation clips (frame number + event type + parameters)
- Notify system: animation system fires events, other systems listen (footsteps, particles, sounds)
- Damage windows: melee attacks only deal damage during specific frames
- Sound sync: footstep sounds on foot-down frames, reload click at magazine insert frame
- Particle sync: muzzle flash at fire frame, shell casing eject
- ECS integration: AnimationEventQueue component, eventDispatchSystem drains it each frame

### Chapter 41: Ragdoll Physics
Physics-driven death animations:
- Map skeleton bones to physics rigid bodies (capsules, boxes)
- Joint constraints between connected bones (hinge for elbows/knees, cone-twist for shoulders)
- Transition: on death, copy current bone transforms to rigid bodies, disable animation, enable physics
- Impulse: apply force from the killing blow's direction for satisfying death reactions
- Settling: after ragdoll stops moving, freeze physics to save performance
- ECS integration: Ragdoll component with body references, ragdollTransitionSystem

### Chapter 42: Animation Layers & Partial Body
Play different animations on different body parts simultaneously:
- Bone masks: define which bones each layer controls (upper body vs lower body)
- Layer system: base layer (locomotion) + overlay layer (shooting/reloading)
- Additive blending: add an animation on top of another (flinch overlay, breathing)
- Priority and weight: layers can override or blend with layers below
- Use case: enemy walks and shoots simultaneously, player reloads while strafing
- ECS integration: AnimationLayer component array, multi-layer evaluation in animationSystem

---

## Phase P: Advanced Rendering & Animation (Chapters 43-44)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 43 | Inverse Kinematics (IK) | Two-bone IK solver, foot placement, IK targets, CCD algorithm | Ch 33 (Skeletal Animation) |
| 44 | PBR Materials | Metallic-roughness workflow, Cook-Torrance BRDF, IBL, HDR environment maps | Ch 7 (Lighting), Ch 35 (Normal Mapping), Ch 28 (Post-Processing) |

### Chapter 43: Inverse Kinematics
Procedurally adjust bone positions to reach target points:
- Forward vs inverse kinematics: FK plays authored poses, IK solves for a target
- Two-bone IK solver: the classic arm/leg solution (law of cosines)
- Foot placement: raycast down from hips, IK feet onto terrain surface, adjust pelvis height
- Hand IK: hands gripping weapons, ledges, or objects at runtime
- CCD (Cyclic Coordinate Descent): general-purpose IK for chains of any length
- Blending IK with authored animation: IK as a post-process on top of skeletal animation
- ECS integration: IKTarget component, ikSystem runs after animationSystem

### Chapter 44: PBR Materials
Physically-based rendering replacing Phong lighting:
- Why PBR: energy conservation, materials look correct under any lighting
- Metallic-roughness workflow: albedo, metallic, roughness, AO texture maps
- Cook-Torrance specular BRDF: normal distribution (GGX), geometry (Smith), Fresnel (Schlick)
- Diffuse: Lambertian divided by pi (energy-conserving)
- Image-Based Lighting (IBL): prefiltered environment map for specular, irradiance map for diffuse
- HDR rendering: render to floating-point FBO, tonemap in post-processing (Ch 28)
- Migration path: swap Phong shader for PBR shader, existing normal maps (Ch 35) still work
- ECS integration: PBRMaterial component replacing the existing material reference

---

## Phase Q: Advanced Particles (Chapters 45-46)

| Ch | Title | Key Concepts | Depends On |
|----|-------|-------------|------------|
| 45 | Advanced Particle Physics & Rendering | Particle-world collision, bouncing/friction, drag/wind forces, trails/ribbons, flipbook texture animation | Ch 20 (Particles), Ch 9 (Collision) |
| 46 | Data-Driven Particle Effects | JSON particle definitions, emitter properties, sub-emitters, effect library (blood, fire, smoke, sparks, explosions) | Ch 45 (Advanced Particles), Ch 40 (Animation Events) |

### Chapter 45: Advanced Particle Physics & Rendering
Upgrade the basic particle pool from Ch 20 into a full-featured particle system:
- Particle-world collision: raycast or sphere-cast each particle against level geometry, bounce with restitution and friction
- Forces beyond gravity: drag (velocity-dependent resistance), wind (global/local force fields), turbulence (noise-based perturbation)
- Particle trails/ribbons: connected strip geometry following a particle's path (rocket trails, bullet tracers, energy beams)
- Flipbook texture animation: texture atlas with animation frames, UV offset per particle age (animated fire, smoke puffs, explosions)
- Particle rotation and angular velocity for tumbling debris
- Soft particles: fade near geometry using depth buffer comparison (prevents hard intersection lines)

### Chapter 46: Data-Driven Particle Effects
Author particle effects in JSON instead of hardcoded C++:
- ParticleEffectDef: emitter shape (point, sphere, cone, box), emission rate, particle properties (lifetime range, speed range, colour curve, size curve)
- Sub-emitters: particles that spawn child particles on birth, death, or collision (firework chains, sparks from bouncing debris, blood dripping)
- Effect library with complete definitions: blood splatter (on hit + drip sub-emitter), fire (flickering + smoke + ember sub-emitters), explosions (flash + debris + smoke + shockwave), bullet impact (sparks + dust), rocket trail (smoke ribbon + ember sub-emitter)
- ParticleEffect component and particleEffectSystem replacing hardcoded spawn functions
- Integration with animation events (Ch 40): effects triggered by name from animation clips

---

## Summary Table

| Phase | Chapters | Effort | Impact |
|-------|----------|--------|--------|
| A: Game States & Menus | 21-22 | Medium | High — feels like a real game |
| B: Save/Load | 23 | Medium-Hard | Medium — expected feature but complex |
| C: Skybox | 24 | Easy | Medium — big visual upgrade for little work |
| D: Extended Content | 25-26 | Medium | Medium — more gameplay depth |
| E: Dev Console | 27 | Medium | High for development — speeds up everything |
| F: Rendering Upgrades | 28-29 | Medium-Hard | High — shadows and post-processing are huge visual upgrades |
| G: Text & Visual Effects | 30-31 | Medium | Medium — proper text and environmental detail |
| H: Performance | 32 | Easy-Medium | High — essential for large levels |
| I: Animation & World | 33-34 | Hard | High — skeletal animation and level transitions complete the engine |
| J: Surface Detail | 35 | Easy-Medium | High — normal mapping is foundational |
| K: Asset Pipeline | 36 | Medium | High — essential for real content |
| L: AI Navigation | 37 | Medium-Hard | High — enemies that can navigate properly |
| M: Rendering Performance | 38 | Medium | Medium — needed for dense scenes |
| N: Environmental Effects | 39 | Medium-Hard | Medium — water is a classic FPS feature |
| O: Animation Polish | 40-42 | Medium-Hard | High — animation events, ragdolls, and layered animation complete the animation pipeline |
| P: Advanced Rendering & Animation | 43-44 | Hard | Medium-High — IK and PBR are pro-level features |
| Q: Advanced Particles | 45-46 | Medium-Hard | High — collision, trails, data-driven effects complete the VFX pipeline |

---

## Progress Tracker

| Ch | Title | Status |
|----|-------|--------|
| 21 | Game State Machine | **COMPLETE** |
| 22 | Main Menu & Pause Screen | **COMPLETE** |
| 23 | Save & Load System | **COMPLETE** |
| 24 | Skybox | **COMPLETE** |
| 25 | Weapon Animations & View Models | **COMPLETE** |
| 26 | Boss Fights & Arenas | **COMPLETE** |
| 27 | Developer Console & Debug Tools | **COMPLETE** |
| 28 | Framebuffers & Post-Processing | **COMPLETE** |
| 29 | Shadow Mapping | **COMPLETE** |
| 30 | Font Rendering | **COMPLETE** |
| 31 | Decals | **COMPLETE** |
| 32 | Frustum Culling | **COMPLETE** |
| 33 | Skeletal Animation | **COMPLETE** |
| 34 | Level Transitions | **COMPLETE** |
| 35 | Normal Mapping | **COMPLETE** |
| 36 | Model Loading (OBJ & glTF) | **COMPLETE** |
| 37 | Pathfinding (A* & Nav Mesh) | **COMPLETE** |
| 38 | Instanced Rendering | **COMPLETE** |
| 39 | Water & Liquid Rendering | **COMPLETE** |
| 40 | Animation Events & Notifies | **COMPLETE** |
| 41 | Ragdoll Physics | **COMPLETE** |
| 42 | Animation Layers & Partial Body | **COMPLETE** |
| 43 | Inverse Kinematics (IK) | **COMPLETE** |
| 44 | PBR Materials | **COMPLETE** |
| 45 | Advanced Particle Physics & Rendering | **COMPLETE** |
| 46 | Data-Driven Particle Effects | **COMPLETE** |

All chapters written to: `D:\Documents\AI\documents\Game Learning\Game_Engine_Tutorial_2\`

---

## Dependency Graph (extending the original)

```
Original Ch 0-20
     │
     ├── Ch 21 (Game States)
     │    └── Ch 22 (Menus)
     │         └── Ch 23 (Save/Load)
     │              └── Ch 34 (Level Transitions)
     │
     ├── Ch 24 (Skybox) — independent, can do anytime after Ch 5
     │
     ├── Ch 25 (View Models) — after Ch 12 + Ch 20
     │
     ├── Ch 26 (Boss Fights) — after Ch 14 + Ch 11
     │
     ├── Ch 27 (Dev Console) — after Ch 15
     │
     ├── Ch 28 (Post-Processing) — after Ch 7
     │    ├── Ch 29 (Shadow Mapping) — after Ch 28
     │    └── Ch 39 (Water) — after Ch 28 + Ch 24
     │
     ├── Ch 30 (Font Rendering) — after Ch 5 + Ch 15
     │
     ├── Ch 31 (Decals) — after Ch 5 + Ch 9
     │
     ├── Ch 32 (Frustum Culling) — after Ch 4 + Ch 9
     │    └── Ch 38 (Instanced Rendering) — after Ch 32 + Ch 6
     │
     ├── Ch 33 (Skeletal Animation) — after Ch 6
     │    ├── Ch 36 (Model Loading) — after Ch 33 + Ch 6
     │    ├── Ch 40 (Animation Events) — after Ch 33 + Ch 16
     │    ├── Ch 41 (Ragdoll Physics) — after Ch 33 + Ch 10
     │    ├── Ch 42 (Animation Layers) — after Ch 33
     │    └── Ch 43 (Inverse Kinematics) — after Ch 33
     │
     ├── Ch 35 (Normal Mapping) — after Ch 5 + Ch 7
     │    └── Ch 44 (PBR Materials) — after Ch 35 + Ch 7 + Ch 28
     │
     ├── Ch 37 (Pathfinding) — after Ch 14 + Ch 9
     │
     └── Ch 45 (Advanced Particles) — after Ch 20 + Ch 9
          └── Ch 46 (Data-Driven Effects) — after Ch 45 + Ch 40
```
