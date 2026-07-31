#include "Camera.h"
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

void Core::Camera::InitCamera(float ar, float _fovAngle, float _nearPlane, float _farPlane, glm::vec3 _origin,float _speed)
{
	fovAngle = _fovAngle;
	fov = tanf(glm::radians(fovAngle) / 2.0f);
	aspectRatio = ar;
	origin = _origin;
	position = _origin;
	speed = _speed;
	nearplane = _nearPlane;
	farplane = _farPlane;

	UpdateCameraVectors();
	CalcViewMatrix();
	CalcProjectionMatrix();
}

void Core::Camera::CalcViewMatrix()
{
	viewMatrix = glm::lookAt(position, position + forward, up);
	inverseViewMatrix = glm::inverse(viewMatrix);
}

void Core::Camera::CalcProjectionMatrix()
{
	projectionMatrix = glm::perspective(glm::radians(fovAngle), aspectRatio, nearplane, farplane);
	projectionMatrix[1][1] *= -1;
}

void Core::Camera::ProcessKeyboard(GLFWwindow* window, float deltaTime)
{
	float velocity = speed * deltaTime;
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		position += forward * velocity;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		position -= forward * velocity;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		position -= right * velocity;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		position += right * velocity;
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		position += up * velocity;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		position -= up * velocity;

	CalcViewMatrix();
}

void Core::Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	if (constrainPitch) {
		pitch = glm::clamp(pitch, -89.0f, 89.0f);
	}

	UpdateCameraVectors();
	CalcViewMatrix();
}

void Core::Camera::ProcessMouseScroll(float yoffset)
{
	fovAngle -= yoffset;
	fovAngle = glm::clamp(fovAngle, 1.0f, 120.0f);
	fov = glm::tan(glm::radians(fovAngle) / 2.0f);

	CalcProjectionMatrix();
}

void Core::Camera::UpdateCameraVectors()
{
	glm::vec3 front;
	front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	forward = glm::normalize(front);
	
	right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
	up = glm::normalize(glm::cross(right, forward));
}
