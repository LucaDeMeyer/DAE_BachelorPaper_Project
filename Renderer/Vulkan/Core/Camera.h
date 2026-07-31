#ifndef CAMERA_H
#define CAMERA_H
#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/fwd.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#define GLFW_INCLUDE_VULKAN 
#include <glm/trigonometric.hpp>

#include "GLFW/glfw3.h"

namespace Core
{
	class Camera final {
	public:
		Camera() = default;
		Camera(const glm::vec3 origin, float fovAngle) : origin(origin), fovAngle(fovAngle) {}
		~Camera() = default;
		Camera(const Camera&) = delete;
		Camera& operator=(const Camera&) = delete;

		void InitCamera(float ar, float _fovAngle, float _nearPlane, float _farPlane, glm::vec3 _origin = { 0.0f, 0.0f, 0.0f },float _speed = .01f);
		void CalcViewMatrix();
		void CalcProjectionMatrix();

		void ProcessKeyboard(GLFWwindow* window, float deltaTime);
		void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch);
		void ProcessMouseScroll(float yoffset);

		void Update(float deltaTime) {
			CalcViewMatrix();
		}


		static bool IsOutsideFrustum(const glm::vec3& point, const glm::vec3& frustumCenter, float frustumRadius)
		{
			return glm::distance(point, frustumCenter) > frustumRadius;
		}

		glm::mat4 GetViewMatrix() const {
			return viewMatrix;
		}

		glm::mat4 GetProjectionMatrix() const {
			return projectionMatrix;
		}

		glm::mat4 GetInverseViewMatrix() const {
			return inverseViewMatrix;
		}

		glm::vec3 GetPosition() const {
			return position;
		}

		glm::vec3 GetForward() const {
			return forward;
		}

		glm::vec3 GetRight() const {
			return right;
		}

		glm::vec3 GetUp() const {
			return up;
		}

		void SetAspectRation(float ar) {
			aspectRatio = ar;
			CalcProjectionMatrix();
		}
		float GetFov() const {
			return fovAngle;
		}
		float GetNear() const {
			return nearplane;
		}
		float GetFar() const {
			return farplane;
		}
		float GetAspectRatio() const {
			return aspectRatio;
		}

		void SetFOV(float FOV)
		{
			fovAngle = FOV;

			fov = tanf(glm::radians(fovAngle) / 2.0f);

			CalcProjectionMatrix();
		}
		float GetSpeed() const { return speed; }
		void SetSpeed(float newspeed)
		{
			speed = newspeed;
		}

	private:

		void UpdateCameraVectors();

		glm::vec3 origin{ 0.0f, 0.0f, 0.0f };
		glm::mat4 projectionMatrix{ 1.0f };
		glm::mat4 viewMatrix{ 1.0f };
		glm::mat4 inverseViewMatrix{ 1.0f };

		glm::vec3 up{ 0.0f, 1.0f, 0.0f };
		glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
		glm::vec3 right{ 1.0f, 0.0f, 0.0f };
		glm::vec3 position{ 0.0f, 0.0f, 0.0f };

		float fovAngle{ 45.0f };
		float fov{};

		float aspectRatio{ 16.0f / 9.0f };

		float nearplane{ .1f };
		float farplane{ 2000.f };

		float speed{ .1f };
		float sensitivity{ 0.1f };

		float yaw{ -90.0f };
		float pitch{ 0.0f };
	};
}
#endif 