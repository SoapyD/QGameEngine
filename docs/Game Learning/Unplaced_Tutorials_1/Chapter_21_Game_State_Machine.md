# Chapter 21: Game State Machine

## What You'll Learn
- Why a game needs states beyond "running"
- Designing a state stack (not just a single state)
- Controlling which ECS systems tick in each state
- State transitions with enter/exit hooks
- Integrating the state machine into the existing game loop

---

## The Problem

Right now, QEngine drops straight into gameplay. There's no main menu, no pause screen, no "game over". When you press Escape, the window closes. A real game needs multiple **modes** — and smooth transitions between them.

These modes are **game states**: distinct configurations of what the player sees, what systems are running, and what input does.

```
Launch → Main Menu → Loading → Playing → Paused → Playing → Game Over → Main Menu
```

Each state has different answers to basic questions:
- What renders on screen?
- What systems are ticking?
- What does keyboard/mouse input do?

---

## State Stack vs State Switch

A simple approach is a single `currentState` variable that switches between states. But this breaks when you need **overlays** — a pause menu drawn on top of the game world. The game world should still render (frozen) while the pause menu sits on top.

The solution is a **state stack**:

```
┌──────────────┐
│  PauseState  │  ← Top: receives input, renders pause overlay
├──────────────┤
│ PlayingState  │  ← Below: still renders (frozen), systems paused
├──────────────┤
│ (bottom)      │
└──────────────┘
```

- Only the **top** state receives input
- States can be **transparent** — meaning the state below them still renders
- Pushing a state pauses (but doesn't destroy) what's underneath
- Popping a state resumes whatever was below

---

## The GameState Base Class

This is one of the few places we use inheritance in QEngine. States have behaviour (enter, exit, update, render) that varies by type — this is what virtual functions are for.

### src/engine/core/game_state.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include <string>

// Forward declaration — states need to push/pop other states
class GameStateManager;

class GameState {
public:
    virtual ~GameState() = default;

    // Called once when this state is first pushed onto the stack
    virtual void enter() {}

    // Called once when this state is removed from the stack
    virtual void exit() {}

    // Called when this state becomes the top state (e.g. state above it was popped)
    virtual void resume() {}

    // Called when another state is pushed on top of this one
    virtual void pause() {}

    // Called every frame while this state is on top
    virtual void update(float dt) = 0;

    // Called every frame — transparent states let the state below render first
    virtual void render() = 0;

    // Does this state allow the state below to render?
    virtual bool isTransparent() const { return false; }

    // Does this state allow the state below to update?
    virtual bool allowsUpdateBelow() const { return false; }

    // Human-readable name for debugging
    virtual std::string getName() const = 0;

protected:
    GameStateManager* m_stateManager = nullptr;
    friend class GameStateManager;
};
```

### C++ Concept: Pure Virtual Functions

`virtual void update(float dt) = 0;` — the `= 0` makes this a **pure virtual** function. Any class inheriting from `GameState` **must** implement `update()` and `render()`. You can't create a `GameState` directly — only concrete subclasses.

This is an **abstract class** — it defines an interface that all states must follow.

---

## The State Manager

The manager owns the stack and handles transitions. It's the only thing the game loop talks to.

### src/engine/core/game_state_manager.h

```cpp
#pragma once

#include "engine/core/game_state.h"
#include <vector>
#include <memory>
#include <functional>

class GameStateManager {
public:
    // Push a new state onto the stack
    void pushState(std::unique_ptr<GameState> state);

    // Pop the top state off the stack
    void popState();

    // Replace the top state (pop + push in one operation)
    void changeState(std::unique_ptr<GameState> state);

    // Clear all states
    void clearStates();

    // Update the top state (and states below it if allowsUpdateBelow)
    void update(float dt);

    // Render states from bottom to top (respecting transparency)
    void render();

    // Is the stack empty? (signals quit)
    bool isEmpty() const { return m_states.empty(); }

    // Get the current top state (for debugging)
    GameState* currentState() const;

private:
    std::vector<std::unique_ptr<GameState>> m_states;

    // Pending operations — applied at the start of next update
    // This prevents modifying the stack during iteration
    struct PendingOperation {
        enum class Type { Push, Pop, Change, Clear };
        Type type;
        std::unique_ptr<GameState> state;  // For Push/Change
    };
    std::vector<PendingOperation> m_pending;

    void processPending();
};
```

### src/engine/core/game_state_manager.cpp

```cpp
#include "engine/core/game_state_manager.h"
#include <iostream>

void GameStateManager::pushState(std::unique_ptr<GameState> state) {
    PendingOperation op;
    op.type = PendingOperation::Type::Push;
    op.state = std::move(state);
    m_pending.push_back(std::move(op));
}

void GameStateManager::popState() {
    PendingOperation op;
    op.type = PendingOperation::Type::Pop;
    m_pending.push_back(std::move(op));
}

void GameStateManager::changeState(std::unique_ptr<GameState> state) {
    PendingOperation op;
    op.type = PendingOperation::Type::Change;
    op.state = std::move(state);
    m_pending.push_back(std::move(op));
}

void GameStateManager::clearStates() {
    PendingOperation op;
    op.type = PendingOperation::Type::Clear;
    m_pending.push_back(std::move(op));
}

void GameStateManager::processPending() {
    for (auto& op : m_pending) {
        switch (op.type) {
            case PendingOperation::Type::Push: {
                if (!m_states.empty()) {
                    m_states.back()->pause();
                }
                op.state->m_stateManager = this;
                op.state->enter();
                m_states.push_back(std::move(op.state));
                std::cout << "State pushed: " << m_states.back()->getName() << std::endl;
                break;
            }
            case PendingOperation::Type::Pop: {
                if (!m_states.empty()) {
                    std::cout << "State popped: " << m_states.back()->getName() << std::endl;
                    m_states.back()->exit();
                    m_states.pop_back();
                    if (!m_states.empty()) {
                        m_states.back()->resume();
                    }
                }
                break;
            }
            case PendingOperation::Type::Change: {
                if (!m_states.empty()) {
                    m_states.back()->exit();
                    m_states.pop_back();
                }
                op.state->m_stateManager = this;
                op.state->enter();
                m_states.push_back(std::move(op.state));
                std::cout << "State changed to: " << m_states.back()->getName() << std::endl;
                break;
            }
            case PendingOperation::Type::Clear: {
                while (!m_states.empty()) {
                    m_states.back()->exit();
                    m_states.pop_back();
                }
                std::cout << "All states cleared" << std::endl;
                break;
            }
        }
    }
    m_pending.clear();
}

void GameStateManager::update(float dt) {
    processPending();

    if (m_states.empty()) return;

    // Update from top down — stop when we hit a state that doesn't allow update below
    for (int i = static_cast<int>(m_states.size()) - 1; i >= 0; i--) {
        m_states[i]->update(dt);
        if (!m_states[i]->allowsUpdateBelow()) break;
    }
}

void GameStateManager::render() {
    if (m_states.empty()) return;

    // Find the lowest state that needs to render
    int renderFrom = static_cast<int>(m_states.size()) - 1;
    for (int i = renderFrom; i > 0; i--) {
        if (!m_states[i]->isTransparent()) break;
        renderFrom = i - 1;
    }

    // Render from bottom to top
    for (int i = renderFrom; i < static_cast<int>(m_states.size()); i++) {
        m_states[i]->render();
    }
}

GameState* GameStateManager::currentState() const {
    return m_states.empty() ? nullptr : m_states.back().get();
}
```

### Why Pending Operations?

State changes are **deferred** — queued and applied at the start of the next `update()`. This prevents a common bug: if a state pushes or pops during its own `update()`, the stack changes while we're iterating it. Deferring makes the stack stable during processing.

---

## C++ Concept: `std::unique_ptr` and Ownership

```cpp
void pushState(std::unique_ptr<GameState> state);
```

`std::unique_ptr` is a smart pointer that **owns** the object it points to. When the `unique_ptr` is destroyed, the object is destroyed too. Key rules:

- Only **one** `unique_ptr` can own an object at a time
- You transfer ownership with `std::move()` — the source becomes `nullptr`
- No manual `delete` needed — memory is freed automatically

```cpp
// Create a state — the unique_ptr owns it
auto state = std::make_unique<PlayingState>(registry);

// Transfer ownership to the manager — we can't use 'state' after this
stateManager.pushState(std::move(state));
// 'state' is now nullptr
```

This is the C++ way to say "exactly one thing is responsible for this object's lifetime."

---

## Concrete States

### PlayingState — The Actual Game

This is what QEngine does now, wrapped in a state:

```cpp
// src/game/states/playing_state.h
#pragma once

#include "engine/core/game_state.h"
#include "engine/core/fixed_timestep.h"
#include "engine/renderer/camera.h"
#include <entt/entt.hpp>

class Window;
class InputManager;
class AudioManager;

class PlayingState : public GameState {
public:
    PlayingState(entt::registry& registry, Window& window,
                  InputManager& input, AudioManager& audio, Camera& camera);

    void enter() override;
    void exit() override;
    void pause() override;
    void resume() override;
    void update(float dt) override;
    void render() override;

    std::string getName() const override { return "Playing"; }

private:
    entt::registry& m_registry;
    Window& m_window;
    InputManager& m_input;
    AudioManager& m_audio;
    Camera& m_camera;
    FixedTimestep m_fixedTimestep;
};
```

```cpp
// src/game/states/playing_state.cpp
#include "game/states/playing_state.h"
#include "game/states/pause_state.h"
#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/game_state_manager.h"

// Include all your system headers
#include "engine/ecs/systems/physics_system.h"
#include "engine/ecs/systems/input_system.h"
#include "engine/ecs/systems/combat_system.h"
#include "engine/ecs/systems/ai_system.h"
#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/systems/audio_system.h"
#include "engine/ecs/systems/hud_system.h"
// ... etc.

PlayingState::PlayingState(entt::registry& registry, Window& window,
                            InputManager& input, AudioManager& audio,
                            Camera& camera)
    : m_registry(registry), m_window(window), m_input(input),
      m_audio(audio), m_camera(camera),
      m_fixedTimestep(registry.ctx().get<PhysicsConfig>().fixedDeltaTime) {}

void PlayingState::enter() {
    // Lock cursor for FPS controls
    glfwSetInputMode(m_window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void PlayingState::exit() {
    // Release cursor
    glfwSetInputMode(m_window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void PlayingState::pause() {
    // Cursor visible when paused
    glfwSetInputMode(m_window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void PlayingState::resume() {
    // Re-lock cursor when unpaused
    glfwSetInputMode(m_window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void PlayingState::update(float dt) {
    // Check for pause (using InputManager from Chapter 5a)
    if (m_input.isKeyPressed(GLFW_KEY_ESCAPE)) {
        m_stateManager->pushState(std::make_unique<PauseState>(
            m_registry, m_window, m_input));
        return;  // Don't process game logic this frame
    }

    // -- Phase: Input --
    m_input.update();
    inputSystem(m_registry);

    // -- Phase: Physics (fixed timestep from Chapter 10a) --
    m_fixedTimestep.accumulate();
    while (m_fixedTimestep.step()) {
        gravitySystem(m_registry);
        movementSystem(m_registry);
        collisionSystem(m_registry);
        jumpSystem(m_registry);
    }

    // -- Phase: GameLogic --
    aiSystem(m_registry, dt);
    combatSystem(m_registry, m_registry.ctx().get<Level>(), dt);
    triggerSystem(m_registry);
    pickupSystem(m_registry);
    hudUpdateSystem(m_registry, dt);
    audioSystem(m_registry, m_audio, m_camera, dt);
}

void PlayingState::render() {
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderSystem(m_registry, m_camera);
    particleSystem(m_registry, m_camera);
    hudSystem(m_registry, m_window);
}
```

### PauseState — Overlay on Top of the Game

```cpp
// src/game/states/pause_state.h
#pragma once

#include "engine/core/game_state.h"
#include <entt/entt.hpp>

class Window;

class PauseState : public GameState {
public:
    PauseState(entt::registry& registry, Window& window, InputManager& input);

    void enter() override;
    void update(float dt) override;
    void render() override;

    bool isTransparent() const override { return true; }  // Game world visible behind
    std::string getName() const override { return "Paused"; }

private:
    entt::registry& m_registry;
    Window& m_window;
    InputManager& m_input;
    int m_selectedOption = 0;
    bool m_escapeReleased = false;  // Prevent immediate unpause

    static constexpr int OPTION_RESUME = 0;
    static constexpr int OPTION_SETTINGS = 1;
    static constexpr int OPTION_QUIT = 2;
    static constexpr int OPTION_COUNT = 3;
};
```

```cpp
// src/game/states/pause_state.cpp
#include "game/states/pause_state.h"
#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/game_state_manager.h"

PauseState::PauseState(entt::registry& registry, Window& window, InputManager& input)
    : m_registry(registry), m_window(window), m_input(input) {}

void PauseState::enter() {
    m_escapeReleased = false;
    m_selectedOption = 0;
}

void PauseState::update(float dt) {
    // Wait for Escape to be released before accepting it again
    if (!m_escapeReleased) {
        if (m_input.isKeyReleased(GLFW_KEY_ESCAPE)) {
            m_escapeReleased = true;
        }
        return;
    }

    // Unpause with Escape
    if (m_input.isKeyPressed(GLFW_KEY_ESCAPE)) {
        m_stateManager->popState();  // Remove pause, resume playing
        return;
    }

    // Navigate menu
    // (Using simple key checks — a proper input system would debounce these)
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;

    bool upNow = m_input.isKeyPressed(GLFW_KEY_UP) ||
                 m_input.isKeyPressed(GLFW_KEY_W);
    bool downNow = m_input.isKeyPressed(GLFW_KEY_DOWN) ||
                   m_input.isKeyPressed(GLFW_KEY_S);
    bool enterNow = m_input.isKeyPressed(GLFW_KEY_ENTER);

    if (upNow && !upPressed) {
        m_selectedOption = (m_selectedOption - 1 + OPTION_COUNT) % OPTION_COUNT;
    }
    if (downNow && !downPressed) {
        m_selectedOption = (m_selectedOption + 1) % OPTION_COUNT;
    }
    if (enterNow && !enterPressed) {
        switch (m_selectedOption) {
            case OPTION_RESUME:
                m_stateManager->popState();
                return;
            case OPTION_SETTINGS:
                // TODO: push SettingsState
                break;
            case OPTION_QUIT:
                m_stateManager->clearStates();  // Empty stack = quit
                return;
        }
    }

    upPressed = upNow;
    downPressed = downNow;
    enterPressed = enterNow;
}

void PauseState::render() {
    // Semi-transparent overlay
    // Render a full-screen dark quad with alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw darkened overlay (reuse HUD quad rendering from Chapter 15)
    // renderQuad(0, 0, screenWidth, screenHeight, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

    // Draw menu options
    const char* options[] = { "Resume", "Settings", "Quit" };

    float centerX = m_window.getWidth() / 2.0f;
    float startY = m_window.getHeight() / 2.0f - 40.0f;

    for (int i = 0; i < OPTION_COUNT; i++) {
        glm::vec4 colour = (i == m_selectedOption)
            ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)   // Yellow = selected
            : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);   // Grey = unselected

        // renderText(options[i], centerX, startY + i * 40.0f, colour);
        // Use the TextRenderer from Chapter 15a
    }

    glDisable(GL_BLEND);
}
```

Notice `isTransparent()` returns `true` — the PlayingState below still renders its game world. The PauseState just draws an overlay on top.

---

## Updating main.cpp

The game loop becomes much cleaner:

```cpp
#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/resource_manager.h"
#include "engine/core/game_state_manager.h"
#include "engine/audio/audio_manager.h"
#include "engine/renderer/camera.h"
#include "engine/physics/physics_config.h"
#include "game/states/main_menu_state.h"  // Chapter 22

int main() {
    Window window(1280, 720, "QEngine");
    InputManager input;
    input.init(window.getHandle());
    ResourceManager resources;

    AudioManager audio;
    audio.init();

    entt::registry registry;
    registry.ctx().emplace<PhysicsConfig>();

    Camera camera;
    GameStateManager stateManager;

    // Start at the main menu
    stateManager.pushState(std::make_unique<MainMenuState>(
        registry, window, input, audio, camera, stateManager));

    float lastFrame = 0.0f;

    while (!window.shouldClose() && !stateManager.isEmpty()) {
        float currentFrame = static_cast<float>(glfwGetTime());
        float dt = currentFrame - lastFrame;
        lastFrame = currentFrame;

        window.pollEvents();

        stateManager.update(dt);
        stateManager.render();

        window.swapBuffers();
    }

    audio.shutdown();
    return 0;
}
```

Everything that was directly in the game loop is now inside state classes. The loop just ticks the state manager.

---

## State Transition Diagram

```
                    ┌──────────┐
         ┌─────────│ MainMenu │◄────────────────────┐
         │ Start   └──────────┘  Quit to Menu        │
         ▼              │                            │
    ┌──────────┐        │ Quit                  ┌────┴─────┐
    │ Loading  │        ▼                       │  Paused  │
    └────┬─────┘   Application                  └────┬─────┘
         │          Closes                           │
         ▼                                      Esc  │  Esc
    ┌──────────┐                                ▲    │
    │ Playing  │────────────────────────────────┘    │
    └────┬─────┘                                     │
         │ Player dies                               │
         ▼                                           │
    ┌──────────┐   Retry                             │
    │ GameOver │──────────► Loading ──► Playing       │
    └──────────┘                                     │
         │ Quit                                      │
         └───────────────────────────────────────────┘
                         (back to Main Menu)
```

---

## What This Doesn't Do (Yet)

- **Main Menu rendering** — covered in Chapter 22
- **Loading screen** — a state that displays a progress bar while loading assets
- **Settings screen** — adjust volume, sensitivity, keybinds

These are all just more `GameState` subclasses following the same pattern.

---

## ECS Note

The state machine is **not** ECS — and it shouldn't be. States control the overall program flow: which systems run, what's on screen, what input does. They're the **context** in which ECS operates.

Think of it this way:
- **GameState** decides: "We're playing the game right now"
- **ECS systems** do the actual work: physics, rendering, AI
- **Components** hold the data: positions, health, velocity

The state machine sits above ECS in the architecture. Each state decides which ECS systems to call during its `update()` and `render()`.

---

## What's Next

In **Chapter 22**, we'll build the MainMenuState — a proper title screen with options, and a loading state that transitions into gameplay.
