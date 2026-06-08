#include "engine/renderer/obj_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

Mesh loadOBJ(const std::string& path)
{
	std::vector<glm::vec3> tempPositions;
	std::vector<glm::vec2> tempTexCoords;
	std::vector<glm::vec3> tempNormals;

	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

    // Map to detect and reuse duplicate vertices
    // Key: "posIndex/texIndex/normIndex" string
    // Value: index in the final vertex array
	std::unordered_map<std::string, unsigned int> vertexMap;

	std::ifstream file(path);
	if(!file.is_open())
	{
		std::cerr << "ERROR: Could not open OBJ file: " << path << std::endl;
		return Mesh(vertices, indices);
	}

	std::string line;
	while (std::getline(file, line)) 
	{
		std::istringstream iss(line);
		std::string prefix;
		iss >> prefix;

		if (prefix == "v")
		{
			glm::vec3 pos;
			iss >> pos.x >> pos.y >> pos.z;
			tempPositions.push_back(pos);
		}
		else if (prefix == "vt")
		{
			glm::vec2 tex;
			iss >> tex.x >> tex.y;
			tempTexCoords.push_back(tex);
		}
		else if (prefix == "vn")
		{
			glm::vec3 norm;
			iss >> norm.x >> norm.y >> norm.z;
			tempNormals.push_back(norm);
		}
		else if (prefix == "f")
		{
			// parse face - can have 3 or more vertices (we handle traingles and quads)
			std::vector<std::string> faceTokens;
			std::string token;
			while (iss >> token)
			{
				faceTokens.push_back(token);
			}

			// triangulate: fan from first vertex (works for convex polygons)
			for (size_t i = 1; i + 1 < faceTokens.size(); i++)
			{
				std::string triTokens[3] =
				{
					faceTokens[0], faceTokens[1], faceTokens[i + 1]
				};

				for (const auto& tok : triTokens) {
					// check if we've already created this exact vertex
					auto it = vertexMap.find(tok);
					if (it != vertexMap.end())
					{
						indices.push_back(it->second);
						continue;
					}

					// parse "posIndex/texIndex/normIndex"
					std::istringstream tokenStream(tok);
					std::string part;
					int posIdx = 0, texIdx = 0, normIdx = 0;

					// position index (required). Guard against malformed
					// tokens so a bad OBJ logs and skips rather than crashing.
					try
					{
						std::getline(tokenStream, part, '/');
						posIdx = std::stoi(part) - 1; // OBJ is 1-based

						// texture coordinate index (optional)
						if(std::getline(tokenStream, part, '/') && !part.empty())
							texIdx = std::stoi(part) - 1;

						// normal index (optional)
						if(std::getline(tokenStream, part, '/') && !part.empty())
							normIdx = std::stoi(part) - 1;
					}
					catch (const std::exception&)
					{
						std::cerr << "WARN: malformed OBJ face token '" << tok
								  << "' in " << path << std::endl;
						continue;
					}

					if (posIdx < 0 || posIdx >= static_cast<int>(tempPositions.size()))
					{
						std::cerr << "WARN: OBJ position index out of range in " << path << std::endl;
						continue;
					}

					Vertex vertex{};
					vertex.position = tempPositions[posIdx];

					if (texIdx >= 0 && texIdx < static_cast<int>(tempTexCoords.size()))
					{
						vertex.texCoords = tempTexCoords[texIdx];
					}

					if (normIdx >= 0 && normIdx < static_cast<int>(tempNormals.size()))
					{
						vertex.normal = tempNormals[normIdx];
					}

					unsigned int newIndex = static_cast<unsigned int>(vertices.size());
					vertices.push_back(vertex);
					indices.push_back(newIndex);
					vertexMap[tok] = newIndex;
				}
			}
		}
		// ignore other lines (comments #, materials, etc.)
	}

	std::cout << "Loaded OBJ: " << path
			<< " (" << vertices.size() << " vertices, "
			<< indices.size() / 3 << " triangles)" << std::endl;

	return Mesh(vertices, indices);
}