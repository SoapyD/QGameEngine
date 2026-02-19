#version 460 core

// input: vertext position (location 0 matches out C++ vertex data layout)
layout (location = 0) in vec3 aPos;
// input: vertex normal (location 1)
layout (location = 1) in vec3 aNormal;

// output: pass colour to fragment shader
out vec3 vertexColor;

// transformation matrices, set for C++ code
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	// gl_Position is a build-in variable - then final screen position
	// it's a vec4: x, y, z, w (w is for perspective division - 1.0 for now)
	gl_Position = projection * view * model * vec4(aPos, 1.0);

	// Use the absolute normal as colour — each face gets a distinct colour
	// +/-X = red, +/-Y = green, +/-Z = blue
	vertexColor = abs(aNormal);
}