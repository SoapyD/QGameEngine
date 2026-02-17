#include "engine/renderer/shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
{
	// 1. read shader source code from files
	std::string vertextSource = readFile(vertexPath);
	std::string fragmentSource = readFile(fragmentPath);

	// 2. compile each shader
	unsigned int vertexShader = compileShader(vertextSource, GL_VERTEX_SHADER);
	unsigned int fragmentShader = compileShader(fragmentSource, GL_FRAGMENT_SHADER);

	m_programId = glCreateProgram();
	glAttachShader(m_programId, vertexShader);
	glAttachShader(m_programId, fragmentShader);
	glLinkProgram(m_programId);
	checkErrors(m_programId, "PROGRAM");

	// 4. Individual shaders are no longer needed after linking
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);	
}

Shader::~Shader()
{
	glDeleteProgram(m_programId);
}

void Shader::use() const
{
	glUseProgram(m_programId);
}

std::string Shader::readFile(const std::string& path) const
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		std::cerr << "ERROR: Could not open shader file: " << path << std::endl;
		return "";
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

unsigned int Shader::compileShader(const std::string& source, GLenum type) const
{
	unsigned int shader = glCreateShader(type);
	const char* src = source.c_str();
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);

	std::string typeName = (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
	checkErrors(shader, typeName);

	return shader;
}

void Shader::checkErrors(unsigned int shader, const std::string& type) const
{
	int success;
	char infoLog[1024];

	if (type == "PROGRAM") {
		glGetProgramiv(shader, GL_LINK_STATUS, &success);
		if (!success)
		{
			glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
			std::cerr << "ERROR: Shader program linking failed\n" << infoLog << std::endl;
		}
	}
	else
	{
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
			std::cerr << "ERROR: " << type << " shader compilation failed\n" << infoLog << std::endl;
		}
	}
} 