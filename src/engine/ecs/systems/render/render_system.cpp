#include "engine/ecs/systems/render/render_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

void renderSystem(entt::registry& registry, const Camera& camera,
	float aspectRatio, float alpha)
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
		dirAmbient = light.ambientStrength;
		hasDirLight = true;
		break; // only use the first one
	}

	// ─── Collect point lights ────────────────────────────────────
	struct PointLightGPU
	{
		glm::vec3 position;
		glm::vec3 color;
		float ambient;
		float linear;
		float quadratic;
	};

	constexpr int MAX_POINT_LIGHTS = 8;
	PointLightGPU pointLightsData[MAX_POINT_LIGHTS];
	int numPointLights = 0;
	
	auto pointView = registry.view<Position, PointLight>();
	for (auto [entity, pos, light] : pointView.each())
	{
		if (numPointLights >= MAX_POINT_LIGHTS) break;

		pointLightsData[numPointLights] =
		{
			pos.value,
			light.color,
			light.ambientStrength,
			light.linear,
			light.quadratic
		};
		numPointLights++;
	}

	// ─── Draw meshes ─────────────────────────────────────────────
	auto meshView = registry.view<Position, MeshRenderer>();

	for (auto [entity, pos, mesh] : meshView.each())
	{
		// Interpolate between the previous and current tick position. Snap
		// (skip interpolation) on large deltas so teleports/respawns don't
		// streak across the level.
		glm::vec3 renderPos = pos.value;
		if (registry.all_of<PrevPosition>(entity))
		{
			const glm::vec3& prev = registry.get<PrevPosition>(entity).value;
			if (glm::distance(prev, pos.value) < 3.0f)
				renderPos = glm::mix(prev, pos.value, alpha);
		}

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, renderPos);

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

		// Light uniforms — each light type has its own uniform names
		loc = glGetUniformLocation(mesh.shaderId, "hasDirLight");
		glUniform1i(loc, hasDirLight ? 1 : 0);
		if (hasDirLight)
		{
			loc = glGetUniformLocation(mesh.shaderId, "dirLightDir");
			glUniform3fv(loc, 1, &dirLightDir[0]);
			loc = glGetUniformLocation(mesh.shaderId, "dirLightColor");
			glUniform3fv(loc, 1, &dirLightColor[0]);
			loc = glGetUniformLocation(mesh.shaderId, "dirLightAmbient");
			glUniform1f(loc, dirAmbient);
		}

		loc = glGetUniformLocation(mesh.shaderId, "numPointLights");
		glUniform1i(loc, numPointLights);

		for (int i = 0; i < numPointLights; i++)
		{
			std::string prefix = "pointLights[" +std::to_string(i) + "].";
			loc = glGetUniformLocation(mesh.shaderId, (prefix + "position").c_str());
			glUniform3fv(loc, 1, &pointLightsData[i].position[0]);
		
			loc = glGetUniformLocation(mesh.shaderId, (prefix + "color").c_str());
			glUniform3fv(loc, 1, &pointLightsData[i].color[0]);

			loc = glGetUniformLocation(mesh.shaderId, (prefix + "ambient").c_str());
			glUniform1f(loc, pointLightsData[i].ambient);

			loc = glGetUniformLocation(mesh.shaderId, (prefix + "linear").c_str());
			glUniform1f(loc, pointLightsData[i].linear);

			loc = glGetUniformLocation(mesh.shaderId, (prefix + "quadratic").c_str());
			glUniform1f(loc, pointLightsData[i].quadratic);
		}

		// bind texture if present
		if (mesh.textureId != 0)
		{
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, mesh.textureId);
			glUniform1i(glGetUniformLocation(mesh.shaderId, "textureSampler"), 0);
		}
		// Flat colour override for debug wireframes (+ enemy hit flash)
		loc = glGetUniformLocation(mesh.shaderId, "colorOverride");
		if (registry.all_of<TagDebugWireframe>(entity))
			glUniform4f(loc, 0.0f, 1.0f, 0.0f, 1.0f);  // bright green
		else if (const DamageFlash* f = registry.try_get<DamageFlash>(entity); f && f->timer > 0.0f)
			glUniform4f(loc, 1.0f, 1.0f, 1.0f, 1.0f);  // hit flash — brief flat white
		else
			glUniform4f(loc, 0.0f, 0.0f, 0.0f, 0.0f);   // disabled (normal lighting)

		// Flat lit albedo for Colour'd entities (e.g. weapon-pickup gun meshes); set per-entity so it can't leak.
		bool hasColour = registry.all_of<Colour>(entity);
		glUniform1i(glGetUniformLocation(mesh.shaderId, "useAlbedo"), hasColour ? 1 : 0);
		if (hasColour)
			glUniform3fv(glGetUniformLocation(mesh.shaderId, "albedoColor"), 1,
				&registry.get<Colour>(entity).value[0]);

		// Switch to wireframe if this is a debug entity
		bool wireframe = registry.all_of<TagDebugWireframe>(entity);
		if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		glBindVertexArray(mesh.vao);

		if (mesh.useIndices)
		{
			glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
		}
		else
		{
			glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
		}

		// Switch back to filled rendering for the next entity
		if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
};