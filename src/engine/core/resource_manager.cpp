#include "engine/core/resource_manager.h"
#include "engine/renderer/obj_loader.h"

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

// ─── Meshes ──────────────────────────────────────────────────────

std::shared_ptr<Mesh> ResourceManager::getMesh
(
	const std::string& name,
	const std::string& path
)
{
	auto it = m_meshes.find(name);
	if (it != m_meshes.end())
	{
		return it->second;
	}

	auto mesh = std::make_shared<Mesh>(loadOBJ(path));
	m_meshes[name] = mesh;
	std::cout << "ResourceManager: cached mesh '" << name << "'" << std::endl;
	return mesh;
}

void ResourceManager::storeMesh(const std::string& name, std::shared_ptr<Mesh> mesh)
{
	m_meshes[name] = mesh;
	std::cout << "ResourceManager: cached mesh '" << name << "'" << std::endl;
}

std::shared_ptr<Mesh> ResourceManager::getMesh(const std::string& name) const
{
	auto it = m_meshes.find(name);
	if (it != m_meshes.end())
	{
		return it->second;
	}

	std::cerr << "ERROR: Mesh '" << name << "' not found in cache" << std::endl;
	return nullptr;
}


// ─── Cleanup ─────────────────────────────────────────────────────

void ResourceManager::clear()
{
	m_shaders.clear();
	m_textures.clear();
	m_meshes.clear();
	std::cout << "ResourceManager: all resources cleared" << std::endl;
}
