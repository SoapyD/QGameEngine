#include "engine/level/level_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>

Level LevelLoader::load
(
	const std::string& path,
	const std::unordered_map<std::string, 
	unsigned int>& textureMap
)
{
	Level level;
	std::ifstream file(path);
	if(!file.is_open())
	{
		std::cerr << "ERROR: Could not open level file: " << path << std::endl;
	}

	std::string line;
	while (std::getline(file, line))
	{
		//skip comments and empty lines
		if (line.empty() || line[0] == '#') continue;

		std::istringstream iss(line);
		std::string type;
		iss >> type;

		if(type == "sector")
		{
			parseSector(line, level);
		}
		else if (type == "surface")
		{
			parseSurface(line, level, textureMap);
		}
		else if (type == "portal")
		{
			parsePortal(line, level);
		}
		else if (type == "entity")
		{
			parseEntity(line, level);
		}
	}

	buildSectorMeshes(level);

	std::cout << "Loaded level: " << path
	<< " (" << level.sectors.size() << " sectors, "
	<< level.portals.size() << " portals, "
	<< level.entities.size() << " entities)" << std::endl;

	return level;
}

void LevelLoader::parseSector(const std::string& line, Level& level)
{
	std::istringstream iss(line);
	std::string type;
	Sector sector;

	iss >> type >> sector.id
		>> sector.boundsMin.x >> sector.boundsMin.y >> sector.boundsMin.z
		>> sector.boundsMax.x >> sector.boundsMax.y >> sector.boundsMax.z;

	// ensure vector is large enough
	if (sector.id >= static_cast<int>(level.sectors.size()))
	{
		level.sectors.resize(sector.id + 1);
	}

	level.sectors[sector.id] = std::move(sector);
}

void LevelLoader::parseSurface
(
	const std::string& line,
	Level& level,
	const std::unordered_map<std::string, unsigned int>& textureMap
)
{
	std::istringstream iss(line);
	std::string type, surfaceType;
	int sectorId;
	Surface surface;	

	iss >> type >> sectorId >> surfaceType;

	for (int i = 0; i < 4; i++)
	{
		iss >> surface.vertices[i].x
			>> surface.vertices[i].y
			>> surface.vertices[i].z;
	}

	iss >> surface.textureName;

	// compute face normal from first three vertices
	surface.normal = computeNormal
	(
		surface.vertices[0],
		surface.vertices[1],
		surface.vertices[2]
	);

	// look up texture ID
	auto it = textureMap.find(surface.textureName);
	if (it != textureMap.end())
	{
		surface.textureId = it->second;
	}

	if(sectorId < static_cast<int>(level.sectors.size()))
	{
		level.sectors[sectorId].surfaces.push_back(surface);
	}
}

void LevelLoader::parsePortal
(
	const std::string& line,
	Level& level
)
{
	std::istringstream iss(line);
	std::string type;
	Portal portal;

	iss >> type >> portal.sectorA >> portal.sectorB;

	for (int i = 0; i < 4; i++)
	{
		iss >> portal.vertices[i].x
			>> portal.vertices[i].y
			>> portal.vertices[i].z;
	}

	int portalIndex = static_cast<int>(level.portals.size());
	level.portals.push_back(portal);

	// link portal to both sectors
	if (portal.sectorA < static_cast<int>(level.sectors.size()))
	{
		level.sectors[portal.sectorA].portalIndices.push_back(portalIndex);
	}
	if (portal.sectorB < static_cast<int>(level.sectors.size()))
	{
		level.sectors[portal.sectorB].portalIndices.push_back(portalIndex);
	}
}

void LevelLoader::parseEntity
(
	const std::string& line,
	Level& level
)
{
	std::istringstream iss(line);
	std::string type;
	LevelEntity entity;

	iss >> type >> entity.type
		>> entity.position.x >> entity.position.y >> entity.position.z;

	// poarse remaining key-value properties
	std::string key, value;
	while(iss >> key >> value)
	{
		entity.properties.push_back({key, value});
	}

	level.entities.push_back(entity);
}

glm::vec3 LevelLoader::computeNormal
(
	const glm::vec3& v0,
	const glm::vec3& v1,
	const glm::vec3& v2	
)
{
	glm::vec3 edge1 = v1 - v0;
	glm::vec3 edge2 = v1 - v0;
	return glm::normalize(glm::cross(edge1, edge2));
}

void buildSectorMeshes(Level& level)
{
	for (auto& sector : level.sectors)
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;

		for(const auto& surface : sector.surfaces)
		{
			unsigned int baseIndex = static_cast<unsigned int>(vertices.size());
		
            // Calculate UV coordinates based on surface dimensions
            // Simple planar projection for now
			float uScale = glm::length(surface.vertices[1] - surface.vertices[0]);
			float vScale = glm::length(surface.vertices[3] - surface.vertices[0]);

			// four vertices for the quad
			vertices.push_back({surface.vertices[0], surface.normal, {0.0f, 0.0f}});
			vertices.push_back({surface.vertices[1], surface.normal, {uScale, 0.0f}});
			vertices.push_back({surface.vertices[2], surface.normal, {uScale, vScale}});
			vertices.push_back({surface.vertices[3], surface.normal, {0.0f, vScale}});

			// two trianges for the quad
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 3);
		}

		if (!vertices.empty())
		{
			sector.mesh = std::make_unique<Mesh>(vertices, indices);
		}
	}
}