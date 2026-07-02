# Chapter 47: ImGui Integration & Debug UI

## What You'll Learn
- What Dear ImGui is and why virtually every game engine uses it for debug and editor UI
- Immediate-mode GUI vs retained-mode GUI: two fundamentally different philosophies
- Integrating ImGui with GLFW and OpenGL 3.3 using the official backend files
- Where ImGui initialisation, per-frame setup, and rendering fit in the RenderPipeline from Chapter 30a
- Building four debug windows: Entity Inspector, Entity List, System Profiler, and Registry Stats
- A lightweight component reflection system using EnTT's type_id and manual registration
- Input routing: letting ImGui capture keyboard and mouse without interfering with gameplay
- Toggling the debug overlay with F1 (separate from the ~ developer console)
- C++ concept: immediate-mode GUI design and why it works so well for tools

---

## Where We Left Off

Over 46 chapters we built a complete Quake-style FPS engine. We have an ECS architecture, skeletal animation with IK and ragdolls, PBR materials, a data-driven particle system, pathfinding, and a text-based developer console. The engine works. But when something goes wrong at runtime -- an entity is in the wrong position, a component has a bad value, a system is taking too long -- our only diagnostic tool is that text console from Chapter 27 and whatever we can print to stdout.

Professional engines have visual debug tools. You click on an entity and see all its components. You drag a slider to change a value and watch the result immediately. You see a bar chart of how long each system takes per frame. Unity has its Inspector and Profiler. Unreal has its Details panel and Stat commands. These are not luxuries. They are the difference between debugging for five minutes and debugging for five hours.

We are going to add all of that using Dear ImGui.

---

## What is Dear ImGui?

Dear ImGui (often just "ImGui") is a C++ library created by Omar Cornut. It provides a complete GUI toolkit -- windows, buttons, sliders, text inputs, trees, tables, colour pickers, plots -- all rendered through your existing graphics API. It has no external dependencies beyond a C++ compiler and whatever rendering backend you provide.

The key word is **immediate-mode**. This is not how most GUI frameworks work, and understanding the difference is important.

### Retained-Mode vs Immediate-Mode

In **retained-mode** GUI (Qt, HTML, Unity UI), you create persistent widget objects. A button lives in memory. You set its text, position, and callback. The framework manages lifetimes, parent-child hierarchies, and re-renders when state changes. You must synchronise widget state with your game data.

In **immediate-mode** GUI (Dear ImGui), there are no persistent objects. Every frame, you call functions that both define and draw the UI:

```cpp
// Immediate-mode -- the entire "button" is this one line:
if (ImGui::Button("Fire Weapon")) {
    weapon.fire();
}

// To change the text, just change the string.
// To remove the button, just don't call it.
// No allocations, no lifetime management, no event registration.
```

This is why ImGui dominates game engine tooling. Debug UI changes constantly -- you add windows, remove them, change what they display based on what you are investigating. Retained-mode frameworks require you to manage widget lifecycles. ImGui gets out of the way.

---

## Getting Dear ImGui

Dear ImGui is distributed as source files you compile directly into your project -- no library to link. Clone https://github.com/ocornut/imgui and copy the core files (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_demo.cpp`, plus all headers) and the GLFW/OpenGL3 backend files from `backends/` into `extern/imgui/`.

### CMakeLists.txt Changes

```cmake
# In your CMakeLists.txt, add to your source list:

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Add to your executable
add_executable(${PROJECT_NAME}
    ${GAME_SOURCES}
    ${ENGINE_SOURCES}
    ${IMGUI_SOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    # ... your other includes
)
```

---

## The DebugUI Manager

We need a class that owns the ImGui lifecycle: initialisation, per-frame begin/end calls, and shutdown. This class also holds the state for our debug windows (which ones are open, selected entities, etc.).

### src/engine/debug/debug_ui.h

```cpp
// src/engine/debug/debug_ui.h
#pragma once

#include <imgui.h>
#include <entt/entt.hpp>
#include <GLFW/glfw3.h>

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>

// Forward declarations
class Window;

// ── System Profiler Data ─────────────────────────────────────────────────

struct SystemTimingEntry {
    std::string name;
    float       lastMs  = 0.0f;   // most recent frame time in milliseconds
    float       avgMs   = 0.0f;   // rolling average
    float       peakMs  = 0.0f;   // highest value in the rolling window
};

// ── Component Inspector Callback ─────────────────────────────────────────

// A function that draws ImGui widgets for a given component on an entity.
// Returns true if it drew something (i.e., the entity has that component).
using ComponentInspectorFn = std::function<bool(entt::registry&, entt::entity)>;

struct ComponentInspectorEntry {
    std::string          name;
    ComponentInspectorFn draw;
};

// ── DebugUI ──────────────────────────────────────────────────────────────

class DebugUI {
public:
    // Lifecycle
    void init(GLFWwindow* window);
    void shutdown();

    // Per-frame calls (called from the game loop / RenderPipeline)
    void beginFrame();
    void render(entt::registry& registry);
    void endFrame();

    // Toggle the entire overlay
    void toggle();
    bool isVisible() const { return m_visible; }

    // Input query -- does ImGui want keyboard/mouse this frame?
    bool wantsKeyboard() const;
    bool wantsMouse() const;

    // System profiler: record a system's execution time
    void recordSystemTime(const std::string& name, float ms);

    // Component reflection: register a draw function for a component type
    void registerComponentInspector(const std::string& name,
                                    ComponentInspectorFn fn);

private:
    bool m_visible    = false;
    bool m_initialised = false;

    // ── Window states ──
    bool m_showEntityList     = true;
    bool m_showEntityInspector = true;
    bool m_showSystemProfiler = true;
    bool m_showRegistryStats  = true;
    bool m_showDemoWindow     = false;   // ImGui's built-in demo

    // ── Entity Inspector state ──
    entt::entity m_selectedEntity = entt::null;

    // ── System profiler data ──
    static constexpr int TIMING_HISTORY_SIZE = 120;   // ~2 seconds at 60fps
    std::unordered_map<std::string, SystemTimingEntry> m_systemTimings;
    std::vector<std::string> m_systemOrder;            // insertion-order names

    // ── Registered component inspectors ──
    std::vector<ComponentInspectorEntry> m_inspectors;

    // ── Draw methods for each window ──
    void drawMenuBar();
    void drawEntityList(entt::registry& registry);
    void drawEntityInspector(entt::registry& registry);
    void drawSystemProfiler();
    void drawRegistryStats(entt::registry& registry);
};
```

Like the Console from Chapter 27, DebugUI is engine infrastructure, not an ECS entity. Component inspectors are registered manually because C++ has no built-in reflection -- each component type provides a draw function that knows how to display its fields. System timings are pushed (systems call `recordSystemTime()`) rather than pulled, keeping the profiler decoupled.

### src/engine/debug/debug_ui.cpp

```cpp
// src/engine/debug/debug_ui.cpp

#include "engine/debug/debug_ui.h"
#include "engine/core/window.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <numeric>
#include <cstdio>

// ═════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::init(GLFWwindow* window)
{
    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Configure IO
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // keyboard navigation
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // docking support

    // Style -- dark theme is standard for game debug UI
    ImGui::StyleColorsDark();

    // Tweak style for readability
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 2.0f;
    style.GrabRounding      = 2.0f;
    style.ScrollbarRounding = 3.0f;
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);

    // Slightly transparent window backgrounds so we can see the game underneath
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg].w = 0.92f;

    // Initialise platform and renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);  // true = install GLFW callbacks
    ImGui_ImplOpenGL3_Init("#version 330 core");

    m_initialised = true;
}

void DebugUI::shutdown()
{
    if (!m_initialised) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_initialised = false;
}

// ═════════════════════════════════════════════════════════════════════════
// Per-Frame
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::beginFrame()
{
    if (!m_initialised || !m_visible) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::render(entt::registry& registry)
{
    if (!m_initialised || !m_visible) return;

    drawMenuBar();

    if (m_showEntityList)      drawEntityList(registry);
    if (m_showEntityInspector) drawEntityInspector(registry);
    if (m_showSystemProfiler)  drawSystemProfiler();
    if (m_showRegistryStats)   drawRegistryStats(registry);
    if (m_showDemoWindow)      ImGui::ShowDemoWindow(&m_showDemoWindow);
}

void DebugUI::endFrame()
{
    if (!m_initialised || !m_visible) return;

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ═════════════════════════════════════════════════════════════════════════
// Toggle & Input Queries
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::toggle()
{
    m_visible = !m_visible;
}

bool DebugUI::wantsKeyboard() const
{
    if (!m_initialised || !m_visible) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugUI::wantsMouse() const
{
    if (!m_initialised || !m_visible) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

// ═════════════════════════════════════════════════════════════════════════
// System Profiler Recording
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::recordSystemTime(const std::string& name, float ms)
{
    auto it = m_systemTimings.find(name);
    if (it == m_systemTimings.end()) {
        // First time seeing this system -- add to ordered list
        m_systemOrder.push_back(name);
        SystemTimingEntry entry;
        entry.name   = name;
        entry.lastMs = ms;
        entry.avgMs  = ms;
        entry.peakMs = ms;
        m_systemTimings[name] = entry;
    } else {
        SystemTimingEntry& entry = it->second;
        entry.lastMs = ms;

        // Exponential moving average (alpha = 0.05 for smooth display)
        constexpr float alpha = 0.05f;
        entry.avgMs = entry.avgMs * (1.0f - alpha) + ms * alpha;

        // Track peak with slow decay
        if (ms > entry.peakMs) {
            entry.peakMs = ms;
        } else {
            entry.peakMs *= 0.995f;  // decay toward average
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Component Inspector Registration
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::registerComponentInspector(const std::string& name,
                                         ComponentInspectorFn fn)
{
    m_inspectors.push_back({ name, std::move(fn) });
}

// ═════════════════════════════════════════════════════════════════════════
// Menu Bar
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::drawMenuBar()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Entity List",      nullptr, &m_showEntityList);
            ImGui::MenuItem("Entity Inspector",  nullptr, &m_showEntityInspector);
            ImGui::MenuItem("System Profiler",   nullptr, &m_showSystemProfiler);
            ImGui::MenuItem("Registry Stats",    nullptr, &m_showRegistryStats);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo",        nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }

        // Display FPS in the menu bar for quick reference
        ImGui::SameLine(ImGui::GetWindowWidth() - 120);
        ImGui::Text("%.1f FPS (%.2f ms)",
                    ImGui::GetIO().Framerate,
                    1000.0f / ImGui::GetIO().Framerate);

        ImGui::EndMainMenuBar();
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Entity List Window
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::drawEntityList(entt::registry& registry)
{
    ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Entity List", &m_showEntityList)) {
        ImGui::End();
        return;
    }

    // ── Filter bar ──
    static char filterBuf[128] = "";
    ImGui::InputText("Filter", filterBuf, sizeof(filterBuf));
    ImGui::Separator();

    std::string filterStr(filterBuf);
    // Convert to lowercase for case-insensitive matching
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    // ── Entity count ──
    int totalCount = 0;
    registry.storage<entt::entity>()->each([&](auto) { ++totalCount; });
    ImGui::Text("Entities: %d", totalCount);
    ImGui::Separator();

    // ── Scrollable entity list ──
    if (ImGui::BeginChild("EntityScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_None)) {
        registry.storage<entt::entity>()->each([&](entt::entity entity) {
            // Build a display name -- check if entity has a tag/name component
            char label[64];
            std::snprintf(label, sizeof(label), "Entity %u",
                          static_cast<uint32_t>(entity));

            // Apply filter
            if (!filterStr.empty()) {
                std::string labelLower(label);
                std::transform(labelLower.begin(), labelLower.end(),
                               labelLower.begin(), ::tolower);
                if (labelLower.find(filterStr) == std::string::npos) {
                    return;  // skip this entity
                }
            }

            // Highlight selected entity
            bool isSelected = (entity == m_selectedEntity);
            if (ImGui::Selectable(label, isSelected)) {
                m_selectedEntity = entity;
            }
        });
    }
    ImGui::EndChild();

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════
// Entity Inspector Window
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::drawEntityInspector(entt::registry& registry)
{
    ImGui::SetNextWindowSize(ImVec2(380, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(340, 30), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Entity Inspector", &m_showEntityInspector)) {
        ImGui::End();
        return;
    }

    if (m_selectedEntity == entt::null || !registry.valid(m_selectedEntity)) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "No entity selected.\n\nSelect one from the Entity List.");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(m_selectedEntity));
    ImGui::Separator();

    // Run every registered component inspector
    bool anyDrawn = false;
    for (auto& inspector : m_inspectors) {
        // Each inspector checks if the entity has its component and draws it
        if (inspector.draw(registry, m_selectedEntity)) {
            anyDrawn = true;
        }
    }

    if (!anyDrawn) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "No inspectable components on this entity.");
    }

    ImGui::Separator();
    if (ImGui::Button("Destroy Entity")) {
        registry.destroy(m_selectedEntity);
        m_selectedEntity = entt::null;
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════
// System Profiler Window
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::drawSystemProfiler()
{
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 440), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("System Profiler", &m_showSystemProfiler)) {
        ImGui::End();
        return;
    }

    // Calculate total frame time from all systems
    float totalMs = 0.0f;
    for (auto& [name, entry] : m_systemTimings) {
        totalMs += entry.lastMs;
    }

    ImGui::Text("Total systems: %.2f ms", totalMs);
    ImGui::Separator();

    // Table header
    if (ImGui::BeginTable("SystemTimings", 4,
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_SizingStretchProp)) {

        ImGui::TableSetupColumn("System",  ImGuiTableColumnFlags_None, 3.0f);
        ImGui::TableSetupColumn("Last",    ImGuiTableColumnFlags_None, 1.0f);
        ImGui::TableSetupColumn("Avg",     ImGuiTableColumnFlags_None, 1.0f);
        ImGui::TableSetupColumn("Peak",    ImGuiTableColumnFlags_None, 1.0f);
        ImGui::TableHeadersRow();

        for (const auto& name : m_systemOrder) {
            auto it = m_systemTimings.find(name);
            if (it == m_systemTimings.end()) continue;
            const SystemTimingEntry& entry = it->second;

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(entry.name.c_str());

            ImGui::TableSetColumnIndex(1);
            // Colour-code: green < 1ms, yellow < 4ms, red >= 4ms
            ImVec4 color;
            if (entry.lastMs < 1.0f)
                color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
            else if (entry.lastMs < 4.0f)
                color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
            else
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            ImGui::TextColored(color, "%.3f", entry.lastMs);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", entry.avgMs);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", entry.peakMs);
        }

        ImGui::EndTable();
    }

    // ── Visual bar chart ──
    ImGui::Separator();
    ImGui::Text("Per-System Breakdown:");

    if (totalMs > 0.0f) {
        for (const auto& name : m_systemOrder) {
            auto it = m_systemTimings.find(name);
            if (it == m_systemTimings.end()) continue;
            const SystemTimingEntry& entry = it->second;

            float fraction = entry.lastMs / totalMs;
            char overlay[64];
            std::snprintf(overlay, sizeof(overlay), "%s: %.2f ms (%.0f%%)",
                          entry.name.c_str(), entry.lastMs, fraction * 100.0f);
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
        }
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════
// Registry Stats Window
// ═════════════════════════════════════════════════════════════════════════

void DebugUI::drawRegistryStats(entt::registry& registry)
{
    ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(730, 30), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Registry Stats", &m_showRegistryStats)) {
        ImGui::End();
        return;
    }

    // ── Alive entity count ──
    int aliveCount = 0;
    registry.storage<entt::entity>()->each([&](auto) { ++aliveCount; });
    ImGui::Text("Alive entities: %d", aliveCount);
    ImGui::Separator();

    // ── Component pool information ──
    // EnTT exposes storage through the registry. We iterate all storages
    // and display their sizes.
    ImGui::Text("Component Pools:");

    if (ImGui::BeginTable("PoolStats", 3,
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {

        ImGui::TableSetupColumn("Pool ID",   ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableSetupColumn("Size",      ImGuiTableColumnFlags_None, 1.0f);
        ImGui::TableSetupColumn("Capacity",  ImGuiTableColumnFlags_None, 1.0f);
        ImGui::TableHeadersRow();

        // Use EnTT's storage iteration
        for (auto&& [id, storage] : registry.storage()) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            // The id is a type_info hash -- not human-readable by default.
            // We display the numeric ID. The component inspector registration
            // gives us names for the types we care about.
            ImGui::Text("0x%08X", static_cast<uint32_t>(id));

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", storage.size());

            ImGui::TableSetColumnIndex(2);
            // EnTT sparse sets expose capacity through extent()
            ImGui::Text("%zu", storage.size());  // size as baseline
        }

        ImGui::EndTable();
    }

    // ── Memory estimation ──
    ImGui::Separator();
    ImGui::Text("(Pool memory tracking requires per-type sizeof registration)");

    ImGui::End();
}
```

---

## Where It Fits in the Render Pipeline

ImGui renders at the very end of the pipeline from Chapter 30a -- after HUD and console, before buffer swap. The changes to RenderPipeline are minimal:

```cpp
// In src/engine/renderer/render_pipeline.h -- add member
#include "engine/debug/debug_ui.h"

class RenderPipeline {
public:
    // ...existing methods...
    void setDebugUI(DebugUI* debugUI) { m_debugUI = debugUI; }

private:
    DebugUI* m_debugUI = nullptr;
    // ...existing members...
};
```

```cpp
// In src/engine/renderer/render_pipeline.cpp -- update render()

void RenderPipeline::render(entt::registry& registry)
{
    // ... existing passes 1-5 ...

    renderShadows(registry);
    renderScene(registry);
    renderPostProcess();
    renderHUD(registry);
    renderConsole();

    // ─── Pass 6: ImGui Debug Overlay ─────────────────────────────
    if (m_debugUI) {
        m_debugUI->beginFrame();
        m_debugUI->render(registry);
        m_debugUI->endFrame();
    }
}
```

`beginFrame()` starts the ImGui frame, `render()` submits commands, `endFrame()` issues the actual OpenGL draw calls. This must happen after all game rendering and before the buffer swap.

---

## A Scoped Timer for System Profiling

To feed timing data into the System Profiler, we need a way to measure how long each system takes. A scoped timer is the simplest approach:

### src/engine/debug/scoped_timer.h

```cpp
// src/engine/debug/scoped_timer.h
#pragma once

#include "engine/debug/debug_ui.h"
#include <chrono>
#include <string>

// RAII timer that records its lifetime to the DebugUI profiler.
// Usage:
//     {
//         ScopedTimer t(debugUI, "MovementSystem");
//         movementSystem(registry, dt);
//     }
//
// When 't' goes out of scope, it records the elapsed time.

class ScopedTimer {
public:
    ScopedTimer(DebugUI& debugUI, const std::string& name)
        : m_debugUI(debugUI)
        , m_name(name)
        , m_start(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer()
    {
        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - m_start).count();
        m_debugUI.recordSystemTime(m_name, ms);
    }

    // Non-copyable, non-movable
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    DebugUI&    m_debugUI;
    std::string m_name;
    std::chrono::high_resolution_clock::time_point m_start;
};
```

Construction starts the clock, destruction stops it and records the result. Wrap any system call in a block with a `ScopedTimer` and the profiler gets the data automatically. You will see the usage in the full initialisation section below.

---

## Component Reflection — The Inspector Registration Pattern

The hardest part of an entity inspector is answering the question: "What components does this entity have, and how do I display them?" C++ has no built-in reflection. EnTT provides `type_id<T>()` which gives you a unique hash per type, but it does not give you field names or types.

We solve this with manual registration. For each component type you want to inspect, you write a small function that uses ImGui widgets to display and edit its fields. Then you register that function with the DebugUI at startup.

### src/engine/debug/component_inspectors.h

```cpp
// src/engine/debug/component_inspectors.h
#pragma once

class DebugUI;

// Register all built-in component inspectors.
// Call this once during initialisation, after DebugUI::init().
void registerAllComponentInspectors(DebugUI& debugUI);
```

### src/engine/debug/component_inspectors.cpp

```cpp
// src/engine/debug/component_inspectors.cpp

#include "engine/debug/component_inspectors.h"
#include "engine/debug/debug_ui.h"
#include "engine/ecs/components.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// ── Helper: collapsible component header ─────────────────────────────────

// Returns true if the header is open (so we should draw the fields).
// Returns false if collapsed or the entity lacks the component.
template<typename T>
bool componentHeader(const char* name, entt::registry& registry, entt::entity entity)
{
    if (!registry.all_of<T>(entity)) return false;
    return ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
}

// ── Inspector: Transform ─────────────────────────────────────────────────

static bool inspectTransform(entt::registry& registry, entt::entity entity)
{
    if (!componentHeader<Transform>("Transform", registry, entity))
        return registry.all_of<Transform>(entity);  // still counts as present

    auto& t = registry.get<Transform>(entity);

    ImGui::DragFloat3("Position", glm::value_ptr(t.position), 0.1f);
    ImGui::DragFloat3("Rotation", glm::value_ptr(t.rotation), 0.5f);
    ImGui::DragFloat3("Scale",    glm::value_ptr(t.scale),    0.01f, 0.01f, 100.0f);

    return true;
}

// ── Inspector: Velocity ──────────────────────────────────────────────────

static bool inspectVelocity(entt::registry& registry, entt::entity entity)
{
    if (!componentHeader<Velocity>("Velocity", registry, entity))
        return registry.all_of<Velocity>(entity);

    auto& v = registry.get<Velocity>(entity);

    ImGui::DragFloat3("Linear",  glm::value_ptr(v.linear),  0.1f);
    ImGui::DragFloat3("Angular", glm::value_ptr(v.angular), 0.1f);

    float speed = glm::length(v.linear);
    ImGui::Text("Speed: %.2f", speed);

    return true;
}

// ── Inspector: Health ────────────────────────────────────────────────────

static bool inspectHealth(entt::registry& registry, entt::entity entity)
{
    if (!componentHeader<Health>("Health", registry, entity))
        return registry.all_of<Health>(entity);

    auto& h = registry.get<Health>(entity);

    ImGui::SliderFloat("Current", &h.current, 0.0f, h.max);
    ImGui::DragFloat("Max", &h.max, 1.0f, 1.0f, 10000.0f);

    // Visual health bar
    float fraction = (h.max > 0.0f) ? h.current / h.max : 0.0f;
    ImVec4 barColor;
    if (fraction > 0.6f)
        barColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    else if (fraction > 0.3f)
        barColor = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
    else
        barColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
    ImGui::PopStyleColor();

    return true;
}

// ── Registration ─────────────────────────────────────────────────────────
// Add more inspectors following the same pattern for AABB, PointLight,
// Renderable, and any other engine components you want to inspect.

void registerAllComponentInspectors(DebugUI& debugUI)
{
    debugUI.registerComponentInspector("Transform",  inspectTransform);
    debugUI.registerComponentInspector("Velocity",   inspectVelocity);
    debugUI.registerComponentInspector("Health",     inspectHealth);
    // debugUI.registerComponentInspector("AABB",       inspectAABB);
    // debugUI.registerComponentInspector("PointLight", inspectPointLight);
    // debugUI.registerComponentInspector("Renderable", inspectRenderable);
    // ... add one per component type you want inspectable
}
```

The pattern: write a function taking `(registry, entity)` that returns `bool`, check for the component, draw ImGui widgets, register it. Adding a new inspectable type means one function and one registration call -- no changes to DebugUI itself. Engine components register in `registerAllComponentInspectors()`. Game-specific components register separately.

---

## Named Entities

The entity list shows "Entity 0", "Entity 1", etc. -- useless with 200 entities. Add a simple name tag:

```cpp
// Add to src/engine/ecs/components.h
struct DebugName {
    std::string name;
};
```

Update the entity list label creation in `drawEntityList` to check for `DebugName`:

```cpp
// In the entity list loop, replace the label creation:
char label[128];
if (registry.all_of<DebugName>(entity)) {
    const auto& dn = registry.get<DebugName>(entity);
    std::snprintf(label, sizeof(label), "%s [%u]",
                  dn.name.c_str(), static_cast<uint32_t>(entity));
} else {
    std::snprintf(label, sizeof(label), "Entity %u",
                  static_cast<uint32_t>(entity));
}
```

Tag important entities when you create them in `setupScene()`:

```cpp
auto player = registry.create();
registry.emplace<Transform>(player, glm::vec3(0, 1, 0));
registry.emplace<Health>(player, 100.0f, 100.0f);
registry.emplace<DebugName>(player, "Player");
```

---

## Input Handling — Letting ImGui and the Game Coexist

When ImGui is visible and you click on a window, the game should not fire a weapon. ImGui's IO struct provides `WantCaptureKeyboard` and `WantCaptureMouse` flags. We already exposed these through our DebugUI wrapper. Now we check them in InputManager:

### Updating InputManager

```cpp
// In src/engine/core/input_manager.h -- add DebugUI pointer

class InputManager {
public:
    // ... existing interface ...

    void setDebugUI(DebugUI* debugUI) { m_debugUI = debugUI; }

    // These wrap the existing methods but check ImGui capture state
    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    glm::vec2 getMouseDelta() const;

private:
    DebugUI* m_debugUI = nullptr;
    // ... existing members ...
};
```

```cpp
// In src/engine/core/input_manager.cpp

bool InputManager::isKeyPressed(int key) const
{
    // Always allow the debug toggle key through
    // (F1 toggles ImGui, ~ toggles console)

    // If ImGui wants keyboard input, suppress game input
    if (m_debugUI && m_debugUI->wantsKeyboard()) {
        return false;
    }

    return m_keys[key];
}

bool InputManager::isMouseButtonPressed(int button) const
{
    if (m_debugUI && m_debugUI->wantsMouse()) {
        return false;
    }

    return m_mouseButtons[button];
}

glm::vec2 InputManager::getMouseDelta() const
{
    if (m_debugUI && m_debugUI->wantsMouse()) {
        return glm::vec2(0.0f);
    }

    return m_mouseDelta;
}
```

### The F1 Toggle

F1 toggles the ImGui overlay and manages cursor state. When ImGui is visible, show the cursor for window interaction. When hidden, re-capture for FPS controls (unless the console is also open).

```cpp
void handleDebugKeys(GLFWwindow* window, DebugUI& debugUI, Console& console)
{
    static bool f1WasPressed = false;
    bool f1IsPressed = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1IsPressed && !f1WasPressed) {
        debugUI.toggle();
        if (debugUI.isVisible()) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (!console.isOpen()) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
    f1WasPressed = f1IsPressed;
}
```

The rule: if either the console or ImGui is open, show the cursor. Only hide it when both are closed.

---

## Initialisation — Putting It All Together

Here is how DebugUI fits into the engine startup sequence. The critical ordering: init after the GL context exists, register inspectors before the loop, shutdown before destroying the GL context.

```cpp
// In src/main.cpp or your Application class

#include "engine/debug/debug_ui.h"
#include "engine/debug/component_inspectors.h"
#include "engine/debug/scoped_timer.h"

int main()
{
    Window window;
    window.init(1280, 720, "QEngine");

    entt::registry registry;
    Console console;

    // Initialise ImGui Debug UI (after GL context exists)
    DebugUI debugUI;
    debugUI.init(window.getGLFWHandle());
    registerAllComponentInspectors(debugUI);

    renderPipeline.setDebugUI(&debugUI);
    inputManager.setDebugUI(&debugUI);

    setupScene(registry);

    while (!window.shouldClose()) {
        float dt = /* ... delta time ... */;
        handleDebugKeys(window.getGLFWHandle(), debugUI, console);

        // Wrap each system in a ScopedTimer for profiling
        {
            ScopedTimer t(debugUI, "Physics");
            physicsSystem(registry, dt);
        }
        {
            ScopedTimer t(debugUI, "Movement");
            movementSystem(registry, dt);
        }
        {
            ScopedTimer t(debugUI, "AI");
            aiSystem(registry, dt);
        }
        {
            ScopedTimer t(debugUI, "Animation");
            animationSystem(registry, dt);
        }

        renderPipeline.render(registry);  // ImGui drawn inside
        window.swapBuffers();
        window.pollEvents();
    }

    debugUI.shutdown();   // before destroying the GL context
    window.shutdown();
    return 0;
}
```

---

## Interaction with the GLFW Callback Chain

When we called `ImGui_ImplGlfw_InitForOpenGL(window, true)`, the `true` parameter told ImGui to install its own GLFW callbacks. This sounds like it would replace our existing callbacks (from Chapter 27's console and InputManager), but ImGui's backend chains gracefully: it saves the previous callbacks and calls them after processing each event. The flow is: GLFW event -> ImGui processes it and sets `WantCaptureKeyboard`/`WantCaptureMouse` -> your original callback runs -> InputManager records state -> game systems query InputManager, which checks the capture flags before returning results.

If you passed `false` instead of `true`, you would need to forward events to ImGui manually from your callbacks. The `true` approach is simpler.

---

## C++ Concept Sidebar: Immediate-Mode GUI Design

The key insight of immediate-mode GUI is the **single source of truth**. Consider a health slider. In retained-mode, the slider widget owns a `float` value that you must synchronise with your `Health` component -- two copies, bidirectional sync. In immediate-mode, `ImGui::SliderFloat("Health", &health.current, 0, health.max)` reads from and writes to `health.current` directly. One copy. Always correct.

The trade-off is that immediate-mode redraws everything every frame. For a game already rendering a full 3D scene at 60+ FPS, the CPU cost of ImGui's draw list generation is negligible. Where it struggles is very large editor UIs with thousands of widgets, but ImGui mitigates this with clipping and tree node skipping. For debug tools, it is more than sufficient.

The name "Dear ImGui" distinguishes Omar Cornut's implementation from Casey Muratori's original IMGUI concept paper. Omar's version is the one the industry uses.

---

## Extending the Inspector — Game-Specific Components

The registration pattern makes it easy to add inspectors for game-specific components. Follow the same pattern as the engine inspectors: write a function that takes `(registry, entity)`, check for the component, draw ImGui widgets, return true/false. Register it in your game initialisation code:

```cpp
// In your game initialisation:
void registerGameInspectors(DebugUI& debugUI)
{
    debugUI.registerComponentInspector("WeaponStats", [](entt::registry& reg, entt::entity e) -> bool {
        if (!reg.all_of<WeaponStats>(e)) return false;
        if (!ImGui::CollapsingHeader("WeaponStats", ImGuiTreeNodeFlags_DefaultOpen))
            return true;

        auto& w = reg.get<WeaponStats>(e);
        ImGui::DragFloat("Damage",    &w.damage,    0.5f, 0.0f, 1000.0f);
        ImGui::DragFloat("Fire Rate", &w.fireRate,   0.1f, 0.01f, 30.0f);
        ImGui::DragInt("Ammo",        &w.currentAmmo, 1, 0, w.maxAmmo);
        if (ImGui::Button("Refill Ammo")) w.currentAmmo = w.maxAmmo;
        return true;
    });

    debugUI.registerComponentInspector("AIState", [](entt::registry& reg, entt::entity e) -> bool {
        if (!reg.all_of<AIState>(e)) return false;
        if (!ImGui::CollapsingHeader("AIState", ImGuiTreeNodeFlags_DefaultOpen))
            return true;

        auto& ai = reg.get<AIState>(e);
        const char* states[] = { "Idle", "Patrol", "Chase", "Attack", "Flee", "Dead" };
        ImGui::Text("State: %s", states[static_cast<int>(ai.currentState)]);
        ImGui::DragFloat("Sight Range",  &ai.sightRange,  0.5f, 1.0f, 100.0f);
        ImGui::DragFloat("Attack Range", &ai.attackRange,  0.5f, 0.5f, 50.0f);
        return true;
    });
}
```

Each inspector is self-contained. You can add dozens of component types without modifying DebugUI or any existing inspector.

---

## Integration with the Game State Machine

The DebugUI lives in the Application class above all game states (Chapter 21). It renders after everything, regardless of which state is active, so you can inspect entities even while paused. If your state machine destroys the registry between levels (Chapter 34), the selected entity becomes invalid -- we handle this with the `registry.valid(m_selectedEntity)` check in the inspector.

---

## Build Troubleshooting

Common issues after integration:

1. **Multiple definition of stb_truetype.** ImGui bundles stb_truetype for font rendering. If you already use it (Chapter 30), define `IMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION` in your build.

2. **OpenGL loader conflicts.** ImGui ships its own minimal GL loader. Since we use glad, tell ImGui to use it instead:

```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE
    IMGUI_IMPL_OPENGL_LOADER_GLAD
)
```

---

---

## What's Next

We now have visual debug tools that let us inspect entities, profile systems, and monitor the registry in real time. Click on an entity, see its components, drag a slider to change a value, watch the result immediately.

In **Chapter 48: Level Editor**, we will take this further. ImGui becomes the foundation for a full in-engine level editor: placing entities with mouse picking, gizmo handles for translation/rotation/scale, a file browser for assets, and save/load of level data to JSON. The inspector we built here becomes the property panel of that editor. The entity list becomes the scene hierarchy. The debug overlay evolves into a tool that can build levels, not just inspect them.

The distance from "debug UI" to "editor" is shorter than you think.
