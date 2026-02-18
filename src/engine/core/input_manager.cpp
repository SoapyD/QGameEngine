#include "engine/core/input_manager.h"


void InputManager::init(GLFWwindow* window)
{
	m_window = window;

	// capture the mouse cursor - essential for FPS-style camera
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Store a pointer to this InputManager inside the GLFW window.
	// This is how the static callback can find our instance.
	// GLFW provides this "user pointer" slot specifically for this purpose.
	glfwSetWindowUserPointer(window, this);

	// register our static callback function
	glfwSetCursorPosCallback(window, mouseCallback);
}

void InputManager::update()
{
	// Reset per-frame mouse deltas.
	// If the mouse did not move this frame, the offsets should be zero,
	// not whatever they were last frame.
	m_mouseXOffset = 0.0f;
	m_mouseYOffset = 0.0f;
}

bool InputManager::isKeyPressed(int key) const
{
	return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool InputManager::isKeyReleased(int key) const
{
	return glfwGetKey(m_window, key) == GLFW_RELEASE;
}


// ─── Static callback ─────────────────────────────────────────────
// GLFW calls this whenever the mouse moves
// We retrieve our InputManager instance from the user pointer
void InputManager::mouseCallback(GLFWwindow* window, double xpos, double ypos)
{
	// retreive the InputManager instance we stored earlier
	auto* input = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
	if (!input) return;
	
	float x = static_cast<float>(xpos);
	float y = static_cast<float>(ypos);

	if (input->m_firstMouse)
	{
		input->m_lastMouseX = x;
		input->m_lastMouseY = y;
		input->m_firstMouse = false;
	}

	input->m_mouseXOffset = x - input->m_lastMouseX;
	input->m_mouseYOffset = input->m_lastMouseY - y; // reversed: y goes bottom-to-top in OpenGL
	input->m_lastMouseX = x;
	input->m_lastMouseY = y;
}