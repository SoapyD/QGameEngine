#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

class Shader
{
	public:
		// load, compule and link a vertex + fragment shader pair
		Shader(const std::string& vertexPath, const std::string& fragmentPath);
		~Shader();

		// activate this shader for subsequent draw calls
		void use() const;

		// get the OpenGL program ID (needed for settings uniforms later)
		unsigned int getId() const { return m_programId; }

		// uniform setters
		void setMat4(const std::string& name, const glm::mat4& value) const;
		void setVec3(const std::string& name, const glm::vec3& value) const;
		void setFloat(const std::string& name, float value) const;
		void setInt(const std::string& name, int value) const;

	private:
		unsigned int m_programId;

		// helper: read a file into a string
		std::string readFile(const std::string& path) const;

		// helper: compile a single shader and return its ID
		unsigned int compileShader(const std::string& source, GLenum type) const;

		// helper: check for compilation/linking errors
		void checkErrors(unsigned int shader, const std::string& type) const;
};
