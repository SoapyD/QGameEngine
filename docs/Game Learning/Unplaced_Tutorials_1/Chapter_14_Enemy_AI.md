# Chapter 14: Enemy AI

## What You'll Learn
- Finite state machines for enemy behaviour
- Line-of-sight checks
- A* pathfinding — finding a route through the level
- Navigation meshes (nav meshes) — where enemies can walk
- Attack patterns and reaction times
- Spawning and managing enemy entities

---

## Quake's AI

Quake's enemies are simple but effective. Each enemy type has:
- A **state** (idle, chasing, attacking, pain, dying)
- A **think function** called every tick
- **Line-of-sight** checks to detect the player
- Simple **pathfinding** (mostly direct pursuit, some types use waypoints)

We'll build the same approach with ECS components and systems.

---

## AI Components

Add to `components.h`:

```cpp
enum class AIState {
    Idle,       // Standing still, hasn't seen the player
    Patrol,     // Walking between waypoints
    Chase,      // Pursuing the player
    Attack,     // In range, attacking
    Pain,       // Just took damage, brief stun
    Dead        // Playing death animation (before entity destruction)
};

struct AIBrain {
    AIState state = AIState::Idle;
    entt::entity target = entt::null;     // Who we're targeting

    float sightRange = 30.0f;             // How far we can see
    float attackRange = 2.0f;             // How close to attack (melee)
    float attackCooldown = 1.0f;          // Seconds between attacks
    float attackTimer = 0.0f;
    float damage = 10.0f;                 // Damage per attack

    float painDuration = 0.3f;            // How long pain stun lasts
    float painTimer = 0.0f;

    float moveSpeed = 4.0f;
    float turnSpeed = 360.0f;             // Degrees per second
};

struct PatrolRoute {
    std::vector<glm::vec3> waypoints;
    int currentWaypoint = 0;
    float waypointRadius = 0.5f;          // How close counts as "arrived"
};
```

---

## Enemy Types as Data

Different enemies are just different component values — no inheritance. Add this factory function to `src/engine/ecs/scene_setup.cpp` (or a dedicated `src/engine/ecs/enemy_factory.cpp`):

```cpp
entt::entity createEnemy(entt::registry& registry, const std::string& type,
                          const glm::vec3& position,
                          unsigned int meshVAO, unsigned int indexCount,
                          unsigned int shaderId, unsigned int textureId) {
    auto enemy = registry.create();

    registry.emplace<Position>(enemy, position);
    registry.emplace<Velocity>(enemy);
    registry.emplace<Rotation>(enemy);
    registry.emplace<Gravity>(enemy);
    registry.emplace<OnGround>(enemy);
    registry.emplace<MeshRenderer>(enemy, meshVAO, 0u, shaderId,
                                    textureId, true, indexCount);
    registry.emplace<TagEnemy>(enemy);

    AIBrain brain{};
    float health = 100.0f;
    glm::vec3 colliderSize(0.4f, 0.9f, 0.4f);

    if (type == "grunt") {
        brain.sightRange = 25.0f;
        brain.attackRange = 15.0f;    // Ranged attacker
        brain.attackCooldown = 0.8f;
        brain.damage = 5.0f;
        brain.moveSpeed = 3.0f;
        health = 30.0f;
    }
    else if (type == "knight") {
        brain.sightRange = 20.0f;
        brain.attackRange = 2.0f;     // Melee attacker
        brain.attackCooldown = 1.0f;
        brain.damage = 20.0f;
        brain.moveSpeed = 5.0f;
        health = 75.0f;
    }
    else if (type == "ogre") {
        brain.sightRange = 30.0f;
        brain.attackRange = 20.0f;
        brain.attackCooldown = 2.0f;
        brain.damage = 40.0f;
        brain.moveSpeed = 2.5f;
        health = 200.0f;
        colliderSize = glm::vec3(0.5f, 1.1f, 0.5f);
    }
    else if (type == "fiend") {
        brain.sightRange = 25.0f;
        brain.attackRange = 3.0f;     // Leaping melee
        brain.attackCooldown = 1.5f;
        brain.damage = 30.0f;
        brain.moveSpeed = 7.0f;       // Fast
        health = 80.0f;
    }

    registry.emplace<AIBrain>(enemy, brain);
    registry.emplace<Health>(enemy, health, health);
    registry.emplace<AABBCollider>(enemy, colliderSize, false);

    return enemy;
}
```

Want a new enemy type? Add another `else if` block with different numbers. No new classes, no new files.

---

## Line of Sight

An enemy can only see the player if:
1. The player is within sight range
2. There's no wall between them

Add this helper to `src/engine/ecs/systems/ai_system.cpp` (it's used by the AI system below):

```cpp
bool hasLineOfSight(const glm::vec3& from, const glm::vec3& to,
                     const Level& level) {
    glm::vec3 direction = to - from;
    float distance = glm::length(direction);
    if (distance < 0.001f) return true;

    direction = glm::normalize(direction);
    Ray ray{ from, direction };

    // Check against level surfaces
    for (const auto& sector : level.sectors) {
        for (const auto& surface : sector.surfaces) {
            // Test ray against each surface quad (two triangles)
            auto hit1 = rayIntersectsTriangle(ray,
                surface.vertices[0], surface.vertices[1], surface.vertices[2]);
            auto hit2 = rayIntersectsTriangle(ray,
                surface.vertices[0], surface.vertices[2], surface.vertices[3]);

            if (hit1.has_value() && hit1.value() < distance) return false;
            if (hit2.has_value() && hit2.value() < distance) return false;
        }
    }

    return true;  // No wall in the way
}
```

This is expensive — it tests against every surface. With a BSP tree, you'd trace through the tree structure (much faster). For our simplified level system, this works for moderate-sized levels. You can optimise later by only testing surfaces in relevant sectors.

---

## The AI System

### src/engine/ecs/systems/ai_system.h

```cpp
#pragma once

#include <entt/entt.hpp>
#include "engine/level/level.h"

void aiSystem(entt::registry& registry, const Level& level, float dt);
```

### src/engine/ecs/systems/ai_system.cpp

```cpp
#include "engine/ecs/systems/ai_system.h"
#include "engine/ecs/components.h"
#include "engine/physics/raycast.h"

void aiSystem(entt::registry& registry, const Level& level, float dt) {
    // Find the player (first entity with TagPlayer)
    entt::entity playerEntity = entt::null;
    glm::vec3 playerPos(0.0f);

    auto playerView = registry.view<Position, TagPlayer>();
    for (auto [entity, pos, tag] : playerView.each()) {
        playerEntity = entity;
        playerPos = pos.value;
        break;
    }

    if (playerEntity == entt::null) return;  // No player in the world

    // Process each enemy
    auto enemyView = registry.view<Position, Velocity, Rotation, AIBrain, Health>();

    for (auto [entity, pos, vel, rot, brain, health] : enemyView.each()) {

        // Dead enemies don't think
        if (health.current <= 0.0f) {
            brain.state = AIState::Dead;
            vel.value = glm::vec3(0.0f);
            continue;
        }

        // Pain stun
        if (brain.state == AIState::Pain) {
            brain.painTimer -= dt;
            vel.value = glm::vec3(0.0f);  // Can't move during pain
            if (brain.painTimer <= 0.0f) {
                brain.state = AIState::Chase;  // Resume chasing
            }
            continue;
        }

        // Cooldown tick
        if (brain.attackTimer > 0.0f) {
            brain.attackTimer -= dt;
        }

        // Calculate distance and direction to player
        glm::vec3 toPlayer = playerPos - pos.value;
        float distToPlayer = glm::length(toPlayer);
        glm::vec3 dirToPlayer = (distToPlayer > 0.001f)
            ? glm::normalize(toPlayer) : glm::vec3(0.0f);

        // ─── State machine ───────────────────────────────────────
        switch (brain.state) {

            case AIState::Idle: {
                vel.value = glm::vec3(0.0f);

                // Check if player is in sight range and visible
                if (distToPlayer <= brain.sightRange) {
                    glm::vec3 eyePos = pos.value + glm::vec3(0.0f, 0.8f, 0.0f);
                    glm::vec3 targetPos = playerPos + glm::vec3(0.0f, 0.8f, 0.0f);

                    if (hasLineOfSight(eyePos, targetPos, level)) {
                        brain.state = AIState::Chase;
                        brain.target = playerEntity;
                    }
                }
                break;
            }

            case AIState::Patrol: {
                // Move between waypoints (if entity has a PatrolRoute)
                if (registry.all_of<PatrolRoute>(entity)) {
                    auto& route = registry.get<PatrolRoute>(entity);

                    if (route.waypoints.empty()) {
                        brain.state = AIState::Idle;
                        break;
                    }

                    glm::vec3 target = route.waypoints[route.currentWaypoint];
                    glm::vec3 toTarget = target - pos.value;
                    float dist = glm::length(glm::vec2(toTarget.x, toTarget.z));

                    if (dist < route.waypointRadius) {
                        // Reached waypoint — advance to next
                        route.currentWaypoint =
                            (route.currentWaypoint + 1) % route.waypoints.size();
                    } else {
                        // Move toward waypoint
                        glm::vec3 moveDir = glm::normalize(
                            glm::vec3(toTarget.x, 0.0f, toTarget.z));
                        vel.value.x = moveDir.x * brain.moveSpeed;
                        vel.value.z = moveDir.z * brain.moveSpeed;
                    }

                    // Check for player while patrolling
                    if (distToPlayer <= brain.sightRange) {
                        glm::vec3 eyePos = pos.value + glm::vec3(0.0f, 0.8f, 0.0f);
                        glm::vec3 targetPos = playerPos + glm::vec3(0.0f, 0.8f, 0.0f);

                        if (hasLineOfSight(eyePos, targetPos, level)) {
                            brain.state = AIState::Chase;
                            brain.target = playerEntity;
                        }
                    }
                }
                break;
            }

            case AIState::Chase: {
                // Turn toward the player
                float targetYaw = glm::degrees(
                    std::atan2(dirToPlayer.x, dirToPlayer.z));
                // Smooth rotation toward target
                float yawDiff = targetYaw - rot.euler.y;
                // Normalize to -180..180
                while (yawDiff > 180.0f) yawDiff -= 360.0f;
                while (yawDiff < -180.0f) yawDiff += 360.0f;

                float maxTurn = brain.turnSpeed * dt;
                if (std::abs(yawDiff) < maxTurn) {
                    rot.euler.y = targetYaw;
                } else {
                    rot.euler.y += (yawDiff > 0 ? maxTurn : -maxTurn);
                }

                // Move toward the player (horizontal only)
                if (distToPlayer > brain.attackRange) {
                    vel.value.x = dirToPlayer.x * brain.moveSpeed;
                    vel.value.z = dirToPlayer.z * brain.moveSpeed;
                } else {
                    // In attack range — switch to attack
                    vel.value.x = 0.0f;
                    vel.value.z = 0.0f;
                    brain.state = AIState::Attack;
                }

                // Lost sight? Keep chasing for a bit (could add a timer here)
                break;
            }

            case AIState::Attack: {
                // Face the player
                float targetYaw = glm::degrees(
                    std::atan2(dirToPlayer.x, dirToPlayer.z));
                rot.euler.y = targetYaw;

                // Stop moving during attack
                vel.value.x = 0.0f;
                vel.value.z = 0.0f;

                // Attack if cooldown is ready
                if (brain.attackTimer <= 0.0f) {
                    // Deal damage to the player
                    if (distToPlayer <= brain.attackRange) {
                        if (registry.all_of<Health>(playerEntity)) {
                            // For ranged attackers: could fire a projectile instead
                            // For melee: direct damage
                            applyDamage(registry, playerEntity, brain.damage);
                        }
                    }
                    brain.attackTimer = brain.attackCooldown;

                    // TODO: play attack animation
                    // TODO: play attack sound (Chapter 16)
                }

                // If player moves out of range, chase again
                if (distToPlayer > brain.attackRange * 1.2f) {
                    brain.state = AIState::Chase;
                }
                break;
            }

            case AIState::Pain:
            case AIState::Dead:
                // Handled above
                break;
        }
    }
}
```

---

## Taking Damage — Triggering Pain State

When an enemy takes damage, give them a brief stun. Modify `applyDamage` or add a separate check:

```cpp
void onEnemyDamaged(entt::registry& registry, entt::entity enemy, float damage) {
    if (!registry.all_of<AIBrain>(enemy)) return;

    auto& brain = registry.get<AIBrain>(enemy);

    // Chance to enter pain state (Quake used a random chance)
    // Simple version: always pain if damage > threshold
    if (damage > 10.0f && brain.state != AIState::Pain) {
        brain.state = AIState::Pain;
        brain.painTimer = brain.painDuration;
    }

    // Wake up idle enemies when shot
    if (brain.state == AIState::Idle) {
        brain.state = AIState::Chase;
        // Find the player to target
        auto playerView = registry.view<TagPlayer>();
        for (auto [player, tag] : playerView.each()) {
            brain.target = player;
            break;
        }
    }
}
```

---

## A* Pathfinding

Direct pursuit (walk toward the player in a straight line) works in open areas but fails when enemies need to navigate around corners. A* finds the shortest path through a graph.

### Navigation Graph

First, we need a graph of walkable positions. The simplest approach: place **nav points** (waypoints) in the level and connect them:

```cpp
struct NavPoint {
    glm::vec3 position;
    std::vector<int> neighbors;  // Indices of connected nav points
};

struct NavGraph {
    std::vector<NavPoint> points;
};
```

### A* Implementation

```cpp
// src/engine/physics/pathfinding.h
#pragma once

#include <glm/glm.hpp>
#include <vector>

struct NavPoint {
    glm::vec3 position;
    std::vector<int> neighbors;
};

struct NavGraph {
    std::vector<NavPoint> points;

    // Find the nearest nav point to a world position
    int findNearest(const glm::vec3& pos) const;
};

// Returns a list of positions from start to goal
std::vector<glm::vec3> findPath(const NavGraph& graph,
                                 const glm::vec3& start,
                                 const glm::vec3& goal);
```

```cpp
// src/engine/physics/pathfinding.cpp
#include "engine/physics/pathfinding.h"
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cmath>

int NavGraph::findNearest(const glm::vec3& pos) const {
    int nearest = -1;
    float nearestDist = std::numeric_limits<float>::max();

    for (int i = 0; i < static_cast<int>(points.size()); i++) {
        float dist = glm::length(points[i].position - pos);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = i;
        }
    }

    return nearest;
}

std::vector<glm::vec3> findPath(const NavGraph& graph,
                                 const glm::vec3& start,
                                 const glm::vec3& goal) {
    int startNode = graph.findNearest(start);
    int goalNode = graph.findNearest(goal);

    if (startNode < 0 || goalNode < 0) return {};
    if (startNode == goalNode) return { graph.points[goalNode].position };

    // A* algorithm
    struct Node {
        int index;
        float fScore;  // g + h
        bool operator>(const Node& other) const { return fScore > other.fScore; }
    };

    // Priority queue: smallest fScore on top
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;

    std::unordered_map<int, float> gScore;    // Cost from start to node
    std::unordered_map<int, int> cameFrom;    // For path reconstruction

    gScore[startNode] = 0.0f;
    float h = glm::length(graph.points[goalNode].position -
                           graph.points[startNode].position);
    openSet.push({ startNode, h });

    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        if (current.index == goalNode) {
            // Reconstruct path
            std::vector<glm::vec3> path;
            int node = goalNode;
            while (cameFrom.find(node) != cameFrom.end()) {
                path.push_back(graph.points[node].position);
                node = cameFrom[node];
            }
            path.push_back(graph.points[startNode].position);
            std::reverse(path.begin(), path.end());
            return path;
        }

        const NavPoint& point = graph.points[current.index];
        for (int neighbor : point.neighbors) {
            float tentativeG = gScore[current.index] +
                glm::length(graph.points[neighbor].position - point.position);

            if (gScore.find(neighbor) == gScore.end() ||
                tentativeG < gScore[neighbor]) {
                cameFrom[neighbor] = current.index;
                gScore[neighbor] = tentativeG;

                float h = glm::length(graph.points[goalNode].position -
                                       graph.points[neighbor].position);
                openSet.push({ neighbor, tentativeG + h });
            }
        }
    }

    return {};  // No path found
}
```

### C++ Concept: `std::priority_queue`

```cpp
std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
```

A priority queue always gives you the smallest (or largest) element. We use `std::greater<Node>` to get the smallest `fScore` first (min-heap).

The three template parameters:
1. `Node` — the element type
2. `std::vector<Node>` — the underlying container
3. `std::greater<Node>` — the comparison (greater = min-heap, less = max-heap)

### How A* Works

A* explores nodes in order of `fScore = gScore + heuristic`:
- **gScore**: actual cost from start to this node
- **heuristic**: estimated cost from this node to the goal (we use straight-line distance)

The heuristic guides the search toward the goal. Without it (heuristic = 0), A* becomes Dijkstra's algorithm — correct but explores more nodes.

```
Start ──→ A ──→ B ──→ Goal
  │              ↑
  └──→ C ──→ D ─┘

gScore: actual distance walked
hScore: straight-line distance to Goal
fScore: g + h (lower = explore first)
```

---

## Using Pathfinding in the AI

Add a path component:

```cpp
struct AIPath {
    std::vector<glm::vec3> waypoints;
    int currentWaypoint = 0;
    float recalculateTimer = 0.0f;
    float recalculateInterval = 1.0f;  // Recalculate path every 1 second
};
```

In the chase state, instead of moving directly toward the player:

```cpp
case AIState::Chase: {
    // Recalculate path periodically
    if (registry.all_of<AIPath>(entity)) {
        auto& path = registry.get<AIPath>(entity);
        path.recalculateTimer -= dt;

        if (path.recalculateTimer <= 0.0f) {
            path.waypoints = findPath(navGraph, pos.value, playerPos);
            path.currentWaypoint = 0;
            path.recalculateTimer = path.recalculateInterval;
        }

        // Follow the path
        if (!path.waypoints.empty() &&
            path.currentWaypoint < static_cast<int>(path.waypoints.size())) {

            glm::vec3 target = path.waypoints[path.currentWaypoint];
            glm::vec3 toTarget = target - pos.value;
            float dist = glm::length(glm::vec2(toTarget.x, toTarget.z));

            if (dist < 1.0f) {
                path.currentWaypoint++;
            } else {
                glm::vec3 moveDir = glm::normalize(
                    glm::vec3(toTarget.x, 0.0f, toTarget.z));
                vel.value.x = moveDir.x * brain.moveSpeed;
                vel.value.z = moveDir.z * brain.moveSpeed;
            }
        }
    } else {
        // No pathfinding — direct pursuit (simple fallback)
        vel.value.x = dirToPlayer.x * brain.moveSpeed;
        vel.value.z = dirToPlayer.z * brain.moveSpeed;
    }

    // Switch to attack when in range
    if (distToPlayer <= brain.attackRange) {
        vel.value.x = 0.0f;
        vel.value.z = 0.0f;
        brain.state = AIState::Attack;
    }
    break;
}
```

---

## Building a Nav Graph

For now, define nav points manually when building levels:

```cpp
NavGraph buildNavGraph(const Level& level) {
    NavGraph graph;

    // Place nav points in open areas of each sector
    // This would eventually be automated, but for now it's manual:
    graph.points.push_back({ glm::vec3(0.0f, 0.5f, 0.0f), {1, 2} });
    graph.points.push_back({ glm::vec3(5.0f, 0.5f, 0.0f), {0, 3} });
    graph.points.push_back({ glm::vec3(0.0f, 0.5f, -5.0f), {0, 3} });
    graph.points.push_back({ glm::vec3(5.0f, 0.5f, -5.0f), {1, 2} });

    // Verify connections with line-of-sight checks
    // (remove connections that are blocked by walls)

    return graph;
}
```

A proper nav mesh generator would automatically place points based on floor geometry. That's a substantial project — for now, manual placement gets enemies moving intelligently.

---

## What's Next

In **Chapter 15**, we'll build the HUD — health bar, ammo counter, crosshair, and on-screen messages. This is where 2D rendering meets the 3D engine.
