#include "engine/renderer/texture.h"
#include "stb_image.h"
#include <iostream>

Texture::Texture(const std::string& path)
	: m_textureId(0), m_width(0), m_height(0), m_channels(0)
{
	// std image loads with (0,0) at top-left, OpenGL expects bottom-left
	stbi_set_flip_vertically_on_load(true);

	// load the image
	unsigned char* data = stbi_load(path.c_str(), &m_width, &m_height, &m_channels, 0);
	if (!data)
	{
		std::cerr << "ERROR: Failed to load texture: " << path << std::endl;
		return;
	}

	// determine format from channel count
	GLenum format = GL_RGB;
	if (m_channels == 1) format = GL_RED;
	else if (m_channels == 3) format = GL_RGB;
	else if (m_channels == 4) format = GL_RGBA;

	// create OpenGL texture
	glGenTextures(1, &m_textureId);
	glBindTexture(GL_TEXTURE_2D, m_textureId);

	// set wrapping behaviour (what happens when UVs go outside 0-1)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // horizontal
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); // vertical

	// set filtering (how to sample when texture is scaled)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// upload pixel data to GPU
	glTexImage2D(GL_TEXTURE_2D, 0, format, m_width, m_height, 0,
				format, GL_UNSIGNED_BYTE, data);

	// generate mipmaps (smaller versions for distant rendering)
	glGenerateMipmap(GL_TEXTURE_2D);

	// free CPU-side image data - it's on the GPU now
	stbi_image_free(data);

	std::cout << "Loaded texture: " << path
		<< " (" << m_width << "x" << m_height
		<< ", " << m_channels << " channels)" << std::endl;
}

Texture::~Texture()
{
	if (m_textureId)
	{
		glDeleteTextures(1, &m_textureId);
	}
}

void Texture::bind(unsigned int unit) const
{
	glActiveTexture(GL_TEXTURE0 + unit); // activate texture unit
	glBindTexture(GL_TEXTURE_2D, m_textureId);
}