#version 460 core

in vec2 TexCoord;

// output: final pixel colour
out vec4 FragColor;

uniform sampler2D textureSampler; // the texture, accessed by unit number

void main()
{
	FragColor = texture(textureSampler, TexCoord);
}