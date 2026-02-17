#version 460 core

// input: colour from vertex shader (interpolated across the triangle)
in vec3 vertexColor;

// output: final pixel colour
out vec4 FragColor;

void main()
{
	FragColor = vec4(vertexColor, 1.0); // RGC from vertex, alpha = 1.0 (opaque)
}