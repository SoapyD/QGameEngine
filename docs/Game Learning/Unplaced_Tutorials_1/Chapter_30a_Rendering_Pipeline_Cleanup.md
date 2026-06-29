# Chapter 30a: Rendering Pipeline Cleanup

> **Prerequisites:** Chapter 30 (Font Rendering) completed. You should have a working FreeType-based `Font` class, a batched `TextRenderer`, text alignment utilities, a full post-processing pipeline with bloom, and shadow mapping. All previous cleanup patterns (ResourceManager, InputManager, MeshFactory, FixedTimestep, PhysicsConfig, ECS HUD components, MathUtils, ParticleEmitterDef, AnimationLibrary, AssetCache, WeaponEffectConfig) should be in place.

---

## Time for Another Cleanup

Five cleanup chapters in, and the pattern should feel familiar: the features work, the code does not scale. Chapters 26 through 30 added boss fights, a developer console, framebuffers, post-processing, shadow mapping, and font rendering. Each chapter focused on getting the feature running. Now we pay the architecture tax.

Open `PlayingState` and look at the render method. You will find something like this:

```cpp
// In src/game/states/playing_state.h (relevant members)

class PlayingState {
    // ... game systems, registry, camera, etc.

    // Post-processing infrastructure
    Framebuffer   m_sceneFBO;
    Framebuffer   m_brightFBO;
    Framebuffer   m_pingFBO;
    Framebuffer   m_pongFBO;
    ScreenQuad    m_screenQuad;

    // Shaders — created individually, no caching
    Shader m_ppFinalShader;
    Shader m_brightPassShader;
    Shader m_blurShader;
    Shader m_skyboxShader;
    Shader m_shadowDepthShader;
    Shader m_worldShader;
    Shader m_particleShader;
    Shader m_viewModelShader;

    // Fonts — scattered member variables
    Font m_hudFont;
    Font m_consoleFont;
    Font m_menuFont;
    TextRenderer m_textRenderer;
    Shader m_textShader;

    // ...
};
```

And the render method:

```cpp
void PlayingState::render() {
    // ─── Pass 1: Render scene into the FBO ───────────────────────
    m_sceneFBO.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view       = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix();

    // 1. Skybox
    glDepthFunc(GL_LEQUAL);
    m_skyboxShader.use();
    // ... set uniforms ...
    m_skybox.render(m_skyboxShader, view, projection);
    glDepthFunc(GL_LESS);

    // 2. Shadow pass
    shadowMapSystem(m_registry, m_camera, m_shadowMap,
                    m_shadowDepthShader);

    // 3. Opaque geometry
    m_worldShader.use();
    // ... set uniforms, bind shadow map ...
    renderSystem(m_registry, m_camera);

    // 4. Transparent geometry / particles
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_particleShader.use();
    particleSystem(m_registry, m_camera);
    glDisable(GL_BLEND);

    // 5. View model (weapon)
    glClear(GL_DEPTH_BUFFER_BIT);
    m_viewModelShader.use();
    viewModelSystem(m_registry, m_camera);

    Framebuffer::unbind();

    // ─── Pass 2: Post-processing ─────────────────────────────────
    glViewport(0, 0, m_window.getWidth(), m_window.getHeight());
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    PostProcessShaders ppShaders;
    ppShaders.brightPass  = &m_brightPassShader;
    ppShaders.blur        = &m_blurShader;
    ppShaders.bloomCombine = &m_ppFinalShader;
    ppShaders.passthrough = &m_ppFinalShader;  // reused

    BloomFBOs bloomFBOs;
    bloomFBOs.bright = &m_brightFBO;
    bloomFBOs.ping   = &m_pingFBO;
    bloomFBOs.pong   = &m_pongFBO;

    postProcessSystem(m_registry, m_sceneFBO, bloomFBOs,
                      m_screenQuad, ppShaders);

    glEnable(GL_DEPTH_TEST);

    // ─── Pass 3: HUD (directly to default framebuffer) ───────────
    m_hudRenderer.render(m_registry,
        static_cast<float>(m_window.getWidth()),
        static_cast<float>(m_window.getHeight()));

    // ─── Pass 4: Console overlay ─────────────────────────────────
    renderConsole(m_console, m_consoleFont, m_textRenderer,
                  m_textShader, m_window.getWidth(),
                  m_window.getHeight());
}
```

And in the constructor:

```cpp
PlayingState::PlayingState(Window& window, entt::registry& registry)
    : m_window(window),
      m_registry(registry),
      m_sceneFBO(window.getWidth(), window.getHeight()),
      m_brightFBO(window.getWidth(), window.getHeight()),
      m_pingFBO(window.getWidth(), window.getHeight()),
      m_pongFBO(window.getWidth(), window.getHeight())
{
    // Load shaders one at a time — no caching, no reuse
    m_ppFinalShader.load("assets/shaders/postprocess.vert",
                         "assets/shaders/pp_final.frag");
    m_brightPassShader.load("assets/shaders/postprocess.vert",
                            "assets/shaders/pp_bloom_bright.frag");
    m_blurShader.load("assets/shaders/postprocess.vert",
                      "assets/shaders/pp_blur.frag");
    m_skyboxShader.load("assets/shaders/skybox.vert",
                        "assets/shaders/skybox.frag");
    m_shadowDepthShader.load("assets/shaders/shadow_depth.vert",
                             "assets/shaders/shadow_depth.frag");
    m_worldShader.load("assets/shaders/world.vert",
                       "assets/shaders/world.frag");
    m_particleShader.load("assets/shaders/particle.vert",
                          "assets/shaders/particle.frag");
    m_viewModelShader.load("assets/shaders/viewmodel.vert",
                           "assets/shaders/viewmodel.frag");
    m_textShader.load("assets/shaders/text.vert",
                      "assets/shaders/text.frag");

    // Load fonts as individual member variables
    m_hudFont.load("assets/fonts/roboto_mono.ttf", 18);
    m_consoleFont.load("assets/fonts/roboto_mono.ttf", 14);
    m_menuFont.load("assets/fonts/roboto_bold.ttf", 32);

    m_textRenderer.init(window.getWidth(), window.getHeight());

    // ... rest of initialisation
}
```

Count the problems:

1. **Render passes are implicit in method ordering.** The seven-stage render pipeline (skybox, shadows, opaque, transparent, view model, post-process, HUD) exists only as comments and code ordering inside one long method. Add a new pass (decals in Chapter 31, water reflections later), and you must find the right spot in a 60-line method and get the GL state transitions correct. There is no structure that enforces or documents the pass ordering.

2. **Shaders are compiled individually with no caching.** Nine `Shader` member variables, each loaded with hardcoded paths. If `PlayingState` and `MenuState` both need the text shader, each compiles its own copy. The same vertex shader (`postprocess.vert`) is compiled three times -- once for each post-process fragment shader. There is no way to retrieve a shader by name at runtime, and no mechanism for hot-reloading during development.

3. **Font instances are scattered as globals.** Three `Font` member variables, a `TextRenderer`, and a `Shader` for text -- all created manually in the constructor. If `MenuState` needs the same font at the same size, it creates a second instance. There is no central font registry, no way to request a font by logical name, and no caching.

4. **GL state transitions are manual and fragile.** The render method manually toggles `GL_BLEND`, `GL_DEPTH_TEST`, `glDepthFunc`, and `glClear` between passes. Miss one transition and you get invisible geometry or blending artifacts. Each pass must know what the previous pass left behind.

5. **PostProcessSystem takes raw pointers.** The `PostProcessShaders` and `BloomFBOs` structs are assembled inline every frame from member pointers. This is boilerplate that obscures the actual post-processing logic.

Here is our plan:

| Problem | Solution |
|---|---|
| Implicit render pass ordering | `RenderPipeline` class with named pass methods |
| Individual shader compilation, no caching | `ShaderCache` with compile-once, retrieve-by-name |
| Scattered font member variables | Extend `ResourceManager` with `getFont()` |
| Manual GL state transitions | Each pass method manages its own entry/exit state |
| Raw pointer structs for post-process | `PostProcessSystem` pulls shaders from `ShaderCache` |

---

## C++ Concept: The Template Method Pattern

This chapter's refactoring leans on the **Template Method** pattern -- one of the Gang of Four behavioural patterns. The idea is simple: define the skeleton of an algorithm in a base method, and let individual steps be overridden or customised.

In its classical form, the pattern uses virtual functions:

```cpp
class Renderer {
public:
    // The template method — defines the algorithm skeleton
    void render() {
        beginFrame();
        renderSkybox();
        renderShadows();
        renderOpaque();
        renderTransparent();
        renderPostProcess();
        renderHUD();
        endFrame();
    }

protected:
    virtual void renderSkybox() = 0;
    virtual void renderOpaque() = 0;
    // ...
};
```

Subclasses override individual steps without changing the algorithm structure. The render order is enforced by the template method, not by whoever calls it.

We are not going to use virtual functions here -- the overhead is unnecessary when we have exactly one renderer. Instead, we use the pattern's *structure*: a single public method that calls private steps in a fixed order. The benefit is the same -- the ordering is encoded once, each step is isolated, and adding a new pass means adding a method call in one place rather than finding the right line in a monolithic function.

This is sometimes called the **Non-Virtual Interface (NVI)** idiom when the template method is non-virtual but calls non-virtual private helpers. It gives you the structural clarity of Template Method without the vtable cost. In a game engine where `render()` is called 60+ times per second, avoiding virtual dispatch on the hot path is a reasonable choice.

The key insight: you do not need inheritance to get the benefits of a design pattern. The pattern is about structure, not syntax.

---

## Step 1: ShaderCache

We start with the `ShaderCache` because the `RenderPipeline` and updated `PostProcessSystem` both need it. The `ShaderCache` compiles each shader program exactly once and stores it by name. Any system can retrieve a shader by its logical name without knowing the file paths.

### Why not extend ResourceManager?

In Chapter 5a, `ResourceManager` was designed around textures -- resources identified by a file path. Shaders are different: they are identified by a *pair* of paths (vertex + fragment), and we want to give them logical names ("bloom_bright", "world", "text") that are independent of file layout. A dedicated `ShaderCache` is cleaner than stretching `ResourceManager` to handle a fundamentally different resource type.

However, fonts (Step 3) *do* fit the `ResourceManager` pattern -- a single file path plus a size parameter. We will extend `ResourceManager` for those.

### engine/renderer/shader_cache.h

```cpp
// engine/renderer/shader_cache.h
#pragma once

#include "engine/renderer/shader.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>
#include <filesystem>

// ─── ShaderCache ─────────────────────────────────────────────────
// Compile-once, retrieve-by-name shader management.
//
// Shaders are stored as shared_ptr so multiple systems can hold
// references without worrying about lifetime. The cache owns the
// canonical copy.
//
// Usage:
//   cache.load("bloom_bright",
//              "assets/shaders/postprocess.vert",
//              "assets/shaders/pp_bloom_bright.frag");
//   auto shader = cache.get("bloom_bright");
//   shader->use();

class ShaderCache
{
public:
    // Load and compile a shader program. Returns the cached shader.
    // If a shader with this name already exists, returns the existing one
    // (does not recompile). This is intentional: load() is idempotent.
    std::shared_ptr<Shader> load(const std::string& name,
                                 const std::string& vertPath,
                                 const std::string& fragPath)
    {
        auto it = m_shaders.find(name);
        if (it != m_shaders.end())
        {
            return it->second;
        }

        auto shader = std::make_shared<Shader>();
        shader->load(vertPath, fragPath);
        m_shaders[name] = shader;

        std::cout << "ShaderCache: compiled '" << name
                  << "' (" << vertPath << " + " << fragPath << ")"
                  << std::endl;

        return shader;
    }

    // Retrieve a previously loaded shader by name.
    // Returns nullptr if the name is not found.
    std::shared_ptr<Shader> get(const std::string& name) const
    {
        auto it = m_shaders.find(name);
        if (it != m_shaders.end())
        {
            return it->second;
        }

        std::cerr << "ShaderCache: shader '" << name
                  << "' not found" << std::endl;
        return nullptr;
    }

    // Check whether a shader with this name has been loaded.
    bool contains(const std::string& name) const
    {
        return m_shaders.find(name) != m_shaders.end();
    }

    // Return the number of cached shader programs.
    size_t size() const { return m_shaders.size(); }

#ifndef NDEBUG
    // ─── Hot Reload (debug builds only) ──────────────────────────
    // Recompile a shader from its original source files.
    // Useful during development: change a .frag file, call reload(),
    // see the result without restarting the engine.
    //
    // This requires storing the source paths alongside the shader.
    // We do this via a parallel map.

    void reload(const std::string& name)
    {
        auto pathIt = m_paths.find(name);
        if (pathIt == m_paths.end())
        {
            std::cerr << "ShaderCache: cannot reload '" << name
                      << "' — source paths not recorded" << std::endl;
            return;
        }

        auto shaderIt = m_shaders.find(name);
        if (shaderIt == m_shaders.end()) return;

        const auto& [vertPath, fragPath] = pathIt->second;

        // Attempt recompilation into a temporary shader.
        // If it fails, keep the old shader intact.
        auto newShader = std::make_shared<Shader>();
        newShader->load(vertPath, fragPath);

        // Basic validation: check that the shader program is non-zero.
        // A more robust check would attempt a test draw call.
        if (newShader->getID() != 0)
        {
            shaderIt->second = newShader;
            std::cout << "ShaderCache: reloaded '" << name << "'"
                      << std::endl;
        }
        else
        {
            std::cerr << "ShaderCache: reload of '" << name
                      << "' failed — keeping old shader" << std::endl;
        }
    }

    // Reload all shaders. Call from a console command or keybind.
    void reloadAll()
    {
        for (const auto& [name, paths] : m_paths)
        {
            reload(name);
        }
    }
#endif

private:
    std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;

#ifndef NDEBUG
    // Source paths stored only in debug builds (for hot-reload)
    std::unordered_map<std::string,
        std::pair<std::string, std::string>> m_paths;
#endif
};
```

Wait -- we need the `load()` method to also record paths in debug builds. Let us update it:

```cpp
    std::shared_ptr<Shader> load(const std::string& name,
                                 const std::string& vertPath,
                                 const std::string& fragPath)
    {
        auto it = m_shaders.find(name);
        if (it != m_shaders.end())
        {
            return it->second;
        }

        auto shader = std::make_shared<Shader>();
        shader->load(vertPath, fragPath);
        m_shaders[name] = shader;

#ifndef NDEBUG
        m_paths[name] = { vertPath, fragPath };
#endif

        std::cout << "ShaderCache: compiled '" << name
                  << "' (" << vertPath << " + " << fragPath << ")"
                  << std::endl;

        return shader;
    }
```

### Complete header (corrected)

Here is the full, final `shader_cache.h` with the path recording included:

```cpp
// engine/renderer/shader_cache.h
#pragma once

#include "engine/renderer/shader.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

class ShaderCache
{
public:
    // Load and compile a shader program, or return existing if already loaded.
    std::shared_ptr<Shader> load(const std::string& name,
                                 const std::string& vertPath,
                                 const std::string& fragPath)
    {
        auto it = m_shaders.find(name);
        if (it != m_shaders.end())
        {
            return it->second;
        }

        auto shader = std::make_shared<Shader>();
        shader->load(vertPath, fragPath);
        m_shaders[name] = shader;

#ifndef NDEBUG
        m_paths[name] = { vertPath, fragPath };
#endif

        std::cout << "ShaderCache: compiled '" << name
                  << "' (" << vertPath << " + " << fragPath << ")"
                  << std::endl;

        return shader;
    }

    // Retrieve a previously loaded shader by name.
    // Returns nullptr if not found.
    std::shared_ptr<Shader> get(const std::string& name) const
    {
        auto it = m_shaders.find(name);
        if (it != m_shaders.end())
        {
            return it->second;
        }

        std::cerr << "ShaderCache: shader '" << name
                  << "' not found" << std::endl;
        return nullptr;
    }

    bool contains(const std::string& name) const
    {
        return m_shaders.find(name) != m_shaders.end();
    }

    size_t size() const { return m_shaders.size(); }

#ifndef NDEBUG
    // ─── Hot Reload (debug builds only) ──────────────────────────
    // Recompile a shader from its original source files.
    // If recompilation fails, the old shader is preserved.
    void reload(const std::string& name)
    {
        auto pathIt = m_paths.find(name);
        if (pathIt == m_paths.end())
        {
            std::cerr << "ShaderCache: cannot reload '" << name
                      << "' — paths not recorded" << std::endl;
            return;
        }

        auto shaderIt = m_shaders.find(name);
        if (shaderIt == m_shaders.end()) return;

        const auto& [vertPath, fragPath] = pathIt->second;

        auto newShader = std::make_shared<Shader>();
        newShader->load(vertPath, fragPath);

        if (newShader->getID() != 0)
        {
            shaderIt->second = newShader;
            std::cout << "ShaderCache: reloaded '" << name << "'"
                      << std::endl;
        }
        else
        {
            std::cerr << "ShaderCache: reload of '" << name
                      << "' failed — keeping old shader" << std::endl;
        }
    }

    void reloadAll()
    {
        for (const auto& [name, paths] : m_paths)
        {
            reload(name);
        }
    }
#endif

private:
    std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;

#ifndef NDEBUG
    std::unordered_map<std::string,
        std::pair<std::string, std::string>> m_paths;
#endif
};
```

### Loading all shaders at startup

Instead of scattered `Shader` member variables, a single initialisation function populates the cache:

```cpp
// In src/game/states/playing_state.cpp or a dedicated init function

void loadShaders(ShaderCache& shaders)
{
    // Post-processing
    shaders.load("pp_final",
        "assets/shaders/postprocess.vert",
        "assets/shaders/pp_final.frag");
    shaders.load("bloom_bright",
        "assets/shaders/postprocess.vert",
        "assets/shaders/pp_bloom_bright.frag");
    shaders.load("blur",
        "assets/shaders/postprocess.vert",
        "assets/shaders/pp_blur.frag");

    // Scene rendering
    shaders.load("skybox",
        "assets/shaders/skybox.vert",
        "assets/shaders/skybox.frag");
    shaders.load("shadow_depth",
        "assets/shaders/shadow_depth.vert",
        "assets/shaders/shadow_depth.frag");
    shaders.load("world",
        "assets/shaders/world.vert",
        "assets/shaders/world.frag");
    shaders.load("particle",
        "assets/shaders/particle.vert",
        "assets/shaders/particle.frag");
    shaders.load("viewmodel",
        "assets/shaders/viewmodel.vert",
        "assets/shaders/viewmodel.frag");

    // Text / HUD
    shaders.load("text",
        "assets/shaders/text.vert",
        "assets/shaders/text.frag");
}
```

All nine shaders, compiled once, accessible by name from anywhere. If `MenuState` also needs the text shader: `shaders.get("text")` -- no recompilation, no new member variable.

### Console command for hot-reload

Wire the reload into the developer console from Chapter 27:

```cpp
#ifndef NDEBUG
console.registerCommand("reload_shaders", "Recompile all shaders from disk",
    [&shaderCache, &console](const std::vector<std::string>& args) {
        if (args.empty())
        {
            shaderCache.reloadAll();
            console.print("All shaders reloaded.");
        }
        else
        {
            shaderCache.reload(args[0]);
            console.print("Reloaded: " + args[0]);
        }
    });
#endif
```

Now you can tweak a fragment shader in your editor, switch to the game window, type `reload_shaders`, and see the result without restarting.

---

## Step 2: Font Resource Management

Fonts in Chapter 30 are created as loose member variables with manual `load()` calls. If two game states need the same font at the same size, each creates its own instance -- separate FreeType initialisation, separate atlas texture, separate GPU memory.

The fix is to extend `ResourceManager` with a `getFont()` method that caches fonts by a `"name:size"` key, just like textures are cached by filename.

### Why ResourceManager and not a FontCache?

Unlike shaders (which are identified by a name we invent and a pair of files), fonts follow the same pattern as textures: one file path, one configuration parameter (pixel size), and we want shared ownership via `shared_ptr`. This fits naturally into `ResourceManager` as established in Chapter 5a. Adding a separate `FontCache` class would duplicate the caching logic.

### engine/core/resource_manager.h (additions)

Add these to your existing `ResourceManager` class:

```cpp
// engine/core/resource_manager.h (additions)
#pragma once

// ... existing includes ...
#include "engine/renderer/font.h"
#include <memory>
#include <string>
#include <unordered_map>

class ResourceManager
{
public:
    // ... existing getTexture(), getShader() methods ...

    // ─── Font Management ─────────────────────────────────────────
    // Fonts are cached by "name:size" key.
    // If the same font at the same size is requested twice, the
    // cached version is returned. This prevents duplicate atlas
    // textures on the GPU.
    //
    // The 'name' parameter is a logical name (e.g. "hud", "console",
    // "menu"). It does not need to match the filename.

    std::shared_ptr<Font> getFont(const std::string& name,
                                  const std::string& fontPath,
                                  int pixelSize)
    {
        std::string key = name + ":" + std::to_string(pixelSize);

        auto it = m_fonts.find(key);
        if (it != m_fonts.end())
        {
            return it->second;
        }

        auto font = std::make_shared<Font>();
        if (!font->load(fontPath, pixelSize))
        {
            std::cerr << "ResourceManager: failed to load font '"
                      << name << "' from " << fontPath << std::endl;
            return nullptr;
        }

        m_fonts[key] = font;
        return font;
    }

    // Retrieve a previously loaded font by its key.
    // Returns nullptr if not found.
    std::shared_ptr<Font> getFont(const std::string& name,
                                  int pixelSize) const
    {
        std::string key = name + ":" + std::to_string(pixelSize);

        auto it = m_fonts.find(key);
        if (it != m_fonts.end())
        {
            return it->second;
        }

        return nullptr;
    }

private:
    // ... existing m_textures, m_shaders maps ...
    std::unordered_map<std::string, std::shared_ptr<Font>> m_fonts;
};
```

### Why a two-parameter overload?

The three-parameter `getFont(name, path, size)` is used at initialisation to load and cache fonts. The two-parameter `getFont(name, size)` is used later by any system that needs to retrieve an already-loaded font without knowing its file path. This mirrors how `ResourceManager::getTexture()` works -- load once with a path, retrieve later by name.

### Loading fonts at startup

Replace the scattered font member variables with `ResourceManager` calls:

```cpp
// Before (Chapter 30):
Font hudFont;
Font consoleFont;
Font menuFont;

hudFont.load("assets/fonts/roboto_mono.ttf", 18);
consoleFont.load("assets/fonts/roboto_mono.ttf", 14);
menuFont.load("assets/fonts/roboto_bold.ttf", 32);

// After (Chapter 30a):
auto hudFont = resources.getFont("hud",
    "assets/fonts/roboto_mono.ttf", 18);
auto consoleFont = resources.getFont("console",
    "assets/fonts/roboto_mono.ttf", 14);
auto menuFont = resources.getFont("menu",
    "assets/fonts/roboto_bold.ttf", 32);
```

Now any game state can retrieve the HUD font with:

```cpp
auto font = resources.getFont("hud", 18);
```

No reloading, no duplicate atlas, no knowledge of the file path needed.

### Font class: shared_ptr compatibility

The `Font` class from Chapter 30 deletes its copy constructor and supports move semantics. Since we are wrapping it in `shared_ptr`, copy semantics are irrelevant -- `shared_ptr` manages the single instance. No changes to the `Font` class are needed.

---

## Step 3: RenderPipeline

This is the largest refactoring in this chapter. We replace the monolithic `PlayingState::render()` method with a `RenderPipeline` class that formalises the render pass ordering.

### Design decisions

1. **Each pass is a method.** `renderSkybox()`, `renderShadows()`, `renderOpaque()`, `renderTransparent()`, `renderViewModels()`, `renderPostProcess()`, `renderHUD()`. The public `execute()` method calls them in order.

2. **Each pass manages its own GL state.** The skybox pass sets `GL_LEQUAL` and restores `GL_LESS`. The transparent pass enables blending and disables it when done. No pass assumes the state left by the previous pass. This eliminates the fragile state-dependency chain.

3. **The pipeline does not own game data.** It takes a `RenderContext` struct containing references to everything it needs: the registry, camera, shaders (via `ShaderCache`), FBOs, and the screen quad. The pipeline is a stateless algorithm, not a data container.

4. **No virtual functions.** As discussed in the C++ concept section, we use the Template Method structure without the virtual dispatch cost. The pipeline has exactly one implementation.

### engine/renderer/render_pipeline.h

```cpp
// engine/renderer/render_pipeline.h
#pragma once

#include "engine/renderer/framebuffer.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/shader_cache.h"
#include "engine/renderer/text_renderer.h"
#include "engine/renderer/font.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>

// Forward declarations
class Camera;
class Skybox;
class ShadowMap;
class Window;
class HUDRenderer;

// ─── RenderContext ───────────────────────────────────────────────
// Everything the pipeline needs to render a frame. Assembled once
// per frame by the game state, passed into the pipeline.
//
// This struct does not own anything — it holds references and
// pointers to objects owned elsewhere. Its lifetime is a single
// frame.

struct RenderContext
{
    entt::registry& registry;
    Camera&         camera;
    Window&         window;
    ShaderCache&    shaders;

    // Scene FBOs
    Framebuffer&    sceneFBO;
    Framebuffer&    brightFBO;
    Framebuffer&    pingFBO;
    Framebuffer&    pongFBO;
    ScreenQuad&     screenQuad;

    // Scene objects
    Skybox*         skybox      = nullptr;
    ShadowMap*      shadowMap   = nullptr;
    HUDRenderer*    hudRenderer = nullptr;

    // Text rendering
    TextRenderer*              textRenderer = nullptr;
    std::shared_ptr<Font>      consoleFont  = nullptr;
};

// ─── RenderPipeline ──────────────────────────────────────────────
// Formalises the render pass ordering:
//
//   1. Skybox         (GL_LEQUAL depth, scene FBO)
//   2. Shadows        (depth-only pass, shadow map FBO)
//   3. Opaque         (world geometry, scene FBO)
//   4. Transparent    (particles, blended, scene FBO)
//   5. View model     (weapon, cleared depth, scene FBO)
//   6. Post-process   (bloom + composite, default FBO)
//   7. HUD            (2D overlay, default FBO)
//
// Each pass is a private method that manages its own GL state.
// The public execute() method calls them in order.

class RenderPipeline
{
public:
    // Execute the full rendering pipeline for one frame.
    void execute(RenderContext& ctx);

private:
    // ─── Pass methods ────────────────────────────────────────
    // Each method assumes GL state is in a clean default state
    // (depth test on, blending off, depth func GL_LESS).
    // Each method restores any state it changes.

    void beginScenePass(RenderContext& ctx);
    void renderSkybox(RenderContext& ctx);
    void renderShadows(RenderContext& ctx);
    void renderOpaque(RenderContext& ctx);
    void renderTransparent(RenderContext& ctx);
    void renderViewModels(RenderContext& ctx);
    void endScenePass(RenderContext& ctx);

    void renderPostProcess(RenderContext& ctx);
    void renderHUD(RenderContext& ctx);
};
```

### engine/renderer/render_pipeline.cpp

```cpp
// engine/renderer/render_pipeline.cpp
#include "engine/renderer/render_pipeline.h"

#include "engine/renderer/camera.h"
#include "engine/renderer/skybox.h"
#include "engine/renderer/shadow_map.h"
#include "engine/core/window.h"
#include "engine/ecs/systems/hud_system.h"

#include <glad/glad.h>

// ─── Execute ─────────────────────────────────────────────────────
// The template method: calls each pass in the correct order.
// This is the only place the render ordering is defined.

void RenderPipeline::execute(RenderContext& ctx)
{
    // ─── Scene passes (render to scene FBO) ──────────────────
    beginScenePass(ctx);
    renderSkybox(ctx);
    renderShadows(ctx);
    renderOpaque(ctx);
    renderTransparent(ctx);
    renderViewModels(ctx);
    endScenePass(ctx);

    // ─── Post-processing (scene FBO -> default FBO) ──────────
    renderPostProcess(ctx);

    // ─── HUD (directly to default FBO) ───────────────────────
    renderHUD(ctx);
}

// ─── Scene Pass Begin/End ────────────────────────────────────────

void RenderPipeline::beginScenePass(RenderContext& ctx)
{
    ctx.sceneFBO.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderPipeline::endScenePass(RenderContext& ctx)
{
    Framebuffer::unbind();
}

// ─── Pass 1: Skybox ──────────────────────────────────────────────
// Rendered first with GL_LEQUAL so it draws at the maximum depth
// value. All subsequent geometry will draw in front of it.

void RenderPipeline::renderSkybox(RenderContext& ctx)
{
    if (!ctx.skybox) return;

    auto shader = ctx.shaders.get("skybox");
    if (!shader) return;

    glm::mat4 view       = ctx.camera.getViewMatrix();
    glm::mat4 projection = ctx.camera.getProjectionMatrix();

    // GL_LEQUAL allows the skybox to pass the depth test at z=1.0
    glDepthFunc(GL_LEQUAL);

    shader->use();
    ctx.skybox->render(*shader, view, projection);

    // Restore default depth function
    glDepthFunc(GL_LESS);
}

// ─── Pass 2: Shadows ─────────────────────────────────────────────
// Renders scene depth from the light's perspective into the shadow
// map FBO. Does not write to the scene FBO.

void RenderPipeline::renderShadows(RenderContext& ctx)
{
    if (!ctx.shadowMap) return;

    auto shader = ctx.shaders.get("shadow_depth");
    if (!shader) return;

    shadowMapSystem(ctx.registry, ctx.camera, *ctx.shadowMap, *shader);
}

// ─── Pass 3: Opaque Geometry ─────────────────────────────────────
// World geometry, enemies, items — everything that does not need
// blending. Depth writes are on, blending is off.

void RenderPipeline::renderOpaque(RenderContext& ctx)
{
    auto shader = ctx.shaders.get("world");
    if (!shader) return;

    shader->use();
    // Set shadow map uniforms, light uniforms, etc.
    // (Your existing renderSystem handles this)
    renderSystem(ctx.registry, ctx.camera);
}

// ─── Pass 4: Transparent Geometry / Particles ────────────────────
// Blended geometry. Rendered after opaque so depth values are
// already written. Depth writes are typically disabled for
// transparent objects to prevent sorting issues.

void RenderPipeline::renderTransparent(RenderContext& ctx)
{
    auto shader = ctx.shaders.get("particle");
    if (!shader) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader->use();
    particleSystem(ctx.registry, ctx.camera);

    glDisable(GL_BLEND);
}

// ─── Pass 5: View Model (Weapon) ─────────────────────────────────
// The weapon is rendered after clearing the depth buffer so it
// always appears in front of the scene. This prevents the gun
// from clipping through walls.

void RenderPipeline::renderViewModels(RenderContext& ctx)
{
    auto shader = ctx.shaders.get("viewmodel");
    if (!shader) return;

    glClear(GL_DEPTH_BUFFER_BIT);

    shader->use();
    viewModelSystem(ctx.registry, ctx.camera);
}

// ─── Pass 6: Post-Processing ─────────────────────────────────────
// Bloom extraction, blur ping-pong, and final composite.
// Reads from the scene FBO, writes to the default framebuffer.

void RenderPipeline::renderPostProcess(RenderContext& ctx)
{
    Framebuffer::unbind();
    glViewport(0, 0, ctx.window.getWidth(), ctx.window.getHeight());
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    // Find the PostProcessSettings component
    auto view = ctx.registry.view<PostProcessSettings>();
    if (view.size_hint() == 0)
    {
        // No settings — passthrough blit
        auto passShader = ctx.shaders.get("pp_final");
        if (passShader)
        {
            passShader->use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,
                          ctx.sceneFBO.getColourTexture());
            passShader->setInt("sceneTexture", 0);
            passShader->setBool("bloomEnabled", false);
            passShader->setFloat("damageIntensity", 0.0f);
            passShader->setFloat("contrast", 1.0f);
            passShader->setFloat("saturation", 1.0f);
            passShader->setBool("vignetteEnabled", false);
            ctx.screenQuad.draw();
        }
        glEnable(GL_DEPTH_TEST);
        return;
    }

    auto& settings = view.get<PostProcessSettings>(view.front());

    GLuint currentTexture = ctx.sceneFBO.getColourTexture();
    GLuint bloomTexture = 0;

    // ─── Bloom extraction + blur ─────────────────────────────
    if (settings.bloomEnabled)
    {
        auto brightShader = ctx.shaders.get("bloom_bright");
        auto blurShader   = ctx.shaders.get("blur");

        if (brightShader && blurShader)
        {
            // Bright-pass extraction
            ctx.brightFBO.bind();
            glClear(GL_COLOR_BUFFER_BIT);
            brightShader->use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, currentTexture);
            brightShader->setInt("screenTexture", 0);
            brightShader->setFloat("threshold",
                                    settings.bloomThreshold);
            ctx.screenQuad.draw();

            // Ping-pong Gaussian blur
            bool horizontal = true;
            GLuint pingPongInput =
                ctx.brightFBO.getColourTexture();

            for (int i = 0; i < settings.bloomIterations; i++)
            {
                Framebuffer& target = horizontal
                    ? ctx.pingFBO : ctx.pongFBO;

                target.bind();
                glClear(GL_COLOR_BUFFER_BIT);
                blurShader->use();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, pingPongInput);
                blurShader->setInt("image", 0);
                blurShader->setBool("horizontal", horizontal);
                ctx.screenQuad.draw();

                pingPongInput = target.getColourTexture();
                horizontal = !horizontal;
            }

            bloomTexture = pingPongInput;
            Framebuffer::unbind();
        }
    }

    // ─── Final composite ─────────────────────────────────────
    Framebuffer::unbind();
    glViewport(0, 0, ctx.window.getWidth(), ctx.window.getHeight());
    glClear(GL_COLOR_BUFFER_BIT);

    auto finalShader = ctx.shaders.get("pp_final");
    if (finalShader)
    {
        finalShader->use();

        // Scene texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
        finalShader->setInt("sceneTexture", 0);

        // Bloom texture
        finalShader->setBool("bloomEnabled", settings.bloomEnabled);
        if (settings.bloomEnabled && bloomTexture != 0)
        {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, bloomTexture);
            finalShader->setInt("bloomTexture", 1);
            finalShader->setFloat("bloomIntensity",
                                   settings.bloomIntensity);
        }

        // Vignette
        finalShader->setBool("vignetteEnabled",
                              settings.vignetteEnabled);
        finalShader->setFloat("vignetteStrength",
                               settings.vignetteStrength);

        // Damage flash
        finalShader->setFloat("damageIntensity",
                               settings.damageFlashIntensity);

        // Colour grading
        finalShader->setFloat("contrast", settings.contrast);
        finalShader->setFloat("saturation", settings.saturation);

        ctx.screenQuad.draw();
    }

    glEnable(GL_DEPTH_TEST);
}

// ─── Pass 7: HUD ─────────────────────────────────────────────────
// 2D overlay rendered directly to the default framebuffer.
// Post-processing does not affect the HUD.

void RenderPipeline::renderHUD(RenderContext& ctx)
{
    if (ctx.hudRenderer)
    {
        ctx.hudRenderer->render(ctx.registry,
            static_cast<float>(ctx.window.getWidth()),
            static_cast<float>(ctx.window.getHeight()));
    }
}
```

### What changed

The `PostProcessShaders` and `BloomFBOs` structs from Chapter 28 are gone. The pipeline pulls shaders from the `ShaderCache` by name. The raw pointer assembly code that was repeated every frame is eliminated.

The bloom logic moved from the standalone `postProcessSystem()` free function into `RenderPipeline::renderPostProcess()`. This is appropriate because the bloom pipeline is inherently a rendering concern -- it coordinates FBO binds, shader binds, and draw calls. It was always render code; now it lives with the rest of the render code.

---

## Step 4: Updated PlayingState

Now let us see how `PlayingState` looks after all three refactors.

### src/game/states/playing_state.h (after)

```cpp
// src/game/states/playing_state.h
#pragma once

#include "engine/core/window.h"
#include "engine/core/resource_manager.h"
#include "engine/renderer/framebuffer.h"
#include "engine/renderer/screen_quad.h"
#include "engine/renderer/shader_cache.h"
#include "engine/renderer/render_pipeline.h"
#include "engine/renderer/text_renderer.h"
#include "engine/ecs/systems/hud_system.h"

#include <entt/entt.hpp>
#include <memory>

class PlayingState
{
public:
    PlayingState(Window& window, entt::registry& registry,
                 ShaderCache& shaders, ResourceManager& resources);

    void update(float dt);
    void render();
    void onResize(int width, int height);

private:
    Window&          m_window;
    entt::registry&  m_registry;
    ShaderCache&     m_shaders;
    ResourceManager& m_resources;

    // FBOs
    Framebuffer m_sceneFBO;
    Framebuffer m_brightFBO;
    Framebuffer m_pingFBO;
    Framebuffer m_pongFBO;
    ScreenQuad  m_screenQuad;

    // Rendering
    RenderPipeline m_pipeline;
    HUDRenderer    m_hudRenderer;
    TextRenderer   m_textRenderer;

    // Scene objects
    Camera  m_camera;
    Skybox  m_skybox;
    // ... other game state ...
};
```

Compare this to the "before" header. Gone are the nine `Shader` member variables. Gone are the three `Font` member variables and the dedicated `Shader m_textShader`. The `ShaderCache` and `ResourceManager` are shared references, not owned members. The `RenderPipeline` replaces the implicit rendering logic.

### src/game/states/playing_state.cpp (after)

```cpp
// src/game/states/playing_state.cpp
#include "game/states/playing_state.h"
#include "engine/renderer/camera.h"
#include "engine/renderer/skybox.h"

PlayingState::PlayingState(Window& window, entt::registry& registry,
                           ShaderCache& shaders,
                           ResourceManager& resources)
    : m_window(window),
      m_registry(registry),
      m_shaders(shaders),
      m_resources(resources),
      m_sceneFBO(window.getWidth(), window.getHeight()),
      m_brightFBO(window.getWidth(), window.getHeight()),
      m_pingFBO(window.getWidth(), window.getHeight()),
      m_pongFBO(window.getWidth(), window.getHeight()),
      m_hudRenderer(shaders.get("hud"),
                    MeshFactory::createHUDQuad().vao,
                    resources.getFont("hud", 18)->getAtlasTexture()),
      m_textRenderer()
{
    m_textRenderer.init(window.getWidth(), window.getHeight());

    // Create the post-process settings entity
    auto settingsEntity = m_registry.create();
    m_registry.emplace<PostProcessSettings>(settingsEntity);

    // ... rest of initialisation (camera, skybox, entities) ...
}

void PlayingState::update(float dt)
{
    // ... existing update logic ...
    // hudUpdateSystem, cameraEffectsUpdateSystem, etc.
}

void PlayingState::render()
{
    // Assemble the render context for this frame
    RenderContext ctx {
        .registry   = m_registry,
        .camera     = m_camera,
        .window     = m_window,
        .shaders    = m_shaders,
        .sceneFBO   = m_sceneFBO,
        .brightFBO  = m_brightFBO,
        .pingFBO    = m_pingFBO,
        .pongFBO    = m_pongFBO,
        .screenQuad = m_screenQuad,
        .skybox     = &m_skybox,
        .shadowMap  = nullptr, // set to &m_shadowMap if you have one
        .hudRenderer = &m_hudRenderer,
        .textRenderer = &m_textRenderer,
        .consoleFont  = m_resources.getFont("console", 14)
    };

    m_pipeline.execute(ctx);
}

void PlayingState::onResize(int width, int height)
{
    m_sceneFBO.resize(width, height);
    m_brightFBO.resize(width, height);
    m_pingFBO.resize(width, height);
    m_pongFBO.resize(width, height);
    m_textRenderer.init(width, height);
}
```

The `render()` method went from 60+ lines of mixed GL state management, shader binding, and system calls to a three-line function: assemble context, execute pipeline. The pipeline enforces the pass ordering. Each pass manages its own state. Adding a new pass (decals, water reflections) means adding one method call in `RenderPipeline::execute()`.

---

## Step 5: Updated Main Initialisation

Here is how the application initialisation changes to set up the shared `ShaderCache` and fonts:

```cpp
// In main.cpp or your application entry point

int main()
{
    Window window("QEngine", 1280, 720);
    InputManager input(window);
    ResourceManager resources;
    ShaderCache shaders;
    entt::registry registry;

    // ─── Load all shaders once ───────────────────────────────────
    loadShaders(shaders);

    // ─── Load all fonts once ─────────────────────────────────────
    resources.getFont("hud",     "assets/fonts/roboto_mono.ttf", 18);
    resources.getFont("console", "assets/fonts/roboto_mono.ttf", 14);
    resources.getFont("menu",    "assets/fonts/roboto_bold.ttf", 32);

    // ─── Create game states ──────────────────────────────────────
    // Both states share the same ShaderCache and ResourceManager.
    // No shader or font is compiled/loaded more than once.
    PlayingState playingState(window, registry, shaders, resources);
    MenuState menuState(window, registry, shaders, resources);

    // ─── Game loop ───────────────────────────────────────────────
    FixedTimestep fixedTimestep;

    while (!window.shouldClose())
    {
        fixedTimestep.accumulate();

        // -- Phase: Input --
        input.update();
        window.pollEvents();

        // -- Phase: Physics --
        while (fixedTimestep.step())
        {
            // ... physics systems ...
        }

        // -- Phase: GameLogic --
        float dt = fixedTimestep.getTimestep();
        hudUpdateSystem(registry, dt);
        cameraEffectsUpdateSystem(registry, dt);
        postProcessUpdateSystem(registry, dt);

        // -- Phase: LateUpdate --
        cameraEffectsApplySystem(registry);
        cameraFollowSystem(registry);

        // -- Phase: Render --
        playingState.render();  // pipeline handles everything

        window.swapBuffers();
    }

    return 0;
}
```

The `ShaderCache` and `ResourceManager` are created once in `main()` and passed by reference to every game state. This is the same ownership pattern we used for `Window` and `InputManager` in Chapter 5a.

---

## Before vs After: Summary

| Aspect | Before (Chapter 30) | After (Chapter 30a) |
|---|---|---|
| **Render pass ordering** | Implicit in 60-line method | `RenderPipeline::execute()` calls 7 named passes |
| **Adding a new pass** | Find the right line, get GL state right | Add one method + one call in `execute()` |
| **GL state management** | Manual, fragile, depends on previous pass | Each pass manages its own entry/exit state |
| **Shader storage** | 9 individual `Shader` members per state | `ShaderCache` with compile-once, retrieve-by-name |
| **Shader sharing** | Each state compiles its own copy | `shaders.get("text")` returns cached instance |
| **postprocess.vert** | Compiled 3 times (once per PP shader) | Compiled once, shared by all PP programs |
| **Hot-reload** | Not possible | `reload_shaders` console command (debug builds) |
| **Font storage** | 3 `Font` members + manual load() | `resources.getFont("hud", 18)` — cached |
| **Font sharing** | Each state creates its own instance | Shared via `ResourceManager` |
| **PostProcessShaders struct** | Assembled from raw pointers each frame | Pipeline pulls from `ShaderCache` |
| **BloomFBOs struct** | Assembled from raw pointers each frame | `RenderContext` holds references directly |
| **PlayingState::render()** | 60+ lines | 3 lines (assemble context, execute, done) |

---

## Updated File Structure

After this chapter, your project tree has these new and modified files:

```
src/
  engine/
    core/
      resource_manager.h       <- MODIFIED: added getFont() methods, m_fonts map
      math_utils.h             <- UNCHANGED (from 20a)
      mesh_factory.h           <- UNCHANGED (from 15a)
      fixed_timestep.h         <- UNCHANGED (from 10a)
      window.h                 <- UNCHANGED (from 5a)
    renderer/
      shader_cache.h           <- NEW: compile-once, retrieve-by-name, hot-reload
      render_pipeline.h        <- NEW: RenderContext struct, RenderPipeline class
      render_pipeline.cpp      <- NEW: execute(), 7 pass methods
      framebuffer.h            <- UNCHANGED (from Chapter 28)
      screen_quad.h            <- UNCHANGED (from Chapter 28)
      font.h                   <- UNCHANGED (from Chapter 30)
      text_renderer.h          <- UNCHANGED (from Chapter 30)
    ecs/
      systems/
        hud_system.h           <- UNCHANGED (from 15a)
        post_process_system.h  <- REMOVED (logic moved into RenderPipeline)
  game/
    states/
      playing_state.h          <- MODIFIED: removed 9 Shader members, 3 Font members
      playing_state.cpp        <- MODIFIED: render() is now 3 lines
  main.cpp                     <- MODIFIED: creates ShaderCache, loads fonts via ResourceManager
```

Add the new `.cpp` file to your `CMakeLists.txt`:

```cmake
add_executable(QEngine
    # ... existing files ...
    src/engine/renderer/render_pipeline.cpp
)
```

Note that `shader_cache.h` is header-only. The `ResourceManager` font additions are also header-only (inline methods in the class body). Only `render_pipeline.cpp` is a new compilation unit.

---

## Build and Test

Rebuild the project:

```bash
cmake --build build
```

You should see the same game as before: skybox renders behind everything, shadows are cast correctly, opaque geometry is lit, particles blend smoothly, the weapon draws in front of everything, bloom glows, vignette darkens edges, and the HUD renders on top without post-processing. The behaviour is identical.

If something does not render:

1. **Check the ShaderCache names.** If you request `shaders.get("world_shader")` but loaded it as `shaders.get("world")`, you get nullptr and the pass silently skips. The names must match exactly. Enable the console output to see which shaders were loaded.

2. **Check that shaders are loaded before the pipeline runs.** The `loadShaders()` function must be called before any game state constructor that uses the cache. If a state tries to `shaders.get("skybox")` before it is loaded, it gets nullptr.

3. **Check the RenderContext assembly.** If you forget to set `ctx.skybox`, the skybox pass is skipped. If you forget to set `ctx.hudRenderer`, the HUD pass is skipped. The pipeline tests for nullptr before each pass -- silent skips are intentional (they let you reuse the pipeline in states that do not have a skybox).

4. **Check the font cache keys.** `resources.getFont("hud", 18)` must match the `"hud"` name and `18` pixel size used during loading. A mismatch returns nullptr.

5. **Test hot-reload.** In a debug build, open the console and type `reload_shaders`. You should see "ShaderCache: reloaded 'world'" (and similar) in the console output. Edit a fragment shader file, save it, and reload. If the new shader has a syntax error, the old shader is preserved and an error message is printed.

---

## Exercises

1. **Add a decal pass.** Chapter 31 introduces decals. Add a `renderDecals()` method to `RenderPipeline` between `renderOpaque()` and `renderTransparent()`. Decals need depth testing but should write to colour only (use `glDepthMask(GL_FALSE)`). Verify that the pipeline's GL state management keeps the pass isolated.

2. **Shader dependency graph.** Some shaders share the same vertex file (`postprocess.vert`). Modify `ShaderCache` to detect when the same vertex file is used by multiple programs and print a summary: "postprocess.vert used by: bloom_bright, blur, pp_final". This is not a functional requirement -- it is a diagnostic that helps you understand your shader dependencies.

3. **Font size aliasing.** Load the same font file at three different sizes through `ResourceManager`: 14, 18, and 24 pixels. Verify that three separate entries exist in the cache and that each returns a different atlas texture. Then request `getFont("hud", 18)` a second time and verify it returns the cached instance (check that the `shared_ptr` use count is 2).

4. **Pass profiling.** Add a simple timer around each pass method in `RenderPipeline::execute()`. Print the per-pass time in milliseconds to the console. This tells you which pass is the bottleneck. In a future chapter, you could display this as a profiling overlay using the `TextRenderer`.

5. **Pipeline variants.** Create a `MinimalPipeline` that skips shadows, post-processing, and particles. Use it for the `MenuState` where you only need a skybox and HUD. This exercises the idea that the pipeline's pass list can vary without changing the pass implementations.

---

## What We Accomplished

No new features. No new visual output. The game looks and feels exactly the same. Here is what changed underneath:

1. **The render pipeline is explicit.** Seven named passes, called in order by one method. The pass ordering is encoded in code structure, not in a 60-line method where you have to count lines to figure out what renders when. Adding a new pass means adding a method and one line in `execute()`.

2. **GL state is encapsulated per pass.** Each pass sets the state it needs and restores what it changed. No pass assumes the state left by the previous pass. This eliminates the class of bugs where changing pass ordering silently breaks rendering.

3. **Shaders are compiled once and shared.** The `ShaderCache` replaces nine `Shader` member variables with named lookup. Multiple game states share the same compiled programs. Hot-reload in debug builds lets you iterate on shaders without restarting the engine.

4. **Fonts are cached and shared.** The `ResourceManager` extension caches `Font` instances by `"name:size"` key. No duplicate atlas textures, no duplicate FreeType initialisations. Any system can retrieve a font by logical name without knowing its file path.

5. **PlayingState is shorter and clearer.** The render method went from 60+ lines to 3. The constructor dropped 12 member variable initialisations. The header has fewer includes, fewer members, and a clearer separation between game logic and rendering infrastructure.

The pattern across all our cleanup chapters is consistent: identify implicit structure, make it explicit; identify duplicated resources, cache them; identify fragile state management, encapsulate it. Chapters 5a through 30a have progressively moved from "code that works" to "architecture that scales". Each cleanup chapter invests a little time to save a lot of time later -- and Chapter 31 (Decals) will be the first beneficiary, adding cleanly into the pipeline we just built.

---

*Next up: **Chapter 31 -- Decals**, where we add bullet holes, scorch marks, and blood splatters that project onto world geometry. With the `RenderPipeline` in place, the decal pass slots in between opaque and transparent rendering with a single method call.*
