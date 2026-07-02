# Chapter 27: Developer Console & Debug Tools

## What You'll Learn
- Building an in-game command console (toggle with ~ key)
- Text input handling with GLFW character callbacks
- Command parsing and execution
- Debug rendering: wireframe AABBs, trigger volumes, nav graph, AI sight lines
- FPS counter and performance display
- Common cheat/debug commands: god, noclip, give, spawn, map

---

## Why Debug Tools Matter

Every minute spent building debug tools saves ten minutes of debugging later. Being able to type `noclip` and fly through walls, or `show_colliders` to see every AABB in the level, is invaluable. Professional game engines (Unreal, Source, id Tech) all have developer consoles — there's a reason.

---

## The Console

### Console Component

The console is engine infrastructure, not an ECS entity. It's a singleton that lives alongside the Window and AudioManager:

### src/engine/debug/console.h

```cpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <deque>

class Console {
public:
    // Toggle console open/closed
    void toggle();
    bool isOpen() const { return m_open; }

    // Register a command
    using CommandFn = std::function<void(const std::vector<std::string>& args)>;
    void registerCommand(const std::string& name, const std::string& help,
                          CommandFn fn);

    // Process a character input (called from GLFW char callback)
    void onChar(unsigned int codepoint);

    // Process key input (backspace, enter, up/down for history)
    void onKey(int key, int action);

    // Print a message to the console output
    void print(const std::string& message);

    // Get the output lines (for rendering)
    const std::deque<std::string>& getOutput() const { return m_output; }

    // Get current input text (for rendering)
    const std::string& getInput() const { return m_inputBuffer; }

    // Get cursor position
    int getCursor() const { return m_cursorPos; }

private:
    bool m_open = false;
    std::string m_inputBuffer;
    int m_cursorPos = 0;

    // Output history
    static constexpr int MAX_OUTPUT_LINES = 200;
    std::deque<std::string> m_output;

    // Input history (up/down arrows)
    static constexpr int MAX_HISTORY = 50;
    std::deque<std::string> m_history;
    int m_historyIndex = -1;  // -1 = current input, 0 = most recent

    // Registered commands
    struct Command {
        std::string help;
        CommandFn fn;
    };
    std::unordered_map<std::string, Command> m_commands;

    void execute(const std::string& line);
    std::vector<std::string> tokenise(const std::string& line);
};
```

### src/engine/debug/console.cpp

```cpp
#include "engine/debug/console.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <sstream>

void Console::toggle() {
    m_open = !m_open;
    if (m_open) {
        m_inputBuffer.clear();
        m_cursorPos = 0;
        m_historyIndex = -1;
    }
}

void Console::registerCommand(const std::string& name, const std::string& help,
                                CommandFn fn) {
    m_commands[name] = { help, fn };
}

void Console::onChar(unsigned int codepoint) {
    if (!m_open) return;
    if (codepoint == '`' || codepoint == '~') return;  // Ignore toggle key

    char c = static_cast<char>(codepoint);
    m_inputBuffer.insert(m_inputBuffer.begin() + m_cursorPos, c);
    m_cursorPos++;
}

void Console::onKey(int key, int action) {
    if (!m_open) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    switch (key) {
        case GLFW_KEY_BACKSPACE:
            if (m_cursorPos > 0) {
                m_inputBuffer.erase(m_cursorPos - 1, 1);
                m_cursorPos--;
            }
            break;

        case GLFW_KEY_DELETE:
            if (m_cursorPos < static_cast<int>(m_inputBuffer.size())) {
                m_inputBuffer.erase(m_cursorPos, 1);
            }
            break;

        case GLFW_KEY_LEFT:
            if (m_cursorPos > 0) m_cursorPos--;
            break;

        case GLFW_KEY_RIGHT:
            if (m_cursorPos < static_cast<int>(m_inputBuffer.size())) m_cursorPos++;
            break;

        case GLFW_KEY_HOME:
            m_cursorPos = 0;
            break;

        case GLFW_KEY_END:
            m_cursorPos = static_cast<int>(m_inputBuffer.size());
            break;

        case GLFW_KEY_ENTER:
            if (!m_inputBuffer.empty()) {
                print("> " + m_inputBuffer);
                execute(m_inputBuffer);

                // Add to history
                m_history.push_front(m_inputBuffer);
                if (m_history.size() > MAX_HISTORY) m_history.pop_back();

                m_inputBuffer.clear();
                m_cursorPos = 0;
                m_historyIndex = -1;
            }
            break;

        case GLFW_KEY_UP:
            if (!m_history.empty() &&
                m_historyIndex < static_cast<int>(m_history.size()) - 1) {
                m_historyIndex++;
                m_inputBuffer = m_history[m_historyIndex];
                m_cursorPos = static_cast<int>(m_inputBuffer.size());
            }
            break;

        case GLFW_KEY_DOWN:
            if (m_historyIndex > 0) {
                m_historyIndex--;
                m_inputBuffer = m_history[m_historyIndex];
                m_cursorPos = static_cast<int>(m_inputBuffer.size());
            } else if (m_historyIndex == 0) {
                m_historyIndex = -1;
                m_inputBuffer.clear();
                m_cursorPos = 0;
            }
            break;
    }
}

void Console::print(const std::string& message) {
    m_output.push_back(message);
    if (m_output.size() > MAX_OUTPUT_LINES) m_output.pop_front();

    // Also print to stdout for debugging
    std::cout << "[Console] " << message << std::endl;
}

void Console::execute(const std::string& line) {
    auto tokens = tokenise(line);
    if (tokens.empty()) return;

    const std::string& cmd = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    auto it = m_commands.find(cmd);
    if (it != m_commands.end()) {
        it->second.fn(args);
    } else {
        print("Unknown command: " + cmd);
        print("Type 'help' for a list of commands.");
    }
}

std::vector<std::string> Console::tokenise(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;

    while (stream >> token) {
        tokens.push_back(token);
    }

    return tokens;
}
```

---

## GLFW Input Hookup

The console needs character input — not just key presses. GLFW provides this through a character callback. Add the callbacks to `src/engine/core/window.cpp` and the setup code to `main.cpp`:

```cpp
// In src/engine/core/window.cpp (or main.cpp):

// Store console pointer for callbacks
static Console* g_console = nullptr;

void charCallback(GLFWwindow* window, unsigned int codepoint) {
    if (g_console) g_console->onChar(codepoint);
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!g_console) return;

    // Toggle console with ~ or `
    if ((key == GLFW_KEY_GRAVE_ACCENT) && action == GLFW_PRESS) {
        g_console->toggle();
        return;
    }

    // Send keys to console when open
    if (g_console->isOpen()) {
        g_console->onKey(key, action);
    }
}

// In main.cpp setup:
Console console;
g_console = &console;
glfwSetCharCallback(window.getHandle(), charCallback);
glfwSetKeyCallback(window.getHandle(), keyCallback);
```

### Blocking Game Input

When the console is open, game input should be blocked:

```cpp
// In PlayingState::update():
void PlayingState::update(float dt) {
    if (m_console.isOpen()) {
        // Console is open — don't process game input
        // But still render the game world (frozen)
        return;
    }

    // Normal game update...
    inputSystem(m_registry, m_window, m_camera, dt);
    // ...
}
```

---

## Registering Commands

Commands are registered at startup. Each one is a lambda that captures references to the systems it needs:

```cpp
void registerDebugCommands(Console& console, entt::registry& registry,
                            Camera& camera, DebugRenderer& debug) {

    // ─── help ───────────────────────────────────────────────────
    console.registerCommand("help", "List all commands",
        [&console](const std::vector<std::string>& args) {
            console.print("Available commands:");
            console.print("  help           - Show this list");
            console.print("  god            - Toggle invincibility");
            console.print("  noclip         - Toggle noclip (fly through walls)");
            console.print("  give <item> <n>- Give item (health, ammo, armor)");
            console.print("  spawn <type>   - Spawn entity at crosshair");
            console.print("  kill           - Kill all enemies");
            console.print("  map <name>     - Load a map");
            console.print("  pos            - Print player position");
            console.print("  fps            - Toggle FPS display");
            console.print("  show_colliders - Toggle AABB wireframes");
            console.print("  show_triggers  - Toggle trigger volume display");
            console.print("  show_navgraph  - Toggle navigation graph");
            console.print("  show_ai        - Toggle AI sight lines");
            console.print("  timescale <f>  - Set time scale (0.1 = slow-mo)");
            console.print("  quit           - Exit the game");
        });

    // ─── god ────────────────────────────────────────────────────
    console.registerCommand("god", "Toggle invincibility",
        [&registry, &console](const std::vector<std::string>& args) {
            auto view = registry.view<TagPlayer, Health>();
            for (auto [entity, tag, health] : view.each()) {
                // Toggle by setting health absurdly high or restoring it
                if (health.max < 9000.0f) {
                    health.max = 99999.0f;
                    health.current = 99999.0f;
                    console.print("God mode ON");
                } else {
                    health.max = 100.0f;
                    health.current = 100.0f;
                    console.print("God mode OFF");
                }
            }
        });

    // ─── noclip ─────────────────────────────────────────────────
    console.registerCommand("noclip", "Toggle noclip mode",
        [&registry, &console](const std::vector<std::string>& args) {
            auto view = registry.view<TagPlayer>();
            for (auto [entity, tag] : view.each()) {
                if (registry.all_of<Gravity>(entity)) {
                    registry.remove<Gravity>(entity);
                    registry.remove<AABBCollider>(entity);
                    console.print("Noclip ON (fly mode)");
                } else {
                    registry.emplace<Gravity>(entity);
                    registry.emplace<AABBCollider>(entity, glm::vec3(0.4f, 0.9f, 0.4f));
                    console.print("Noclip OFF");
                }
            }
        });

    // ─── give ───────────────────────────────────────────────────
    console.registerCommand("give", "Give item: give health 50",
        [&registry, &console](const std::vector<std::string>& args) {
            if (args.size() < 2) {
                console.print("Usage: give <health|ammo|armor> <amount>");
                return;
            }

            float amount = std::stof(args[1]);
            auto view = registry.view<TagPlayer, Health>();
            for (auto [entity, tag, health] : view.each()) {
                if (args[0] == "health") {
                    health.current = std::min(health.current + amount, health.max);
                    console.print("Gave " + args[1] + " health");
                }
                // Add ammo, armor similarly
            }
        });

    // ─── kill ───────────────────────────────────────────────────
    console.registerCommand("kill", "Kill all enemies",
        [&registry, &console](const std::vector<std::string>& args) {
            int count = 0;
            auto view = registry.view<AIBrain, Health>();
            for (auto [entity, ai, health] : view.each()) {
                health.current = 0.0f;
                count++;
            }
            console.print("Killed " + std::to_string(count) + " enemies");
        });

    // ─── pos ────────────────────────────────────────────────────
    console.registerCommand("pos", "Print player position",
        [&registry, &console](const std::vector<std::string>& args) {
            auto view = registry.view<TagPlayer, Position>();
            for (auto [entity, tag, pos] : view.each()) {
                console.print("Position: " +
                    std::to_string(pos.value.x) + ", " +
                    std::to_string(pos.value.y) + ", " +
                    std::to_string(pos.value.z));
            }
        });

    // ─── fps ────────────────────────────────────────────────────
    console.registerCommand("fps", "Toggle FPS display",
        [&debug, &console](const std::vector<std::string>& args) {
            debug.showFPS = !debug.showFPS;
            console.print(debug.showFPS ? "FPS display ON" : "FPS display OFF");
        });

    // ─── show_colliders ─────────────────────────────────────────
    console.registerCommand("show_colliders", "Toggle AABB wireframes",
        [&debug, &console](const std::vector<std::string>& args) {
            debug.showColliders = !debug.showColliders;
            console.print(debug.showColliders ? "Colliders ON" : "Colliders OFF");
        });

    // ─── show_triggers ──────────────────────────────────────────
    console.registerCommand("show_triggers", "Toggle trigger volumes",
        [&debug, &console](const std::vector<std::string>& args) {
            debug.showTriggers = !debug.showTriggers;
            console.print(debug.showTriggers ? "Triggers ON" : "Triggers OFF");
        });

    // ─── show_navgraph ──────────────────────────────────────────
    console.registerCommand("show_navgraph", "Toggle navigation graph",
        [&debug, &console](const std::vector<std::string>& args) {
            debug.showNavGraph = !debug.showNavGraph;
            console.print(debug.showNavGraph ? "Nav graph ON" : "Nav graph OFF");
        });

    // ─── timescale ──────────────────────────────────────────────
    console.registerCommand("timescale", "Set time scale: timescale 0.5",
        [&debug, &console](const std::vector<std::string>& args) {
            if (args.empty()) {
                console.print("Current timescale: " + std::to_string(debug.timeScale));
                return;
            }
            debug.timeScale = std::clamp(std::stof(args[0]), 0.01f, 10.0f);
            console.print("Timescale set to " + args[0]);
        });

    // ─── quit ───────────────────────────────────────────────────
    console.registerCommand("quit", "Exit the game",
        [](const std::vector<std::string>& args) {
            exit(0);
        });
}
```

---

## Debug Renderer

A utility for drawing wireframe shapes — colliders, triggers, lines, and points. Uses `GL_LINES` with a simple colour shader.

### src/engine/debug/debug_renderer.h

```cpp
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <entt/entt.hpp>

class Shader;
class Camera;

struct DebugRenderer {
    // Toggle flags (set by console commands)
    bool showColliders = false;
    bool showTriggers = false;
    bool showNavGraph = false;
    bool showAI = false;
    bool showFPS = false;
    float timeScale = 1.0f;

    // FPS tracking
    float fpsTimer = 0.0f;
    int frameCount = 0;
    float currentFPS = 0.0f;

    // Draw all enabled debug visuals
    void render(entt::registry& registry, const Camera& camera,
                 Shader& debugShader, float dt);

private:
    // Vertex data for line drawing
    struct DebugVertex {
        glm::vec3 position;
        glm::vec3 colour;
    };

    std::vector<DebugVertex> m_lines;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    bool m_initialised = false;

    void init();
    void drawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& colour);
    void drawAABB(const glm::vec3& center, const glm::vec3& halfExtents,
                   const glm::vec3& colour);
    void flush(const Camera& camera, Shader& shader);
};
```

### src/engine/debug/debug_renderer.cpp

```cpp
#include "engine/debug/debug_renderer.h"
#include "engine/ecs/components.h"
#include "engine/renderer/camera.h"
#include "engine/renderer/shader.h"
#include <glad/glad.h>

void DebugRenderer::init() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                           sizeof(DebugVertex), (void*)0);
    glEnableVertexAttribArray(0);

    // Colour attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                           sizeof(DebugVertex),
                           (void*)offsetof(DebugVertex, colour));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_initialised = true;
}

void DebugRenderer::drawLine(const glm::vec3& a, const glm::vec3& b,
                               const glm::vec3& colour) {
    m_lines.push_back({ a, colour });
    m_lines.push_back({ b, colour });
}

void DebugRenderer::drawAABB(const glm::vec3& center, const glm::vec3& half,
                               const glm::vec3& colour) {
    glm::vec3 mn = center - half;
    glm::vec3 mx = center + half;

    // 12 edges of a box
    // Bottom face
    drawLine({mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, colour);
    drawLine({mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, colour);
    drawLine({mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, colour);
    drawLine({mn.x, mn.y, mx.z}, {mn.x, mn.y, mn.z}, colour);
    // Top face
    drawLine({mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, colour);
    drawLine({mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, colour);
    drawLine({mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, colour);
    drawLine({mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, colour);
    // Vertical edges
    drawLine({mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, colour);
    drawLine({mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, colour);
    drawLine({mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, colour);
    drawLine({mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, colour);
}

void DebugRenderer::flush(const Camera& camera, Shader& shader) {
    if (m_lines.empty()) return;
    if (!m_initialised) init();

    shader.use();
    shader.setMat4("view", camera.getViewMatrix());
    shader.setMat4("projection", camera.getProjectionMatrix());

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                  m_lines.size() * sizeof(DebugVertex),
                  m_lines.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_LINES, 0, static_cast<int>(m_lines.size()));
    glBindVertexArray(0);

    m_lines.clear();
}

void DebugRenderer::render(entt::registry& registry, const Camera& camera,
                             Shader& debugShader, float dt) {

    // ─── FPS counter ────────────────────────────────────────────
    frameCount++;
    fpsTimer += dt;
    if (fpsTimer >= 0.5f) {
        currentFPS = frameCount / fpsTimer;
        frameCount = 0;
        fpsTimer = 0.0f;
    }

    // ─── Collider wireframes ────────────────────────────────────
    if (showColliders) {
        auto view = registry.view<Position, AABBCollider>();
        for (auto [entity, pos, col] : view.each()) {
            glm::vec3 colour(0.0f, 1.0f, 0.0f);  // Green

            // Red for entities with low health
            if (registry.all_of<Health>(entity)) {
                float hp = registry.get<Health>(entity).current;
                float maxHp = registry.get<Health>(entity).max;
                if (hp / maxHp < 0.3f) colour = glm::vec3(1.0f, 0.0f, 0.0f);
            }

            drawAABB(pos.value, col.halfExtents, colour);
        }
    }

    // ─── Trigger volumes ────────────────────────────────────────
    if (showTriggers) {
        auto view = registry.view<Position, TriggerVolume>();
        for (auto [entity, pos, trigger] : view.each()) {
            drawAABB(pos.value, trigger.halfExtents,
                      glm::vec3(1.0f, 1.0f, 0.0f));  // Yellow
        }
    }

    // ─── AI sight lines ─────────────────────────────────────────
    if (showAI) {
        // Find player position
        glm::vec3 playerPos(0.0f);
        auto players = registry.view<TagPlayer, Position>();
        for (auto [e, tag, ppos] : players.each()) {
            playerPos = ppos.value;
            break;
        }

        auto view = registry.view<AIBrain, Position>();
        for (auto [entity, ai, pos] : view.each()) {
            glm::vec3 colour;
            switch (ai.state) {
                case AIState::Idle:    colour = glm::vec3(0.5f, 0.5f, 0.5f); break;
                case AIState::Chase:   colour = glm::vec3(1.0f, 0.5f, 0.0f); break;
                case AIState::Attack:  colour = glm::vec3(1.0f, 0.0f, 0.0f); break;
                default:               colour = glm::vec3(0.0f, 0.5f, 1.0f); break;
            }
            drawLine(pos.value, playerPos, colour);
        }
    }

    // ─── Flush all debug lines ──────────────────────────────────
    glDisable(GL_DEPTH_TEST);  // Draw on top of everything
    flush(camera, debugShader);
    glEnable(GL_DEPTH_TEST);

    // ─── FPS text (rendered separately via HUD) ─────────────────
    if (showFPS) {
        // Render FPS using BitmapFont from Chapter 15
        // font.renderText("FPS: " + std::to_string((int)currentFPS),
        //                  10, 10, 1.0f, glm::vec4(1, 1, 0, 1));
    }
}
```

### Debug Shader

A minimal shader that just passes through position and colour:

```glsl
// assets/shaders/debug.vert
#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 view;
uniform mat4 projection;

out vec3 fragColor;

void main() {
    gl_Position = projection * view * vec4(aPos, 1.0);
    fragColor = aColor;
}
```

```glsl
// assets/shaders/debug.frag
#version 460 core
in vec3 fragColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(fragColor, 1.0);
}
```

---

## Console Rendering

The console is drawn as a HUD overlay — a semi-transparent background with text:

```cpp
void renderConsole(const Console& console, BitmapFont& font,
                    int screenWidth, int screenHeight) {
    if (!console.isOpen()) return;

    // ─── Background (half-screen dark overlay) ──────────────────
    float consoleHeight = screenHeight * 0.4f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // renderQuad(0, 0, screenWidth, consoleHeight, glm::vec4(0, 0, 0, 0.85f));

    // ─── Output text (scrolls up from bottom of console) ────────
    float lineHeight = 18.0f;
    float y = consoleHeight - 30.0f;  // Start just above input line
    const auto& output = console.getOutput();

    int maxVisible = static_cast<int>((consoleHeight - 40.0f) / lineHeight);
    int start = std::max(0, static_cast<int>(output.size()) - maxVisible);

    for (int i = start; i < static_cast<int>(output.size()); i++) {
        // font.renderText(output[i], 10, y, 1.0f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
        y -= lineHeight;
    }

    // ─── Input line ─────────────────────────────────────────────
    float inputY = consoleHeight - 10.0f;
    std::string inputLine = "] " + console.getInput();
    // font.renderText(inputLine, 10, inputY, 1.0f, glm::vec4(1, 1, 1, 1));

    // Blinking cursor
    float blinkRate = 2.0f;
    if (fmod(glfwGetTime() * blinkRate, 1.0f) < 0.5f) {
        // Draw cursor at the right position
        // float cursorX = 10 + font.measureText("] " + input.substr(0, cursor));
        // renderQuad(cursorX, inputY - 2, 2, lineHeight, white);
    }

    glDisable(GL_BLEND);
}
```

```
Console appearance:
┌──────────────────────────────────────────────┐
│ QEngine v0.1.0                               │
│ Type 'help' for a list of commands           │
│                                              │
│ > god                                        │
│ God mode ON                                  │
│ > show_colliders                             │
│ Colliders ON                                 │
│ > give health 50                             │
│ Gave 50 health                               │
│ > pos                                        │
│ Position: 3.241, 1.000, -5.872               │
│ ] noclip_                                    │
├──────────────────────────────────────────────┤
│           (game world visible below)          │
```

---

## Time Scale

The `timescale` command enables slow-motion and fast-forward:

```cpp
// In the game loop, multiply dt by the timescale:
float dt = rawDeltaTime * debugRenderer.timeScale;

stateManager.update(dt);
```

`timescale 0.1` = 10x slow motion. `timescale 2.0` = double speed. Essential for debugging fast-moving projectiles or animation timing.

---

## C++ Concept: `std::function` and Lambdas

```cpp
using CommandFn = std::function<void(const std::vector<std::string>& args)>;

console.registerCommand("god", "Toggle invincibility",
    [&registry, &console](const std::vector<std::string>& args) {
        // This lambda captures registry and console by reference
        // It can be stored, copied, and called later
    });
```

`std::function` is a type-erased function wrapper — it can hold any callable: lambdas, function pointers, functors. The `[&registry, &console]` is the **capture list** — it tells the lambda which variables from the surrounding scope it can access.

- `[&]` — capture everything by reference
- `[=]` — capture everything by value (copy)
- `[&registry, &console]` — capture specific variables by reference
- `[registry]` — capture by value (copy)

Capturing by reference is lighter but the captured variables must outlive the lambda. Since our Console and registry live for the entire program, reference captures are safe here.

---

## What's Next

You've now completed the QEngine nice-to-haves tutorial series. With Chapters 0-27 combined, you have:

- A complete ECS game engine with OpenGL rendering
- FPS gameplay: weapons, enemies, items, physics
- Audio, networking, particles, and polish
- Game states, menus, save/load
- Skybox, view model animations, boss fights
- Developer tools for debugging everything

From here, the roadmaps for **TrenchBroom integration**, **multiplayer infrastructure**, and **top-down shooter adaptation** show how to extend QEngine in different directions — all building on the same ECS foundation.
