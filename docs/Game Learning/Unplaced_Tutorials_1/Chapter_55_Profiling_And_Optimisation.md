# Chapter 55: Profiling & Optimisation

## What You'll Learn
- Why profiling matters -- "don't guess, measure" and the danger of premature optimisation
- CPU-bound vs GPU-bound -- how to tell which side of the pipeline is your bottleneck
- A `ScopedTimer` class that records the cost of any named code region using `std::chrono`
- A `Profiler` singleton with hierarchical named regions and per-frame history
- Per-system timing: wrapping each ECS system call to measure its cost every frame
- OpenGL GPU timer queries (`GL_TIME_ELAPSED`, async result readback) for per-pass GPU timing
- A `GPUProfiler` class that measures the G-buffer pass, lighting pass, SSAO, post-processing, and AA
- An ImGui profiling overlay: frame time graph, CPU system breakdown, GPU pass breakdown, entity/draw call/triangle counts
- Draw call analysis: what a draw call costs, how to count them, how to reduce them
- Memory budgets: estimating and tracking GPU memory usage across textures, buffers, and framebuffers
- A systematic bottleneck identification flowchart for diagnosing any performance problem
- Common optimisations and when to apply each one -- a review of every technique built across the series
- C++ concept: `std::chrono` -- high-resolution clocks, duration types, and why `clock()` is wrong for game profiling

---

## Don't Guess, Measure

You have been building this engine for 54 chapters. The render pipeline has a shadow pass, a G-buffer pass, SSAO with a blur pass, deferred lighting, a skybox pass, a forward transparency pass, view model rendering, TAA resolve, post-processing, and FXAA cleanup. On the CPU side, physics runs every frame, AI pathfinding evaluates, animation systems blend bones, the particle system updates thousands of particles, and the ECS dispatches a dozen different systems.

Something is going to be slow. The question is: what?

The single most common mistake in game optimisation is guessing. "The particle system looks complex, it's probably slow." So you spend two days rewriting the particle update loop, benchmarking shows a 0.3ms improvement, and the game is still dropping frames because the actual bottleneck was the SSAO pass running at full resolution with 64 samples.

```
THE OPTIMISATION TRAP

  Developer's intuition:  "AI pathfinding is probably slow"
  Reality:

  System                  Time (ms)
  -------                 ---------
  Physics                    0.8
  AI Pathfinding             0.4    <-- actually fast
  Animation                  1.2
  Particle Update            0.6
  Render Submission          3.1    <-- actual bottleneck
  -------
  Total CPU frame time:      6.1

  Without measurement, the developer optimises AI (saving 0.2ms)
  instead of render submission (which could save 2.0ms).
  The frame rate barely changes. Two days wasted.
```

The rule is simple: **profile first, optimise second**. Every millisecond you spend on code that is not the bottleneck is a millisecond wasted. This chapter builds the tools to identify bottlenecks precisely, so every optimisation effort is targeted and effective.

---

## CPU-Bound vs GPU-Bound

Before measuring individual systems, you need to answer the highest-level question: is the CPU or the GPU the bottleneck?

The CPU and GPU work in parallel. The CPU prepares draw commands and submits them. The GPU executes those commands and renders pixels. The frame is not complete until both have finished. If the CPU takes 8ms and the GPU takes 5ms, the frame time is 8ms -- CPU-bound. If the CPU takes 4ms and the GPU takes 10ms, the frame time is 10ms -- GPU-bound.

```
CPU-BOUND vs GPU-BOUND

  CPU-bound (CPU takes longer than GPU):

    CPU: [====PREPARE====|====SUBMIT====]
    GPU:       [==RENDER==]...waiting...
    Frame:     |<---------- 8ms -------->|

    Symptoms:
    - Reducing resolution does NOT improve FPS
    - Reducing scene complexity DOES improve FPS
    - GPU utilisation is below 100%

  GPU-bound (GPU takes longer than CPU):

    CPU: [==PREPARE==]...waiting...
    GPU:       [====G-BUF====|====LIGHT====|====POST====]
    Frame:     |<-------------- 12ms ------------------>|

    Symptoms:
    - Reducing resolution DOES improve FPS
    - Simplifying shaders DOES improve FPS
    - CPU utilisation is low during rendering
```

The simplest test: halve the rendering resolution. If FPS increases significantly, you are GPU-bound. If FPS barely changes, you are CPU-bound. Our profiling tools will give much more precise answers, but this quick test tells you which tool to reach for first.

---

## CPU Profiling: The ScopedTimer

The foundation of CPU profiling is measuring how long a block of code takes. We want a timer that starts when entering a scope and records the elapsed time when leaving. This is the classic RAII pattern.

```cpp
// src/engine/profiling/scoped_timer.h
#pragma once

#include <chrono>
#include <string>

class Profiler;  // Forward declaration

class ScopedTimer {
public:
    ScopedTimer(Profiler& profiler, const std::string& name);
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    Profiler&   m_profiler;
    std::string m_name;
    std::chrono::steady_clock::time_point m_start;
};
```

```cpp
// src/engine/profiling/scoped_timer.cpp
#include "engine/profiling/scoped_timer.h"
#include "engine/profiling/profiler.h"

ScopedTimer::ScopedTimer(Profiler& profiler, const std::string& name)
    : m_profiler(profiler)
    , m_name(name)
    , m_start(std::chrono::steady_clock::now())
{
    m_profiler.beginRegion(m_name);
}

ScopedTimer::~ScopedTimer() {
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - m_start).count();
    m_profiler.endRegion(m_name, ms);
}
```

Usage is one line:

```cpp
void PhysicsSystem::update(entt::registry& registry, float dt) {
    ScopedTimer timer(profiler, "Physics");

    // ... all physics work ...
}
// Timer destructor fires here, records elapsed time
```

---

## The Profiler Class

Individual timings are useful, but we need history. A single frame's numbers fluctuate -- you need rolling averages and frame-by-frame graphs. The `Profiler` stores named regions with a ring buffer of recent samples.

```cpp
// src/engine/profiling/profiler.h
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <array>

struct ProfileRegion {
    std::string name;

    static constexpr int HISTORY_SIZE = 300;  // ~5 seconds at 60 FPS
    std::array<float, HISTORY_SIZE> history{};
    int    writeIndex    = 0;
    float  currentFrame  = 0.0f;  // Accumulated time for current frame
    float  averageMs     = 0.0f;  // Rolling average
    float  peakMs        = 0.0f;  // Peak in current history window
    int    depth         = 0;     // Nesting depth for hierarchical display
};

class Profiler {
public:
    static Profiler& instance();

    // Called by ScopedTimer
    void beginRegion(const std::string& name);
    void endRegion(const std::string& name, double ms);

    // Called once per frame to finalise and rotate history
    void endFrame();

    // Access for ImGui overlay
    const std::unordered_map<std::string, ProfileRegion>& getRegions() const {
        return m_regions;
    }

    // Ordered list of region names (insertion order for consistent display)
    const std::vector<std::string>& getRegionOrder() const {
        return m_regionOrder;
    }

    // Frame-level stats
    float getFrameTimeMs() const { return m_frameTimeMs; }
    float getFPS() const;

    // Draw call tracking
    void resetDrawCalls()    { m_drawCalls = 0; m_triangles = 0; }
    void recordDrawCall(int triangleCount);
    int  getDrawCalls() const  { return m_drawCalls; }
    int  getTriangles() const  { return m_triangles; }

    // Entity count (set externally from ECS)
    void setEntityCount(int count) { m_entityCount = count; }
    int  getEntityCount() const    { return m_entityCount; }

private:
    Profiler() = default;

    std::unordered_map<std::string, ProfileRegion> m_regions;
    std::vector<std::string> m_regionOrder;  // Preserves insertion order

    int   m_currentDepth = 0;
    float m_frameTimeMs  = 0.0f;

    // Per-frame counters
    int m_drawCalls   = 0;
    int m_triangles   = 0;
    int m_entityCount = 0;

    // Frame timing
    std::array<float, ProfileRegion::HISTORY_SIZE> m_frameHistory{};
    int m_frameWriteIndex = 0;
};
```

```cpp
// src/engine/profiling/profiler.cpp
#include "engine/profiling/profiler.h"
#include <algorithm>
#include <numeric>

Profiler& Profiler::instance() {
    static Profiler s_instance;
    return s_instance;
}

void Profiler::beginRegion(const std::string& name) {
    if (m_regions.find(name) == m_regions.end()) {
        m_regions[name].name = name;
        m_regionOrder.push_back(name);
    }
    m_regions[name].depth = m_currentDepth;
    m_currentDepth++;
}

void Profiler::endRegion(const std::string& name, double ms) {
    m_currentDepth--;
    m_regions[name].currentFrame += static_cast<float>(ms);
}

void Profiler::endFrame() {
    for (auto& [name, region] : m_regions) {
        // Write current frame's time into the ring buffer
        region.history[region.writeIndex] = region.currentFrame;
        region.writeIndex = (region.writeIndex + 1) % ProfileRegion::HISTORY_SIZE;

        // Compute rolling average
        float sum = 0.0f;
        float peak = 0.0f;
        for (float sample : region.history) {
            sum += sample;
            peak = std::max(peak, sample);
        }
        region.averageMs = sum / static_cast<float>(ProfileRegion::HISTORY_SIZE);
        region.peakMs = peak;

        // Reset for next frame
        region.currentFrame = 0.0f;
    }

    // Total frame time (sum of top-level regions only, or use wall clock)
    float totalFrame = 0.0f;
    for (const auto& [name, region] : m_regions) {
        if (region.depth == 0) {
            totalFrame += region.history[(region.writeIndex
                + ProfileRegion::HISTORY_SIZE - 1) % ProfileRegion::HISTORY_SIZE];
        }
    }
    m_frameTimeMs = totalFrame;

    m_frameHistory[m_frameWriteIndex] = m_frameTimeMs;
    m_frameWriteIndex = (m_frameWriteIndex + 1)
        % ProfileRegion::HISTORY_SIZE;
}

float Profiler::getFPS() const {
    if (m_frameTimeMs <= 0.0f) return 0.0f;
    return 1000.0f / m_frameTimeMs;
}

void Profiler::recordDrawCall(int triangleCount) {
    m_drawCalls++;
    m_triangles += triangleCount;
}
```

### Instrumenting ECS Systems

Wrap every system call in the game loop:

```cpp
// In your main update loop (e.g., PlayingState::update)
void PlayingState::update(float dt) {
    auto& profiler = Profiler::instance();
    profiler.resetDrawCalls();

    {
        ScopedTimer t(profiler, "Physics");
        m_physicsSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "AI");
        m_aiSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "Animation");
        m_animationSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "Particles");
        m_particleSystem.update(m_registry, dt);
    }
    {
        ScopedTimer t(profiler, "Render Prep");
        m_renderPrepSystem.update(m_registry);
    }
    {
        ScopedTimer t(profiler, "Render");
        m_renderPipeline.execute(m_renderContext);
    }

    profiler.setEntityCount(
        static_cast<int>(m_registry.storage<entt::entity>().in_use()));
    profiler.endFrame();
}
```

Every system is now measured every frame. The profiler accumulates a 5-second history window.

---

## GPU Profiling: Timer Queries

CPU timing tells you how long the CPU spends on each system. But the rendering pass is special: the CPU just submits commands and returns. The actual GPU work happens asynchronously. Measuring the CPU time of `renderGBuffer()` tells you how long it took to *submit* the draw calls, not how long the GPU took to *execute* them.

OpenGL provides **timer queries** to measure GPU execution time.

```
GPU TIMING: WHY CPU TIMERS ARE WRONG FOR RENDER PASSES

  CPU perspective:
    renderGBuffer() {
        glDrawElements(...);    // Returns immediately (queued)
        glDrawElements(...);    // Returns immediately (queued)
        glDrawElements(...);    // Returns immediately (queued)
    }
    // CPU thinks: "That took 0.3ms"
    // Reality: GPU hasn't even started yet

  GPU perspective:
    ...still working on last frame's post-process...
    ...now starting G-buffer...
    ...G-buffer draw 1... draw 2... draw 3...
    // GPU reality: "G-buffer took 2.1ms"
```

### OpenGL Timer Queries

The mechanism is `GL_TIME_ELAPSED` queries. You wrap a section of OpenGL calls between `glBeginQuery` and `glEndQuery`. The GPU records timestamps internally. You read the result later -- critically, **not in the same frame**, because the GPU is working behind the CPU.

```cpp
// src/engine/profiling/gpu_profiler.h
#pragma once

#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

struct GPUTimerQuery {
    // Double-buffered: we write queries in frame N, read results in frame N+1
    GLuint queryObjects[2] = {0, 0};
    int    writeIndex      = 0;
    bool   hasResult       = false;

    static constexpr int HISTORY_SIZE = 300;
    std::array<float, HISTORY_SIZE> history{};
    int   historyIndex = 0;
    float lastMs       = 0.0f;
    float averageMs    = 0.0f;
    float peakMs       = 0.0f;
};

class GPUProfiler {
public:
    static GPUProfiler& instance();

    void init();
    void shutdown();

    // Wrap a GPU pass:
    void beginPass(const std::string& name);
    void endPass(const std::string& name);

    // Call once per frame before starting new passes -- collects last frame's results
    void collectResults();

    // Access for ImGui
    const std::unordered_map<std::string, GPUTimerQuery>& getTimers() const {
        return m_timers;
    }

    const std::vector<std::string>& getPassOrder() const {
        return m_passOrder;
    }

    float getTotalGPUMs() const { return m_totalGPUMs; }

private:
    GPUProfiler() = default;

    std::unordered_map<std::string, GPUTimerQuery> m_timers;
    std::vector<std::string> m_passOrder;
    float m_totalGPUMs = 0.0f;
};
```

```cpp
// src/engine/profiling/gpu_profiler.cpp
#include "engine/profiling/gpu_profiler.h"
#include <algorithm>

GPUProfiler& GPUProfiler::instance() {
    static GPUProfiler s_instance;
    return s_instance;
}

void GPUProfiler::init() {
    // Queries are created on demand in beginPass
}

void GPUProfiler::shutdown() {
    for (auto& [name, timer] : m_timers) {
        glDeleteQueries(2, timer.queryObjects);
    }
    m_timers.clear();
    m_passOrder.clear();
}

void GPUProfiler::beginPass(const std::string& name) {
    auto it = m_timers.find(name);
    if (it == m_timers.end()) {
        GPUTimerQuery timer;
        glGenQueries(2, timer.queryObjects);
        m_timers[name] = timer;
        m_passOrder.push_back(name);
        it = m_timers.find(name);
    }

    GPUTimerQuery& timer = it->second;
    glBeginQuery(GL_TIME_ELAPSED, timer.queryObjects[timer.writeIndex]);
}

void GPUProfiler::endPass(const std::string& name) {
    glEndQuery(GL_TIME_ELAPSED);
}

void GPUProfiler::collectResults() {
    m_totalGPUMs = 0.0f;

    for (auto& [name, timer] : m_timers) {
        // Read from the OTHER buffer (last frame's query)
        int readIndex = 1 - timer.writeIndex;

        if (timer.hasResult) {
            GLuint64 elapsed = 0;
            GLint available = GL_FALSE;

            glGetQueryObjectiv(timer.queryObjects[readIndex],
                               GL_QUERY_RESULT_AVAILABLE, &available);

            if (available) {
                glGetQueryObjectui64v(timer.queryObjects[readIndex],
                                     GL_QUERY_RESULT, &elapsed);

                // Convert nanoseconds to milliseconds
                timer.lastMs = static_cast<float>(elapsed) / 1000000.0f;

                // Record in history
                timer.history[timer.historyIndex] = timer.lastMs;
                timer.historyIndex = (timer.historyIndex + 1)
                    % GPUTimerQuery::HISTORY_SIZE;

                // Compute average and peak
                float sum = 0.0f;
                float peak = 0.0f;
                for (float sample : timer.history) {
                    sum += sample;
                    peak = std::max(peak, sample);
                }
                timer.averageMs = sum
                    / static_cast<float>(GPUTimerQuery::HISTORY_SIZE);
                timer.peakMs = peak;
            }
        }

        // Swap: next frame reads what we just wrote
        timer.writeIndex = readIndex;
        timer.hasResult = true;

        m_totalGPUMs += timer.lastMs;
    }
}
```

### Why Double-Buffered Queries?

The GPU works behind the CPU. When you call `glEndQuery` in frame N, the GPU might not have finished executing those commands yet. If you immediately call `glGetQueryObjectui64v`, you either get stale data or force the GPU to flush (stalling both CPU and GPU).

Double-buffering solves this: in frame N, you write to query buffer A and read results from query buffer B (which was written in frame N-1). By the time you read B, the GPU has almost certainly finished frame N-1's work.

```
DOUBLE-BUFFERED GPU QUERIES

  Frame N:
    Write queries into buffer A
    Read results from buffer B (written in frame N-1)

  Frame N+1:
    Write queries into buffer B
    Read results from buffer A (written in frame N)

  The one-frame delay is invisible to the user.
  The profiling data shown is always one frame behind,
  which is fine for a diagnostic overlay.
```

### Instrumenting the Render Pipeline

```cpp
// In render_pipeline.cpp
void RenderPipeline::execute(RenderContext& ctx) {
    auto& gpu = GPUProfiler::instance();
    gpu.collectResults();  // Read last frame's GPU timings

    // ── Shadow pass ──────────────────────────────────────
    gpu.beginPass("Shadows");
    renderShadows(ctx);
    gpu.endPass("Shadows");

    // ── G-buffer pass ────────────────────────────────────
    gpu.beginPass("G-Buffer");
    renderGBuffer(ctx);
    gpu.endPass("G-Buffer");

    // ── Motion vectors (TAA) ─────────────────────────────
    gpu.beginPass("Velocity");
    renderMotionVectors(ctx);
    gpu.endPass("Velocity");

    // ── SSAO ─────────────────────────────────────────────
    gpu.beginPass("SSAO");
    renderSSAO(ctx);
    gpu.endPass("SSAO");

    // ── SSAO Blur ────────────────────────────────────────
    gpu.beginPass("SSAO Blur");
    renderSSAOBlur(ctx);
    gpu.endPass("SSAO Blur");

    // ── Deferred lighting ────────────────────────────────
    gpu.beginPass("Lighting");
    renderDeferredLighting(ctx);
    gpu.endPass("Lighting");

    // ── Skybox ───────────────────────────────────────────
    gpu.beginPass("Skybox");
    renderSkybox(ctx);
    gpu.endPass("Skybox");

    // ── Forward transparency ─────────────────────────────
    gpu.beginPass("Forward");
    renderForwardTransparent(ctx);
    gpu.endPass("Forward");

    // ── View models ──────────────────────────────────────
    gpu.beginPass("View Model");
    renderViewModels(ctx);
    gpu.endPass("View Model");

    // ── TAA Resolve ──────────────────────────────────────
    gpu.beginPass("TAA Resolve");
    renderTAAResolve(ctx);
    gpu.endPass("TAA Resolve");

    // ── Post-processing ──────────────────────────────────
    gpu.beginPass("Post-Process");
    renderPostProcess(ctx);
    gpu.endPass("Post-Process");

    // ── FXAA ─────────────────────────────────────────────
    gpu.beginPass("FXAA");
    renderFXAA(ctx);
    gpu.endPass("FXAA");

    // ── HUD (not GPU-profiled -- it's cheap) ─────────────
    renderHUD(ctx);
}
```

Now every render pass is measured on the GPU side. The overhead of timer queries is negligible -- a few nanoseconds per query.

---

## The Profiling Overlay

We built ImGui in Chapter 47. Now we use it to display everything we are measuring. The overlay is toggled with F2 (separate from the F1 debug inspector).

```cpp
// src/engine/profiling/profiling_overlay.h
#pragma once

class ProfilingOverlay {
public:
    void toggle() { m_visible = !m_visible; }
    bool isVisible() const { return m_visible; }

    void render();

private:
    bool m_visible = false;

    void renderFrameGraph();
    void renderCPUBreakdown();
    void renderGPUBreakdown();
    void renderStats();
    void renderMemoryBudget();
};
```

```cpp
// src/engine/profiling/profiling_overlay.cpp
#include "engine/profiling/profiling_overlay.h"
#include "engine/profiling/profiler.h"
#include "engine/profiling/gpu_profiler.h"

#include <imgui.h>
#include <algorithm>

void ProfilingOverlay::render() {
    if (!m_visible) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Profiler (F2)", &m_visible)) {
        renderFrameGraph();
        ImGui::Separator();
        renderStats();
        ImGui::Separator();
        renderCPUBreakdown();
        ImGui::Separator();
        renderGPUBreakdown();
        ImGui::Separator();
        renderMemoryBudget();
    }
    ImGui::End();
}

void ProfilingOverlay::renderFrameGraph() {
    auto& profiler = Profiler::instance();

    float fps = profiler.getFPS();
    float frameMs = profiler.getFrameTimeMs();

    ImGui::Text("FPS: %.1f  |  Frame: %.2f ms", fps, frameMs);

    // Colour-coded frame time indicator
    ImVec4 colour;
    if (frameMs < 16.67f)       colour = ImVec4(0.2f, 0.9f, 0.2f, 1.0f);  // Green: 60+ FPS
    else if (frameMs < 33.33f)  colour = ImVec4(0.9f, 0.9f, 0.2f, 1.0f);  // Yellow: 30-60
    else                        colour = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);  // Red: <30

    ImGui::SameLine();
    ImGui::TextColored(colour, "[%s]",
        frameMs < 16.67f ? "60+" : frameMs < 33.33f ? "30-60" : "<30");

    // Frame time history graph
    // Collect the last 300 samples from the profiler's frame history
    // For simplicity, we use a static buffer updated each frame
    static float frameHistory[300] = {};
    static int offset = 0;
    frameHistory[offset] = frameMs;
    offset = (offset + 1) % 300;

    ImGui::PlotLines("##frametime", frameHistory, 300, offset,
                     nullptr, 0.0f, 33.33f, ImVec2(400, 60));
    ImGui::Text("Target: 16.67ms (60 FPS)");
}

void ProfilingOverlay::renderStats() {
    auto& profiler = Profiler::instance();
    auto& gpu = GPUProfiler::instance();

    ImGui::Text("Entities:   %d", profiler.getEntityCount());
    ImGui::Text("Draw Calls: %d", profiler.getDrawCalls());
    ImGui::Text("Triangles:  %d", profiler.getTriangles());
    ImGui::Text("CPU Total:  %.2f ms", profiler.getFrameTimeMs());
    ImGui::Text("GPU Total:  %.2f ms", gpu.getTotalGPUMs());

    // CPU vs GPU bound indicator
    float cpuMs = profiler.getFrameTimeMs();
    float gpuMs = gpu.getTotalGPUMs();
    if (cpuMs > gpuMs * 1.2f) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), ">> CPU-BOUND <<");
    } else if (gpuMs > cpuMs * 1.2f) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1, 1), ">> GPU-BOUND <<");
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Balanced");
    }
}

void ProfilingOverlay::renderCPUBreakdown() {
    auto& profiler = Profiler::instance();

    if (!ImGui::CollapsingHeader("CPU Systems", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    float totalMs = profiler.getFrameTimeMs();

    for (const auto& name : profiler.getRegionOrder()) {
        const auto& region = profiler.getRegions().at(name);

        // Indent by depth
        if (region.depth > 0) {
            ImGui::Indent(region.depth * 16.0f);
        }

        // Horizontal bar showing proportion of frame time
        float fraction = (totalMs > 0.0f) ? region.averageMs / totalMs : 0.0f;
        fraction = std::clamp(fraction, 0.0f, 1.0f);

        char label[128];
        snprintf(label, sizeof(label), "%s: %.2f ms (avg) / %.2f ms (peak)",
                 name.c_str(), region.averageMs, region.peakMs);

        ImGui::ProgressBar(fraction, ImVec2(300, 16), label);

        if (region.depth > 0) {
            ImGui::Unindent(region.depth * 16.0f);
        }
    }
}

void ProfilingOverlay::renderGPUBreakdown() {
    auto& gpu = GPUProfiler::instance();

    if (!ImGui::CollapsingHeader("GPU Passes", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    float totalMs = gpu.getTotalGPUMs();

    for (const auto& name : gpu.getPassOrder()) {
        const auto& timer = gpu.getTimers().at(name);

        float fraction = (totalMs > 0.0f) ? timer.averageMs / totalMs : 0.0f;
        fraction = std::clamp(fraction, 0.0f, 1.0f);

        char label[128];
        snprintf(label, sizeof(label), "%s: %.2f ms (avg) / %.2f ms (peak)",
                 name.c_str(), timer.averageMs, timer.peakMs);

        // Colour-code passes that take more than 3ms
        if (timer.averageMs > 3.0f) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        }

        ImGui::ProgressBar(fraction, ImVec2(300, 16), label);

        if (timer.averageMs > 3.0f) {
            ImGui::PopStyleColor();
        }
    }
}

void ProfilingOverlay::renderMemoryBudget() {
    if (!ImGui::CollapsingHeader("GPU Memory (estimated)"))
        return;

    // These values would come from your resource managers
    // Here we show the structure -- you would integrate with
    // your texture manager, mesh manager, etc.
    ImGui::Text("Textures:      --- MB");
    ImGui::Text("Vertex Data:   --- MB");
    ImGui::Text("Framebuffers:  --- MB");
    ImGui::Text("Total:         --- MB");
    ImGui::TextWrapped(
        "Note: Integrate with ResourceManager to populate these values. "
        "See the Memory Budgets section below for estimation formulas.");
}
```

### Wiring Up the Toggle

```cpp
// In your input handling (e.g., PlayingState::handleInput)
if (input.isKeyPressed(GLFW_KEY_F2)) {
    m_profilingOverlay.toggle();
}

// In your render loop, after all game rendering but before swap:
m_profilingOverlay.render();
```

```
THE PROFILING OVERLAY

  +------------------------------------------+
  | Profiler (F2)                      [x]   |
  |------------------------------------------|
  | FPS: 62.3  |  Frame: 16.05 ms    [60+]  |
  | [||||||||||||||||||||         ] 16.05ms  |
  | Target: 16.67ms (60 FPS)                 |
  |------------------------------------------|
  | Entities:   347                          |
  | Draw Calls: 142                          |
  | Triangles:  1,247,830                    |
  | CPU Total:  6.12 ms                      |
  | GPU Total:  9.41 ms                      |
  | >> GPU-BOUND <<                          |
  |------------------------------------------|
  | CPU Systems                          [-] |
  |   Physics:     0.82 ms  [====      ]     |
  |   AI:          0.41 ms  [==        ]     |
  |   Animation:   1.23 ms  [======    ]     |
  |   Particles:   0.58 ms  [===       ]     |
  |   Render Prep: 1.74 ms  [========  ]     |
  |   Render:      1.34 ms  [=======   ]     |
  |------------------------------------------|
  | GPU Passes                           [-] |
  |   Shadows:     1.10 ms  [====      ]     |
  |   G-Buffer:    2.31 ms  [=========+]     |
  |   SSAO:        1.84 ms  [=======   ]     |
  |   SSAO Blur:   0.22 ms  [=        ]     |
  |   Lighting:    2.14 ms  [========= ]     |
  |   Post-Proc:   0.89 ms  [====      ]     |
  |   FXAA:        0.51 ms  [==        ]     |
  |   TAA Resolve: 0.40 ms  [==        ]     |
  +------------------------------------------+
```

This overlay immediately tells you: "The GPU is the bottleneck. The G-buffer pass and lighting pass are the two most expensive. Focus optimisation efforts there."

---

## Draw Call Analysis

A **draw call** is a command that tells the GPU to execute a batch of work: `glDrawElements`, `glDrawArrays`, `glDrawElementsInstanced`, etc. Each draw call has overhead.

```
ANATOMY OF A DRAW CALL

  Before glDrawElements() executes, the driver must:

  1. Validate the current OpenGL state
  2. Translate shader uniforms to GPU-native format
  3. Resolve texture bindings and sampler states
  4. Check framebuffer completeness
  5. Submit the command to the GPU command queue

  This overhead is roughly constant per draw call, regardless
  of how many triangles the call draws.

  Result: 1000 draw calls of 10 triangles each is MUCH slower
  than 1 draw call of 10,000 triangles.

  Typical cost per draw call: 5-20 microseconds on the CPU
  (heavily driver-dependent).
```

### Counting Draw Calls

We already added a draw call counter to the Profiler. To use it, wrap your mesh draw calls:

```cpp
// In your Mesh::draw() method or wherever glDrawElements is called
void Mesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    Profiler::instance().recordDrawCall(m_indexCount / 3);
}
```

Now the overlay shows the exact number of draw calls per frame. A healthy target for a Quake-style level is 100-400 draw calls. If you see 2000+, you have a batching problem.

### Reducing Draw Calls

We have already built the three primary techniques:

**1. Instanced rendering (Chapter 38).** If 200 identical crates exist in the level, instancing draws them all in one draw call instead of 200 separate calls. The instance buffer sends per-instance transforms to the GPU.

**2. Frustum culling (Chapter 32).** Objects outside the view are not submitted at all. This does not reduce the draw call cost per call, but eliminates calls entirely for invisible objects. A typical arena with 500 entities might only have 150 in view.

**3. Material sorting.** State changes between draw calls -- switching shaders, binding different textures -- are expensive. Sorting objects by material so that all objects sharing the same shader and textures are drawn consecutively minimises state changes.

```cpp
// Sort renderables by material before drawing
std::sort(renderList.begin(), renderList.end(),
    [](const Renderable& a, const Renderable& b) {
        // Primary sort: shader program ID
        if (a.shaderID != b.shaderID)
            return a.shaderID < b.shaderID;
        // Secondary sort: diffuse texture ID
        return a.textureID < b.textureID;
    });
```

```
EFFECT OF MATERIAL SORTING

  Unsorted draw order (worst case -- constant state changes):

    Object 1: Shader A, Texture 1    <- bind A, bind 1
    Object 2: Shader B, Texture 3    <- bind B, bind 3  (2 state changes)
    Object 3: Shader A, Texture 2    <- bind A, bind 2  (2 state changes)
    Object 4: Shader B, Texture 3    <- bind B, bind 3  (2 state changes)
    Object 5: Shader A, Texture 1    <- bind A, bind 1  (2 state changes)
    Total state changes: 8

  Sorted draw order:

    Object 1: Shader A, Texture 1    <- bind A, bind 1
    Object 5: Shader A, Texture 1    <- (no change)
    Object 3: Shader A, Texture 2    <- bind 2           (1 state change)
    Object 2: Shader B, Texture 3    <- bind B, bind 3   (2 state changes)
    Object 4: Shader B, Texture 3    <- (no change)
    Total state changes: 3

  Same objects, same triangles, 62% fewer state changes.
```

---

## Memory Budgets

GPU memory is finite. Every texture, vertex buffer, index buffer, and framebuffer attachment lives in VRAM. Knowing how much memory each asset type consumes lets you make informed decisions about quality settings.

### Estimating Memory Per Asset Type

```
GPU MEMORY ESTIMATION FORMULAS

  Textures:
    bytes = width x height x bytes_per_pixel x mip_factor

    Format          Bytes/Pixel    Typical Use
    ------          -----------    -----------
    RGBA8           4              Diffuse/albedo
    RGB8            3              (Often padded to 4 by driver)
    RG16F           4              Normal maps, velocity
    RGBA16F         8              HDR colour buffers
    R8              1              AO, roughness, metallic
    DXT1/BC1        0.5            Compressed diffuse (no alpha)
    DXT5/BC3        1.0            Compressed diffuse (with alpha)
    BC5             1.0            Compressed normal maps

    Mip factor: with full mip chain, total = base x 1.33
    (Each mip is 1/4 the size: 1 + 1/4 + 1/16 + ... = 4/3)

    Example: 2048x2048 RGBA8 with mips
    = 2048 x 2048 x 4 x 1.33 = ~22 MB

  Vertex Buffers:
    bytes = vertex_count x bytes_per_vertex

    Typical vertex: pos(12) + normal(12) + uv(8) + tangent(12)
    = 44 bytes/vertex

    10,000-triangle mesh ~ 30,000 vertices x 44 = ~1.3 MB

  Framebuffers (G-buffer example at 1920x1080):
    Position:   1920 x 1080 x 8  (RGBA16F)  = 16.6 MB
    Normal:     1920 x 1080 x 8  (RGBA16F)  = 16.6 MB
    Albedo:     1920 x 1080 x 4  (RGBA8)    =  8.3 MB
    Depth:      1920 x 1080 x 4  (DEPTH24)  =  8.3 MB
    ────────────────────────────────────────────────────
    G-buffer total:                           ~49.8 MB

    Plus: SSAO FBO, SSAO blur FBO, scene FBO,
          post-process FBO, TAA history x2, velocity...
    Framebuffers total at 1080p: ~150-200 MB
```

### Tracking Memory Usage

A simple memory tracker integrated with your resource managers:

```cpp
// src/engine/profiling/gpu_memory_tracker.h
#pragma once

#include <string>
#include <unordered_map>

class GPUMemoryTracker {
public:
    static GPUMemoryTracker& instance();

    void recordAllocation(const std::string& category, size_t bytes);
    void recordDeallocation(const std::string& category, size_t bytes);

    size_t getCategoryBytes(const std::string& category) const;
    size_t getTotalBytes() const;

    float getCategoryMB(const std::string& category) const {
        return static_cast<float>(getCategoryBytes(category))
            / (1024.0f * 1024.0f);
    }
    float getTotalMB() const {
        return static_cast<float>(getTotalBytes()) / (1024.0f * 1024.0f);
    }

    const std::unordered_map<std::string, size_t>& getCategories() const {
        return m_categories;
    }

private:
    GPUMemoryTracker() = default;
    std::unordered_map<std::string, size_t> m_categories;
};
```

```cpp
// src/engine/profiling/gpu_memory_tracker.cpp
#include "engine/profiling/gpu_memory_tracker.h"

GPUMemoryTracker& GPUMemoryTracker::instance() {
    static GPUMemoryTracker s_instance;
    return s_instance;
}

void GPUMemoryTracker::recordAllocation(const std::string& category,
                                         size_t bytes) {
    m_categories[category] += bytes;
}

void GPUMemoryTracker::recordDeallocation(const std::string& category,
                                           size_t bytes) {
    auto it = m_categories.find(category);
    if (it != m_categories.end()) {
        it->second = (it->second >= bytes) ? it->second - bytes : 0;
    }
}

size_t GPUMemoryTracker::getCategoryBytes(const std::string& category) const {
    auto it = m_categories.find(category);
    return (it != m_categories.end()) ? it->second : 0;
}

size_t GPUMemoryTracker::getTotalBytes() const {
    size_t total = 0;
    for (const auto& [cat, bytes] : m_categories) {
        total += bytes;
    }
    return total;
}
```

Integrate it where resources are created:

```cpp
// In TextureManager when loading a texture
GLuint texture = loadTexture(path);
size_t bytes = width * height * bytesPerPixel * 1.33;  // With mips
GPUMemoryTracker::instance().recordAllocation("Textures", bytes);

// In GBuffer::init()
size_t gbufferBytes = width * height * (8 + 8 + 4 + 4);  // All attachments
GPUMemoryTracker::instance().recordAllocation("Framebuffers", gbufferBytes);
```

Now the profiling overlay's memory section can display real numbers:

```cpp
void ProfilingOverlay::renderMemoryBudget() {
    if (!ImGui::CollapsingHeader("GPU Memory (estimated)"))
        return;

    auto& mem = GPUMemoryTracker::instance();

    for (const auto& [category, bytes] : mem.getCategories()) {
        ImGui::Text("%-16s %.1f MB", category.c_str(), mem.getCategoryMB(category));
    }
    ImGui::Separator();
    ImGui::Text("Total:           %.1f MB", mem.getTotalMB());
}
```

---

## Bottleneck Identification Flowchart

When the frame rate drops below your target, follow this decision tree:

```
BOTTLENECK IDENTIFICATION FLOWCHART

  Frame time > 16.67ms (below 60 FPS)?
  |
  +-- NO --> You're fine. Ship it.
  |
  +-- YES
       |
       Compare CPU total vs GPU total (from profiler overlay)
       |
       +-- CPU total > GPU total --> CPU-BOUND
       |    |
       |    Which CPU system is slowest? (check CPU breakdown)
       |    |
       |    +-- Physics
       |    |    - Too many collision pairs? Simplify collision shapes.
       |    |    - Reduce physics tick rate (60Hz -> 30Hz for distant objects).
       |    |
       |    +-- AI / Pathfinding
       |    |    - Spread path queries across multiple frames.
       |    |    - Use simpler steering for distant enemies.
       |    |
       |    +-- Animation
       |    |    - Reduce bone count for LOD meshes.
       |    |    - Skip bone updates for off-screen entities (frustum cull).
       |    |
       |    +-- Render Prep / Submission
       |    |    - Too many draw calls? Use instancing (Ch 38).
       |    |    - Sort by material to reduce state changes.
       |    |    - Frustum cull more aggressively (Ch 32).
       |    |
       |    +-- Particle Update
       |         - Reduce max particle counts.
       |         - Use GPU particle simulation (compute shaders).
       |
       +-- GPU total > CPU total --> GPU-BOUND
            |
            Which GPU pass is slowest? (check GPU breakdown)
            |
            +-- G-Buffer pass
            |    - Too many triangles? Use LOD (Ch 51).
            |    - Frustum culling not working? Check cull stats.
            |    - Vertex-heavy shaders? Simplify vertex transforms.
            |
            +-- Lighting pass
            |    - Too many lights? Use light culling / tiled deferred.
            |    - Light volumes too large? Reduce point light radii.
            |    - Shader too heavy? Simplify BRDF for distant lights.
            |
            +-- SSAO
            |    - Reduce sample count (64 -> 32 -> 16).
            |    - Render at half resolution, then upsample.
            |    - Reduce radius to limit texture cache misses.
            |
            +-- Shadows
            |    - Reduce shadow map resolution (2048 -> 1024).
            |    - Use fewer cascade levels.
            |    - Cull shadow casters more aggressively.
            |
            +-- Post-processing / AA
                 - Disable FXAA (cheapest to cut).
                 - TAA is usually cheap -- check velocity pass.
                 - Reduce bloom iterations.
```

This flowchart is not a one-time exercise. Performance characteristics change as you add content to the game. A level with open vistas is GPU-bound on the G-buffer pass. A level with 50 enemies in a tight room is CPU-bound on AI and physics. Profile each level individually.

---

## Common Optimisations: When to Apply Each

Over the course of this series, we have built several techniques that exist primarily for performance. Here is when each one matters and when it does not.

### Frustum Culling (Chapter 32)

**When it helps:** Large open levels with many entities. If only 30% of entities are visible, frustum culling eliminates 70% of draw calls. This helps both CPU (fewer draw calls to prepare) and GPU (fewer triangles to rasterise).

**When it doesn't help:** Small enclosed rooms where nearly everything is always visible. The culling test itself has a cost (~0.1 microsecond per AABB test), which is negligible for 500 entities but worth noting.

### Level of Detail (Chapter 51)

**When it helps:** GPU-bound on the G-buffer pass with many high-polygon models visible at varying distances. LOD reduces vertex processing and rasterisation load for distant objects.

**When it doesn't help:** Scenes where most objects are close to the camera. If everything is within 20 metres, you are always at LOD 0 and the system adds overhead without saving anything.

### Instanced Rendering (Chapter 38)

**When it helps:** Many instances of the same mesh (crates, columns, foliage, enemies of the same type). Reduces draw calls from N to 1 per unique mesh. Critical when CPU-bound on render submission.

**When it doesn't help:** Scenes with all unique meshes. If every object is different, there is nothing to instance.

### Deferred Rendering (Chapter 52)

**When it helps:** Scenes with many dynamic lights. Deferred rendering scales O(pixels x lights) instead of O(objects x lights x overdraw). With 50+ lights, the savings are enormous.

**When it doesn't help:** Scenes with very few lights (1-4). Forward rendering is simpler and has no G-buffer memory overhead.

### Texture Compression (BC1/BC3/BC5)

**When it helps:** Always. Compressed textures use 4-8x less memory and bandwidth than uncompressed RGBA8. Memory bandwidth is one of the most common GPU bottlenecks. A 2048x2048 RGBA8 texture is 16 MB; the same texture in BC3 is 2 MB.

**When it doesn't help:** It always helps. The quality loss from modern BC compression is minimal. Use BC1 for RGB (no alpha), BC3 for RGBA, BC5 for normal maps (two-channel).

### Material Sorting

**When it helps:** When you have many different materials and draw calls. Sorting objects by shader and texture before drawing reduces driver overhead from state changes.

**When it doesn't help:** If you only have 2-3 materials, the overhead of sorting may exceed the savings.

### Early-Z and Depth Pre-Pass

**When it helps:** Scenes with heavy overdraw and expensive fragment shaders. A depth pre-pass renders all geometry with a trivial shader that only writes depth. The main pass then uses the populated depth buffer with `GL_EQUAL` depth test, ensuring the expensive PBR shader runs exactly once per visible pixel.

**When it doesn't help:** Deferred rendering already solves overdraw -- the G-buffer pass writes depth, and the lighting pass runs per-pixel regardless. A depth pre-pass helps forward rendering scenarios.

```cpp
// Depth pre-pass technique
void RenderPipeline::renderDepthPrePass(RenderContext& ctx) {
    // Use a minimal shader that only writes depth
    auto& depthShader = ctx.shaders.get("depth_only");
    depthShader->use();

    // Disable colour writes -- we only want depth
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Draw all opaque geometry
    for (const auto& renderable : ctx.opaqueList) {
        depthShader->setMat4("mvp", ctx.viewProjection * renderable.model);
        renderable.mesh->draw();
    }

    // Restore colour writes for the main pass
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Main pass uses GL_EQUAL -- only fragments at exactly the right depth pass
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);  // No need to write depth again
}
```

---

## Configuration Integration

Wire the profiling settings into `ConfigManager` so they persist:

```lua
-- config.lua
profiling = {
    overlay_visible  = false,  -- Start hidden, toggle with F2
    overlay_key      = "F2",
    cpu_profiling    = true,   -- Can disable to remove overhead
    gpu_profiling    = true,
    memory_tracking  = true,
    history_seconds  = 5,      -- How many seconds of history to keep
}
```

```cpp
// During initialisation
auto& config = registry.ctx().get<ConfigManager>();

bool cpuProfiling = config.get<bool>("profiling.cpu_profiling", true);
bool gpuProfiling = config.get<bool>("profiling.gpu_profiling", true);

if (gpuProfiling) {
    GPUProfiler::instance().init();
}
```

When profiling is disabled, the `ScopedTimer` and `GPUProfiler` calls should be compiled out or short-circuit. A simple approach is a macro that wraps the scoped timer creation:

```cpp
#ifdef QENGINE_PROFILING
    #define PROFILE_SCOPE(name) \
        ScopedTimer _timer_##__LINE__(Profiler::instance(), name)
    #define GPU_PROFILE_BEGIN(name) \
        GPUProfiler::instance().beginPass(name)
    #define GPU_PROFILE_END(name) \
        GPUProfiler::instance().endPass(name)
#else
    #define PROFILE_SCOPE(name)
    #define GPU_PROFILE_BEGIN(name)
    #define GPU_PROFILE_END(name)
#endif
```

With these macros, instrumenting any function is a one-liner:

```cpp
void PhysicsSystem::update(entt::registry& registry, float dt) {
    PROFILE_SCOPE("Physics");
    // ... all physics work ...
}
```

In release builds, define `QENGINE_PROFILING` to 0 and every profiling call compiles to nothing. Zero overhead.

---

## C++ Concept Sidebar: `std::chrono`

Throughout this chapter we used `std::chrono::steady_clock` for CPU timing. This deserves explanation, because C++ has multiple clocks, and choosing the wrong one produces incorrect results.

### Why Not `clock()`?

The C library function `clock()` measures **CPU time**, not **wall time**. If your thread sleeps for 10ms while waiting for a mutex, `clock()` does not count that time. If your thread runs on two cores simultaneously (e.g., OpenMP), `clock()` counts double. For game profiling, you want wall time -- the actual elapsed duration that the player experiences.

```cpp
// WRONG for game profiling:
#include <ctime>
clock_t start = clock();
doWork();
clock_t end = clock();
double ms = 1000.0 * (end - start) / CLOCKS_PER_SEC;
// This measures CPU time, not wall time.
// A 10ms sleep() shows as 0ms.
// A 5ms function on 2 threads shows as 10ms.
```

### The Three `std::chrono` Clocks

C++11 introduced three clock types in `<chrono>`:

**`std::chrono::system_clock`** -- Wall clock time. Can be mapped to calendar dates (time_since_epoch). But it can be adjusted by the operating system (NTP sync, daylight saving, manual change). If the system clock jumps backwards during your measurement, you get a negative duration.

**`std::chrono::steady_clock`** -- Monotonic clock. Guaranteed to never go backwards. Guaranteed to advance at a uniform rate. This is the correct choice for measuring elapsed durations.

**`std::chrono::high_resolution_clock`** -- The highest-resolution clock available. On most platforms, this is an alias for `steady_clock`. On some platforms, it is an alias for `system_clock`. The standard does not guarantee monotonicity. Do not use it unless you have verified what it maps to on your target platform.

```cpp
// CORRECT for game profiling:
#include <chrono>
auto start = std::chrono::steady_clock::now();
doWork();
auto end = std::chrono::steady_clock::now();

// Duration types handle unit conversion automatically
std::chrono::duration<double, std::milli> elapsed = end - start;
double ms = elapsed.count();

// Or more explicitly:
auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
double ms2 = ns.count() / 1000000.0;
```

### Duration Types

`std::chrono` uses a type-safe duration system. The type itself encodes the unit:

```cpp
using namespace std::chrono;

nanoseconds  ns(1000);      // 1000 nanoseconds
microseconds us = duration_cast<microseconds>(ns);  // 1 microsecond
milliseconds ms = duration_cast<milliseconds>(ns);  // 0 (truncated)

// Floating-point durations avoid truncation:
duration<double, std::milli> ms_f = ns;  // 0.001 milliseconds

// Arithmetic works as expected:
auto total = ms + us;  // Promotes to common type

// C++14 literals:
using namespace std::chrono_literals;
auto half_second = 500ms;
auto frame_budget = 16.667ms;
```

The key benefit: you cannot accidentally mix units. A function returning `nanoseconds` cannot be silently assigned to a variable expecting `seconds`. The compiler enforces correctness.

### Practical Resolution

On modern hardware, `steady_clock` typically has sub-microsecond resolution. You can check:

```cpp
using Clock = std::chrono::steady_clock;
auto period = Clock::period();
std::cout << "Clock resolution: "
          << period.num << "/" << period.den << " seconds"
          << std::endl;
// Typical output: 1/1000000000 (nanosecond resolution)
```

For game profiling, microsecond resolution is more than sufficient. A single frame at 60 FPS is 16,667 microseconds. Even sub-system timing (individual physics steps, individual draw calls) operates in the 10-1000 microsecond range.

---

## Putting It All Together

Here is the complete frame flow with profiling integrated:

```
COMPLETE PROFILED FRAME

  1. Profiler::instance().resetDrawCalls()
  2. GPUProfiler::instance().collectResults()     // Read last frame's GPU data

  CPU Systems (each wrapped in ScopedTimer):
  3. Physics system update
  4. AI system update
  5. Animation system update
  6. Particle system update
  7. Render preparation (frustum cull, LOD select, sort by material)

  GPU Passes (each wrapped in GPU timer queries):
  8. Shadow pass
  9. G-buffer pass (geometry only)
  10. Velocity pass (TAA motion vectors)
  11. SSAO pass
  12. SSAO blur pass
  13. Deferred lighting pass
  14. Skybox pass
  15. Forward transparency pass
  16. View model pass
  17. TAA resolve pass
  18. Post-processing pass (bloom, tone mapping)
  19. FXAA pass

  20. Profiling overlay (ImGui -- renders on top of everything)
  21. Profiler::instance().endFrame()              // Finalise CPU stats
  22. Swap buffers
```

With this infrastructure, you can answer any performance question:

- "Why is this level running at 45 FPS?" Open the overlay. GPU-bound. Lighting pass takes 7ms because there are 80 point lights with large radii. Reduce radii or implement tiled deferred.
- "Why does the frame hitch when enemies spawn?" CPU spike in Animation system. 12 enemies all start their spawn animation on the same frame. Stagger the spawns across 3 frames.
- "Why does memory usage climb over time?" Memory tracker shows Textures growing. A streaming system is loading textures but not unloading them when entities leave the level. Add an eviction policy.

---

## File Summary

| File | Status | Purpose |
|------|--------|---------|
| `src/engine/profiling/scoped_timer.h` | **New** | `ScopedTimer` class -- RAII timer using `std::chrono::steady_clock` |
| `src/engine/profiling/scoped_timer.cpp` | **New** | `ScopedTimer` implementation |
| `src/engine/profiling/profiler.h` | **New** | `Profiler` singleton -- named regions, history, draw call tracking |
| `src/engine/profiling/profiler.cpp` | **New** | `Profiler` implementation -- rolling averages, frame statistics |
| `src/engine/profiling/gpu_profiler.h` | **New** | `GPUProfiler` class -- OpenGL timer queries, double-buffered |
| `src/engine/profiling/gpu_profiler.cpp` | **New** | `GPUProfiler` implementation -- async result readback |
| `src/engine/profiling/gpu_memory_tracker.h` | **New** | `GPUMemoryTracker` -- category-based GPU memory accounting |
| `src/engine/profiling/gpu_memory_tracker.cpp` | **New** | `GPUMemoryTracker` implementation |
| `src/engine/profiling/profiling_overlay.h` | **New** | `ProfilingOverlay` -- ImGui-based performance display |
| `src/engine/profiling/profiling_overlay.cpp` | **New** | `ProfilingOverlay` implementation -- frame graph, breakdowns, stats |
| `src/engine/renderer/render_pipeline.cpp` | **Modified** | Added GPU timer query instrumentation around every render pass |
| `config.lua` | **Modified** | Added `profiling` configuration section |

---

## What's Next

In **Chapter 55a: Production Rendering Cleanup**, we will revisit the entire rendering stack and polish it for production quality. We will refactor the render pipeline to use a data-driven pass system where passes are defined in configuration rather than hardcoded, add proper error recovery for GPU resource creation failures, implement a render graph that automatically manages framebuffer lifetimes and dependencies, and clean up the shader management system with hot-reloading for faster iteration.

---

And with that, the main tutorial series is complete.

Over 55 chapters, you built a game engine from nothing. An empty window became a textured triangle, then a lit scene, then a physics simulation, then an ECS-driven game world with enemies, weapons, animations, particles, and a full rendering pipeline. You added PBR materials, skeletal animation with inverse kinematics, ragdoll physics, deferred rendering, SSAO, anti-aliasing, a level editor, a scripting language, and now the tools to measure and optimise all of it.

Every system in this engine exists because you wrote it. There is no magic, no black box, no "the framework handles that." When a bug appears, you know where to look because you built every layer. When a feature needs to change, you know what to change because the architecture is yours. That understanding is the point of this entire series.

The engine is not done -- an engine is never done. But it is *yours*, and it works, and you know exactly why.

Build something with it.