# QEngine — Multiplayer Infrastructure & Co-op Roadmap

Covers everything above the netcode layer: lobbies, server browsers, matchmaking, co-op campaign, NAT traversal, and the web services that support them. Some of this lives in the engine, some is a separate project.

---

## Architecture Overview

The networking chapters (17-19) handle the **game session** — once players are connected, everything works. This roadmap handles everything that happens **before and around** the game session:

```
┌─────────────────────────────────────────────────────────┐
│                    Player's Machine                      │
│                                                         │
│  QEngine Client ──── ENet (UDP) ──── Game Server        │
│       │                                    │            │
│       └──── HTTPS ──── Master Server ──────┘            │
│                        (web service)                    │
└─────────────────────────────────────────────────────────┘

Master Server (separate project):
  - Lobby management
  - Server browser / registration
  - NAT punch-through relay
  - Player accounts (optional)
```

Two distinct projects:
1. **QEngine** (C++) — the game client and game server, extended with lobby UI and co-op
2. **QMaster** (C++ or Python) — a lightweight web service for server discovery and matchmaking

---

## Part 1: Co-op Campaign (Engine-Side)

This is the highest-value feature and lives entirely in QEngine. No separate project needed.

### Phase 1.1: Host/Join Model

The simplest multiplayer model — one player **hosts**, others **join** by IP.

```
Host presses "Host Game"
  → QEngine starts a local game server on port 7777
  → QEngine also connects as a client to localhost:7777
  → Host plays the game as normal

Friend presses "Join Game", enters host's IP
  → QEngine connects to host:7777
  → Joins the session as player 2/3/4
```

#### New Components

```cpp
// Replaces single-player TagPlayer
struct PlayerInfo {
    uint8_t playerIndex;       // 0-3 for 4-player co-op
    uint32_t networkId;        // Network entity ID
    std::string name;          // Display name
    bool isHost = false;
};

// Session configuration
struct SessionConfig {
    std::string mapName;
    int maxPlayers = 4;
    bool friendlyFire = false;
    int difficulty = 1;        // 0=easy, 1=normal, 2=hard
    bool isCoOp = true;        // vs deathmatch
};
```

#### What Changes from Solo

| System | Solo | Co-op |
|--------|------|-------|
| Player spawning | 1 spawn point | Multiple spawn points (`info_player_start`, `info_player_start2`, etc.) |
| Camera | Follows the one player | Each client follows its own player |
| AI targeting | Always targets the player | Targets nearest/most threatening player |
| Health/Death | Game over on death | Respawn after delay, or bleed-out + revive |
| Pickups | Instant grab | First-touch grabs, or shared pool |
| Progression | Single player triggers | Host controls level progression, synced to all |
| Difficulty | Balanced for 1 | Scale enemy count/health by player count |

#### Difficulty Scaling

```cpp
float difficultyMultiplier(int playerCount) {
    // More players = more enemies and tougher enemies
    // But not linear — 4 players shouldn't face 4x enemies
    switch (playerCount) {
        case 1: return 1.0f;
        case 2: return 1.5f;
        case 3: return 1.8f;
        case 4: return 2.0f;
        default: return 1.0f;
    }
}

// When spawning a wave:
int enemyCount = baseCount * difficultyMultiplier(playerCount);
float enemyHealth = baseHealth * (1.0f + 0.15f * (playerCount - 1));
```

### Phase 1.2: Death & Revive System

Co-op needs a death model that isn't "game over":

```cpp
struct DownedState {
    float bleedOutTimer;       // Time until permanent death
    float bleedOutDuration = 30.0f;
    bool beingRevived = false;
    float reviveProgress = 0.0f;
    float reviveTime = 3.0f;   // Seconds to hold interact
};
```

**Flow:**
1. Player health reaches 0 → enters Downed state (can't move, can look around)
2. Teammate approaches + holds interact → revive progress fills
3. If revive completes → player stands up with 30% health
4. If bleed-out timer expires → permanent death (respawn at next checkpoint)
5. If all players are downed → mission failed, restart from checkpoint

### Phase 1.3: Level Progression Sync

The host controls level flow. All players must be in the exit zone to advance:

```cpp
struct LevelExit {
    AABB triggerVolume;
    std::string nextMap;
    int playersInZone = 0;
    int playersRequired;       // Set to session player count
};

// In trigger system:
void levelExitSystem(entt::registry& registry, const SessionConfig& session) {
    auto view = registry.view<LevelExit>();
    for (auto [entity, exit] : view.each()) {
        // Count players in the zone
        exit.playersInZone = countPlayersInVolume(registry, exit.triggerVolume);

        if (exit.playersInZone >= exit.playersRequired) {
            // All players at exit — load next map
            loadMap(exit.nextMap);
        }
    }
}
```

### Phase 1.4: Co-op UI

Additions to the HUD (Chapter 15):
- Player name labels above each co-op partner
- Health bars for all players (small, at screen edge)
- Revive prompt when near a downed player
- "Waiting for players" at level exit showing who's missing

---

## Part 2: Lobby System (Engine + Master Server)

### Phase 2.1: In-Game Lobby (Engine-Side)

Before the game starts, players gather in a lobby:

```
┌────────────────────────────────────────┐
│           QEngine Co-op Lobby          │
│                                        │
│  Map: E1M1 - The Slipgate Complex      │
│  Difficulty: Normal                    │
│  Friendly Fire: Off                    │
│                                        │
│  Players:                              │
│    1. PlayerOne (Host)      ✓ Ready    │
│    2. FragMaster            ✓ Ready    │
│    3. BunnyHopper           ○ Not Ready│
│    4. (empty)                          │
│                                        │
│  [Start Game]  [Settings]  [Leave]     │
└────────────────────────────────────────┘
```

#### Lobby State

```cpp
enum class LobbyState {
    WaitingForPlayers,
    Countdown,       // All ready — 3 second countdown
    Loading,         // Map loading
    InGame
};

struct LobbyPlayer {
    uint32_t networkId;
    std::string name;
    bool ready = false;
    uint8_t team = 0;          // For team modes
    uint8_t characterSkin = 0;
};

struct Lobby {
    LobbyState state = LobbyState::WaitingForPlayers;
    SessionConfig config;
    std::vector<LobbyPlayer> players;
    float countdownTimer = 3.0f;
};
```

#### Lobby Packets

```cpp
enum class LobbyPacketType : uint8_t {
    PlayerJoined,      // Server → all: new player info
    PlayerLeft,        // Server → all: player disconnected
    PlayerReady,       // Client → server: toggle ready
    ChatMessage,       // Bidirectional: text chat
    ChangeMap,         // Host → server: change map selection
    ChangeSettings,    // Host → server: change difficulty, etc.
    StartCountdown,    // Server → all: everyone ready, starting
    CancelCountdown,   // Server → all: someone unreadied
    LoadMap,           // Server → all: begin loading
    MapLoaded,         // Client → server: finished loading
    AllLoaded,         // Server → all: everyone loaded, start game
};
```

### Phase 2.2: Text Chat

Simple reliable packets with a string payload:

```cpp
struct ChatMessage {
    uint8_t playerIndex;
    std::string message;       // Max 256 chars
};

// Render in lobby and in-game HUD (bottom-left, fades after 5 seconds)
```

This is trivial to implement — a reliable ENet packet with a type byte and a length-prefixed string. The HUD renders it as timed text entries.

---

## Part 3: Master Server (Separate Project — QMaster)

This is a **separate executable** — a lightweight web service that game servers register with and clients query. It doesn't touch game logic.

### Why a Separate Project

The master server runs on a cloud VPS (or your home server). It's always online. It does three things:
1. **Server registration** — game servers announce themselves
2. **Server browser** — clients query for available games
3. **NAT punch-through** — helps players behind routers connect

### Technology Choice

Two options:

**Option A: C++ with cpp-httplib (stays in the C++ ecosystem)**
- Single-header HTTP library
- Lightweight, no dependencies
- Good learning exercise
- Stores data in-memory (no database needed for this scale)

**Option B: Python with Flask/FastAPI (simpler, faster to build)**
- Easier to deploy
- Better for web stuff
- Can always rewrite in C++ later

The roadmap assumes Option A (C++) since the tutorials teach C++.

### Phase 3.1: Server Registration

Game servers send a heartbeat every 30 seconds:

```
POST /api/servers/heartbeat
{
    "name": "Tom's Co-op Server",
    "map": "e1m1",
    "players": 2,
    "maxPlayers": 4,
    "gameMode": "coop",
    "version": "0.1.0",
    "port": 7777,
    "ip": <detected from request>
}
```

The master server stores this in memory. Servers that miss 3 heartbeats are removed.

```cpp
// QMaster — in-memory server list
struct GameServerEntry {
    std::string name;
    std::string ip;
    uint16_t port;
    std::string map;
    int players;
    int maxPlayers;
    std::string gameMode;
    std::string version;
    std::chrono::steady_clock::time_point lastHeartbeat;
};

std::vector<GameServerEntry> serverList;
std::mutex serverListMutex;

// Prune dead servers every 10 seconds
void pruneServers() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::seconds(90);  // 3 missed heartbeats
    serverList.erase(
        std::remove_if(serverList.begin(), serverList.end(),
            [&](const GameServerEntry& s) { return s.lastHeartbeat < cutoff; }),
        serverList.end()
    );
}
```

### Phase 3.2: Server Browser

Clients query the master server for available games:

```
GET /api/servers?gameMode=coop&hasSlots=true

Response:
[
    {
        "name": "Tom's Co-op Server",
        "ip": "203.0.113.45",
        "port": 7777,
        "map": "e1m1",
        "players": 2,
        "maxPlayers": 4,
        "gameMode": "coop",
        "ping": null
    },
    ...
]
```

The client then pings each server directly (UDP) to measure latency and displays:

```
┌──────────────────────────────────────────────────────────┐
│                    Server Browser                        │
│                                                          │
│  Name                    Map     Players  Mode   Ping    │
│  ───────────────────────────────────────────────────────  │
│  Tom's Co-op Server      e1m1    2/4      Co-op   35ms  │
│  Frag Palace             e1m3    6/16     DM      52ms  │
│  Newbie Friendly         e1m1    1/4      Co-op   78ms  │
│                                                          │
│  [Join]  [Refresh]  [Filter]  [Direct Connect]  [Back]  │
└──────────────────────────────────────────────────────────┘
```

### Phase 3.3: QMaster Project Structure

```
QMaster/
├── CMakeLists.txt
├── extern/
│   └── httplib/
│       └── httplib.h          ← cpp-httplib (single header)
├── src/
│   ├── main.cpp               ← HTTP server entry point
│   ├── server_list.h          ← In-memory server storage
│   ├── server_list.cpp
│   ├── routes.h               ← API endpoint handlers
│   └── routes.cpp
└── README.md
```

The entire master server is ~300-400 lines of C++. It's a single executable you run on any machine with a public IP.

---

## Part 4: NAT Traversal

The hardest part of peer-to-peer-style hosting. When the host is behind a home router, other players can't connect directly.

### The Problem

```
Player A (host)                    Player B (joining)
  192.168.1.5:7777                   192.168.1.10
       │                                   │
  ┌────┴────┐                         ┌────┴────┐
  │ Router A │  ← NAT blocks          │ Router B │
  │ Public:  │    inbound              │ Public:  │
  │ 1.2.3.4  │    connections          │ 5.6.7.8  │
  └──────────┘                         └──────────┘
       │                                   │
       └───────── Internet ────────────────┘
          Player B can't reach 192.168.1.5
```

### Solutions (from simplest to most robust)

#### Option 1: Port Forwarding (manual, no code needed)
The host forwards port 7777 on their router to their PC. Document this in a setup guide. This is what most Quake servers do.

#### Option 2: UPnP (automatic port forwarding)
Use the miniupnpc library to automatically request a port forward:

```cpp
// Using miniupnpc
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

bool openPort(uint16_t port) {
    UPNPDev* devList = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, nullptr);
    if (!devList) return false;

    UPNPUrls urls;
    IGDdatas data;
    char lanAddr[64];

    if (UPNP_GetValidIGD(devList, &urls, &data, lanAddr, sizeof(lanAddr)) != 1) {
        freeUPNPDevlist(devList);
        return false;
    }

    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%d", port);

    int result = UPNP_AddPortMapping(
        urls.controlURL, data.first.servicetype,
        portStr, portStr, lanAddr, "QEngine", "UDP", nullptr, "0"
    );

    FreeUPNPUrls(&urls);
    freeUPNPDevlist(devList);
    return result == UPNPCOMMAND_SUCCESS;
}
```

Works on most home routers. Some have UPnP disabled — falls back to manual port forwarding.

#### Option 3: UDP Hole Punching (via master server)
The master server acts as a relay to help both players' routers open a path:

```
1. Host registers with master server (master sees host's public IP:port)
2. Joiner asks master server to connect them to the host
3. Master tells both sides to send a UDP packet to each other's public IP:port
4. Both routers see outbound traffic → create NAT mapping
5. Subsequent packets flow directly between players
```

This works for most NAT types but fails with "symmetric NAT" (some corporate networks).

#### Option 4: Relay Server (guaranteed, but adds latency)
If hole punching fails, route game traffic through the master server:

```
Host ←→ Master Server ←→ Joiner
```

Adds latency (the master server's round-trip) but always works. The master server forwards UDP packets verbatim — it doesn't understand game protocol, just relays bytes.

### Recommended Approach

```
Try UPnP first (automatic, works for hosting)
  ↓ fails
Try UDP hole punching (works for most home connections)
  ↓ fails
Fall back to relay (always works, slightly more latency)
  ↓ fails
Show "Direct Connect" instructions with port forwarding guide
```

---

## Part 5: Game Modes

With the lobby and networking in place, game modes are just different system configurations.

### 5.1: Co-op Campaign (from Part 1)
- Shared progression through levels
- Revive system
- Difficulty scaling
- All players at exit to advance

### 5.2: Deathmatch (Free-for-All)
- Reuse existing weapon/damage systems from Ch 12
- Add respawning: on death → 3 second delay → spawn at random spawn point
- Score tracking: kills, deaths, K/D ratio
- Match timer or frag limit
- Weapon/item respawn timers (already in Ch 13)

```cpp
struct DeathmatchState {
    int fragLimit = 20;        // First to 20 kills wins
    float timeLimit = 600.0f;  // 10 minutes
    float timer = 0.0f;
    std::unordered_map<uint32_t, int> kills;
    std::unordered_map<uint32_t, int> deaths;
};
```

### 5.3: Team Deathmatch
- Same as DM but with team assignment
- Friendly fire toggle
- Team score tracking
- Team-coloured player models

```cpp
struct TeamInfo {
    uint8_t team;              // 0 = red, 1 = blue
    // Collision mask: don't damage teammates (unless friendly fire on)
};
```

### 5.4: Survival / Horde Mode
- Co-op but in a single arena
- Waves of enemies (reuse WaveSpawner from top-down roadmap)
- Escalating difficulty
- No level progression — survive as long as possible
- Shared score

This is the easiest mode to implement — it's just a single level with the wave system.

---

## Implementation Order

### Minimum Viable Multiplayer (do these first)

```
1. Host/Join by IP          ← Direct connect, no master server needed
2. Co-op lobby              ← Ready up, pick map, start game
3. Co-op death/revive       ← Don't just game-over when one player dies
4. Difficulty scaling        ← 4 players shouldn't face solo enemy counts
5. Level progression sync   ← Everyone at the exit to advance
6. Text chat                ← Players need to communicate
```

After this, you have a working co-op game over LAN or direct IP.

### Server Discovery (do these next)

```
7. QMaster project setup    ← Separate C++ project with cpp-httplib
8. Server registration      ← Game servers announce to master
9. Server browser UI        ← In-game list of available servers
10. Server pinging          ← Show latency in the browser
```

After this, players can find games without sharing IP addresses.

### NAT Traversal (do these last)

```
11. UPnP auto port forward ← Works for most hosts
12. UDP hole punching       ← Works for most joiners
13. Relay fallback          ← Always works
```

After this, hosting "just works" for most players.

### Extra Game Modes (anytime after step 6)

```
14. Deathmatch              ← Reuses everything, just different scoring
15. Team Deathmatch         ← Adds team assignment
16. Horde/Survival mode     ← Single arena, wave spawner
```

---

## Project Summary

| Project | Language | What It Does | Deployment |
|---------|----------|-------------|------------|
| QEngine | C++ | Game client + game server (host) | Player's machine |
| QMaster | C++ | Server browser, NAT relay | Cloud VPS or home server |

### Dependency Additions

| Library | Purpose | Where |
|---------|---------|-------|
| cpp-httplib | HTTP client (QEngine → QMaster) and server (QMaster) | Both projects |
| miniupnpc | UPnP port forwarding | QEngine only |
| nlohmann/json | JSON parsing for API responses | Both projects |

All three are header-only or small C/C++ libraries. No heavyweight frameworks.

### Total Scope Estimate

| Phase | New Files | Complexity |
|-------|-----------|------------|
| Co-op gameplay (1.1-1.4) | ~5-6 | Medium — mostly new components + systems |
| Lobby system (2.1-2.2) | ~3-4 | Medium — new game state + packets |
| QMaster (3.1-3.3) | ~5-6 (separate project) | Low-Medium — small HTTP server |
| NAT traversal (4) | ~2-3 | Hard — networking edge cases |
| Game modes (5) | ~2-3 per mode | Low — reuses existing systems |

Co-op campaign through server browser is roughly the same scope as one of the original tutorial phases (4-5 chapters worth of work). NAT traversal is the only genuinely difficult part.
