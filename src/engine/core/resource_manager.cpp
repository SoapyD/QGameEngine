#include "engine/core/resource_manager.h"

// ─── Shaders ─────────────────────────────────────────────────────

std::shared_ptr<Shader> ResourceManager::getShader
(
	const std::string& name,
	const std::string& vertexPath,
	const std::string& fragmentPath
)
{
	// check if we already have this shader cached
	auto it = m_shaders.find(name);
	if (it != m_shaders.end())
	{
		return it->second;
	}

	// not cached - load it, store it, return it
	auto shader = std::make_shared<Shader>(vertexPath, fragmentPath);
	m_shaders[name] = shader;
	std::cout << "ResourceManager: cached shader '" << name << "'" << std::endl;
	return shader;	
}

std::shared_ptr<Shader> ResourceManager::getShader(const std::string& name) const
{
	auto it = m_shaders.find(name);
	if(it != m_shaders.end())
	{
		return it->second;
	}

	std::cerr << "ERROR Shader '" << name << "' not found in cache" << std::endl;
	return nullptr;
}

// ─── Textures ────────────────────────────────────────────────────


std::shared_ptr<Texture> ResourceManager::getTexture
(
	const std::string& name,
	const std::string& path
)
{
	// check if we already have this texture cached
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}

	// not cached - load it, store it, return it
	auto texture = std::make_shared<Texture>(path);
	m_textures[name] = texture;
	std::cout << "ResourceManager: cached texture '" << name << "'" << std::endl;
	return texture;	
}

std::shared_ptr<Texture> ResourceManager::getTexture
(
	const std::string& name
) const
{
	auto it = m_textures.find(name);
	if(it != m_textures.end())
	{
		return it->second;
	}

	std::cerr << "ERROR Texture '" << name << "' not found in cache" << std::endl;
	return nullptr;
}

// ─── Cleanup ─────────────────────────────────────────────────────

void ResourceManager::clear()
{
	m_shaders.clear();
	m_textures.clear();
	std::cout << "ResourceManager: all resources cleared" << std::endl;
}
