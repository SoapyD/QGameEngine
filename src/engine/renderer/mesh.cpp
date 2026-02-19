#include "engine/renderer/mesh.h"

Mesh::Mesh(const std::vector<Vertex>& vertices,
	const std::vector<unsigned int>& indices)
{
	m_indexCount = static_cast<unsigned int>(indices.size());
	setupMesh(vertices, indices);
}

Mesh::~Mesh()
{
	cleanup();
}

Mesh::Mesh(Mesh&& other) noexcept
	: m_vao(other.m_vao)
	, m_vbo(other.m_vbo)
	, m_ebo(other.m_ebo)
	, m_indexCount(other.m_indexCount)
{
	other.m_vao = 0;
	other.m_vbo = 0;
	other.m_ebo = 0;
	other.m_indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
	if (this != &other) 
	{
		cleanup();
		m_vao = other.m_vao;
		m_vbo = other.m_vbo;
		m_ebo = other.m_ebo;
		m_indexCount = other.m_indexCount;
		other.m_vao = 0;
		other.m_vbo = 0;
		other.m_ebo = 0;
		other.m_indexCount = 0;
	}
	return *this;
}

void Mesh::setupMesh(const std::vector<Vertex>& vertices,
					const std::vector<unsigned int>& indices)
{
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glBindVertexArray(m_vao);

	// upload vertex data
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData
	(
		GL_ARRAY_BUFFER, 
		vertices.size() * sizeof(Vertex), 
		vertices.data(), 
		GL_STATIC_DRAW
	);

	// upload index data
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
	glBufferData
	(
		GL_ELEMENT_ARRAY_BUFFER, 
		indices.size() * sizeof(unsigned int), 
		indices.data(), 
		GL_STATIC_DRAW
	);

	// Vertex attribute layout:
	// Position (location 0): 3 floats at offset 0
	glVertexAttribPointer(
		0,                    				// Attribute index (matches layout(location = 0) in shader)
		3,                    				// Number of components (vec3 = 3 floats)
		GL_FLOAT,             				// Data type
		GL_FALSE,             				// Normalise? No
		sizeof(Vertex),       				// Stride: bytes between consecutive vertices (6 floats total)
		(void*)offsetof(Vertex, position)   // Offset: where this attribute starts within each vertex
	);
	glEnableVertexAttribArray(0);

	// Normal (location 1): 3 floats at offset of 'normal' member
	glVertexAttribPointer(
		1,                    				// Attribute index (matches layout(location = 0) in shader)
		3,                    				// Number of components (vec3 = 3 floats)
		GL_FLOAT,             				// Data type
		GL_FALSE,             				// Normalise? No
		sizeof(Vertex),       				// Stride: bytes between consecutive vertices (6 floats total)
		(void*)offsetof(Vertex, normal)   // Offset: where this attribute starts within each vertex
	);
	glEnableVertexAttribArray(1);

	// Normal (location 2): 2 floats at offset of 'texCoords' member
	glVertexAttribPointer(
		2,                    				// Attribute index (matches layout(location = 0) in shader)
		2,                    				// Number of components (vec3 = 3 floats)
		GL_FLOAT,             				// Data type
		GL_FALSE,             				// Normalise? No
		sizeof(Vertex),       				// Stride: bytes between consecutive vertices (6 floats total)
		(void*)offsetof(Vertex, texCoords)   // Offset: where this attribute starts within each vertex
	);
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);
}

void Mesh::cleanup()
{
	if(m_vao) glDeleteVertexArrays(1, &m_vao);
	if(m_vbo) glDeleteBuffers(1, &m_vbo);
	if(m_ebo) glDeleteBuffers(1, &m_ebo);
	m_vao = m_vbo = m_ebo = 0;
}
