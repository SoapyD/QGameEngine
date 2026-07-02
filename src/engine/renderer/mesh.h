#pragma once

#include "engine/ecs/components/core.h"   // Vertex
#include <glad/glad.h>
#include <vector>
#include <string>
#include <cstddef>

class Mesh
{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
		// GL-free stub (all handles 0). Lets the headless harness hold a named
		// mesh without a GL context; never drawn. cleanup() already no-ops on 0.
		explicit Mesh(std::nullptr_t) noexcept {}
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
