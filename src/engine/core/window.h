#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

class Window
{
	public: 
		// visible=false creates a hidden window — used by the headless harness
		// to get a GL context without showing anything on screen.
		Window(int width, int height, const std::string& title, bool visible = true);
		~Window();

		bool shouldClose() const;
		void swapBuffers();
		void pollEvents();

		GLFWwindow* getHandle() const { return m_window; }

		// True only if GLFW/GLAD init and window creation all succeeded.
		bool isValid() const { return m_window != nullptr; }

		// Query the live framebuffer size so callers (aspect ratio, HUD) stay
		// correct after a window resize.
		int getWidth() const { int w = m_width, h = m_height; if (m_window) glfwGetFramebufferSize(m_window, &w, &h); return w; }
		int getHeight() const { int w = m_width, h = m_height; if (m_window) glfwGetFramebufferSize(m_window, &w, &h); return h; }

	private:
		GLFWwindow* m_window;
		int m_width;
		int m_height;

		static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
};
