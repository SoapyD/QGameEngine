# Chapter 18: State Synchronisation

## What You'll Learn
- Server snapshots — serialising the entire game state
- Snapshot history and delta compression
- Client-side interpolation — smoothly moving between snapshots
- Entity creation and destruction across the network
- Handling packet loss gracefully

---

## The Core Problem

The server runs the real game at 60 ticks per second. The client needs to know where everything is. We can't send 60 snapshots per second to every client — that's too much bandwidth. Instead:

1. Server sends snapshots at a **lower rate** (e.g. 20 per second)
2. Client **interpolates** between the two most recent snapshots
3. Result: smooth 60fps rendering from 20 updates per second

---

## Server Snapshots

A snapshot is the complete state of every networked entity at a specific server tick.

### Building a Snapshot

```cpp
void sendSnapshot(GameServer& server, entt::registry& registry,
                   uint32_t serverTick,
                   const std::unordered_map<uint32_t, uint32_t>& lastInputMap) {

    auto view = registry.view<NetworkId, Position, Velocity, Health>();

    // Count entities
    uint16_t entityCount = 0;
    for (auto entity : view) { entityCount++; }

    PacketWriter writer;

    // Header
    SnapshotHeader header;
    header.tick = serverTick;
    header.entityCount = entityCount;
    writer.write(header.type);
    writer.write(header.tick);
    // lastInputSequence is per-client — we'll handle that below
    writer.write(header.entityCount);

    // Entity states
    for (auto [entity, netId, pos, vel, health] : view.each()) {
        EntityState state;
        state.networkId = netId.id;
        state.position = pos.value;
        state.velocity = vel.value;
        state.health = health.current;
        state.flags = 0;

        // Pack rotation
        if (registry.all_of<Rotation>(entity)) {
            auto& rot = registry.get<Rotation>(entity);
            state.yaw = rot.euler.y;
            state.pitch = rot.euler.x;
        }

        if (registry.all_of<OnGround>(entity) &&
            registry.get<OnGround>(entity).value) {
            state.flags |= 0x01;  // Bit 0: on ground
        }

        writer.write(state);
    }

    // Broadcast to all clients
    server.broadcastSnapshot(writer.data(), writer.size());
}
```

### Snapshot Size

For each entity: 4 (netId) + 12 (pos) + 12 (vel) + 4 (yaw) + 4 (pitch) + 4 (health) + 1 (flags) = **41 bytes**.

For 16 players: 41 * 16 = 656 bytes + header = ~670 bytes per snapshot.
At 20 snapshots/second: ~13 KB/s outbound per client. Very manageable.

---

## Delta Compression

Sending full snapshots wastes bandwidth when most entities haven't changed. **Delta compression** only sends what changed since the last acknowledged snapshot.

### How It Works

1. Server keeps a history of recent snapshots
2. Server knows which snapshot each client last acknowledged
3. Server sends only the **differences** between the acknowledged snapshot and the current one

```cpp
struct SnapshotHistory {
    static constexpr int MAX_HISTORY = 64;

    struct StoredSnapshot {
        uint32_t tick;
        std::vector<EntityState> entities;
    };

    StoredSnapshot snapshots[MAX_HISTORY];
    int writeIndex = 0;

    void store(uint32_t tick, const std::vector<EntityState>& entities) {
        snapshots[writeIndex % MAX_HISTORY] = { tick, entities };
        writeIndex++;
    }

    const StoredSnapshot* find(uint32_t tick) const {
        for (int i = 0; i < MAX_HISTORY; i++) {
            if (snapshots[i].tick == tick) return &snapshots[i];
        }
        return nullptr;
    }
};
```

### Delta Encoding an Entity

```cpp
struct EntityDelta {
    uint32_t networkId;
    uint8_t changedFields;  // Bitfield: which fields changed
    // Only changed fields follow in the packet
};

// Field flags
constexpr uint8_t FIELD_POSITION = 0x01;
constexpr uint8_t FIELD_VELOCITY = 0x02;
constexpr uint8_t FIELD_YAW      = 0x04;
constexpr uint8_t FIELD_PITCH    = 0x08;
constexpr uint8_t FIELD_HEALTH   = 0x10;
constexpr uint8_t FIELD_FLAGS    = 0x20;

void writeDelta(PacketWriter& writer, const EntityState& current,
                 const EntityState& baseline) {
    uint8_t changed = 0;

    if (current.position != baseline.position)  changed |= FIELD_POSITION;
    if (current.velocity != baseline.velocity)  changed |= FIELD_VELOCITY;
    if (current.yaw != baseline.yaw)            changed |= FIELD_YAW;
    if (current.pitch != baseline.pitch)        changed |= FIELD_PITCH;
    if (current.health != baseline.health)      changed |= FIELD_HEALTH;
    if (current.flags != baseline.flags)        changed |= FIELD_FLAGS;

    writer.write(current.networkId);
    writer.write(changed);

    if (changed & FIELD_POSITION)  writer.write(current.position);
    if (changed & FIELD_VELOCITY)  writer.write(current.velocity);
    if (changed & FIELD_YAW)       writer.write(current.yaw);
    if (changed & FIELD_PITCH)     writer.write(current.pitch);
    if (changed & FIELD_HEALTH)    writer.write(current.health);
    if (changed & FIELD_FLAGS)     writer.write(current.flags);
}
```

If an entity is standing still and not taking damage, its delta is just 5 bytes (networkId + changed = 0x00) instead of 41 bytes. For a 16-player match where most players are stationary at any moment, this saves substantial bandwidth.

---

## Client-Side Interpolation

The client receives snapshots at ~20 per second but renders at 60+ fps. Between snapshots, we **interpolate** — blend smoothly from the previous snapshot's positions to the current one.

### The Interpolation Buffer

```cpp
struct InterpolationBuffer {
    struct Snapshot {
        uint32_t tick;
        float serverTime;
        std::vector<EntityState> entities;
    };

    static constexpr int BUFFER_SIZE = 32;
    Snapshot buffer[BUFFER_SIZE];
    int count = 0;
    int writeIndex = 0;

    void push(const Snapshot& snapshot) {
        buffer[writeIndex % BUFFER_SIZE] = snapshot;
        writeIndex++;
        count = std::min(count + 1, BUFFER_SIZE);
    }

    // Get the two snapshots to interpolate between
    // renderTime is slightly behind the latest server time (interpolation delay)
    bool getSurrounding(float renderTime, Snapshot*& from, Snapshot*& to) {
        // Find the two snapshots that bracket renderTime
        int latest = (writeIndex - 1 + BUFFER_SIZE) % BUFFER_SIZE;

        for (int i = 0; i < count - 1; i++) {
            int idxA = (latest - i - 1 + BUFFER_SIZE) % BUFFER_SIZE;
            int idxB = (latest - i + BUFFER_SIZE) % BUFFER_SIZE;

            if (buffer[idxA].serverTime <= renderTime &&
                buffer[idxB].serverTime >= renderTime) {
                from = &buffer[idxA];
                to = &buffer[idxB];
                return true;
            }
        }

        return false;  // Not enough data yet
    }
};
```

### Applying Interpolation

```cpp
void interpolateEntities(entt::registry& registry,
                          InterpolationBuffer& interpBuffer,
                          float renderTime) {
    InterpolationBuffer::Snapshot* from = nullptr;
    InterpolationBuffer::Snapshot* to = nullptr;

    if (!interpBuffer.getSurrounding(renderTime, from, to)) return;

    // Calculate interpolation factor (0.0 = from, 1.0 = to)
    float range = to->serverTime - from->serverTime;
    float t = (range > 0.001f)
        ? (renderTime - from->serverTime) / range
        : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);

    // Interpolate each entity
    for (const auto& toState : to->entities) {
        // Find matching entity in 'from' snapshot
        const EntityState* fromState = nullptr;
        for (const auto& fs : from->entities) {
            if (fs.networkId == toState.networkId) {
                fromState = &fs;
                break;
            }
        }

        if (!fromState) continue;  // New entity — just snap to position

        // Find the local entity
        entt::entity entity = findByNetworkId(registry, toState.networkId);
        if (entity == entt::null) continue;

        // Interpolate position
        if (registry.all_of<Position>(entity)) {
            registry.get<Position>(entity).value =
                glm::mix(fromState->position, toState.position, t);
        }

        // Interpolate rotation
        if (registry.all_of<Rotation>(entity)) {
            auto& rot = registry.get<Rotation>(entity);

            // Interpolate yaw (handle wrapping around 360)
            float yawDiff = toState.yaw - fromState->yaw;
            while (yawDiff > 180.0f) yawDiff -= 360.0f;
            while (yawDiff < -180.0f) yawDiff += 360.0f;
            rot.euler.y = fromState->yaw + yawDiff * t;

            rot.euler.x = glm::mix(fromState->pitch, toState.pitch, t);
        }

        // Health: snap (don't interpolate — it's either changed or it hasn't)
        if (registry.all_of<Health>(entity)) {
            registry.get<Health>(entity).current = toState.health;
        }
    }
}
```

### The Interpolation Delay

The client renders **slightly in the past** — typically one snapshot interval behind (50ms at 20 snapshots/sec). This ensures there's always a "from" and "to" snapshot to interpolate between.

```
Server time: ───────────────────────────────▶
              S1       S2       S3       S4
              ┆        ┆        ┆        ┆

Client render time: ─────────────────────▶
                         ↑
                    Rendering here
                    (between S2 and S3)
```

This adds 50ms of visual latency for other players. Your own player uses **prediction** (Chapter 19) to feel instant.

---

## Handling Entity Creation and Destruction

When a new entity appears in a snapshot that the client doesn't have:

```cpp
void applySnapshot(entt::registry& registry,
                    const std::vector<EntityState>& entities) {
    // Track which network IDs are in this snapshot
    std::unordered_set<uint32_t> activeIds;

    for (const auto& state : entities) {
        activeIds.insert(state.networkId);

        entt::entity entity = findByNetworkId(registry, state.networkId);

        if (entity == entt::null) {
            // New entity — create it
            entity = registry.create();
            registry.emplace<NetworkId>(entity, state.networkId);
            registry.emplace<Position>(entity, state.position);
            registry.emplace<Velocity>(entity, state.velocity);
            registry.emplace<Rotation>(entity,
                glm::vec3(state.pitch, state.yaw, 0.0f));
            registry.emplace<Health>(entity, state.health, state.health);

            // TODO: assign visual based on entity type
            // (need to include entity type in the network state)
        }
    }

    // Destroy entities no longer in the snapshot
    auto view = registry.view<NetworkId>();
    std::vector<entt::entity> toDestroy;

    for (auto [entity, netId] : view.each()) {
        if (activeIds.find(netId.id) == activeIds.end()) {
            toDestroy.push_back(entity);
        }
    }

    for (auto e : toDestroy) {
        registry.destroy(e);
    }
}
```

---

## Handling Packet Loss

UDP packets can be lost. Our system handles this gracefully:

- **Lost snapshot?** The client just interpolates between the two snapshots it does have. Movement might be slightly jerky for one frame.
- **Lost input?** The server doesn't receive the input. The client's prediction diverges from the server — corrected when the next snapshot arrives (Chapter 19).
- **Multiple lost snapshots?** The client might need to extrapolate (predict forward from the last known state) instead of interpolating. Extrapolation is less accurate but prevents freezing.

### Extrapolation (Fallback)

```cpp
// If we don't have two snapshots to interpolate between:
void extrapolate(entt::registry& registry, float dt) {
    auto view = registry.view<Position, Velocity>();
    for (auto [entity, pos, vel] : view.each()) {
        pos.value += vel.value * dt;
    }
}
```

This uses the last known velocity to predict where entities will be. It drifts quickly if the entity changes direction, so it's a last resort.

---

## C++ Concept: `std::deque`

```cpp
#include <deque>
std::deque<Snapshot> snapshotHistory;
```

A `deque` (double-ended queue) allows efficient insertion and removal at both ends. Useful for snapshot history where you push new snapshots on the back and remove old ones from the front:

```cpp
snapshotHistory.push_back(newSnapshot);
if (snapshotHistory.size() > MAX_HISTORY) {
    snapshotHistory.pop_front();
}
```

Unlike `std::vector`, `pop_front()` doesn't require shifting all elements.

---

## Bandwidth Summary

| Data | Size | Rate | Bandwidth |
|------|------|------|-----------|
| Client input | ~30 bytes | 60/sec | 1.8 KB/s per client |
| Full snapshot (16 players) | ~670 bytes | 20/sec | 13 KB/s per client |
| Delta snapshot (typical) | ~200 bytes | 20/sec | 4 KB/s per client |

For 16 clients: server outbound ~64-208 KB/s. Easily handled by modern connections.

---

## What's Next

In **Chapter 19**, we'll implement client-side prediction — making your own character feel instant despite network latency. This is the hardest part of netcode and what separates a sluggish online game from a responsive one.
