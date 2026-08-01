#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>

#include "ResourceTypes.h"

namespace Core
{
	class ResourceManager;
}

namespace RenderTypes
{
    /// @brief The standard vertex format for all 3D geometry in the engine.
    /// Interleaved layout for optimal GPU cache fetching.
    struct Vertex {
        glm::vec3 position;
        glm::vec3 color;
        glm::vec3 normal;
        glm::vec2 texCoord;
        glm::vec3 tangent;
        glm::vec3 biTangent;

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 6> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};

            // Position
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, position);

            // Color
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            // Normal
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, normal);

            // TexCoord
            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

            // Tangent 
            attributeDescriptions[4].binding = 0;
            attributeDescriptions[4].location = 4;
            attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[4].offset = offsetof(Vertex, tangent);

            // BiTangent
            attributeDescriptions[5].binding = 0;
            attributeDescriptions[5].location = 5;
            attributeDescriptions[5].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[5].offset = offsetof(Vertex, biTangent);

            return attributeDescriptions;
        }
    };


    struct PhysicalCameraSettings {
        float aperture = 2.8f;
        float shutterSpeed = 1.0f / 60.0f;
        float iso = 400.0f;
    };


    struct RenderContext
    {
        VkCommandBuffer cmd;
        Core::ResourceManager* resourceManager;
        uint32_t currentFrameIndex;
        float deltaTime;
        int debugViewMode;
        PhysicalCameraSettings cameraSettings;
    };

    /// @brief Global View/Projection matrices (Aligned to GLSL std140).
    struct CameraUBO {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 invView;
        alignas(16) glm::mat4 invProj;
        alignas(16) glm::mat4 invViewProj;
    };

    /// @brief Per-instance data for hardware instancing (Aligned to GLSL std140).
    struct InstanceData {
        alignas(16) glm::mat4 model;
        alignas(16) uint32_t materialID;
        uint32_t padding[3]; // Explicit padding to meet the 16-byte alignment requirement
    };

    /// @brief Ultra-fast constant data sent directly to the shader registers.
    /// Uses Vulkan 1.2 Buffer Device Address (BDA) for bindless geometry fetching.
    struct PassPushConstants {
        VkDeviceAddress vertexAddress; // 8 bytes
        uint32_t instanceID;           // 4 bytes
        uint32_t lightIndex;
    };

    /// @brief PBR Point Light data. Packed into vec4s for perfect std430 alignment.
    struct PointLight 
    {
        glm::vec4 position; // xyz = position, w = radius
        glm::vec4 color;    // xyz = color, w = luminance
    };

    /// @brief PBR Directional Light data (The Sun).
    struct DirectionalLight {
        glm::vec4 direction; // xyz = direction, w = lux
        glm::vec4 color;     // xyz = color, w = unused
    };

    /// @brief A massive Storage Buffer (SSBO) containing all scene lighting.
    struct LightSSBO
    {
        DirectionalLight sun;
        uint32_t pointLightCount;
        float padding[3];   // Aligns the array below to a 16-byte boundary!
        PointLight pointLights[1024];
    };

    /// @brief CPU-side storage for Cascaded Shadow Map (CSM) split calculations.
    struct CascadeData {
        glm::mat4 lightSpaceMatrices[4];
        float splitDepths[4];
    };

    /// @brief GPU-side uniform buffer for CSMs. 
    /// Depths packed into a single vec4 to satisfy GLSL alignment rules.
    struct CascadeUBO {
        glm::mat4 lightSpaceMatrices[4];
        glm::vec4 splitDepths;
    };

    struct PointShadowUBO
    {
        glm::mat4 pointLightMatrices[10][6]; // one for each face
    };

    struct LightingDebugPushConstant
    {
        int debugMode;
    };

    struct ShaderBindingTable {
        Core::BufferHandle bufferHandle;
        VkStridedDeviceAddressRegionKHR raygenRegion{};
        VkStridedDeviceAddressRegionKHR missRegion{};
        VkStridedDeviceAddressRegionKHR hitRegion{};
        VkStridedDeviceAddressRegionKHR callableRegion{};
    };
}
#endif
