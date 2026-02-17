#include "engine/core/window.h"
#include "engine/renderer/shader.h"
#include <iostream>

int main() 
{
	Window window(1280, 720, "QEngine");

	// ─── Shader ──────────────────────────────────────────────────
	Shader basicShader(
		"assets/shaders/basic.vert",
		"assets/shaders/basic.frag"
	);

	// ─── Triangle vertex data ────────────────────────────────────
	float vertices[] =
	{
		// positions	// colours
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // Bottom-left  (red)
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // Bottom-right (green)
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // Top          (blue)
    };

	// ─── Create VAO and VBO ──────────────────────────────────────
	unsigned int VAO, VBO;

	glGenVertexArrays(1, &VAO); // Generate 1 VAO
	glGenBuffers(1, &VBO); // Generate 1 VBO

	// bind the vao first = it will "record" subsequent VBO and attribute config
	glBindVertexArray(VAO);

	// Bind the VBO and upload vetex data to GPU
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	// tell openGL to interpret the vertex data:

	// attribute 0: Position (3 floats, starting at offset 0)
	glVertexAttribPointer(
		0,                    // Attribute index (matches layout(location = 0) in shader)
		3,                    // Number of components (vec3 = 3 floats)
		GL_FLOAT,             // Data type
		GL_FALSE,             // Normalise? No
		6 * sizeof(float),    // Stride: bytes between consecutive vertices (6 floats total)
		(void*)0              // Offset: where this attribute starts within each vertex
	);
	glEnableVertexAttribArray(0);

	// attribute 1: colour (3 floats, starting at offset 3 floats in)
    glVertexAttribPointer(
		1, 
		3, 
		GL_FLOAT, 
		GL_FALSE, 
		6 * sizeof(float),
        (void*)(3 * sizeof(float))
	);
	glEnableVertexAttribArray(1);

	// unbind (optional, for safety)
	glBindVertexArray(0);

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

		// draw the traignle
		basicShader.use(); // activate our shader
		glBindVertexArray(VAO); // bind our vertex data
		glDrawArrays(GL_TRIANGLES, 0, 3); // draw 3 vertices as a triangle

		window.swapBuffers();

	}

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	return 0;
}