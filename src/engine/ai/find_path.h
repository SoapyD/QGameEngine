#pragma once

#include <glm/glm.hpp>
#include <vector>

struct NavGrid;

// A* over the walkability grid from `from` to `to`. Returns the waypoint centres
// to walk (excluding the start cell), or empty if unreachable, within one cell,
// or the node budget is exceeded. Blocked start/goal cells snap to the nearest
// open cell first. 8-connected, no diagonal corner-cutting.
std::vector<glm::vec3> findPath(const NavGrid& grid, const glm::vec3& from, const glm::vec3& to);
