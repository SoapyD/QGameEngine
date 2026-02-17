#include "engine/ecs/systems/render_system.h"
#include "engine/ecs/components.h"
#include <glad/glad.h>

void renderSystem(entt::registry& registry)
{
	auto view = registry.view<MeshRenderer>();

	for (auto [entity, mesh] : view.each())
	{
		glUseProgram(mesh.shaderId);
		glBindVertexArray(mesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
	}
};