#include "engine/renderer/camera.h"
#include <algorithm> // for std::clamp

Camera::Camera(glm::vec3 position)
	: m_position(position)
	, m_front(glm::vec3(0.0f, 0.0f, -1.0f))
	, m_worldUp(glm::vec3(0.0f, 1.0f, 0.0f))
	, m_yaw(-90.0f) // -90 so we start looking down -Z (into the screen)
	, m_pitch(0.0f)
{
	updateVectors();
}

glm::mat4 Camera::getViewMatrix() const
{
	return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const
{
	return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

void Camera::processKeyboard(int direction, float deltaTime)
{
	float velocity = moveSpeed * deltaTime;

	// movement is relative to where the camera is facing
	switch (direction)
	{
		case FORWARD: m_position += m_front * velocity; break;
		case BACKWARD: m_position -= m_front * velocity; break;
		case LEFT: m_position -= m_right * velocity; break;
		case RIGHT: m_position += m_right * velocity; break;
	}
}

void Camera::processMouse(float xOffset, float yOffset)
{
	xOffset *= mouseSensitivity;
	yOffset *= mouseSensitivity;

	m_yaw += xOffset;
	m_pitch += yOffset;

	// clamp pitch so you can't look past straight up/down
	m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

	updateVectors();
}

void Camera::updateVectors()
{
	// calculate new front vector from yaw and pitch
	glm::vec3 front;
	front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
	front.y = sin(glm::radians(m_pitch));
	front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
	m_front = glm::normalize(front);

	// recalculate right and up vectors
	m_right = glm::normalize(glm::cross(m_front, m_worldUp));
	m_up = glm::normalize(glm::cross(m_right, m_front));
}