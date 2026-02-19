#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
	FragPos = vec3(model * vec4(aPos, 1.0));

	// Performance note: Computing inverse() per vertex in the 
	// shader is expensive. In production, you'd compute the 
	// normal matrix on the CPU once and pass it as a uniform. 
	// For learning, the shader approach is clearer.
	Normal = mat3(transpose(inverse(model))) * aNormal;
	TexCoord = aTexCoord;
	gl_Position = projection * view * vec4(FragPos, 1.0);
}