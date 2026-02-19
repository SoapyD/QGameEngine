#version 460 core

// input: vertext position (location 0 matches out C++ vertex data layout)
layout (location = 0) in vec3 aPos;
// input: vertex colour (location 1)
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

// output: pass colour to fragment shader
out vec2 TexCoord;
out vec3 Normal; // pass to fragment shader for lighting (chapter 7)
out vec3 FragPos; // workd-space position for lighting

// transformation matrices, set for C++ code
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	FragPos = vec3(model * vec4(aPos, 1.0));
	Normal = mat3(transpose(inverse(model))) * aNormal;
	TexCoord = aTexCoord;
	// gl_Position is a build-in variable - then final screen position
	// it's a vec4: x, y, z, w (w is for perspective division - 1.0 for now)
	gl_Position = projection * view * model * vec4(aPos, 1.0);

}