#version 460 core

// output: final pixel colour
out vec4 FragColor;

uniform vec3 textColor;
uniform float alpha;

void main()
{
	FragColor = vec4(textColor, alpha); // RGC from vertex, alpha = 1.0 (opaque)
}