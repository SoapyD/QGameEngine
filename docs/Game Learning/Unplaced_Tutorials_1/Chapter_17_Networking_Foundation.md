# Chapter 17: Networking Foundation

## What You'll Learn
- Client-server architecture — why not peer-to-peer
- UDP vs TCP and why games use UDP
- ENet — a reliable UDP library
- Setting up a server and client
- Packet definitions and serialisation
- The game loop split: server tick vs client tick
- Connecting, disconnecting, and basic message passing

---

## Why Client-Server?

Doom used **peer-to-peer**: every player ran the game and sent their inputs to all others. If one player lagged, everyone waited. Quake switched to **client-server**: one machine is the authority, clients just send inputs and receive state.

| | Peer-to-Peer | Client-Server |
|---|---|---|
| Authority | Everyone (must agree) | Server only |
| Cheating | Hard to prevent | Server validates everything |
| Latency | Worst player's lag affects all | Only affects that player |
| Scaling | Gets worse with more players | Linear (server handles all) |
| Complexity | Simpler to start | More complex but more robust |

Every modern FPS uses client-server. The server is the single source of truth.

---

## UDP vs TCP

| | TCP | UDP |
|---|---|---|
| Delivery | Guaranteed, ordered | Not guaranteed, not ordered |
| Latency | Higher (waits for retransmission) | Lower (just sends) |
| Use in games | Chat, file transfer, login | Game state, inputs, position |

Games need low latency above all else. If a position update is lost, we don't want TCP to stall waiting for retransmission — the next update will arrive with newer data anyway.

But some messages (chat, "player joined", score updates) must be reliable. We need **both** reliable and unreliable channels.

---

## ENet

ENet provides exactly this: reliable UDP. It gives you:
- **Reliable ordered** channels (like TCP but over UDP)
- **Unreliable** channels (raw UDP)
- Connection management (connect, disconnect, timeout)
- Packet fragmentation (splits large messages automatically)

### Setup

```bash
git submodule add https://github.com/lsalzman/enet.git extern/enet
```

Add to CMakeLists.txt:
```cmake
add_subdirectory(extern/enet)
target_link_libraries(QEngine PRIVATE ... enet)
```

On Windows, ENet also needs:
```cmake
if(WIN32)
    target_link_libraries(QEngine PRIVATE ws2_32 winmm)
endif()
```

---

## The Network Architecture

```
┌─────────────────────────────────────────────┐
│                   SERVER                      │
│                                               │
│  Receives inputs from all clients             │
│  Runs authoritative game simulation           │
│  Sends game state snapshots to all clients    │
│                                               │
└──────────┬──────────────────┬────────────────┘
           │                  │
      Network            Network
           │                  │
┌──────────▼───────┐  ┌──────▼──────────────┐
│    CLIENT A       │  │    CLIENT B          │
│                   │  │                      │
│  Sends inputs     │  │  Sends inputs        │
│  Receives state   │  │  Receives state      │
│  Renders + predicts│  │  Renders + predicts  │
└───────────────────┘  └──────────────────────┘
```

---

## Packet Definitions

Every message between client and server needs a defined format. We'll use a simple binary protocol.

### src/engine/network/protocol.h

```cpp
#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

// ─── Packet Types ────────────────────────────────────────────────
enum class PacketType : uint8_t {
    // Client → Server
    ClientInput     = 1,   // Player's input for this tick
    ClientJoin      = 2,   // Request to join the game
    ClientLeave     = 3,   // Disconnect notification

    // Server → Client
    ServerSnapshot  = 10,  // Full game state
    ServerWelcome   = 11,  // Assign client ID, send initial state
    ServerPlayerJoin = 12, // Another player joined
    ServerPlayerLeave = 13,// Another player left
};

// ─── Client Input Packet ─────────────────────────────────────────
struct InputPacket {
    PacketType type = PacketType::ClientInput;
    uint32_t sequence;          // Input sequence number (for prediction)
    uint32_t clientId;
    float moveX;                // -1 to 1
    float moveZ;                // -1 to 1
    float yaw;                  // Camera yaw
    float pitch;                // Camera pitch
    bool jump;
    bool fire;
    uint8_t weaponSlot;
};

// ─── Entity State (part of a snapshot) ───────────────────────────
struct EntityState {
    uint32_t networkId;
    glm::vec3 position;
    glm::vec3 velocity;
    float yaw;
    float pitch;
    float health;
    uint8_t flags;              // Bitfield: on_ground, firing, etc.
};

// ─── Server Snapshot Packet ──────────────────────────────────────
struct SnapshotHeader {
    PacketType type = PacketType::ServerSnapshot;
    uint32_t tick;               // Server tick number
    uint32_t lastInputSequence;  // Last input processed for this client
    uint16_t entityCount;
    // Followed by entityCount * EntityState
};

// ─── Welcome Packet ──────────────────────────────────────────────
struct WelcomePacket {
    PacketType type = PacketType::ServerWelcome;
    uint32_t clientId;
    uint32_t networkId;          // The entity ID assigned to this player
};

// ─── Serialisation helpers ───────────────────────────────────────

// Write raw bytes into a buffer
class PacketWriter {
public:
    template<typename T>
    void write(const T& value) {
        size_t offset = m_data.size();
        m_data.resize(offset + sizeof(T));
        std::memcpy(m_data.data() + offset, &value, sizeof(T));
    }

    const uint8_t* data() const { return m_data.data(); }
    size_t size() const { return m_data.size(); }

private:
    std::vector<uint8_t> m_data;
};

// Read raw bytes from a buffer
class PacketReader {
public:
    PacketReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_offset(0) {}

    template<typename T>
    bool read(T& value) {
        if (m_offset + sizeof(T) > m_size) return false;
        std::memcpy(&value, m_data + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return true;
    }

    size_t remaining() const { return m_size - m_offset; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_offset;
};
```

### C++ Concept: `std::memcpy` and Serialisation

```cpp
std::memcpy(destination, &value, sizeof(T));
```

`memcpy` copies raw bytes. Serialisation (turning structs into bytes for network transfer) and deserialisation (bytes back to structs) are fundamental to networking.

**Why not just send the struct directly?** You can for simple cases (same compiler, same platform). But struct padding, endianness, and alignment can differ between machines. Our `PacketWriter`/`PacketReader` approach is explicit and portable.

### C++ Concept: `reinterpret_cast` (and why we avoid it)

> **Note:** This is a conceptual explanation only — do not add this code to your project. It explains an anti-pattern so you understand why we use `memcpy` instead.

You might see code like:
```cpp
InputPacket* packet = reinterpret_cast<InputPacket*>(data);
```

This casts raw bytes to a struct pointer. It's fast but dangerous:
- Alignment violations (crash on some CPUs)
- Undefined behaviour if the data doesn't match the struct layout
- Breaks if struct padding differs between sender and receiver

`memcpy` is always safe. The compiler optimises it to be just as fast.

---

## The Server

### src/engine/network/server.h

```cpp
#pragma once

#include <enet/enet.h>
#include <entt/entt.hpp>
#include <unordered_map>
#include <functional>

class GameServer {
public:
    GameServer();
    ~GameServer();

    bool start(uint16_t port, int maxClients = 16);
    void stop();

    // Process incoming packets (call every server tick)
    void poll();

    // Send a snapshot to all clients
    void broadcastSnapshot(const uint8_t* data, size_t size);

    // Send to a specific client
    void sendTo(uint32_t clientId, const uint8_t* data, size_t size,
                bool reliable = true);

    // Callbacks
    std::function<void(uint32_t clientId)> onClientConnect;
    std::function<void(uint32_t clientId)> onClientDisconnect;
    std::function<void(uint32_t clientId, const uint8_t* data, size_t size)> onPacket;

private:
    ENetHost* m_host = nullptr;
    std::unordered_map<uint32_t, ENetPeer*> m_clients;
    uint32_t m_nextClientId = 1;
};
```

### src/engine/network/server.cpp

```cpp
#include "engine/network/server.h"
#include <iostream>

GameServer::GameServer() {
    enet_initialize();
}

GameServer::~GameServer() {
    stop();
    enet_deinitialize();
}

bool GameServer::start(uint16_t port, int maxClients) {
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    m_host = enet_host_create(&address, maxClients, 2, 0, 0);
    // 2 channels: 0 = reliable (inputs, join/leave), 1 = unreliable (snapshots)

    if (!m_host) {
        std::cerr << "ERROR: Failed to create server on port " << port << std::endl;
        return false;
    }

    std::cout << "Server started on port " << port << std::endl;
    return true;
}

void GameServer::stop() {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_clients.clear();
}

void GameServer::poll() {
    if (!m_host) return;

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                uint32_t clientId = m_nextClientId++;
                m_clients[clientId] = event.peer;
                event.peer->data = reinterpret_cast<void*>(
                    static_cast<uintptr_t>(clientId));

                std::cout << "Client " << clientId << " connected" << std::endl;

                if (onClientConnect) {
                    onClientConnect(clientId);
                }
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT: {
                uint32_t clientId = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(event.peer->data));

                std::cout << "Client " << clientId << " disconnected" << std::endl;

                m_clients.erase(clientId);
                if (onClientDisconnect) {
                    onClientDisconnect(clientId);
                }
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                uint32_t clientId = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(event.peer->data));

                if (onPacket) {
                    onPacket(clientId, event.packet->data, event.packet->dataLength);
                }

                enet_packet_destroy(event.packet);
                break;
            }

            default:
                break;
        }
    }
}

void GameServer::broadcastSnapshot(const uint8_t* data, size_t size) {
    ENetPacket* packet = enet_packet_create(data, size, 0);  // Unreliable
    enet_host_broadcast(m_host, 1, packet);  // Channel 1 = unreliable
}

void GameServer::sendTo(uint32_t clientId, const uint8_t* data, size_t size,
                          bool reliable) {
    auto it = m_clients.find(clientId);
    if (it == m_clients.end()) return;

    uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    uint8_t channel = reliable ? 0 : 1;

    ENetPacket* packet = enet_packet_create(data, size, flags);
    enet_peer_send(it->second, channel, packet);
}
```

---

## The Client

### src/engine/network/client.h

```cpp
#pragma once

#include <enet/enet.h>
#include <string>
#include <functional>

class GameClient {
public:
    GameClient();
    ~GameClient();

    bool connect(const std::string& host, uint16_t port);
    void disconnect();

    // Process incoming packets (call every frame)
    void poll();

    // Send data to the server
    void send(const uint8_t* data, size_t size, bool reliable = true);

    bool isConnected() const { return m_connected; }
    uint32_t getClientId() const { return m_clientId; }

    // Callbacks
    std::function<void()> onConnect;
    std::function<void()> onDisconnect;
    std::function<void(const uint8_t* data, size_t size)> onPacket;

    uint32_t m_clientId = 0;

private:
    ENetHost* m_host = nullptr;
    ENetPeer* m_server = nullptr;
    bool m_connected = false;
};
```

### src/engine/network/client.cpp

```cpp
#include "engine/network/client.h"
#include <iostream>

GameClient::GameClient() {
    enet_initialize();
}

GameClient::~GameClient() {
    disconnect();
    enet_deinitialize();
}

bool GameClient::connect(const std::string& host, uint16_t port) {
    m_host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!m_host) {
        std::cerr << "ERROR: Failed to create client host" << std::endl;
        return false;
    }

    ENetAddress address;
    enet_address_set_host(&address, host.c_str());
    address.port = port;

    m_server = enet_host_connect(m_host, &address, 2, 0);
    if (!m_server) {
        std::cerr << "ERROR: Failed to connect to " << host
                  << ":" << port << std::endl;
        return false;
    }

    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    return true;
}

void GameClient::disconnect() {
    if (m_server) {
        enet_peer_disconnect(m_server, 0);

        // Wait for disconnect acknowledgement (with timeout)
        ENetEvent event;
        bool disconnected = false;
        while (enet_host_service(m_host, &event, 3000) > 0) {
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                disconnected = true;
                break;
            }
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }

        if (!disconnected) {
            enet_peer_reset(m_server);
        }

        m_server = nullptr;
    }

    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }

    m_connected = false;
}

void GameClient::poll() {
    if (!m_host) return;

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                m_connected = true;
                std::cout << "Connected to server" << std::endl;
                if (onConnect) onConnect();
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                m_connected = false;
                std::cout << "Disconnected from server" << std::endl;
                if (onDisconnect) onDisconnect();
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                if (onPacket) {
                    onPacket(event.packet->data, event.packet->dataLength);
                }
                enet_packet_destroy(event.packet);
                break;

            default:
                break;
        }
    }
}

void GameClient::send(const uint8_t* data, size_t size, bool reliable) {
    if (!m_server) return;

    uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    uint8_t channel = reliable ? 0 : 1;

    ENetPacket* packet = enet_packet_create(data, size, flags);
    enet_peer_send(m_server, channel, packet);
}
```

---

## The Game Loop Split

In single-player, we had one game loop. In multiplayer, there are two:

### Server Loop

```cpp
// Server runs at a fixed tick rate (e.g. 60 ticks/sec)
const float SERVER_TICK_RATE = 1.0f / 60.0f;
float serverAccumulator = 0.0f;

while (serverRunning) {
    float dt = getDeltaTime();
    serverAccumulator += dt;

    server.poll();  // Receive client inputs

    while (serverAccumulator >= SERVER_TICK_RATE) {
        // Process all queued inputs
        processClientInputs(registry);

        // Run authoritative simulation
        physicsSystem(registry, SERVER_TICK_RATE);
        collisionSystem(registry, spatialHash, level, SERVER_TICK_RATE);
        movementSystem(registry, SERVER_TICK_RATE);
        combatSystem(registry, level, SERVER_TICK_RATE);
        // ... other systems ...

        serverTick++;
        serverAccumulator -= SERVER_TICK_RATE;

        // Send snapshot to all clients
        sendSnapshot(server, registry, serverTick);
    }
}
```

### Client Loop

```cpp
while (!window.shouldClose()) {
    float dt = getDeltaTime();

    client.poll();  // Receive snapshots from server

    // Gather local input
    InputPacket input = gatherInput(window);
    input.sequence = inputSequence++;
    sendInput(client, input);

    // Apply latest snapshot from server
    applyLatestSnapshot(registry);

    // Predict locally (Chapter 19)
    // ...

    // Render
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderSystem(registry, camera, aspectRatio);
    window.swapBuffers();
}
```

The server runs the real game. The client sends inputs and renders what the server tells it. Chapters 18 and 19 fill in the details of snapshots and prediction.

---

## Sending an Input Packet

```cpp
void sendInput(GameClient& client, const InputPacket& input) {
    PacketWriter writer;
    writer.write(input.type);
    writer.write(input.sequence);
    writer.write(input.clientId);
    writer.write(input.moveX);
    writer.write(input.moveZ);
    writer.write(input.yaw);
    writer.write(input.pitch);
    writer.write(input.jump);
    writer.write(input.fire);
    writer.write(input.weaponSlot);

    client.send(writer.data(), writer.size(), true);  // Reliable
}
```

---

## Receiving and Processing on the Server

```cpp
server.onPacket = [&](uint32_t clientId, const uint8_t* data, size_t size) {
    PacketReader reader(data, size);
    PacketType type;
    reader.read(type);

    switch (type) {
        case PacketType::ClientInput: {
            InputPacket input;
            input.type = type;
            reader.read(input.sequence);
            reader.read(input.clientId);
            reader.read(input.moveX);
            reader.read(input.moveZ);
            reader.read(input.yaw);
            reader.read(input.pitch);
            reader.read(input.jump);
            reader.read(input.fire);
            reader.read(input.weaponSlot);

            input.clientId = clientId;  // Override with verified client ID
            inputQueue.push_back(input);
            break;
        }

        case PacketType::ClientJoin: {
            // Create a player entity for this client
            spawnPlayerForClient(registry, clientId);
            break;
        }

        default:
            break;
    }
};
```

**Security note**: The server sets `input.clientId = clientId` — never trust the client's self-reported ID. The server knows which connection sent the packet. This prevents players from sending inputs as someone else.

---

## Network IDs

In single-player, `entt::entity` is the entity ID. In multiplayer, each machine has its own registry with different entity IDs. We need a shared **network ID**:

```cpp
struct NetworkId {
    uint32_t id;
};

// Server assigns network IDs when creating entities
uint32_t nextNetworkId = 1;

entt::entity createNetworkedEntity(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<NetworkId>(entity, nextNetworkId++);
    return entity;
}

// Client looks up entities by network ID
entt::entity findByNetworkId(entt::registry& registry, uint32_t networkId) {
    auto view = registry.view<NetworkId>();
    for (auto [entity, netId] : view.each()) {
        if (netId.id == networkId) return entity;
    }
    return entt::null;
}
```

---

## What's Next

In **Chapter 18**, we'll implement state synchronisation — sending snapshots from the server and interpolating between them on the client. This is what makes other players move smoothly on your screen.
