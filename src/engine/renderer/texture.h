#pragma once

#include <glad/glad.h>
#include <string>
#include <cstddef>

class Texture
{
	public:
		Texture(const std::string& path);
		// GL-free stub (texture id 0). For the headless harness — never bound;
		// the destructor already skips glDeleteTextures on a 0 id.
		explicit Texture(std::nullptr_t)
			: m_textureId(0), m_width(0), m_height(0), m_channels(0) {}
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
