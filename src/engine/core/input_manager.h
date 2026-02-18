#pragma once
#include <GLFW/glfw3.h>

class InputManager
{
	public:
		// initialise the input manager and register GLFW callback
		void init(GLFWwindow* window);

		// call once per frame to reset per-frame state (mouse deltas)
		void update();

		// ─── Key queries ─────────────────────────────────────────
		bool isKeyPressed(int key) const;
		bool isKeyReleased(int key) const;

		// ─── Mouse queries ───────────────────────────────────────
		float getMouseXOffset() const { return m_mouseXOffset; }
		float getMouseYOffset() const { return m_mouseYOffset; }
		float getMouseX() const { return m_lastMouseX; }
		float getMouseY() const { return m_lastMouseY; }

	private:
		GLFWwindow* m_window = nullptr;

		// ─── Mouse state ─────────────────────────────────────────
		float m_lastMouseX = 640.0f;
		float m_lastMouseY = 360.0f;
		float m_mouseXOffset = 0.0f;
		float m_mouseYOffset = 0.0f;
		bool m_firstMouse = true;

		// GLFW callback - must be static (C function pointer requirement)
		static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
};
