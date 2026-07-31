#ifndef SCENE_H
#define SCENE_H
#include <vector>
#include "Object.h"
#include "RenderTypes.h" 
namespace Core {
    /// @brief The high-level container for all visible entities, lights, and camera data.
    /// The application populates this, and the Render Graph consumes it to draw the frame.
    class Scene {
    public:
        Scene() = default;

        void AddObject(const Object& obj) {
            m_objects.push_back(obj);
        }

        void UpdateCamera(const glm::mat4& view, const glm::mat4& projection) {
            m_cameraUBO.view = view;
            m_cameraUBO.proj = projection;
        }

        const std::vector<Object>& GetObjects() const { return m_objects; }
        const RenderTypes::CameraUBO& GetCameraUBO() const { return m_cameraUBO; }

        VkDescriptorSet globalDescriptorSet = VK_NULL_HANDLE;

        void UpdateSceneTransform()
        {
            glm::mat4 sceneMatrix = glm::mat4(1.0f);

            sceneMatrix = glm::translate(sceneMatrix, m_globalPosition);

            sceneMatrix = glm::rotate(sceneMatrix, glm::radians(m_globalRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            sceneMatrix = glm::rotate(sceneMatrix, glm::radians(m_globalRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            sceneMatrix = glm::rotate(sceneMatrix, glm::radians(m_globalRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

            sceneMatrix = glm::scale(sceneMatrix, m_globalScale);
            for (auto& obj : m_objects) {

                obj.UpdateLocalTransform();
                obj.transform = sceneMatrix * obj.localTransform;
            }
        }
    	
    	std::vector<RenderTypes::PointLight>& GetPointLights()  { return pointLights; }
        RenderTypes::DirectionalLight& GetDirectionalLight()  { return directionalLight; }

        RenderTypes::PointLight AddPointLight(glm::vec3 pos, glm::vec3 color, float lumen, float radius)
        {
	        RenderTypes::PointLight light;
            light.position = { pos,radius };
            light.color = { color,lumen };
            pointLights.push_back(light);
            return light;
        }

        RenderTypes::DirectionalLight AddDirectionalLight(glm::vec3 direction, glm::vec3 color, float lux)
        {
            directionalLight.direction = { direction,lux };
            directionalLight.color = { color,0 };
            return directionalLight;
        }

        void RemovePointLight(int index)
        {
            if (index >= 0 && index < pointLights.size()) {
                pointLights.erase(pointLights.begin() + index);
            }
        }

    private:
        std::vector<Object> m_objects;
        RenderTypes::CameraUBO m_cameraUBO;

        glm::vec3 m_globalPosition{ 0.0f, 0.0f, 0.0f };
        glm::vec3 m_globalRotation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 m_globalScale{ 1.0f };

        std::vector<RenderTypes::PointLight> pointLights;
        RenderTypes::DirectionalLight directionalLight;
    };
}
#endif