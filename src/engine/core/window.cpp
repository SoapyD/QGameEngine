#include "engine/core/window.h"
#include <iostream>

Window::Window(int width, int height, const std::string& title)
	: m_window(nullptr), m_width(width), m_height(height)
{
	// ─── Initialise GLFW ─────────────────────────────────────────
	if (!glfwInit())
	{
		std::cerr << "Failed to initialise GLFW" << std::endl;
		return;
	}

	// Tell GLFW we want OpenGL 4.6 Core Profile
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// ─── Create the window ───────────────────────────────────────
	m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!m_window)
	{
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return;
	}

	// make this window's OpenGL context current
	glfwMakeContextCurrent(m_window);

	// ─── Load OpenGL function pointers with GLAD ─────────────────
	if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cerr << "Failed to initialise GLAD" << std::endl;
		return;
	}

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK); // cull back faces (default)
	glFrontFace(GL_CCW); // counter-clockwise = front (default)

	// set the viewport and register the resize callback
	glViewport(0, 0, width, height);
	glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);

	// print GPU info
	std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
	std::cout << "GPU: " << glGetString(GL_RENDERER) << std::endl;
}

Window::~Window()
{
	if (m_window)
	{
		glfwDestroyWindow(m_window);
	}
	glfwTerminate();
} 

bool Window::shouldClose() const
{
	return glfwWindowShouldClose(m_window);
}

void Window::swapBuffers() 
{
	glfwSwapBuffers(m_window);
}

void Window::pollEvents() 
{
	glfwPollEvents();
}

// called whenever the window is resized
void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}