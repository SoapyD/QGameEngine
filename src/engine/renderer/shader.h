#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <cstddef>
#include <unordered_map>

class Shader
{
	public:
		// load, compule and link a vertex + fragment shader pair
		Shader(const std::string& vertexPath, const std::string& fragmentPath);
		// GL-free stub (program id 0). For the headless harness — never used to
		// draw; the destructor skips glDeleteProgram on a 0 id.
		explicit Shader(std::nullptr_t) : m_programId(0) {}
		~Shader();

		// activate this shader for subsequent draw calls
		void use() const;

		// get the OpenGL program ID (needed for settings uniforms later)
		unsigned int getId() const { return m_programId; }

		// false if any shader failed to compile or the program failed to link
		// (so callers can detect a broken shader instead of rendering black)
		bool isValid() const { return m_valid; }

		// uniform setters
		void setMat4(const std::string& name, const glm::mat4& value) const;
		void setVec3(const std::string& name, const glm::vec3& value) const;
		void setFloat(const std::string& name, float value) const;
		void setInt(const std::string& name, int value) const;

	private:
		unsigned int m_programId;
		mutable bool m_valid = true;  // cleared by checkErrors on failure

		// Cache of uniform name → location. glGetUniformLocation is a string
		// lookup on the driver side; caching avoids repeating it every frame.
		mutable std::unordered_map<std::string, int> m_uniformCache;

		// helper: look up (and cache) a uniform location by name
		int uniformLocation(const std::string& name) const;

		// helper: read a file into a string
		std::string readFile(const std::string& path) const;

		// helper: compile a single shader and return its ID
		unsigned int compileShader(const std::string& source, GLenum type) const;

		// helper: check for compilation/linking errors
		void checkErrors(unsigned int shader, const std::string& type) const;
};
