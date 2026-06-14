#pragma once

#include "engine/renderer/shader.h"
#include "engine/renderer/texture.h"
#include "engine/renderer/mesh.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

class ResourceManager
{
	public:
		// ─── Shaders ─────────────────────────────────────────────
		std::shared_ptr<Shader> getShader
		(
			const std::string& name,
			const std::string& vertexPath,
			const std::string& fragmentPath
		);

		// retrieve a previously loaded shader by name
		std::shared_ptr<Shader> getShader(const std::string& name) const;

		// store an already-constructed shader (e.g. a GL-free headless stub)
		void storeShader(const std::string& name, std::shared_ptr<Shader> shader);

		// ─── Textures ────────────────────────────────────────────
		// load a texture (or return the cache version if already loaded)
		std::shared_ptr<Texture> getTexture
		(
			const std::string& name,
			const std::string& path
		);

		// retrieve a previously loaded texture by name
		std::shared_ptr<Texture> getTexture(const std::string& name) const;

		// store an already-constructed texture (e.g. a GL-free headless stub)
		void storeTexture(const std::string& name, std::shared_ptr<Texture> texture);

		// ─── Meshes ──────────────────────────────────────────────
		// load a mesh from an OBJ file (or return the cached version)
		std::shared_ptr<Mesh> getMesh
		(
			const std::string& name,
			const std::string& path
		);

		// store a procedurally generated mesh
		void storeMesh(const std::string& name, std::shared_ptr<Mesh> mesh);

		// retrieve a previously loaded mesh by name
		std::shared_ptr<Mesh> getMesh(const std::string& name) const;

		// ─── Cleanup ─────────────────────────────────────────────
		// Drop all cached resources.
		// Actual GPU cleanup happens when the last shared_ptr is released.
		void clear();

	private:
		std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;
		std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
		std::unordered_map<std::string, std::shared_ptr<Mesh>> m_meshes;
};
