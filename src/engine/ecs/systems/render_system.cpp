#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

void renderSystem(entt::registry& registry, const Camera& camera, float aspectRatio)
{
	glm::mat4 view = camera.getViewMatrix();
	glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

	auto viewGroup = registry.view<Position, MeshRenderer>();

	for (auto [entity, pos, mesh] : viewGroup.each())
	{
		// build the model matrix from the entity's position (and rotation/scale if present)
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

		// we need to set uniforms via the raw OpenGL calls here
		// since we only have the shader ID, not the shader object
		GLint modelLoc = glGetUniformLocation(mesh.shaderId, "model");
		GLint viewLoc = glGetUniformLocation(mesh.shaderId, "view");
		GLint projLoc = glGetUniformLocation(mesh.shaderId, "projection");

		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, &projection[0][0]);

		glBindVertexArray(mesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
	}
};