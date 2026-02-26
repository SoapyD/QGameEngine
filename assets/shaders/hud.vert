#version 460 core

// input: vertext position (location 0 matches out C++ vertex data layout)
layout (location = 0) in vec3 aPos;

// transformation matrix, set from C++ code
uniform mat4 projection;

void main()
{
	// gl_Position is a built-in variable - the final screen position
	// No model matrix needed — positions are already in screen space
	gl_Position = projection * vec4(aPos, 1.0);
}