# Chapter 48: Level Editor

## What You'll Learn
- Why an in-engine level editor is worth building instead of relying on external tools
- Adding an EditorState to the game state machine that freezes gameplay systems
- Implementing a free-fly camera decoupled from the player entity
- Mouse ray casting from screen space to world space (unprojection)
- Entity placement with snap-to-grid and a template palette
- Transform gizmos for translate, rotate, and scale (coloured axis handles)
- A property panel that auto-generates ImGui widgets from ECS components
- Extending the save/load system for full level serialisation
- Undo/redo with the command pattern
- C++ concept: the Command Pattern (undo/redo, macro recording, network replays)

---

## Where We Left Off

Chapter 47 gave us ImGui integrated into QEngine. We can inspect entities, view component data, and interact with debug windows while the game is running. But all of that is read-only observation. If we want to move an entity, we have to open the JSON level file, change three numbers, reload, and see if it looks right. Want to add a new enemy? Edit JSON, reload, check, repeat. That cycle is slow and error-prone.

What we need is the ability to directly manipulate the world inside the engine. Click on an entity, drag it to a new position, tweak its properties, save the level. That is a level editor.

```
CURRENT WORKFLOW (external editing)
────────────────────────────────────────────────────────────────────
  Designer: *opens level_01.json in text editor*
  Designer: *changes enemy position from [10, 0, 5] to [12, 0, 8]*
  Designer: *saves file, switches to engine, reloads level*
  Designer: "That's too far right. Let me try [11, 0, 7]..."
  (repeat 20 times per entity)

TARGET WORKFLOW (in-engine editor)
────────────────────────────────────────────────────────────────────
  Designer: *presses F5 to enter editor mode*
  Designer: *clicks on enemy, drags it with the translate gizmo*
  Designer: *sees the result immediately, adjusts further*
  Designer: *presses Ctrl+S to save, F5 to play-test*
  (one smooth loop, no context switching)
```

This chapter builds the editor. It is not a full Unity-class tool. It is a practical, functional level editor that lets you place entities, move them, edit their properties, and save results. Good enough to build real levels.

---

## Architecture Overview

The editor lives inside the existing engine. It is a new game state that replaces the normal gameplay loop with an editing loop. The same renderer draws the same world — but systems like physics, AI, and damage do not tick.

```
EDITOR ARCHITECTURE
──────────────────────────────────────────────────────────────────────

  ┌─────────────────────────────────────────────────────────────────┐
  │                       GameStateManager                          │
  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
  │  │ PlayingState  │  │  PauseState  │  │    EditorState       │  │
  │  │ (all systems  │  │ (overlay)    │  │ (free camera, gizmos │  │
  │  │  tick)        │  │              │  │  ImGui panels, no    │  │
  │  │              │  │              │  │  gameplay systems)    │  │
  │  └──────────────┘  └──────────────┘  └──────────────────────┘  │
  └─────────────────────────────────────────────────────────────────┘

  EditorState owns:
    ├── EditorCamera        (free-fly, not attached to player)
    ├── SelectionManager    (tracks which entity is selected)
    ├── GizmoRenderer       (translate/rotate/scale handles)
    ├── PropertyPanel       (ImGui component editor)
    ├── EntityPalette       (template browser for placing new entities)
    ├── LevelSerializer     (save/load the full level)
    └── UndoStack           (command history for undo/redo)
```

---

## Editor State

The EditorState is a GameState subclass. When active, it runs the renderer and ImGui but skips all gameplay systems. The world is frozen — entities stay where they are, AI does not think, projectiles do not fly.

### src/engine/editor/editor_state.h

```cpp
#pragma once

#include "engine/core/game_state.h"
#include "engine/editor/editor_camera.h"
#include "engine/editor/selection_manager.h"
#include "engine/editor/gizmo_renderer.h"
#include "engine/editor/property_panel.h"
#include "engine/editor/entity_palette.h"
#include "engine/editor/undo_stack.h"
#include "engine/editor/level_serializer.h"
#include <entt/entt.hpp>

class RenderPipeline;
class InputManager;
class Console;
class Window;

class EditorState : public GameState {
public:
    EditorState(entt::registry& registry,
                RenderPipeline& renderer,
                InputManager& input,
                Console& console,
                Window& window);

    void enter() override;
    void exit() override;
    void update(float dt) override;
    void render() override;

    std::string getName() const override { return "EditorState"; }
    bool isTransparent() const override { return false; }

private:
    void handleInput(float dt);
    void handleMousePick();
    void handleGizmoInteraction(float dt);
    void drawMenuBar();
    void drawToolbar();

    entt::registry& m_registry;
    RenderPipeline& m_renderer;
    InputManager&   m_input;
    Console&        m_console;
    Window&         m_window;

    EditorCamera      m_camera;
    SelectionManager  m_selection;
    GizmoRenderer     m_gizmo;
    PropertyPanel     m_propertyPanel;
    EntityPalette     m_palette;
    UndoStack         m_undoStack;
    LevelSerializer   m_serializer;

    // Current gizmo mode
    enum class GizmoMode { Translate, Rotate, Scale };
    GizmoMode m_gizmoMode = GizmoMode::Translate;

    // Grid snapping
    bool  m_snapEnabled = true;
    float m_snapSize    = 1.0f;

    // Current level file path
    std::string m_currentLevelPath;
    bool        m_unsavedChanges = false;
};
```

### src/engine/editor/editor_state.cpp

```cpp
#include "engine/editor/editor_state.h"
#include "engine/core/game_state_manager.h"
#include "engine/rendering/render_pipeline.h"
#include "engine/input/input_manager.h"
#include "engine/debug/console.h"
#include "engine/core/window.h"
#include <imgui.h>
#include <iostream>

EditorState::EditorState(entt::registry& registry,
                         RenderPipeline& renderer,
                         InputManager& input,
                         Console& console,
                         Window& window)
    : m_registry(registry)
    , m_renderer(renderer)
    , m_input(input)
    , m_console(console)
    , m_window(window)
    , m_camera()
    , m_selection(registry)
    , m_gizmo()
    , m_propertyPanel(registry)
    , m_palette(registry)
    , m_serializer(registry)
{
}

void EditorState::enter() {
    std::cout << "[Editor] Entering editor mode" << std::endl;

    // Unlock the cursor so the user can interact with ImGui and gizmos
    m_window.setCursorMode(CursorMode::Normal);

    // Position the editor camera at the player's location (if one exists)
    auto playerView = m_registry.view<TagPlayer, Position>();
    for (auto entity : playerView) {
        auto& pos = playerView.get<Position>(entity);
        m_camera.setPosition(pos.value + glm::vec3(0, 5, 10));
        m_camera.lookAt(pos.value);
        break;
    }

    m_console.print("Editor mode active. F5 = play, Ctrl+S = save.");
}

void EditorState::exit() {
    std::cout << "[Editor] Exiting editor mode" << std::endl;
}

void EditorState::update(float dt) {
    // No gameplay systems tick here — no physics, no AI, no damage.
    // We only update editor-specific things.

    handleInput(dt);
    m_camera.update(dt, m_input);
    m_propertyPanel.update();
}

void EditorState::render() {
    // Render the world normally through the render pipeline,
    // but using the editor camera instead of the player camera
    m_renderer.render(m_registry, m_camera.getViewMatrix(),
                      m_camera.getProjectionMatrix());

    // Draw gizmo overlay for the selected entity
    if (m_selection.hasSelection()) {
        entt::entity selected = m_selection.getSelected();
        if (m_registry.valid(selected) && m_registry.all_of<Position>(selected)) {
            auto& pos = m_registry.get<Position>(selected);
            auto* rot = m_registry.try_get<Rotation>(selected);
            auto* scl = m_registry.try_get<Scale>(selected);

            glm::vec3 euler = rot ? rot->euler : glm::vec3(0.0f);
            glm::vec3 scale = scl ? scl->value : glm::vec3(1.0f);

            m_gizmo.draw(m_camera, pos.value, euler, scale, m_gizmoMode);
        }
    }

    // Draw all ImGui editor panels
    drawMenuBar();
    drawToolbar();
    m_propertyPanel.draw(m_selection.getSelected());
    m_palette.draw();

    // Draw selection highlight (wireframe box around selected entity)
    if (m_selection.hasSelection()) {
        m_selection.drawHighlight(m_camera);
    }
}

void EditorState::handleInput(float dt) {
    // F5 — switch to play mode
    if (m_input.wasKeyPressed(GLFW_KEY_F5)) {
        m_stateManager->popState();  // return to PlayingState below
        return;
    }

    // Gizmo mode switches
    if (m_input.wasKeyPressed(GLFW_KEY_W)) m_gizmoMode = GizmoMode::Translate;
    if (m_input.wasKeyPressed(GLFW_KEY_E)) m_gizmoMode = GizmoMode::Rotate;
    if (m_input.wasKeyPressed(GLFW_KEY_R)) m_gizmoMode = GizmoMode::Scale;

    // Snap toggle
    if (m_input.wasKeyPressed(GLFW_KEY_G)) m_snapEnabled = !m_snapEnabled;

    // Undo/Redo
    if (m_input.isKeyDown(GLFW_KEY_LEFT_CONTROL)) {
        if (m_input.wasKeyPressed(GLFW_KEY_Z)) {
            m_undoStack.undo();
            m_unsavedChanges = true;
        }
        if (m_input.wasKeyPressed(GLFW_KEY_Y)) {
            m_undoStack.redo();
            m_unsavedChanges = true;
        }
        // Save
        if (m_input.wasKeyPressed(GLFW_KEY_S)) {
            if (!m_currentLevelPath.empty()) {
                m_serializer.saveLevel(m_currentLevelPath);
                m_unsavedChanges = false;
                m_console.print("Level saved: " + m_currentLevelPath);
            } else {
                m_console.print("No file path set. Use File > Save As.");
            }
        }
    }

    // Delete selected entity
    if (m_input.wasKeyPressed(GLFW_KEY_DELETE)) {
        if (m_selection.hasSelection()) {
            entt::entity selected = m_selection.getSelected();
            if (m_registry.valid(selected)) {
                // Record for undo
                m_undoStack.execute(std::make_unique<DeleteEntityCommand>(
                    m_registry, selected, m_serializer));
                m_selection.clearSelection();
                m_unsavedChanges = true;
            }
        }
    }

    // Mouse pick — left click when not dragging a gizmo
    if (m_input.wasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) &&
        !ImGui::GetIO().WantCaptureMouse &&
        !m_gizmo.isInteracting())
    {
        handleMousePick();
    }

    // Gizmo drag
    if (m_gizmo.isInteracting()) {
        handleGizmoInteraction(dt);
    }
}

void EditorState::handleMousePick() {
    // Get mouse position in screen coordinates
    double mx, my;
    m_input.getMousePosition(mx, my);

    // Cast a ray from the camera through the mouse position
    glm::vec3 rayOrigin, rayDir;
    m_camera.screenToWorldRay(
        static_cast<float>(mx), static_cast<float>(my),
        m_window.getWidth(), m_window.getHeight(),
        rayOrigin, rayDir);

    // Test against all selectable entities
    entt::entity hit = m_selection.pickEntity(rayOrigin, rayDir);

    if (hit != entt::null) {
        m_selection.select(hit);
    } else {
        m_selection.clearSelection();
    }
}

void EditorState::handleGizmoInteraction(float dt) {
    if (!m_selection.hasSelection()) return;

    entt::entity selected = m_selection.getSelected();
    if (!m_registry.valid(selected)) return;

    glm::vec3 delta = m_gizmo.getDragDelta(m_camera, m_input,
                                             m_window.getWidth(),
                                             m_window.getHeight());

    if (m_snapEnabled && m_gizmoMode == GizmoMode::Translate) {
        delta.x = std::round(delta.x / m_snapSize) * m_snapSize;
        delta.y = std::round(delta.y / m_snapSize) * m_snapSize;
        delta.z = std::round(delta.z / m_snapSize) * m_snapSize;
    }

    // The gizmo records a TransformCommand when the drag starts,
    // and updates it continuously during the drag. The command is
    // committed to the undo stack when the mouse button is released.
}

void EditorState::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Level", "Ctrl+N")) {
                m_serializer.newLevel();
                m_selection.clearSelection();
                m_undoStack.clear();
                m_currentLevelPath.clear();
                m_unsavedChanges = false;
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                // In a real implementation, this opens a file dialog.
                // For now, we use a hardcoded path or console command.
                m_console.print("Use console: editor_load <path>");
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (!m_currentLevelPath.empty()) {
                    m_serializer.saveLevel(m_currentLevelPath);
                    m_unsavedChanges = false;
                }
            }
            if (ImGui::MenuItem("Save As...")) {
                m_console.print("Use console: editor_saveas <path>");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit Editor", "F5")) {
                m_stateManager->popState();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_undoStack.canUndo())) {
                m_undoStack.undo();
                m_unsavedChanges = true;
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_undoStack.canRedo())) {
                m_undoStack.redo();
                m_unsavedChanges = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del", false, m_selection.hasSelection())) {
                if (m_selection.hasSelection()) {
                    entt::entity selected = m_selection.getSelected();
                    m_undoStack.execute(std::make_unique<DeleteEntityCommand>(
                        m_registry, selected, m_serializer));
                    m_selection.clearSelection();
                    m_unsavedChanges = true;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Property Panel", nullptr, &m_propertyPanel.visible);
            ImGui::MenuItem("Entity Palette", nullptr, &m_palette.visible);
            ImGui::Separator();
            ImGui::MenuItem("Snap to Grid", "G", &m_snapEnabled);
            ImGui::SliderFloat("Snap Size", &m_snapSize, 0.25f, 4.0f);
            ImGui::EndMenu();
        }

        // Show unsaved indicator
        if (m_unsavedChanges) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "* Unsaved Changes");
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorState::drawToolbar() {
    ImGui::SetNextWindowPos(ImVec2(0, 20));
    ImGui::SetNextWindowSize(ImVec2(200, 40));
    ImGui::Begin("##Toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoScrollbar);

    bool isTranslate = (m_gizmoMode == GizmoMode::Translate);
    bool isRotate    = (m_gizmoMode == GizmoMode::Rotate);
    bool isScale     = (m_gizmoMode == GizmoMode::Scale);

    if (isTranslate) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    if (ImGui::Button("W Move")) m_gizmoMode = GizmoMode::Translate;
    if (isTranslate) ImGui::PopStyleColor();

    ImGui::SameLine();

    if (isRotate) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    if (ImGui::Button("E Rot")) m_gizmoMode = GizmoMode::Rotate;
    if (isRotate) ImGui::PopStyleColor();

    ImGui::SameLine();

    if (isScale) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    if (ImGui::Button("R Scl")) m_gizmoMode = GizmoMode::Scale;
    if (isScale) ImGui::PopStyleColor();

    ImGui::End();
}
```

### Entering the Editor from Gameplay

The PlayingState needs a key binding to push the EditorState. Add this to PlayingState's update:

```cpp
// In PlayingState::update(float dt)
if (m_input.wasKeyPressed(GLFW_KEY_F5)) {
    m_stateManager->pushState(std::make_unique<EditorState>(
        m_registry, m_renderer, m_input, m_console, m_window));
}
```

Because the EditorState is pushed on top of PlayingState, popping it (F5 inside the editor) returns cleanly to gameplay. The state stack handles this automatically — PlayingState's `resume()` re-locks the cursor and resumes systems.

---

## Editor Camera

The editor camera is a free-fly camera. Right-click and drag to look around. WASD to move. Mouse wheel to change speed. It is not attached to any entity — it is purely a tool for navigating the scene.

### src/engine/editor/editor_camera.h

```cpp
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class InputManager;

class EditorCamera {
public:
    EditorCamera();

    void update(float dt, InputManager& input);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;

    void setPosition(const glm::vec3& pos) { m_position = pos; }
    void lookAt(const glm::vec3& target);

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;

    // Unproject a screen-space point to a world-space ray
    void screenToWorldRay(float mouseX, float mouseY,
                          int screenWidth, int screenHeight,
                          glm::vec3& outOrigin, glm::vec3& outDirection) const;

    float fov  = 60.0f;
    float near = 0.1f;
    float far  = 500.0f;
    float aspectRatio = 16.0f / 9.0f;

private:
    glm::vec3 m_position = {0, 5, 10};
    float m_yaw   = -90.0f;  // degrees
    float m_pitch = -20.0f;  // degrees

    float m_moveSpeed     = 10.0f;
    float m_lookSensitivity = 0.15f;
    float m_speedMultiplier = 1.0f;

    // Track last mouse position for delta calculation
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool   m_rightMouseDown = false;
};
```

### src/engine/editor/editor_camera.cpp

```cpp
#include "engine/editor/editor_camera.h"
#include "engine/input/input_manager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

EditorCamera::EditorCamera() {}

void EditorCamera::update(float dt, InputManager& input) {
    // --- Look (right-click drag) ---
    bool rmb = input.isMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
    double mx, my;
    input.getMousePosition(mx, my);

    if (rmb) {
        if (m_rightMouseDown) {
            // Calculate mouse delta
            float dx = static_cast<float>(mx - m_lastMouseX) * m_lookSensitivity;
            float dy = static_cast<float>(m_lastMouseY - my) * m_lookSensitivity;

            m_yaw   += dx;
            m_pitch += dy;
            m_pitch  = std::clamp(m_pitch, -89.0f, 89.0f);
        }
        m_rightMouseDown = true;
    } else {
        m_rightMouseDown = false;
    }

    m_lastMouseX = mx;
    m_lastMouseY = my;

    // --- Speed adjustment (scroll wheel) ---
    float scroll = input.getScrollDelta();
    if (scroll != 0.0f) {
        m_speedMultiplier *= (scroll > 0) ? 1.2f : 0.8f;
        m_speedMultiplier = std::clamp(m_speedMultiplier, 0.1f, 50.0f);
    }

    // --- Movement (WASD + QE for up/down, only while right-click is held) ---
    if (rmb) {
        float speed = m_moveSpeed * m_speedMultiplier * dt;
        glm::vec3 forward = getForward();
        glm::vec3 right   = getRight();
        glm::vec3 up      = {0, 1, 0};

        if (input.isKeyDown(GLFW_KEY_W)) m_position += forward * speed;
        if (input.isKeyDown(GLFW_KEY_S)) m_position -= forward * speed;
        if (input.isKeyDown(GLFW_KEY_A)) m_position -= right * speed;
        if (input.isKeyDown(GLFW_KEY_D)) m_position += right * speed;
        if (input.isKeyDown(GLFW_KEY_Q)) m_position -= up * speed;
        if (input.isKeyDown(GLFW_KEY_E)) m_position += up * speed;

        // Shift for faster movement
        if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT)) {
            m_position += forward * speed; // double speed when shift held
        }
    }
}

void EditorCamera::lookAt(const glm::vec3& target) {
    glm::vec3 dir = glm::normalize(target - m_position);
    m_yaw   = glm::degrees(std::atan2(dir.z, dir.x));
    m_pitch = glm::degrees(std::asin(dir.y));
}

glm::vec3 EditorCamera::getForward() const {
    float yawRad   = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);
    return glm::normalize(glm::vec3(
        std::cos(yawRad) * std::cos(pitchRad),
        std::sin(pitchRad),
        std::sin(yawRad) * std::cos(pitchRad)
    ));
}

glm::vec3 EditorCamera::getRight() const {
    return glm::normalize(glm::cross(getForward(), glm::vec3(0, 1, 0)));
}

glm::vec3 EditorCamera::getUp() const {
    return glm::normalize(glm::cross(getRight(), getForward()));
}

glm::mat4 EditorCamera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + getForward(), glm::vec3(0, 1, 0));
}

glm::mat4 EditorCamera::getProjectionMatrix() const {
    return glm::perspective(glm::radians(fov), aspectRatio, near, far);
}

void EditorCamera::screenToWorldRay(float mouseX, float mouseY,
                                     int screenWidth, int screenHeight,
                                     glm::vec3& outOrigin,
                                     glm::vec3& outDirection) const
{
    // Convert mouse position to normalised device coordinates [-1, 1]
    float ndcX = (2.0f * mouseX) / screenWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY) / screenHeight;  // flip Y

    // Clip-space point on the near plane
    glm::vec4 clipNear = {ndcX, ndcY, -1.0f, 1.0f};
    glm::vec4 clipFar  = {ndcX, ndcY,  1.0f, 1.0f};

    // Unproject to world space
    glm::mat4 invVP = glm::inverse(getProjectionMatrix() * getViewMatrix());

    glm::vec4 worldNear = invVP * clipNear;
    glm::vec4 worldFar  = invVP * clipFar;
    worldNear /= worldNear.w;
    worldFar  /= worldFar.w;

    outOrigin    = glm::vec3(worldNear);
    outDirection = glm::normalize(glm::vec3(worldFar - worldNear));
}
```

### Why Unproject Instead of Forward Ray?

The player camera in gameplay always shoots from the screen centre. A simple approach is `ray = camera.forward()`. But in the editor, we click anywhere on screen. A click in the top-left corner needs a ray that goes through that exact pixel, not through the centre. Unprojection gives us the correct ray for any pixel.

```
SCREEN-TO-WORLD RAY (side view)
──────────────────────────────────────────────────────────────────

  Screen                              World
  ┌────────────┐
  │    * click  │                      * hit point
  │      \      │                     /
  │       \     │     unproject      /
  │        \    │    ──────────►    /
  │         \   │                 / ray
  │     camera  │                * camera position
  └────────────┘

  NDC coordinates (-1 to +1) × inverse(projection × view) = world point
  Two points (near plane and far plane) define the ray direction.
```

---

## Entity Selection and Ray Picking

When the user clicks in the viewport, we need to figure out which entity (if any) they clicked on. This means testing our mouse ray against every selectable entity's bounding volume.

### src/engine/editor/selection_manager.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

class EditorCamera;

class SelectionManager {
public:
    explicit SelectionManager(entt::registry& registry);

    // Pick the nearest entity along the ray
    entt::entity pickEntity(const glm::vec3& rayOrigin,
                            const glm::vec3& rayDir);

    void select(entt::entity entity);
    void clearSelection();

    bool hasSelection() const { return m_selected != entt::null; }
    entt::entity getSelected() const { return m_selected; }

    // Draw a wireframe highlight around the selected entity
    void drawHighlight(const EditorCamera& camera);

private:
    // Ray-AABB intersection test, returns distance or -1 on miss
    float rayAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                  const glm::vec3& boxMin, const glm::vec3& boxMax) const;

    entt::registry& m_registry;
    entt::entity    m_selected = entt::null;
};
```

### src/engine/editor/selection_manager.cpp

```cpp
#include "engine/editor/selection_manager.h"
#include "engine/editor/editor_camera.h"
#include "engine/ecs/components.h"
#include <limits>
#include <algorithm>

SelectionManager::SelectionManager(entt::registry& registry)
    : m_registry(registry)
{
}

entt::entity SelectionManager::pickEntity(const glm::vec3& rayOrigin,
                                           const glm::vec3& rayDir)
{
    entt::entity closest = entt::null;
    float closestDist = std::numeric_limits<float>::max();

    // Test against all entities that have a Position and an AABB
    auto view = m_registry.view<Position>();
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);

        // Use the entity's collision AABB if available, otherwise a default box
        glm::vec3 halfExtent = {0.5f, 0.5f, 0.5f};
        if (auto* aabb = m_registry.try_get<AABB>(entity)) {
            halfExtent = aabb->halfExtents;
        }

        glm::vec3 boxMin = pos.value - halfExtent;
        glm::vec3 boxMax = pos.value + halfExtent;

        float dist = rayAABB(rayOrigin, rayDir, boxMin, boxMax);
        if (dist >= 0.0f && dist < closestDist) {
            closestDist = dist;
            closest = entity;
        }
    }

    return closest;
}

void SelectionManager::select(entt::entity entity) {
    m_selected = entity;
}

void SelectionManager::clearSelection() {
    m_selected = entt::null;
}

float SelectionManager::rayAABB(const glm::vec3& rayOrigin,
                                 const glm::vec3& rayDir,
                                 const glm::vec3& boxMin,
                                 const glm::vec3& boxMax) const
{
    // Slab method — test the ray against each pair of axis-aligned planes
    //
    //     boxMin.x          boxMax.x
    //         |                 |
    //   ------+--------*-------+------  ray
    //         |   tMin  \      |
    //         |          \     |
    //         |     box   \    |
    //         |            tMax|
    //
    // If the intervals overlap on all three axes, the ray hits the box.

    glm::vec3 invDir = 1.0f / rayDir;

    glm::vec3 t1 = (boxMin - rayOrigin) * invDir;
    glm::vec3 t2 = (boxMax - rayOrigin) * invDir;

    glm::vec3 tmin = glm::min(t1, t2);
    glm::vec3 tmax = glm::max(t1, t2);

    float tNear = std::max({tmin.x, tmin.y, tmin.z});
    float tFar  = std::min({tmax.x, tmax.y, tmax.z});

    if (tNear > tFar || tFar < 0.0f) return -1.0f;

    return tNear >= 0.0f ? tNear : tFar;
}

void SelectionManager::drawHighlight(const EditorCamera& camera) {
    if (m_selected == entt::null || !m_registry.valid(m_selected)) return;

    auto* pos = m_registry.try_get<Position>(m_selected);
    if (!pos) return;

    glm::vec3 halfExtent = {0.5f, 0.5f, 0.5f};
    if (auto* aabb = m_registry.try_get<AABB>(m_selected)) {
        halfExtent = aabb->halfExtents;
    }

    // Draw a wireframe box using debug line rendering
    // (Uses the debug draw system from Ch 27)
    glm::vec3 min = pos->value - halfExtent;
    glm::vec3 max = pos->value + halfExtent;
    glm::vec4 color = {1.0f, 0.8f, 0.0f, 1.0f};  // yellow highlight

    // 12 edges of a box
    // Bottom face
    debugDrawLine(min, {max.x, min.y, min.z}, color);
    debugDrawLine({max.x, min.y, min.z}, {max.x, min.y, max.z}, color);
    debugDrawLine({max.x, min.y, max.z}, {min.x, min.y, max.z}, color);
    debugDrawLine({min.x, min.y, max.z}, min, color);
    // Top face
    debugDrawLine({min.x, max.y, min.z}, {max.x, max.y, min.z}, color);
    debugDrawLine({max.x, max.y, min.z}, max, color);
    debugDrawLine(max, {min.x, max.y, max.z}, color);
    debugDrawLine({min.x, max.y, max.z}, {min.x, max.y, min.z}, color);
    // Vertical edges
    debugDrawLine(min, {min.x, max.y, min.z}, color);
    debugDrawLine({max.x, min.y, min.z}, {max.x, max.y, min.z}, color);
    debugDrawLine({max.x, min.y, max.z}, max, color);
    debugDrawLine({min.x, min.y, max.z}, {min.x, max.y, max.z}, color);
}
```

The slab method is the standard algorithm for ray-AABB intersection. It works by computing entry and exit distances along each axis and checking whether the intervals overlap. Fast, branchless, and correct.

---

## Transform Gizmos

A gizmo is a visual handle overlaid on the selected entity. Dragging an axis handle transforms the entity along that axis. This is the core interaction of any 3D editor.

```
TRANSLATE GIZMO                 ROTATE GIZMO              SCALE GIZMO

       Y (green)                     / \                    Y ■ (green)
       |                            / G \                    |
       |                           |  R  |                   |
       *------- X (red)             \ B /                    *------- X ■ (red)
      /                              \ /                    /
     /                                                     /
    Z (blue)                    R=red G=green B=blue      Z ■ (blue)

  Drag along axis            Drag around ring            Drag to scale
  to translate               to rotate                   along axis

  Colour convention: X = red, Y = green, Z = blue (RGB = XYZ)
```

### src/engine/editor/gizmo_renderer.h

```cpp
#pragma once

#include <glm/glm.hpp>

class EditorCamera;
class InputManager;

class GizmoRenderer {
public:
    enum class Mode { Translate, Rotate, Scale };
    enum class Axis { None, X, Y, Z };

    GizmoRenderer();

    // Draw the gizmo at the given transform
    void draw(const EditorCamera& camera,
              const glm::vec3& position,
              const glm::vec3& rotation,
              const glm::vec3& scale,
              int modeInt);  // 0=translate, 1=rotate, 2=scale

    // Begin interaction when mouse clicks on a gizmo axis
    bool tryBeginDrag(const EditorCamera& camera,
                      const InputManager& input,
                      const glm::vec3& entityPos,
                      int screenWidth, int screenHeight);

    // Get the drag delta in world space since last frame
    glm::vec3 getDragDelta(const EditorCamera& camera,
                           const InputManager& input,
                           int screenWidth, int screenHeight);

    // End interaction when mouse button is released
    void endDrag();

    bool isInteracting() const { return m_activeAxis != Axis::None; }
    Axis getActiveAxis() const { return m_activeAxis; }

private:
    void drawTranslateGizmo(const EditorCamera& camera, const glm::vec3& pos);
    void drawRotateGizmo(const EditorCamera& camera, const glm::vec3& pos);
    void drawScaleGizmo(const EditorCamera& camera, const glm::vec3& pos);

    // Test if the mouse ray is close enough to a gizmo axis line
    Axis pickAxis(const EditorCamera& camera,
                  const glm::vec3& entityPos,
                  float mouseX, float mouseY,
                  int screenWidth, int screenHeight) const;

    // Project a world-space axis direction onto the screen-space mouse delta
    glm::vec3 projectMouseToAxis(const EditorCamera& camera,
                                 const InputManager& input,
                                 const glm::vec3& axisDir,
                                 int screenWidth, int screenHeight) const;

    Axis      m_activeAxis = Axis::None;
    Mode      m_mode       = Mode::Translate;
    glm::vec3 m_dragStart  = {0, 0, 0};
    glm::vec3 m_lastDragPos = {0, 0, 0};
    float     m_gizmoSize   = 1.5f;  // visual size in world units
};
```

### src/engine/editor/gizmo_renderer.cpp

```cpp
#include "engine/editor/gizmo_renderer.h"
#include "engine/editor/editor_camera.h"
#include "engine/input/input_manager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

GizmoRenderer::GizmoRenderer() {}

void GizmoRenderer::draw(const EditorCamera& camera,
                          const glm::vec3& position,
                          const glm::vec3& rotation,
                          const glm::vec3& scale,
                          int modeInt)
{
    m_mode = static_cast<Mode>(modeInt);

    // Scale the gizmo size based on distance to camera so it stays
    // visually consistent regardless of zoom level
    float dist = glm::length(camera.getPosition() - position);
    float visualSize = m_gizmoSize * (dist * 0.1f);
    visualSize = std::max(visualSize, 0.5f);

    switch (m_mode) {
    case Mode::Translate: drawTranslateGizmo(camera, position); break;
    case Mode::Rotate:    drawRotateGizmo(camera, position);    break;
    case Mode::Scale:     drawScaleGizmo(camera, position);     break;
    }
}

void GizmoRenderer::drawTranslateGizmo(const EditorCamera& camera,
                                         const glm::vec3& pos)
{
    float dist = glm::length(camera.getPosition() - pos);
    float len = m_gizmoSize * (dist * 0.08f);

    glm::vec4 red   = {1, 0, 0, 1};
    glm::vec4 green = {0, 1, 0, 1};
    glm::vec4 blue  = {0, 0, 1, 1};

    // Highlight the active axis
    if (m_activeAxis == Axis::X) red   = {1, 1, 0, 1};
    if (m_activeAxis == Axis::Y) green = {1, 1, 0, 1};
    if (m_activeAxis == Axis::Z) blue  = {1, 1, 0, 1};

    // X axis — red line with arrowhead
    debugDrawLine(pos, pos + glm::vec3(len, 0, 0), red);
    debugDrawLine(pos + glm::vec3(len, 0, 0),
                  pos + glm::vec3(len * 0.85f, len * 0.08f, 0), red);
    debugDrawLine(pos + glm::vec3(len, 0, 0),
                  pos + glm::vec3(len * 0.85f, -len * 0.08f, 0), red);

    // Y axis — green
    debugDrawLine(pos, pos + glm::vec3(0, len, 0), green);
    debugDrawLine(pos + glm::vec3(0, len, 0),
                  pos + glm::vec3(len * 0.08f, len * 0.85f, 0), green);
    debugDrawLine(pos + glm::vec3(0, len, 0),
                  pos + glm::vec3(-len * 0.08f, len * 0.85f, 0), green);

    // Z axis — blue
    debugDrawLine(pos, pos + glm::vec3(0, 0, len), blue);
    debugDrawLine(pos + glm::vec3(0, 0, len),
                  pos + glm::vec3(0, len * 0.08f, len * 0.85f), blue);
    debugDrawLine(pos + glm::vec3(0, 0, len),
                  pos + glm::vec3(0, -len * 0.08f, len * 0.85f), blue);
}

void GizmoRenderer::drawRotateGizmo(const EditorCamera& camera,
                                      const glm::vec3& pos)
{
    float dist = glm::length(camera.getPosition() - pos);
    float radius = m_gizmoSize * (dist * 0.07f);

    glm::vec4 red   = {1, 0, 0, 1};
    glm::vec4 green = {0, 1, 0, 1};
    glm::vec4 blue  = {0, 0, 1, 1};

    if (m_activeAxis == Axis::X) red   = {1, 1, 0, 1};
    if (m_activeAxis == Axis::Y) green = {1, 1, 0, 1};
    if (m_activeAxis == Axis::Z) blue  = {1, 1, 0, 1};

    const int segments = 32;
    float step = glm::two_pi<float>() / segments;

    // X rotation ring (in the YZ plane)
    for (int i = 0; i < segments; ++i) {
        float a0 = i * step;
        float a1 = (i + 1) * step;
        glm::vec3 p0 = pos + glm::vec3(0, std::cos(a0), std::sin(a0)) * radius;
        glm::vec3 p1 = pos + glm::vec3(0, std::cos(a1), std::sin(a1)) * radius;
        debugDrawLine(p0, p1, red);
    }

    // Y rotation ring (in the XZ plane)
    for (int i = 0; i < segments; ++i) {
        float a0 = i * step;
        float a1 = (i + 1) * step;
        glm::vec3 p0 = pos + glm::vec3(std::cos(a0), 0, std::sin(a0)) * radius;
        glm::vec3 p1 = pos + glm::vec3(std::cos(a1), 0, std::sin(a1)) * radius;
        debugDrawLine(p0, p1, green);
    }

    // Z rotation ring (in the XY plane)
    for (int i = 0; i < segments; ++i) {
        float a0 = i * step;
        float a1 = (i + 1) * step;
        glm::vec3 p0 = pos + glm::vec3(std::cos(a0), std::sin(a0), 0) * radius;
        glm::vec3 p1 = pos + glm::vec3(std::cos(a1), std::sin(a1), 0) * radius;
        debugDrawLine(p0, p1, blue);
    }
}

void GizmoRenderer::drawScaleGizmo(const EditorCamera& camera,
                                     const glm::vec3& pos)
{
    float dist = glm::length(camera.getPosition() - pos);
    float len = m_gizmoSize * (dist * 0.08f);
    float boxSize = len * 0.08f;

    glm::vec4 red   = {1, 0, 0, 1};
    glm::vec4 green = {0, 1, 0, 1};
    glm::vec4 blue  = {0, 0, 1, 1};

    if (m_activeAxis == Axis::X) red   = {1, 1, 0, 1};
    if (m_activeAxis == Axis::Y) green = {1, 1, 0, 1};
    if (m_activeAxis == Axis::Z) blue  = {1, 1, 0, 1};

    // Lines with small cubes at the end (drawn as wireframe boxes)
    // X
    debugDrawLine(pos, pos + glm::vec3(len, 0, 0), red);
    glm::vec3 xEnd = pos + glm::vec3(len, 0, 0);
    debugDrawWireBox(xEnd - glm::vec3(boxSize), xEnd + glm::vec3(boxSize), red);

    // Y
    debugDrawLine(pos, pos + glm::vec3(0, len, 0), green);
    glm::vec3 yEnd = pos + glm::vec3(0, len, 0);
    debugDrawWireBox(yEnd - glm::vec3(boxSize), yEnd + glm::vec3(boxSize), green);

    // Z
    debugDrawLine(pos, pos + glm::vec3(0, 0, len), blue);
    glm::vec3 zEnd = pos + glm::vec3(0, 0, len);
    debugDrawWireBox(zEnd - glm::vec3(boxSize), zEnd + glm::vec3(boxSize), blue);
}

bool GizmoRenderer::tryBeginDrag(const EditorCamera& camera,
                                  const InputManager& input,
                                  const glm::vec3& entityPos,
                                  int screenWidth, int screenHeight)
{
    double mx, my;
    input.getMousePosition(mx, my);

    Axis picked = pickAxis(camera, entityPos,
                           static_cast<float>(mx), static_cast<float>(my),
                           screenWidth, screenHeight);

    if (picked != Axis::None) {
        m_activeAxis = picked;
        m_dragStart = entityPos;
        m_lastDragPos = entityPos;
        return true;
    }

    return false;
}

glm::vec3 GizmoRenderer::getDragDelta(const EditorCamera& camera,
                                       const InputManager& input,
                                       int screenWidth, int screenHeight)
{
    if (m_activeAxis == Axis::None) return {0, 0, 0};

    glm::vec3 axisDir = {0, 0, 0};
    switch (m_activeAxis) {
    case Axis::X: axisDir = {1, 0, 0}; break;
    case Axis::Y: axisDir = {0, 1, 0}; break;
    case Axis::Z: axisDir = {0, 0, 1}; break;
    default: break;
    }

    glm::vec3 projected = projectMouseToAxis(camera, input, axisDir,
                                              screenWidth, screenHeight);
    glm::vec3 delta = projected - m_lastDragPos;
    m_lastDragPos = projected;
    return delta;
}

void GizmoRenderer::endDrag() {
    m_activeAxis = Axis::None;
}

GizmoRenderer::Axis GizmoRenderer::pickAxis(
    const EditorCamera& camera,
    const glm::vec3& entityPos,
    float mouseX, float mouseY,
    int screenWidth, int screenHeight) const
{
    // Cast a ray from the mouse position
    glm::vec3 rayOrigin, rayDir;
    camera.screenToWorldRay(mouseX, mouseY, screenWidth, screenHeight,
                            rayOrigin, rayDir);

    float dist = glm::length(camera.getPosition() - entityPos);
    float len = m_gizmoSize * (dist * 0.08f);
    float threshold = len * 0.15f;  // how close the ray must pass to an axis

    // Test distance from ray to each axis line segment
    struct AxisTest { Axis axis; glm::vec3 dir; };
    AxisTest axes[] = {
        {Axis::X, {1, 0, 0}},
        {Axis::Y, {0, 1, 0}},
        {Axis::Z, {0, 0, 1}}
    };

    Axis closest = Axis::None;
    float closestDist = threshold;

    for (auto& test : axes) {
        // Closest point between ray and axis line
        glm::vec3 axisStart = entityPos;
        glm::vec3 axisEnd   = entityPos + test.dir * len;

        // Simplified: project ray origin onto the axis line and measure
        // perpendicular distance
        glm::vec3 w = rayOrigin - axisStart;
        glm::vec3 u = axisEnd - axisStart;
        float t = glm::dot(w, u) / glm::dot(u, u);
        t = std::clamp(t, 0.0f, 1.0f);

        glm::vec3 closestOnAxis = axisStart + u * t;

        // Also find closest point on the ray
        float rayT = glm::dot(closestOnAxis - rayOrigin, rayDir);
        if (rayT < 0) continue;  // behind camera
        glm::vec3 closestOnRay = rayOrigin + rayDir * rayT;

        float distance = glm::length(closestOnAxis - closestOnRay);

        if (distance < closestDist) {
            closestDist = distance;
            closest = test.axis;
        }
    }

    return closest;
}

glm::vec3 GizmoRenderer::projectMouseToAxis(
    const EditorCamera& camera,
    const InputManager& input,
    const glm::vec3& axisDir,
    int screenWidth, int screenHeight) const
{
    double mx, my;
    input.getMousePosition(mx, my);

    glm::vec3 rayOrigin, rayDir;
    camera.screenToWorldRay(static_cast<float>(mx), static_cast<float>(my),
                            screenWidth, screenHeight, rayOrigin, rayDir);

    // Find the point on the axis line closest to the mouse ray
    // This gives the projected world position along the constrained axis
    glm::vec3 w = rayOrigin - m_dragStart;
    float a = glm::dot(rayDir, rayDir);
    float b = glm::dot(rayDir, axisDir);
    float c = glm::dot(axisDir, axisDir);
    float d = glm::dot(rayDir, w);
    float e = glm::dot(axisDir, w);

    float denom = a * c - b * b;
    if (std::abs(denom) < 0.0001f) return m_lastDragPos;

    float t = (b * d - a * e) / denom;

    return m_dragStart + axisDir * t;
}
```

---

## Entity Placement and the Template Palette

The entity palette is an ImGui window listing entity templates — predefined configurations of components that can be stamped into the level with a click. Think of it as a toolbox of prefabs.

### src/engine/editor/entity_palette.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <functional>

struct EntityTemplate {
    std::string name;
    std::string category;  // "Enemies", "Items", "Lights", "Triggers", etc.
    std::string description;

    // Factory function that creates the entity and attaches components
    std::function<entt::entity(entt::registry&, const glm::vec3& position)> create;
};

class EntityPalette {
public:
    explicit EntityPalette(entt::registry& registry);

    void draw();

    // Is the user currently in "placement mode" (clicked a template, ready to place)?
    bool isPlacing() const { return m_placingTemplate >= 0; }

    // Create the selected template at the given position
    entt::entity placeEntity(const glm::vec3& position);

    // Cancel placement mode
    void cancelPlacement() { m_placingTemplate = -1; }

    bool visible = true;

private:
    void registerDefaultTemplates();

    entt::registry& m_registry;
    std::vector<EntityTemplate> m_templates;
    int m_placingTemplate = -1;  // index into m_templates, -1 = not placing
};
```

### src/engine/editor/entity_palette.cpp

```cpp
#include "engine/editor/entity_palette.h"
#include "engine/ecs/components.h"
#include <imgui.h>

EntityPalette::EntityPalette(entt::registry& registry)
    : m_registry(registry)
{
    registerDefaultTemplates();
}

void EntityPalette::registerDefaultTemplates() {
    // ─── Enemies ─────────────────────────────────────────────
    m_templates.push_back({
        "Grunt",
        "Enemies",
        "Basic enemy with patrol AI",
        [](entt::registry& reg, const glm::vec3& pos) -> entt::entity {
            auto e = reg.create();
            reg.emplace<Position>(e, pos);
            reg.emplace<Rotation>(e, glm::vec3(0.0f));
            reg.emplace<Scale>(e, glm::vec3(1.0f));
            reg.emplace<Health>(e, 50.0f, 50.0f);
            reg.emplace<AIComponent>(e, AIBehaviour::Patrol);
            reg.emplace<ModelRef>(e, "models/enemies/grunt.obj");
            reg.emplace<AABB>(e, glm::vec3(0.4f, 0.9f, 0.4f));
            reg.emplace<EditorTag>(e, "Grunt");
            return e;
        }
    });

    m_templates.push_back({
        "Enforcer",
        "Enemies",
        "Heavy enemy with ranged attack",
        [](entt::registry& reg, const glm::vec3& pos) -> entt::entity {
            auto e = reg.create();
            reg.emplace<Position>(e, pos);
            reg.emplace<Rotation>(e, glm::vec3(0.0f));
            reg.emplace<Scale>(e, glm::vec3(1.2f));
            reg.emplace<Health>(e, 120.0f, 120.0f);
            reg.emplace<AIComponent>(e, AIBehaviour::Aggressive);
            reg.emplace<ModelRef>(e, "models/enemies/enforcer.obj");
            reg.emplace<AABB>(e, glm::vec3(0.5f, 1.0f, 0.5f));
            reg.emplace<EditorTag>(e, "Enforcer");
            return e;
        }
    });

    // ─── Items ───────────────────────────────────────────────
    m_templates.push_back({
        "Health Pack",
        "Items",
        "Restores 25 health",
        [](entt::registry& reg, const glm::vec3& pos) -> entt::entity {
            auto e = reg.create();
            reg.emplace<Position>(e, pos);
            reg.emplace<Rotation>(e, glm::vec3(0.0f));
            reg.emplace<Scale>(e, glm::vec3(0.5f));
            reg.emplace<Pickup>(e, PickupType::Health, 25.0f);
            reg.emplace<ModelRef>(e, "models/items/health_pack.obj");
            reg.emplace<AABB>(e, glm::vec3(0.3f, 0.3f, 0.3f));
            reg.emplace<EditorTag>(e, "Health Pack");
            return e;
        }
    });

    m_templates.push_back({
        "Ammo Box",
        "Items",
        "Restores 20 shotgun shells",
        [](entt::registry& reg, const glm::vec3& pos) -> entt::entity {
            auto e = reg.create();
            reg.emplace<Position>(e, pos);
            reg.emplace<Rotation>(e, glm::vec3(0.0f));
            reg.emplace<Scale>(e, glm::vec3(0.4f));
            reg.emplace<Pickup>(e, PickupType::Ammo, 20.0f);
            reg.emplace<ModelRef>(e, "models/items/ammo_box.obj");
            reg.emplace<AABB>(e, glm::vec3(0.25f, 0.2f, 0.25f));
            reg.emplace<EditorTag>(e, "Ammo Box");
            return e;
        }
    });

    // ─── Lights ──────────────────────────────────────────────
    m_templates.push_back({
        "Point Light",
        "Lights",
        "Omnidirectional light source",
        [](entt::registry& reg, const glm::vec3& pos) -> entt::entity {
            auto e = reg.create();
            reg.emplace<Position>(e, pos);
            reg.emplace<PointLight>(e, glm::vec3(1.0f, 0.9f, 0.7f), 15.0f, 1.0f);
            reg.emplace<EditorTag>(e, "Point Light");
            return e;
        }
    });

    // ─── Triggers ────────────────────────────────────────────
    m_templates.push_back({
        "Trigger Volume",
        "Triggers",
        "Invisible volume that fires events on enter",
        [](entt::registry& reg, const glm::vec3& pos) -> entt::entity {
            auto e = reg.create();
            reg.emplace<Position>(e, pos);
            reg.emplace<Scale>(e, glm::vec3(2.0f));
            reg.emplace<TriggerVolume>(e, "");  // empty event name
            reg.emplace<AABB>(e, glm::vec3(1.0f, 1.0f, 1.0f));
            reg.emplace<EditorTag>(e, "Trigger Volume");
            return e;
        }
    });

    m_templates.push_back({
        "Player Spawn",
        "Spawns",
        "Player start position",
        [](entt::registry& reg, const glm::vec3& pos) -> entt::entity {
            auto e = reg.create();
            reg.emplace<Position>(e, pos);
            reg.emplace<Rotation>(e, glm::vec3(0.0f));
            reg.emplace<PlayerSpawn>(e);
            reg.emplace<EditorTag>(e, "Player Spawn");
            return e;
        }
    });
}

void EntityPalette::draw() {
    if (!visible) return;

    ImGui::SetNextWindowSize(ImVec2(250, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Entity Palette", &visible);

    if (m_placingTemplate >= 0) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Placing: %s",
                           m_templates[m_placingTemplate].name.c_str());
        ImGui::Text("Click in viewport to place. Escape to cancel.");
        ImGui::Separator();
    }

    // Group templates by category
    std::string lastCategory;
    for (int i = 0; i < static_cast<int>(m_templates.size()); ++i) {
        const auto& tmpl = m_templates[i];

        if (tmpl.category != lastCategory) {
            if (!lastCategory.empty()) ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s",
                               tmpl.category.c_str());
            lastCategory = tmpl.category;
        }

        bool isSelected = (m_placingTemplate == i);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        }

        if (ImGui::Button(tmpl.name.c_str(), ImVec2(-1, 0))) {
            m_placingTemplate = i;
        }

        if (isSelected) {
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tmpl.description.c_str());
        }
    }

    ImGui::End();
}

entt::entity EntityPalette::placeEntity(const glm::vec3& position) {
    if (m_placingTemplate < 0) return entt::null;

    entt::entity e = m_templates[m_placingTemplate].create(m_registry, position);
    // Don't clear placement mode — user can keep clicking to place multiple
    return e;
}
```

### The EditorTag Component

We add a simple tag component so the editor can identify and label entities:

```cpp
// In src/engine/ecs/components.h (add alongside existing components)

struct EditorTag {
    std::string name;         // human-readable label
    bool editorOnly = false;  // if true, not saved/loaded in gameplay
};
```

---

## Property Panel

The property panel shows and edits the components on the selected entity. It builds on Chapter 47's inspector but adds write access — drag sliders, colour pickers, text inputs, and enum dropdowns.

### src/engine/editor/property_panel.h

```cpp
#pragma once

#include <entt/entt.hpp>

class PropertyPanel {
public:
    explicit PropertyPanel(entt::registry& registry);

    void update();
    void draw(entt::entity selected);

    bool visible = true;

private:
    // Draw editors for specific component types
    void drawTransform(entt::entity e);
    void drawHealth(entt::entity e);
    void drawAI(entt::entity e);
    void drawPointLight(entt::entity e);
    void drawPickup(entt::entity e);
    void drawTriggerVolume(entt::entity e);
    void drawModelRef(entt::entity e);
    void drawEditorTag(entt::entity e);

    entt::registry& m_registry;
};
```

### src/engine/editor/property_panel.cpp

```cpp
#include "engine/editor/property_panel.h"
#include "engine/ecs/components.h"
#include <imgui.h>
#include <glm/glm.hpp>

PropertyPanel::PropertyPanel(entt::registry& registry)
    : m_registry(registry)
{
}

void PropertyPanel::update() {
    // Nothing to update each frame — ImGui is immediate mode
}

void PropertyPanel::draw(entt::entity selected) {
    if (!visible) return;

    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Properties", &visible);

    if (selected == entt::null || !m_registry.valid(selected)) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    // Entity ID
    ImGui::Text("Entity: %u", static_cast<unsigned int>(selected));
    ImGui::Separator();

    // Draw each component editor if the component exists
    drawEditorTag(selected);
    drawTransform(selected);
    drawHealth(selected);
    drawAI(selected);
    drawPointLight(selected);
    drawPickup(selected);
    drawTriggerVolume(selected);
    drawModelRef(selected);

    ImGui::End();
}

void PropertyPanel::drawEditorTag(entt::entity e) {
    auto* tag = m_registry.try_get<EditorTag>(e);
    if (!tag) return;

    if (ImGui::CollapsingHeader("Editor Tag", ImGuiTreeNodeFlags_DefaultOpen)) {
        char buf[128];
        std::strncpy(buf, tag->name.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';

        if (ImGui::InputText("Name", buf, sizeof(buf))) {
            tag->name = buf;
        }

        ImGui::Checkbox("Editor Only", &tag->editorOnly);
    }
}

void PropertyPanel::drawTransform(entt::entity e) {
    auto* pos = m_registry.try_get<Position>(e);
    auto* rot = m_registry.try_get<Rotation>(e);
    auto* scl = m_registry.try_get<Scale>(e);

    if (!pos && !rot && !scl) return;

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (pos) {
            ImGui::DragFloat3("Position", &pos->value.x, 0.1f);
        }
        if (rot) {
            ImGui::DragFloat3("Rotation", &rot->euler.x, 1.0f, -360.0f, 360.0f);
        }
        if (scl) {
            ImGui::DragFloat3("Scale", &scl->value.x, 0.01f, 0.01f, 100.0f);
        }
    }
}

void PropertyPanel::drawHealth(entt::entity e) {
    auto* health = m_registry.try_get<Health>(e);
    if (!health) return;

    if (ImGui::CollapsingHeader("Health")) {
        ImGui::DragFloat("Current", &health->current, 1.0f, 0.0f, health->max);
        ImGui::DragFloat("Max", &health->max, 1.0f, 1.0f, 10000.0f);

        // Visual health bar
        float fraction = health->current / health->max;
        ImGui::ProgressBar(fraction, ImVec2(-1, 0));
    }
}

void PropertyPanel::drawAI(entt::entity e) {
    auto* ai = m_registry.try_get<AIComponent>(e);
    if (!ai) return;

    if (ImGui::CollapsingHeader("AI")) {
        const char* behaviours[] = {"Idle", "Patrol", "Aggressive", "Defensive"};
        int current = static_cast<int>(ai->behaviour);
        if (ImGui::Combo("Behaviour", &current, behaviours,
                         IM_ARRAYSIZE(behaviours))) {
            ai->behaviour = static_cast<AIBehaviour>(current);
        }

        ImGui::DragFloat("Sight Range", &ai->sightRange, 0.5f, 1.0f, 100.0f);
        ImGui::DragFloat("Attack Range", &ai->attackRange, 0.5f, 0.5f, 50.0f);
        ImGui::DragFloat("Patrol Radius", &ai->patrolRadius, 0.5f, 1.0f, 50.0f);
    }
}

void PropertyPanel::drawPointLight(entt::entity e) {
    auto* light = m_registry.try_get<PointLight>(e);
    if (!light) return;

    if (ImGui::CollapsingHeader("Point Light")) {
        ImGui::ColorEdit3("Color", &light->color.x);
        ImGui::DragFloat("Radius", &light->radius, 0.5f, 0.1f, 200.0f);
        ImGui::DragFloat("Intensity", &light->intensity, 0.1f, 0.0f, 100.0f);
    }
}

void PropertyPanel::drawPickup(entt::entity e) {
    auto* pickup = m_registry.try_get<Pickup>(e);
    if (!pickup) return;

    if (ImGui::CollapsingHeader("Pickup")) {
        const char* types[] = {"Health", "Ammo", "Armour", "Weapon", "Key"};
        int current = static_cast<int>(pickup->type);
        if (ImGui::Combo("Type", &current, types, IM_ARRAYSIZE(types))) {
            pickup->type = static_cast<PickupType>(current);
        }

        ImGui::DragFloat("Value", &pickup->value, 1.0f, 0.0f, 1000.0f);
    }
}

void PropertyPanel::drawTriggerVolume(entt::entity e) {
    auto* trigger = m_registry.try_get<TriggerVolume>(e);
    if (!trigger) return;

    if (ImGui::CollapsingHeader("Trigger Volume")) {
        char buf[256];
        std::strncpy(buf, trigger->eventName.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';

        if (ImGui::InputText("Event Name", buf, sizeof(buf))) {
            trigger->eventName = buf;
        }

        ImGui::Checkbox("Once Only", &trigger->onceOnly);
        ImGui::Checkbox("Active", &trigger->active);
    }
}

void PropertyPanel::drawModelRef(entt::entity e) {
    auto* model = m_registry.try_get<ModelRef>(e);
    if (!model) return;

    if (ImGui::CollapsingHeader("Model")) {
        char buf[256];
        std::strncpy(buf, model->path.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';

        if (ImGui::InputText("Path", buf, sizeof(buf))) {
            model->path = buf;
        }
    }
}
```

Every component gets its own drawing function. This is a deliberate choice — component editors are specific enough that a generic "draw any struct" system would be more complex than simply writing one function per component. When you add a new component type, you add one `drawNewComponent()` function. It takes five minutes.

---

## Level Save and Load

The level serializer extends Chapter 23's save system. The difference: a save game captures runtime state (health, ammo, AI state), while a level file captures the designed layout (positions, templates, properties). The level file is what the editor produces.

### src/engine/editor/level_serializer.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class LevelSerializer {
public:
    explicit LevelSerializer(entt::registry& registry);

    // Save the entire level to a JSON file
    bool saveLevel(const std::string& filepath);

    // Load a level from a JSON file, replacing all current entities
    bool loadLevel(const std::string& filepath);

    // Clear all entities (new empty level)
    void newLevel();

    // Serialise a single entity (used by undo/redo)
    json serialiseEntity(entt::entity entity) const;

    // Deserialise a single entity from JSON, returns the new entity
    entt::entity deserialiseEntity(const json& entityJson);

private:
    entt::registry& m_registry;
};
```

### src/engine/editor/level_serializer.cpp

```cpp
#include "engine/editor/level_serializer.h"
#include "engine/ecs/components.h"
#include "engine/ecs/serialisation.h"
#include <fstream>
#include <iostream>
#include <filesystem>

LevelSerializer::LevelSerializer(entt::registry& registry)
    : m_registry(registry)
{
}

bool LevelSerializer::saveLevel(const std::string& filepath) {
    json level;

    // ─── Metadata ────────────────────────────────────────────
    level["version"] = 1;
    level["type"]    = "level";

    // ─── Entities ────────────────────────────────────────────
    json entities = json::array();

    auto view = m_registry.view<Position>();
    for (auto entity : view) {
        // Skip editor-only entities that shouldn't persist
        if (auto* tag = m_registry.try_get<EditorTag>(entity)) {
            if (tag->editorOnly) continue;
        }

        entities.push_back(serialiseEntity(entity));
    }

    level["entities"] = entities;

    // ─── Write to file ──────────────────────────────────────
    std::filesystem::create_directories(
        std::filesystem::path(filepath).parent_path());

    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[LevelSerializer] Failed to open: " << filepath << std::endl;
        return false;
    }

    file << level.dump(2);
    std::cout << "[LevelSerializer] Saved " << entities.size()
              << " entities to " << filepath << std::endl;
    return true;
}

bool LevelSerializer::loadLevel(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[LevelSerializer] Failed to open: " << filepath << std::endl;
        return false;
    }

    json level;
    try {
        level = json::parse(file);
    } catch (const json::parse_error& e) {
        std::cerr << "[LevelSerializer] Parse error: " << e.what() << std::endl;
        return false;
    }

    // Clear existing entities
    newLevel();

    // Reconstruct entities
    int count = 0;
    for (const auto& entityJson : level["entities"]) {
        deserialiseEntity(entityJson);
        count++;
    }

    std::cout << "[LevelSerializer] Loaded " << count
              << " entities from " << filepath << std::endl;
    return true;
}

void LevelSerializer::newLevel() {
    // Destroy all entities that have a Position (game entities)
    // Keep engine-internal entities (cameras, audio listeners, etc.)
    auto view = m_registry.view<Position>();
    std::vector<entt::entity> toDestroy;
    for (auto entity : view) {
        toDestroy.push_back(entity);
    }
    for (auto entity : toDestroy) {
        m_registry.destroy(entity);
    }
}

json LevelSerializer::serialiseEntity(entt::entity entity) const {
    json j;

    // Use the EditorTag name as a human-readable identifier
    if (auto* tag = m_registry.try_get<EditorTag>(entity)) {
        j["editorTag"] = tag->name;
    }

    // Serialise each component type we support
    if (auto* c = m_registry.try_get<Position>(entity))      j["Position"]      = *c;
    if (auto* c = m_registry.try_get<Rotation>(entity))      j["Rotation"]      = *c;
    if (auto* c = m_registry.try_get<Scale>(entity))         j["Scale"]         = *c;
    if (auto* c = m_registry.try_get<Health>(entity))        j["Health"]        = *c;
    if (auto* c = m_registry.try_get<AIComponent>(entity))   j["AIComponent"]   = *c;
    if (auto* c = m_registry.try_get<PointLight>(entity))    j["PointLight"]    = *c;
    if (auto* c = m_registry.try_get<Pickup>(entity))        j["Pickup"]        = *c;
    if (auto* c = m_registry.try_get<TriggerVolume>(entity)) j["TriggerVolume"] = *c;
    if (auto* c = m_registry.try_get<ModelRef>(entity))      j["ModelRef"]      = *c;
    if (auto* c = m_registry.try_get<AABB>(entity))          j["AABB"]          = *c;
    if (auto* c = m_registry.try_get<Mover>(entity))         j["Mover"]         = *c;

    // Tags (presence-only, no data)
    if (m_registry.all_of<TagPlayer>(entity))    j["tags"].push_back("TagPlayer");
    if (m_registry.all_of<PlayerSpawn>(entity))  j["tags"].push_back("PlayerSpawn");

    return j;
}

entt::entity LevelSerializer::deserialiseEntity(const json& j) {
    entt::entity e = m_registry.create();

    // Restore each component
    if (j.contains("Position"))      m_registry.emplace<Position>(e,      j["Position"].get<Position>());
    if (j.contains("Rotation"))      m_registry.emplace<Rotation>(e,      j["Rotation"].get<Rotation>());
    if (j.contains("Scale"))         m_registry.emplace<Scale>(e,         j["Scale"].get<Scale>());
    if (j.contains("Health"))        m_registry.emplace<Health>(e,        j["Health"].get<Health>());
    if (j.contains("AIComponent"))   m_registry.emplace<AIComponent>(e,   j["AIComponent"].get<AIComponent>());
    if (j.contains("PointLight"))    m_registry.emplace<PointLight>(e,    j["PointLight"].get<PointLight>());
    if (j.contains("Pickup"))        m_registry.emplace<Pickup>(e,        j["Pickup"].get<Pickup>());
    if (j.contains("TriggerVolume")) m_registry.emplace<TriggerVolume>(e, j["TriggerVolume"].get<TriggerVolume>());
    if (j.contains("ModelRef"))      m_registry.emplace<ModelRef>(e,      j["ModelRef"].get<ModelRef>());
    if (j.contains("AABB"))          m_registry.emplace<AABB>(e,          j["AABB"].get<AABB>());
    if (j.contains("Mover"))         m_registry.emplace<Mover>(e,         j["Mover"].get<Mover>());

    // Tags
    if (j.contains("tags")) {
        for (const auto& tag : j["tags"]) {
            std::string t = tag.get<std::string>();
            if (t == "TagPlayer")   m_registry.emplace<TagPlayer>(e);
            if (t == "PlayerSpawn") m_registry.emplace<PlayerSpawn>(e);
        }
    }

    // EditorTag
    if (j.contains("editorTag")) {
        m_registry.emplace<EditorTag>(e, j["editorTag"].get<std::string>());
    }

    return e;
}
```

### Example Level JSON

Here is what a saved level file looks like:

```json
{
  "version": 1,
  "type": "level",
  "entities": [
    {
      "editorTag": "Player Spawn",
      "Position": { "value": { "x": 0.0, "y": 1.0, "z": 0.0 } },
      "Rotation": { "euler": { "x": 0.0, "y": 0.0, "z": 0.0 } },
      "tags": ["PlayerSpawn"]
    },
    {
      "editorTag": "Grunt",
      "Position": { "value": { "x": 10.0, "y": 0.0, "z": -5.0 } },
      "Rotation": { "euler": { "x": 0.0, "y": 180.0, "z": 0.0 } },
      "Scale": { "value": { "x": 1.0, "y": 1.0, "z": 1.0 } },
      "Health": { "current": 50.0, "max": 50.0 },
      "AIComponent": { "behaviour": 1, "sightRange": 20.0 },
      "ModelRef": { "path": "models/enemies/grunt.obj" },
      "AABB": { "halfExtents": { "x": 0.4, "y": 0.9, "z": 0.4 } }
    },
    {
      "editorTag": "Health Pack",
      "Position": { "value": { "x": 5.0, "y": 0.5, "z": 3.0 } },
      "Scale": { "value": { "x": 0.5, "y": 0.5, "z": 0.5 } },
      "Pickup": { "type": 0, "value": 25.0 },
      "ModelRef": { "path": "models/items/health_pack.obj" }
    },
    {
      "editorTag": "Point Light",
      "Position": { "value": { "x": 5.0, "y": 3.0, "z": 0.0 } },
      "PointLight": { "color": { "x": 1.0, "y": 0.9, "z": 0.7 }, "radius": 15.0, "intensity": 1.0 }
    }
  ]
}
```

Human-readable. Diffable in version control. Editable by hand if needed.

---

## Undo/Redo — The Command Pattern

Every operation in the editor that changes state — moving an entity, creating an entity, deleting an entity, changing a property — is wrapped in a command object. Commands know how to do and undo their operation. An undo stack tracks the command history.

```
UNDO STACK

  ┌──────────────────────────────────────────────────────┐
  │                    Undo Stack                         │
  │                                                      │
  │  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐    │
  │  │ Create │  │ Move   │  │ Move   │  │ Delete │    │
  │  │ Grunt  │  │ Grunt  │  │ Light  │  │ Pack   │    │
  │  │        │  │ (3,0,5)│  │ (5,3,0)│  │        │    │
  │  └────────┘  └────────┘  └────────┘  └────────┘    │
  │      0           1           2           3          │
  │                                         ↑           │
  │                                     m_current       │
  │                                                      │
  │  Ctrl+Z: undo command at m_current, decrement       │
  │  Ctrl+Y: increment m_current, redo command          │
  └──────────────────────────────────────────────────────┘

  If a new command is executed while m_current < stack.size()-1,
  all commands after m_current are discarded (the redo history
  is invalidated).
```

### src/engine/editor/undo_stack.h

```cpp
#pragma once

#include <vector>
#include <memory>
#include <string>

// Base class for all editor commands
class EditorCommand {
public:
    virtual ~EditorCommand() = default;

    virtual void execute() = 0;     // do the operation
    virtual void undo()    = 0;     // reverse the operation
    virtual std::string description() const = 0;
};

class UndoStack {
public:
    // Execute a command and push it onto the stack
    void execute(std::unique_ptr<EditorCommand> cmd);

    // Undo the most recent command
    void undo();

    // Redo the most recently undone command
    void redo();

    bool canUndo() const { return m_current >= 0; }
    bool canRedo() const { return m_current < static_cast<int>(m_commands.size()) - 1; }

    void clear();

    // For debug display
    int size() const { return static_cast<int>(m_commands.size()); }
    int currentIndex() const { return m_current; }

private:
    std::vector<std::unique_ptr<EditorCommand>> m_commands;
    int m_current = -1;  // index of the most recently executed command
};
```

### src/engine/editor/undo_stack.cpp

```cpp
#include "engine/editor/undo_stack.h"
#include <iostream>

void UndoStack::execute(std::unique_ptr<EditorCommand> cmd) {
    // Discard any commands after the current position (invalidate redo history)
    if (m_current < static_cast<int>(m_commands.size()) - 1) {
        m_commands.erase(m_commands.begin() + m_current + 1, m_commands.end());
    }

    cmd->execute();
    m_commands.push_back(std::move(cmd));
    m_current = static_cast<int>(m_commands.size()) - 1;

    std::cout << "[Undo] Executed: "
              << m_commands[m_current]->description() << std::endl;
}

void UndoStack::undo() {
    if (!canUndo()) return;

    m_commands[m_current]->undo();
    std::cout << "[Undo] Undone: "
              << m_commands[m_current]->description() << std::endl;
    m_current--;
}

void UndoStack::redo() {
    if (!canRedo()) return;

    m_current++;
    m_commands[m_current]->execute();
    std::cout << "[Undo] Redone: "
              << m_commands[m_current]->description() << std::endl;
}

void UndoStack::clear() {
    m_commands.clear();
    m_current = -1;
}
```

### Concrete Command Classes

### src/engine/editor/editor_commands.h

```cpp
#pragma once

#include "engine/editor/undo_stack.h"
#include "engine/editor/level_serializer.h"
#include "engine/ecs/components.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

// ─── Transform Command ──────────────────────────────────────────
// Records a position/rotation/scale change on one entity

class TransformCommand : public EditorCommand {
public:
    TransformCommand(entt::registry& registry, entt::entity entity,
                     const glm::vec3& oldPos, const glm::vec3& newPos,
                     const glm::vec3& oldRot, const glm::vec3& newRot,
                     const glm::vec3& oldScale, const glm::vec3& newScale)
        : m_registry(registry), m_entity(entity)
        , m_oldPos(oldPos), m_newPos(newPos)
        , m_oldRot(oldRot), m_newRot(newRot)
        , m_oldScale(oldScale), m_newScale(newScale)
    {}

    void execute() override {
        if (auto* p = m_registry.try_get<Position>(m_entity)) p->value = m_newPos;
        if (auto* r = m_registry.try_get<Rotation>(m_entity)) r->euler = m_newRot;
        if (auto* s = m_registry.try_get<Scale>(m_entity))    s->value = m_newScale;
    }

    void undo() override {
        if (auto* p = m_registry.try_get<Position>(m_entity)) p->value = m_oldPos;
        if (auto* r = m_registry.try_get<Rotation>(m_entity)) r->euler = m_oldRot;
        if (auto* s = m_registry.try_get<Scale>(m_entity))    s->value = m_oldScale;
    }

    std::string description() const override {
        return "Transform entity " + std::to_string(static_cast<uint32_t>(m_entity));
    }

private:
    entt::registry& m_registry;
    entt::entity    m_entity;
    glm::vec3       m_oldPos, m_newPos;
    glm::vec3       m_oldRot, m_newRot;
    glm::vec3       m_oldScale, m_newScale;
};

// ─── Create Entity Command ──────────────────────────────────────
// Records the creation of a new entity. Undo destroys it, redo recreates it.

class CreateEntityCommand : public EditorCommand {
public:
    CreateEntityCommand(entt::registry& registry,
                        LevelSerializer& serializer,
                        entt::entity entity)
        : m_registry(registry)
        , m_serializer(serializer)
        , m_entity(entity)
        , m_snapshot(serializer.serialiseEntity(entity))
    {}

    void execute() override {
        // Entity already exists on first execution.
        // On redo, recreate from snapshot.
        if (!m_registry.valid(m_entity)) {
            m_entity = m_serializer.deserialiseEntity(m_snapshot);
        }
    }

    void undo() override {
        if (m_registry.valid(m_entity)) {
            m_snapshot = m_serializer.serialiseEntity(m_entity);
            m_registry.destroy(m_entity);
        }
    }

    std::string description() const override {
        return "Create entity";
    }

private:
    entt::registry&  m_registry;
    LevelSerializer& m_serializer;
    entt::entity     m_entity;
    json             m_snapshot;
};

// ─── Delete Entity Command ──────────────────────────────────────
// Records the deletion of an entity. Undo recreates it from a snapshot.

class DeleteEntityCommand : public EditorCommand {
public:
    DeleteEntityCommand(entt::registry& registry,
                        entt::entity entity,
                        LevelSerializer& serializer)
        : m_registry(registry)
        , m_serializer(serializer)
        , m_entity(entity)
        , m_snapshot(serializer.serialiseEntity(entity))
    {}

    void execute() override {
        if (m_registry.valid(m_entity)) {
            m_snapshot = m_serializer.serialiseEntity(m_entity);
            m_registry.destroy(m_entity);
        }
    }

    void undo() override {
        m_entity = m_serializer.deserialiseEntity(m_snapshot);
    }

    std::string description() const override {
        return "Delete entity";
    }

private:
    entt::registry&  m_registry;
    LevelSerializer& m_serializer;
    entt::entity     m_entity;
    json             m_snapshot;
};

// ─── Property Change Command ────────────────────────────────────
// Generic property change using JSON snapshots of a single component

class PropertyChangeCommand : public EditorCommand {
public:
    PropertyChangeCommand(entt::registry& registry,
                          LevelSerializer& serializer,
                          entt::entity entity,
                          const std::string& componentName,
                          const json& oldValue,
                          const json& newValue)
        : m_registry(registry)
        , m_serializer(serializer)
        , m_entity(entity)
        , m_componentName(componentName)
        , m_oldValue(oldValue)
        , m_newValue(newValue)
    {}

    void execute() override {
        // In a real implementation, this would use a component type
        // registry to set the component from JSON. For now we
        // re-serialise the full entity as a simpler approach.
    }

    void undo() override {
        // Restore old value similarly
    }

    std::string description() const override {
        return "Change " + m_componentName;
    }

private:
    entt::registry&  m_registry;
    LevelSerializer& m_serializer;
    entt::entity     m_entity;
    std::string      m_componentName;
    json             m_oldValue;
    json             m_newValue;
};
```

### Why Snapshot-Based Undo?

The `CreateEntityCommand` and `DeleteEntityCommand` use JSON snapshots to capture the full state of an entity. This has a trade-off:

```
APPROACH 1: SNAPSHOT (what we use)
──────────────────────────────────────────
  + Simple: serialise entity to JSON on delete, deserialise on undo
  + Handles any number of components automatically
  + Works even if component types change between undo/redo
  - Slightly more memory per command (a small JSON blob)
  - Entity ID may change on redo (entt::entity is recycled)

APPROACH 2: COMPONENT-BY-COMPONENT
──────────────────────────────────────────
  + Preserves exact entity ID
  + Less memory per command
  - Must enumerate every component type explicitly
  - Breaks if you add a new component type and forget to update undo code
```

The snapshot approach is simpler and more maintainable. The entity ID changing on redo is a minor issue — if something holds a reference to the old entity, it breaks. But in the editor, the only persistent reference is the selection, and we update that after every redo.

---

## C++ Concept: The Command Pattern

The command pattern encapsulates an action as an object. The object stores everything needed to perform and reverse the action. This decouples "what to do" from "when to do it."

```
COMMAND PATTERN STRUCTURE
──────────────────────────────────────────────────────────────────

  ┌──────────┐     executes     ┌─────────────────┐
  │  Invoker  │ ──────────────► │  Command (base)  │
  │  (editor) │                 │   + execute()    │
  └──────────┘                 │   + undo()       │
                                └────────┬────────┘
                                         │ inherits
                    ┌────────────────────┼────────────────────┐
                    │                    │                    │
            ┌───────────────┐  ┌────────────────┐  ┌─────────────────┐
            │ TransformCmd  │  │ CreateCmd      │  │ DeleteCmd       │
            │ (stores old & │  │ (stores entity │  │ (stores entity  │
            │  new position)│  │  snapshot)     │  │  snapshot)      │
            └───────────────┘  └────────────────┘  └─────────────────┘
```

The command pattern appears far beyond undo/redo:

**Macro recording.** Record a sequence of commands, replay them later. "Place 10 lights in a circle" becomes a loop that creates 10 `CreateEntityCommand` objects with computed positions.

**Network replays.** In a multiplayer game, the server sends commands to clients: "player 3 moved to position X", "player 1 fired weapon". Each command is a small serialisable object. To replay a match, replay the command stream.

**Deferred execution.** Queue commands for later. A cutscene system might queue "open door", "spawn enemy", "play sound" as commands triggered by timeline keyframes.

**Transaction batching.** Group multiple commands into one undo step. Moving a group of selected entities is N `TransformCommand` objects wrapped in a single `CompoundCommand` that undoes all of them together.

```cpp
// A compound command groups multiple commands into one undo step
class CompoundCommand : public EditorCommand {
public:
    void add(std::unique_ptr<EditorCommand> cmd) {
        m_children.push_back(std::move(cmd));
    }

    void execute() override {
        for (auto& cmd : m_children) cmd->execute();
    }

    void undo() override {
        // Undo in reverse order
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
            (*it)->undo();
        }
    }

    std::string description() const override {
        return "Compound (" + std::to_string(m_children.size()) + " operations)";
    }

private:
    std::vector<std::unique_ptr<EditorCommand>> m_children;
};
```

The key insight is that the command pattern turns actions into data. Once actions are data, you can store them, transmit them, replay them, and reverse them.

---

## File Layout

After this chapter, the editor source files are:

```
src/engine/editor/
    editor_state.h            ← EditorState (GameState subclass)
    editor_state.cpp
    editor_camera.h           ← Free-fly camera with screen-to-world ray
    editor_camera.cpp
    selection_manager.h       ← Ray picking and selection tracking
    selection_manager.cpp
    gizmo_renderer.h          ← Translate/rotate/scale visual handles
    gizmo_renderer.cpp
    property_panel.h          ← ImGui property editor for components
    property_panel.cpp
    entity_palette.h          ← Template browser for placing new entities
    entity_palette.cpp
    level_serializer.h        ← Full level save/load to JSON
    level_serializer.cpp
    undo_stack.h              ← Command pattern undo/redo stack
    undo_stack.cpp
    editor_commands.h         ← TransformCommand, CreateCommand, DeleteCommand
```

---

## Putting It All Together

Here is the complete flow of an editing session:

```
EDITING SESSION FLOW
────────────────────────────────────────────────────────────────────

  1. Player presses F5 during gameplay
     → PlayingState pushes EditorState onto state stack
     → Cursor unlocked, gameplay systems paused

  2. Editor camera activated
     → Right-click + WASD to fly around the frozen level
     → Scroll wheel adjusts fly speed

  3. Click on an enemy in the viewport
     → Mouse ray cast hits the enemy's AABB
     → SelectionManager stores the entity
     → Yellow wireframe highlight appears
     → Property panel shows Position, Rotation, Health, AI

  4. Press W to ensure translate mode, drag the Y axis gizmo
     → GizmoRenderer highlights Y axis yellow
     → TransformCommand records old position
     → Entity moves vertically with the mouse
     → Snap-to-grid rounds to nearest 1.0 unit
     → On mouse release, command committed to UndoStack

  5. Open Entity Palette, click "Point Light"
     → Placement mode active
     → Click in viewport → ray cast finds ground position
     → CreateEntityCommand fires, entity appears
     → Light immediately visible in the scene

  6. Ctrl+Z to undo the light placement
     → CreateEntityCommand::undo() destroys the light entity
     → Ctrl+Y to redo → light reappears from JSON snapshot

  7. Ctrl+S to save
     → LevelSerializer writes all entities to JSON
     → File written to levels/level_01.json

  8. F5 to return to gameplay
     → EditorState popped from stack
     → PlayingState resumes, cursor locked, systems tick
     → Player can now play-test the level they just edited
```

---

## Console Commands

Register editor commands with the developer console for operations that are easier to type than click:

```cpp
// In EditorState::enter()
m_console.registerCommand("editor_load", "Load a level file",
    [this](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            m_console.print("Usage: editor_load <filepath>");
            return;
        }
        if (m_serializer.loadLevel(args[1])) {
            m_currentLevelPath = args[1];
            m_selection.clearSelection();
            m_undoStack.clear();
            m_unsavedChanges = false;
        }
    });

m_console.registerCommand("editor_saveas", "Save level to a new file",
    [this](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            m_console.print("Usage: editor_saveas <filepath>");
            return;
        }
        if (m_serializer.saveLevel(args[1])) {
            m_currentLevelPath = args[1];
            m_unsavedChanges = false;
            m_console.print("Saved to: " + args[1]);
        }
    });

m_console.registerCommand("editor_snap", "Set snap grid size",
    [this](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            m_console.print("Current snap: " + std::to_string(m_snapSize));
            return;
        }
        m_snapSize = std::stof(args[1]);
        m_console.print("Snap size set to: " + std::to_string(m_snapSize));
    });
```

---

## What's Next

The level editor gives us tools to build content visually, but every behaviour in the game is still hardcoded in C++. Enemy AI patterns, trigger logic, door sequences — changing any of them requires recompiling. Chapter 49 introduces **Lua Scripting**, embedding a lightweight scripting language into QEngine so that gameplay logic can be defined in external script files, edited without recompilation, and hot-reloaded while the game runs.