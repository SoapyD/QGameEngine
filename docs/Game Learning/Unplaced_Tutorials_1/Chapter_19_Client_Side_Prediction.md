# Chapter 19: Client-Side Prediction

## What You'll Learn
- Why prediction is necessary
- Input buffering and replay
- Server reconciliation — correcting prediction errors
- Lag compensation for shooting
- The complete netcode pipeline

---

## The Problem

Without prediction, pressing W to move forward would:

1. Send input to server (~30ms)
2. Server processes it, sends snapshot back (~30ms)
3. Client receives and applies the result

Total: ~60ms of input lag. At 100ms ping (common), you'd feel 100ms delay on every keypress. This makes a game feel sluggish and unplayable for an FPS.

**Prediction** solves this: the client immediately applies its own input locally, without waiting for the server. When the server's response arrives, the client checks if it was right and corrects if needed.

---

## The Prediction Loop

```
1. Client presses W
2. Client IMMEDIATELY moves locally (prediction)
3. Client sends input to server
4. Server processes input, sends authoritative state back
5. Client receives state, checks if prediction was correct
6. If wrong: snap to server state and replay unacknowledged inputs
```

The player never feels latency on their own character. Other players are interpolated (Chapter 18) and have inherent visual latency — but your own character feels instant.

---

## Input Buffer

The client stores every input it sends, indexed by sequence number:

```cpp
struct PredictionState {
    static constexpr int BUFFER_SIZE = 128;

    struct InputEntry {
        uint32_t sequence;
        InputPacket input;
        glm::vec3 predictedPosition;   // Where we thought we'd be
        glm::vec3 predictedVelocity;
    };

    InputEntry buffer[BUFFER_SIZE];
    uint32_t oldestSequence = 0;
    uint32_t newestSequence = 0;

    void store(uint32_t sequence, const InputPacket& input,
               const glm::vec3& position, const glm::vec3& velocity) {
        int index = sequence % BUFFER_SIZE;
        buffer[index] = { sequence, input, position, velocity };
        newestSequence = sequence;
        if (sequence >= BUFFER_SIZE) {
            oldestSequence = sequence - BUFFER_SIZE + 1;
        }
    }

    InputEntry* get(uint32_t sequence) {
        if (sequence < oldestSequence || sequence > newestSequence) {
            return nullptr;
        }
        return &buffer[sequence % BUFFER_SIZE];
    }
};
```

---

## Applying Prediction Locally

When the client gathers input, it immediately simulates the result:

```cpp
void predictLocalPlayer(entt::registry& registry, entt::entity localPlayer,
                          const InputPacket& input, PredictionState& prediction,
                          const Level& level, float dt) {

    auto& pos = registry.get<Position>(localPlayer);
    auto& vel = registry.get<Velocity>(localPlayer);

    // Apply the same movement logic the server would use
    glm::vec3 wishDir(input.moveX, 0.0f, input.moveZ);
    if (glm::length(wishDir) > 0.001f) {
        wishDir = glm::normalize(wishDir);

        // Rotate wish direction by camera yaw
        float yawRad = glm::radians(input.yaw);
        glm::vec3 rotated;
        rotated.x = wishDir.x * cos(yawRad) - wishDir.z * sin(yawRad);
        rotated.y = 0.0f;
        rotated.z = wishDir.x * sin(yawRad) + wishDir.z * cos(yawRad);
        wishDir = rotated;
    }

    // Apply acceleration (same as server — must be IDENTICAL)
    bool onGround = registry.all_of<OnGround>(localPlayer) &&
                     registry.get<OnGround>(localPlayer).value;
    float maxSpeed = onGround ? 7.0f : 1.0f;
    float accel = onGround ? 10.0f : 10.0f;

    if (glm::length(wishDir) > 0.001f) {
        applyAcceleration(vel.value, wishDir, maxSpeed, accel, dt);
    }

    // Gravity
    if (!onGround) {
        vel.value.y -= 20.0f * dt;
    }

    // Jump
    if (input.jump && onGround) {
        vel.value.y = 8.0f;
    }

    // Friction
    if (onGround) {
        float speed = glm::length(glm::vec2(vel.value.x, vel.value.z));
        if (speed > 0.1f) {
            float drop = speed * 6.0f * dt;
            float newSpeed = std::max(speed - drop, 0.0f);
            float scale = newSpeed / speed;
            vel.value.x *= scale;
            vel.value.z *= scale;
        } else {
            vel.value.x = 0.0f;
            vel.value.z = 0.0f;
        }
    }

    // Movement (collision would go here too)
    pos.value += vel.value * dt;

    // Store prediction for later reconciliation
    prediction.store(input.sequence, input, pos.value, vel.value);
}
```

### Critical: Determinism

The client's prediction **must use identical physics logic** to the server. If the client uses slightly different acceleration, friction, or collision, predictions will always be wrong and the player will constantly get corrected (jittering).

This is the hardest constraint of prediction: the simulation must be deterministic and shared between client and server. In practice, this means extracting physics into shared code that both the server and client call.

---

## Server Reconciliation

When a snapshot arrives from the server, it tells the client:
- The authoritative position of the local player
- The sequence number of the last input the server processed

The client then:
1. Compares the server's position with its predicted position for that sequence
2. If they match (or are close enough): prediction was correct, do nothing
3. If they differ: snap to the server's position, then **replay** all inputs the server hasn't processed yet

```cpp
void reconcile(entt::registry& registry, entt::entity localPlayer,
                const EntityState& serverState, uint32_t serverLastInput,
                PredictionState& prediction, const Level& level, float dt) {

    auto& pos = registry.get<Position>(localPlayer);
    auto& vel = registry.get<Velocity>(localPlayer);

    // Check if server state matches our prediction for that input
    auto* predicted = prediction.get(serverLastInput);
    if (!predicted) {
        // We don't have this prediction anymore — just snap
        pos.value = serverState.position;
        vel.value = serverState.velocity;
        return;
    }

    float error = glm::length(serverState.position - predicted->predictedPosition);

    if (error < 0.01f) {
        // Prediction was correct — no correction needed
        return;
    }

    // ─── Prediction was wrong — correct ──────────────────────────

    // Snap to server state
    pos.value = serverState.position;
    vel.value = serverState.velocity;

    // Replay all inputs AFTER the server's last processed input
    for (uint32_t seq = serverLastInput + 1; seq <= prediction.newestSequence; seq++) {
        auto* entry = prediction.get(seq);
        if (!entry) continue;

        // Re-simulate this input from the corrected state
        // (same logic as predictLocalPlayer)
        glm::vec3 wishDir(entry->input.moveX, 0.0f, entry->input.moveZ);
        if (glm::length(wishDir) > 0.001f) {
            wishDir = glm::normalize(wishDir);
            float yawRad = glm::radians(entry->input.yaw);
            glm::vec3 rotated;
            rotated.x = wishDir.x * cos(yawRad) - wishDir.z * sin(yawRad);
            rotated.y = 0.0f;
            rotated.z = wishDir.x * sin(yawRad) + wishDir.z * cos(yawRad);
            wishDir = rotated;
        }

        bool onGround = vel.value.y >= -0.1f && vel.value.y <= 0.1f;
        float maxSpeed = onGround ? 7.0f : 1.0f;
        float accel = 10.0f;

        if (glm::length(wishDir) > 0.001f) {
            applyAcceleration(vel.value, wishDir, maxSpeed, accel, dt);
        }

        if (!onGround) vel.value.y -= 20.0f * dt;
        if (entry->input.jump && onGround) vel.value.y = 8.0f;

        pos.value += vel.value * dt;

        // Update the stored prediction with the corrected result
        entry->predictedPosition = pos.value;
        entry->predictedVelocity = vel.value;
    }
}
```

### Visualising Reconciliation

```
Client input sequence: 10  11  12  13  14  15  16  17
Client predicted pos:  A   B   C   D   E   F   G   H

Server says: "At input 13, position was D'"  (D' ≠ D — prediction wrong!)

Client:
  1. Snap to D'
  2. Replay input 14 from D' → E'
  3. Replay input 15 from E' → F'
  4. Replay input 16 from F' → G'
  5. Replay input 17 from G' → H'
  6. Now at H' (corrected position)
```

The player might see a small correction jitter if the error is large. Smoothing can reduce this:

```cpp
// Instead of snapping directly, blend toward the corrected position
glm::vec3 corrected = /* result of reconciliation */;
float blendFactor = 0.3f;  // Smooth over several frames
pos.value = glm::mix(pos.value, corrected, blendFactor);
```

---

## Lag Compensation for Shooting

When you click fire, you're aiming at where enemies appear on your screen. But those enemies are being interpolated from data that's already ~50-100ms old (interpolation delay + network latency). The enemy might have already moved.

**Lag compensation** solves this: when the server processes a shot, it rewinds time to where the target was when the shooter clicked.

### Server-Side Rewind

```cpp
struct PositionHistory {
    static constexpr int MAX_HISTORY = 128;

    struct Entry {
        uint32_t tick;
        float serverTime;
        glm::vec3 position;
        glm::vec3 halfExtents;
    };

    Entry history[MAX_HISTORY];
    int count = 0;
    int writeIndex = 0;

    void record(uint32_t tick, float time, const glm::vec3& pos,
                 const glm::vec3& extents) {
        history[writeIndex % MAX_HISTORY] = { tick, time, pos, extents };
        writeIndex++;
        count = std::min(count + 1, MAX_HISTORY);
    }

    // Get the position at a specific time (interpolate between entries)
    glm::vec3 positionAtTime(float time) const {
        // Find surrounding entries
        for (int i = 1; i < count; i++) {
            int idxA = (writeIndex - i - 1 + MAX_HISTORY) % MAX_HISTORY;
            int idxB = (writeIndex - i + MAX_HISTORY) % MAX_HISTORY;

            if (history[idxA].serverTime <= time &&
                history[idxB].serverTime >= time) {
                float range = history[idxB].serverTime - history[idxA].serverTime;
                float t = (range > 0.001f)
                    ? (time - history[idxA].serverTime) / range
                    : 0.0f;
                return glm::mix(history[idxA].position, history[idxB].position, t);
            }
        }

        // Default to latest
        return history[(writeIndex - 1 + MAX_HISTORY) % MAX_HISTORY].position;
    }
};
```

### Processing a Shot with Lag Compensation

```cpp
void processHitscanShot(entt::registry& registry, entt::entity shooter,
                          const glm::vec3& origin, const glm::vec3& direction,
                          float clientTime, float range) {

    Ray ray{ origin, direction };
    float closestDist = range;
    entt::entity hitEntity = entt::null;

    auto view = registry.view<PositionHistory, AABBCollider, Health>();

    for (auto [entity, posHistory, col, health] : view.each()) {
        if (entity == shooter) continue;

        // Rewind this entity to where the shooter SAW it
        glm::vec3 rewindPos = posHistory.positionAtTime(clientTime);
        AABB rewindBox = AABB::fromCenterSize(rewindPos, col.halfExtents);

        auto hit = rayIntersectsAABB(ray, rewindBox);
        if (hit.has_value() && hit.value() < closestDist) {
            closestDist = hit.value();
            hitEntity = entity;
        }
    }

    if (hitEntity != entt::null) {
        // Hit confirmed — apply damage at current time
        applyDamage(registry, hitEntity, 10.0f);
    }
}
```

The server checks the shot against where the target **was** when the shooter clicked, not where it **is now**. This means hits feel correct from the shooter's perspective.

### The Trade-Off

Lag compensation means a player might get hit even after they've ducked behind a wall — because on the shooter's screen, they were still visible. This is the classic "I was behind the wall!" complaint in online shooters. It's a fundamental trade-off: favour the shooter (hits feel responsive) or the target (dodging feels responsive). Most games favour the shooter.

---

## The Complete Client Frame

Putting it all together:

```cpp
// Client main loop
while (!window.shouldClose()) {
    float dt = getDeltaTime();

    // 1. Receive network data
    client.poll();

    // 2. Process latest snapshot
    if (newSnapshotReceived) {
        // Apply to other entities (interpolated — Chapter 18)
        pushToInterpolationBuffer(latestSnapshot);

        // Reconcile local player
        EntityState* myState = findMyState(latestSnapshot, myNetworkId);
        if (myState) {
            reconcile(registry, localPlayer, *myState,
                       latestSnapshot.lastInputSequence,
                       predictionState, level, FIXED_TIMESTEP);
        }
    }

    // 3. Gather and send input
    InputPacket input = gatherInput(window);
    input.sequence = inputSequence++;
    input.clientId = client.getClientId();
    sendInput(client, input);

    // 4. Predict locally
    predictLocalPlayer(registry, localPlayer, input,
                        predictionState, level, dt);

    // 5. Interpolate other entities
    float renderTime = clientTime - INTERPOLATION_DELAY;
    interpolateEntities(registry, interpBuffer, renderTime);

    // 6. Run local-only systems (audio, particles, HUD)
    audioSystem(registry, audio, camera, dt);

    // 7. Render
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderSystem(registry, camera, aspectRatio);
    // HUD...
    window.swapBuffers();
}
```

---

## Summary of the Netcode Pipeline

| Step | Where | What |
|------|-------|------|
| Input gathered | Client | Read keyboard/mouse |
| Input sent | Client → Server | Unreliable or reliable |
| Input predicted | Client | Apply locally for instant feel |
| Input processed | Server | Authoritative simulation |
| Snapshot built | Server | Serialise all entity states |
| Snapshot sent | Server → Client | Unreliable (delta compressed) |
| Other entities interpolated | Client | Smooth between snapshots |
| Local player reconciled | Client | Correct prediction errors |
| Shots lag-compensated | Server | Rewind time for hit detection |

---

## What's Next

In **Chapter 20**, we'll add the finishing touches — particle systems, muzzle flash, explosions, screen shake, and other effects that make the game feel polished and impactful.
