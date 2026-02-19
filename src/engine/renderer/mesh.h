#pragma once

#include "engine/ecs/components.h"
#include <glad/glad.h>
#include <vector>
#include <string>

class Mesh
{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);	
		~Mesh();

		// prevent copying (GPU resources shouldn't be duplicated accidentally)
		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;

		// allow moving (transfer ownership)
		Mesh(Mesh&& other) noexcept;
		Mesh& operator=(Mesh&& other) noexcept;

		unsigned int getVAO() const { return m_vao; }
		unsigned int getIndexCount() const { return m_indexCount; }

	private:
		unsigned int m_vao = 0;
		unsigned int m_vbo = 0;
		unsigned int m_ebo = 0;  // element buffer object (index buffer)
		unsigned int m_indexCount = 0;

		void setupMesh(const std::vector<Vertex>& vertices,
						const std::vector<unsigned int>& indices);

		void cleanup();
};
