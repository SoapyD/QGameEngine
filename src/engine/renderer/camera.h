#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
	public:
		Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f));

		// get the view matric for this frame
		glm::mat4 getViewMatrix() const;

		// get the projection matrix
		glm::mat4 getProjectionMatrix(float aspectRatio) const;

		// process input
		void processKeyboard(int direction, float deltaTime);
		void processMouse(float xOffset, float yOffset);

		glm::vec3 getPosition() const { return m_position; }
		glm::vec3 getFront() const { return m_front; }

		// movement directions )used as the 'direction' parameter)
		static constexpr int FORWARD = 0;
		static constexpr int BACKWARD = 1;
		static constexpr int LEFT = 2;
		static constexpr int RIGHT = 3;

		float fov = 70.0f;
		float moveSpeed = 5.0f;
		float mouseSensitivity = 0.1f;
		float nearPlane = 0.1f;
		float farPlane = 1000.0f;

	private:
		glm::vec3 m_position;
		glm::vec3 m_front;
		glm::vec3 m_up;
		glm::vec3 m_right;
		glm::vec3 m_worldUp;

		float m_yaw; // horizontal rotation (look left/right)
		float m_pitch; // vertical rotation (look up/down)

		void updateVectors();
};
