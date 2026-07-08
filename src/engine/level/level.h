#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include "engine/renderer/mesh.h"

struct Surface
{
	glm::vec3 vertices[4]; // quad corners
	glm::vec3 normal; // computed from vertices
	std::string textureName;
	unsigned int textureId = 0;
};

struct Portal
{
	int sectorA;
	int sectorB;
	glm::vec3 vertices[4]; // then opening between sectors
};

struct Sector
{
	int id;
	glm::vec3 boundsMin; // axis-aligned bounding box
	glm::vec3 boundsMax;
	std::vector<Surface> surfaces;
	std::vector<int> portalIndices; // indices into level::portals
	std::unique_ptr<Mesh> mesh; // combined mesh of all surfaces
};

struct LevelEntity
{
	std::string type; // "player_start", "light", "enemy", etc.
	glm::vec3 position;
	std::vector<std::pair<std::string, std::string>> properties;
};

struct Level
{
	std::vector<Sector> sectors;
	std::vector<Portal> portals;
	std::vector<LevelEntity> entities;

	// Extra render meshes owned by the level but not tied to a single sector's
	// `mesh` slot — used by the .map loader, which builds one mesh per texture
	// group. Kept alive here for the level's lifetime because ~Mesh frees the VAO
	// the MeshRenderer components reference.
	std::vector<std::unique_ptr<Mesh>> renderMeshes;

	// Per-brush corner-vertex sets for convex-hull collision (the .map loader fills
	// these; the C++ showcase leaves it empty and uses per-surface AABB colliders).
	// When non-empty, createLevelBodies builds one Jolt ConvexHullShape per entry
	// instead of AABB-per-surface — so angled brushes collide by their true shape.
	std::vector<std::vector<glm::vec3>> collisionHulls;
};
