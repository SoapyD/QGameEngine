#version 460 core

// output: final pixel colour
out vec4 FragColor;

uniform vec3 textColor;

void main()
{
	FragColor = vec4(textColor, 1.0); // RGC from vertex, alpha = 1.0 (opaque)
}