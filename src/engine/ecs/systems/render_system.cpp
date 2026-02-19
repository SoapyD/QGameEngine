#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

void renderSystem(entt::registry& registry, const Camera& camera, 
	float aspectRatio)
{
	glm::mat4 view = camera.getViewMatrix();
	glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

	// ─── Find lights ─────────────────────────────────────────────
	// directional light (use first one found)
	glm::vec3 dirLightDir(0.0f);
	glm::vec3 dirLightColor(0.0f);
	float dirAmbient = 0.1f;
	bool hasDirLight = false;

	auto dirView = registry.view<DirectionalLight>();
	for (auto [entity, light] : dirView.each())
	{
		dirLightDir = light.direction;
		dirLightColor = light.color;
		dirAmbient = light.ambienStrength;
		hasDirLight = true;
		break; // only use the first one
	}

	// point light (use first one found)
	glm::vec3 pointLightPos(0.0f);
	glm::vec3 pointLightColor(0.0f);
	float pointAmbient = 0.05f;
	bool hasPointLight = false;
	
	auto pointView = registry.view<Position, PointLight>();

	for (auto [entity, pos, light] : pointView.each())
	{
		pointLightPos = pos.value;
		pointLightColor = light.color;
		pointAmbient = light.ambienStrength;
		hasPointLight = true;
		break;
	}

	// ─── Draw meshes ─────────────────────────────────────────────
	auto meshView = registry.view<Position, MeshRenderer>();

	for (auto [entity, pos, mesh] : meshView.each())
	{
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, pos.value);

		if (registry.all_of<Rotation>(entity))
		{
			auto& rot = registry.get<Rotation>(entity);
			model = glm::rotate(model, glm::radians(rot.euler.y), glm::vec3(0, 1, 0));
			model = glm::rotate(model, glm::radians(rot.euler.x), glm::vec3(1, 0, 0));
			model = glm::rotate(model, glm::radians(rot.euler.z), glm::vec3(0, 0, 1));
		}

		if (registry.all_of<Scale>(entity))
		{
			auto& scl = registry.get<Scale>(entity);
			model = glm::scale(model, scl.value);
		}

		glUseProgram(mesh.shaderId);

		// transform uniforms
		GLint loc;
		
		// we need to set uniforms via the raw OpenGL calls here
		// since we only have the shader ID, not the shader object
		loc = glGetUniformLocation(mesh.shaderId, "model");
		glUniformMatrix4fv(loc, 1, GL_FALSE, &model[0][0]);
		loc = glGetUniformLocation(mesh.shaderId, "view");
		glUniformMatrix4fv(loc, 1, GL_FALSE, &view[0][0]);
		loc = glGetUniformLocation(mesh.shaderId, "projection");
		glUniformMatrix4fv(loc, 1, GL_FALSE, &projection[0][0]);

		// camera position (for specular)
		loc = glGetUniformLocation(mesh.shaderId, "viewPos");
		glUniform3fv(loc, 1, &camera.getPosition()[0]);

		// Material
		loc = glGetUniformLocation(mesh.shaderId, "shininess");
		glUniform1f(loc, 32.0f);

		// light uniforms (use directional light, or pointlight or both)
		if (hasDirLight)
		{
			loc = glGetUniformLocation(mesh.shaderId, "lightType");
			glUniform1i(loc, 0);
			loc = glGetUniformLocation(mesh.shaderId, "lightDir");
			glUniform3fv(loc, 1, &dirLightDir[0]);
			loc = glGetUniformLocation(mesh.shaderId, "lightColor");
			glUniform3fv(loc, 1, &dirLightColor[0]);
			loc = glGetUniformLocation(mesh.shaderId, "ambientStrength");
			glUniform1f(loc, dirAmbient);
		}

		if (hasPointLight)
		{
			loc = glGetUniformLocation(mesh.shaderId, "lightType");
			glUniform1i(loc, 1);
			loc = glGetUniformLocation(mesh.shaderId, "lightPos");
			glUniform3fv(loc, 1, &pointLightPos[0]);
			loc = glGetUniformLocation(mesh.shaderId, "lightColor");
			glUniform3fv(loc, 1, &pointLightColor[0]);
			loc = glGetUniformLocation(mesh.shaderId, "ambientStrength");
			glUniform1f(loc, pointAmbient);
		}

		// bind texture if present
		if (mesh.textureId != 0)
		{
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, mesh.textureId);
			glUniform1i(glGetUniformLocation(mesh.shaderId, "textureSampler"), 0);
		}

		glBindVertexArray(mesh.vao);

		if (mesh.useIndices)
		{
			glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
		}
		else
		{
			glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
		}
	}
};