# Chapter 22: Main Menu & Pause Screen

## What You'll Learn
- Building a main menu using the state machine from Chapter 21
- Menu rendering reusing the HUD system from Chapter 15
- Button navigation with keyboard and mouse
- A loading state with transition
- Settings screen for volume, sensitivity, and resolution
- Wiring it all together into a complete game flow

---

## Main Menu State

The main menu is the first thing the player sees. It needs to:
1. Render a title and background
2. Show menu options (New Game, Settings, Quit)
3. Handle navigation (arrow keys / mouse)
4. Transition to gameplay or settings

### src/game/states/main_menu_state.h

```cpp
#pragma once

#include "engine/core/game_state.h"
#include <entt/entt.hpp>

class Window;
class InputManager;
class AudioManager;
class Camera;
class GameStateManager;

class MainMenuState : public GameState {
public:
    MainMenuState(entt::registry& registry, Window& window,
                   InputManager& input, AudioManager& audio,
                   Camera& camera, GameStateManager& stateManager);

    void enter() override;
    void update(float dt) override;
    void render() override;

    std::string getName() const override { return "MainMenu"; }

private:
    entt::registry& m_registry;
    Window& m_window;
    InputManager& m_input;
    AudioManager& m_audio;
    Camera& m_camera;
    GameStateManager& m_stateManager;

    int m_selectedOption = 0;

    static constexpr int OPTION_NEW_GAME = 0;
    static constexpr int OPTION_SETTINGS = 1;
    static constexpr int OPTION_QUIT = 2;
    static constexpr int OPTION_COUNT = 3;

    // Input debouncing
    bool m_keyStates[OPTION_COUNT + 2] = {};  // up, down, enter, escape

    // Background animation
    float m_time = 0.0f;

    void startNewGame();
};
```

### src/game/states/main_menu_state.cpp

```cpp
#include "game/states/main_menu_state.h"
#include "game/states/loading_state.h"
#include "game/states/settings_state.h"
#include "engine/core/window.h"
#include "engine/core/input_manager.h"
#include "engine/core/game_state_manager.h"

MainMenuState::MainMenuState(entt::registry& registry, Window& window,
                              InputManager& input, AudioManager& audio,
                              Camera& camera, GameStateManager& stateManager)
    : m_registry(registry), m_window(window), m_input(input),
      m_audio(audio), m_camera(camera), m_stateManager(stateManager) {}

void MainMenuState::enter() {
    m_selectedOption = 0;
    glfwSetInputMode(m_window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // Play menu music
    // m_audio.playLoop("menu_music", 0.5f);
}

void MainMenuState::update(float dt) {
    m_time += dt;

    // ─── Keyboard navigation (using InputManager from Chapter 5a) ─
    bool upNow = m_input.isKeyPressed(GLFW_KEY_UP) ||
                 m_input.isKeyPressed(GLFW_KEY_W);
    bool downNow = m_input.isKeyPressed(GLFW_KEY_DOWN) ||
                   m_input.isKeyPressed(GLFW_KEY_S);
    bool enterNow = m_input.isKeyPressed(GLFW_KEY_ENTER);
    bool escapeNow = m_input.isKeyPressed(GLFW_KEY_ESCAPE);

    // Edge detection — only trigger on press, not hold
    if (upNow && !m_keyStates[0]) {
        m_selectedOption = (m_selectedOption - 1 + OPTION_COUNT) % OPTION_COUNT;
        // m_audio.playSound("menu_move");
    }
    if (downNow && !m_keyStates[1]) {
        m_selectedOption = (m_selectedOption + 1) % OPTION_COUNT;
        // m_audio.playSound("menu_move");
    }
    if (enterNow && !m_keyStates[2]) {
        // m_audio.playSound("menu_select");
        switch (m_selectedOption) {
            case OPTION_NEW_GAME:
                startNewGame();
                return;
            case OPTION_SETTINGS:
                m_stateManager.pushState(std::make_unique<SettingsState>(
                    m_window, m_input, m_audio));
                return;
            case OPTION_QUIT:
                m_stateManager.clearStates();
                return;
        }
    }
    if (escapeNow && !m_keyStates[3]) {
        m_stateManager.clearStates();  // Quit from main menu
        return;
    }

    m_keyStates[0] = upNow;
    m_keyStates[1] = downNow;
    m_keyStates[2] = enterNow;
    m_keyStates[3] = escapeNow;

    // ─── Mouse navigation ───────────────────────────────────────
    double mouseX, mouseY;
    glfwGetCursorPos(m_window.getHandle(), &mouseX, &mouseY);

    float menuStartY = m_window.getHeight() / 2.0f;
    float itemHeight = 50.0f;

    for (int i = 0; i < OPTION_COUNT; i++) {
        float itemY = menuStartY + i * itemHeight;
        if (mouseY >= itemY && mouseY < itemY + itemHeight) {
            if (m_selectedOption != i) {
                m_selectedOption = i;
                // m_audio.playSound("menu_move");
            }

            // Click to select
            if (glfwGetMouseButton(m_window.getHandle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                // Same switch as enter key above
            }
        }
    }
}

void MainMenuState::render() {
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ─── Background ─────────────────────────────────────────────
    // Option 1: Static background image (a fullscreen textured quad)
    // Option 2: Animated — slowly panning camera over a demo level
    // Option 3: Simple gradient (what we'll do for now)

    // ─── Title ──────────────────────────────────────────────────
    // Render "QENGINE" in large text
    // Use TextRenderer from Chapter 15a:
    // font.renderText("QENGINE", centerX, titleY, 3.0f, glm::vec4(1.0f));

    // ─── Menu Options ───────────────────────────────────────────
    const char* options[] = { "New Game", "Settings", "Quit" };

    float centerX = m_window.getWidth() / 2.0f;
    float startY = m_window.getHeight() / 2.0f;

    for (int i = 0; i < OPTION_COUNT; i++) {
        glm::vec4 colour;
        float scale;

        if (i == m_selectedOption) {
            // Selected: bright yellow, slightly larger, with a pulse
            float pulse = 0.9f + 0.1f * sin(m_time * 4.0f);
            colour = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            scale = 2.0f * pulse;
        } else {
            colour = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
            scale = 1.5f;
        }

        // font.renderText(options[i], centerX, startY + i * 50.0f, scale, colour);
    }

    // ─── Footer ─────────────────────────────────────────────────
    // font.renderText("v0.1.0", 10.0f, screenHeight - 20.0f, 1.0f, grey);
}

void MainMenuState::startNewGame() {
    // Transition to loading screen, which will load the first map
    m_stateManager.changeState(std::make_unique<LoadingState>(
        m_registry, m_window, m_audio, m_camera, m_stateManager,
        "e1m1"  // First map
    ));
}
```

---

## Loading State

A transition state that displays a loading screen while assets and the map are loaded. Once loading is complete, it transitions to PlayingState.

### src/game/states/loading_state.h

```cpp
#pragma once

#include "engine/core/game_state.h"
#include <entt/entt.hpp>
#include <string>

class Window;
class AudioManager;
class Camera;
class GameStateManager;

class LoadingState : public GameState {
public:
    LoadingState(entt::registry& registry, Window& window,
                  AudioManager& audio, Camera& camera,
                  GameStateManager& stateManager,
                  const std::string& mapName);

    void enter() override;
    void update(float dt) override;
    void render() override;

    std::string getName() const override { return "Loading"; }

private:
    entt::registry& m_registry;
    Window& m_window;
    AudioManager& m_audio;
    Camera& m_camera;
    GameStateManager& m_stateManager;

    std::string m_mapName;
    float m_progress = 0.0f;
    std::string m_statusText = "Loading...";
    int m_loadStep = 0;
    bool m_loadComplete = false;

    void performLoadStep();
};
```

### src/game/states/loading_state.cpp

```cpp
#include "game/states/loading_state.h"
#include "game/states/playing_state.h"
#include "engine/core/window.h"
#include "engine/core/game_state_manager.h"
// #include "engine/level/level_loader.h"

LoadingState::LoadingState(entt::registry& registry, Window& window,
                            AudioManager& audio, Camera& camera,
                            GameStateManager& stateManager,
                            const std::string& mapName)
    : m_registry(registry), m_window(window), m_audio(audio),
      m_camera(camera), m_stateManager(stateManager), m_mapName(mapName) {}

void LoadingState::enter() {
    m_loadStep = 0;
    m_progress = 0.0f;
    m_loadComplete = false;
    m_statusText = "Loading " + m_mapName + "...";
}

void LoadingState::update(float dt) {
    if (m_loadComplete) {
        // Transition to playing
        m_stateManager.changeState(std::make_unique<PlayingState>(
            m_registry, m_window, m_audio, m_camera));
        return;
    }

    // Perform one load step per frame
    // This keeps the loading screen responsive (renders between steps)
    performLoadStep();
}

void LoadingState::performLoadStep() {
    switch (m_loadStep) {
        case 0:
            m_statusText = "Clearing previous level...";
            m_progress = 0.0f;
            // Clear the ECS registry
            m_registry.clear();
            break;

        case 1:
            m_statusText = "Loading map geometry...";
            m_progress = 0.2f;
            // loadLevel(m_registry, "assets/maps/" + m_mapName + ".map");
            break;

        case 2:
            m_statusText = "Loading textures...";
            m_progress = 0.4f;
            // Load map-specific textures
            break;

        case 3:
            m_statusText = "Spawning entities...";
            m_progress = 0.6f;
            // Spawn player, enemies, items from map entity data
            break;

        case 4:
            m_statusText = "Loading sounds...";
            m_progress = 0.8f;
            // Load map-specific audio
            break;

        case 5:
            m_statusText = "Ready!";
            m_progress = 1.0f;
            m_loadComplete = true;
            break;
    }

    m_loadStep++;
}

void LoadingState::render() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float screenW = static_cast<float>(m_window.getWidth());
    float screenH = static_cast<float>(m_window.getHeight());

    // ─── Map name ───────────────────────────────────────────────
    // font.renderText(m_mapName, screenW / 2, screenH / 2 - 60, 2.5f, white);

    // ─── Status text ────────────────────────────────────────────
    // font.renderText(m_statusText, screenW / 2, screenH / 2, 1.5f, grey);

    // ─── Progress bar ───────────────────────────────────────────
    float barWidth = 400.0f;
    float barHeight = 20.0f;
    float barX = (screenW - barWidth) / 2.0f;
    float barY = screenH / 2.0f + 40.0f;

    // Background (dark grey)
    // renderQuad(barX, barY, barWidth, barHeight, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));

    // Fill (green, width scaled by progress)
    // renderQuad(barX, barY, barWidth * m_progress, barHeight, glm::vec4(0.0f, 0.8f, 0.0f, 1.0f));
}
```

### Why One Step Per Frame?

If we loaded everything in one go during `enter()`, the screen would freeze — no rendering happens during a single function call. By spreading the load across multiple frames (one step per `update()`), the loading screen renders and updates the progress bar between steps.

For a production engine, you'd use a background thread for loading. But one-step-per-frame is simple and effective for QEngine's scale.

---

## Settings State

### src/game/states/settings_state.h

```cpp
#pragma once

#include "engine/core/game_state.h"

class Window;
class AudioManager;

struct GameSettings {
    float masterVolume = 1.0f;
    float sfxVolume = 1.0f;
    float musicVolume = 0.5f;
    float mouseSensitivity = 0.1f;
    bool fullscreen = false;
    int resolutionIndex = 0;  // Index into a list of resolutions
};

class SettingsState : public GameState {
public:
    SettingsState(Window& window, InputManager& input, AudioManager& audio);

    void enter() override;
    void update(float dt) override;
    void render() override;

    bool isTransparent() const override { return true; }
    std::string getName() const override { return "Settings"; }

    // Global settings — accessible from anywhere
    static GameSettings settings;

private:
    Window& m_window;
    InputManager& m_input;
    AudioManager& m_audio;

    int m_selectedOption = 0;
    bool m_escapeReleased = false;

    enum Option {
        OPT_MASTER_VOLUME,
        OPT_SFX_VOLUME,
        OPT_MUSIC_VOLUME,
        OPT_SENSITIVITY,
        OPT_FULLSCREEN,
        OPT_BACK,
        OPT_COUNT
    };
};
```

### src/game/states/settings_state.cpp

```cpp
#include "game/states/settings_state.h"
#include "engine/core/window.h"
#include "engine/core/game_state_manager.h"
#include "engine/audio/audio_manager.h"
#include <algorithm>

GameSettings SettingsState::settings;  // Static member definition

SettingsState::SettingsState(Window& window, InputManager& input, AudioManager& audio)
    : m_window(window), m_input(input), m_audio(audio) {}

void SettingsState::enter() {
    m_selectedOption = 0;
    m_escapeReleased = false;
}

void SettingsState::update(float dt) {
    if (!m_escapeReleased) {
        if (m_input.isKeyReleased(GLFW_KEY_ESCAPE)) {
            m_escapeReleased = true;
        }
        return;
    }

    // Back with Escape
    if (m_input.isKeyPressed(GLFW_KEY_ESCAPE)) {
        m_stateManager->popState();
        return;
    }

    // Navigation (with debounce)
    static bool keys[4] = {};
    bool up = m_input.isKeyPressed(GLFW_KEY_UP);
    bool down = m_input.isKeyPressed(GLFW_KEY_DOWN);
    bool left = m_input.isKeyPressed(GLFW_KEY_LEFT);
    bool right = m_input.isKeyPressed(GLFW_KEY_RIGHT);

    if (up && !keys[0]) m_selectedOption = (m_selectedOption - 1 + OPT_COUNT) % OPT_COUNT;
    if (down && !keys[1]) m_selectedOption = (m_selectedOption + 1) % OPT_COUNT;

    // Adjust values with left/right
    float step = 0.05f;
    bool changed = false;

    if ((left && !keys[2]) || (right && !keys[3])) {
        float dir = right ? 1.0f : -1.0f;

        switch (m_selectedOption) {
            case OPT_MASTER_VOLUME:
                settings.masterVolume = std::clamp(settings.masterVolume + dir * step, 0.0f, 1.0f);
                changed = true;
                break;
            case OPT_SFX_VOLUME:
                settings.sfxVolume = std::clamp(settings.sfxVolume + dir * step, 0.0f, 1.0f);
                changed = true;
                break;
            case OPT_MUSIC_VOLUME:
                settings.musicVolume = std::clamp(settings.musicVolume + dir * step, 0.0f, 1.0f);
                changed = true;
                break;
            case OPT_SENSITIVITY:
                settings.mouseSensitivity = std::clamp(
                    settings.mouseSensitivity + dir * 0.01f, 0.01f, 0.5f);
                break;
            case OPT_FULLSCREEN:
                settings.fullscreen = !settings.fullscreen;
                // TODO: apply fullscreen toggle to the window
                break;
            case OPT_BACK:
                if (right) {
                    m_stateManager->popState();
                    return;
                }
                break;
        }
    }

    keys[0] = up; keys[1] = down; keys[2] = left; keys[3] = right;
}

void SettingsState::render() {
    // Semi-transparent background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // renderQuad(0, 0, screenW, screenH, glm::vec4(0, 0, 0, 0.7f));

    float startY = 150.0f;
    float rowHeight = 45.0f;

    struct SettingsRow {
        const char* label;
        float value;       // -1 for non-slider items
        bool isBool;
        bool boolValue;
    };

    SettingsRow rows[] = {
        { "Master Volume",    settings.masterVolume,     false, false },
        { "SFX Volume",       settings.sfxVolume,        false, false },
        { "Music Volume",     settings.musicVolume,      false, false },
        { "Mouse Sensitivity", settings.mouseSensitivity, false, false },
        { "Fullscreen",       -1.0f,                     true,  settings.fullscreen },
        { "Back",             -1.0f,                     false, false },
    };

    for (int i = 0; i < OPT_COUNT; i++) {
        float y = startY + i * rowHeight;
        bool selected = (i == m_selectedOption);
        glm::vec4 colour = selected
            ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
            : glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);

        // Render label
        // font.renderText(rows[i].label, 200.0f, y, 1.5f, colour);

        // Render value
        if (rows[i].isBool) {
            // font.renderText(rows[i].boolValue ? "ON" : "OFF", 600.0f, y, 1.5f, colour);
        } else if (rows[i].value >= 0.0f) {
            // Render a slider bar
            float barX = 550.0f;
            float barW = 200.0f;
            float barH = 12.0f;
            // renderQuad(barX, y + 6, barW, barH, darkGrey);
            // renderQuad(barX, y + 6, barW * rows[i].value, barH, colour);

            // Percentage text
            // int pct = static_cast<int>(rows[i].value * 100);
            // font.renderText(std::to_string(pct) + "%", barX + barW + 20, y, 1.0f, colour);
        }
    }

    glDisable(GL_BLEND);
}
```

---

## Game Over State

Add this to `src/game/states/game_over_state.h`:

```cpp
class GameOverState : public GameState {
public:
    GameOverState(Window& window, GameStateManager& stateManager,
                   int finalScore, const std::string& causeOfDeath);

    void update(float dt) override;
    void render() override;

    bool isTransparent() const override { return true; }  // Show frozen game behind
    std::string getName() const override { return "GameOver"; }

private:
    Window& m_window;
    GameStateManager& m_stateManager;
    int m_score;
    std::string m_causeOfDeath;
    float m_fadeIn = 0.0f;
    int m_selectedOption = 0;  // 0 = Retry, 1 = Quit to Menu
};
```

The game over screen fades in (increasing alpha on the overlay), displays the score and cause of death, and offers Retry (reload the current map) or Quit to Menu.

---

## C++ Concept: `static` Members

```cpp
static GameSettings settings;
```

A `static` member belongs to the **class**, not to any particular instance. There's exactly one `GameSettings settings` shared across the entire program. Any code can access it via `SettingsState::settings`.

This is appropriate for global settings — there's only one set of volume/sensitivity settings, and multiple systems need to read them (the audio system reads volume, the camera reads sensitivity, the window reads resolution).

---

## The Complete State Flow

```
Application starts
    │
    ▼
MainMenuState::enter()
    │
    ├── "New Game" → LoadingState → PlayingState
    │                                    │
    │                    Escape ──► PauseState (transparent overlay)
    │                                    │
    │                    Resume ──► pop PauseState, back to Playing
    │                    Quit   ──► MainMenuState
    │                                    │
    │                    Death  ──► GameOverState (transparent overlay)
    │                                    │
    │                    Retry  ──► LoadingState → PlayingState
    │                    Quit   ──► MainMenuState
    │
    ├── "Settings" → SettingsState (transparent overlay on menu)
    │                    │
    │                    Back ──► pop, back to MainMenuState
    │
    └── "Quit" → clearStates() → isEmpty() → application exits
```

---

## What's Next

In **Chapter 23**, we'll build a save/load system — serialising the entire ECS registry to disk so the player can resume where they left off.
