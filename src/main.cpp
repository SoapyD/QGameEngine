#include <iostream>

// Third-party libraries - verify they're found
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

int main()
{
	std::cout << "QEngine starting..." << std::endl;

	// quick test: create a GLM vector
	glm::vec3 position(1.0f, 2.0f, 3.0f);
	std::cout << "Position: " << position.x << ", " << position.y << ", " << position.z << std::endl;

	// Quick test: create an Entt registry
	entt::registry registry;
	auto entity = registry.create();
	std::cout << "Created entity: " << (uint32_t)entity << std::endl;

	std::cout << "All systems go!" << std::endl;
	return 0;
}