#pragma once

#include <glad/glad.h>
#include <string>

class Texture
{
	public:
		Texture(const std::string& path);
		~Texture();

		// Bind this texture to a texture unit (0, 1, 2, etc.)
		void bind(unsigned int unit = 0) const;

		unsigned int getId() const { return m_textureId; }
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }

	private:
		unsigned int m_textureId;
		int m_width;
		int m_height;
		int m_channels;
};
