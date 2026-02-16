#include "engine/core/window.h"
#include <iostream>

int main() 
{
	Window window(1280, 720, "QEngine");

	float deltaTime = 0.0f;
	float lastFrame = 0.0f;

	while (!window.shouldClose())
	{
		float currentFrame = (float)glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		window.pollEvents();

		if (glfwGetKey(window.getHandle(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(window.getHandle(), true);
		}

		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		window.swapBuffers();

	}
	return 0;
}