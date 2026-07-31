#ifndef RESOURCE_TYPES_H
#define RESOURCE_TYPES_H
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>

namespace Core
{
  
    /// @brief A virtual handle used by the Render Graph to track textures safely.
    struct TextureHandle {
        uint32_t id = UINT32_MAX;
        bool IsValid() const { return id != UINT32_MAX; }
    };

    /// @brief A virtual handle used by the Render Graph to track buffers safely.
    struct BufferHandle {
        uint32_t id = UINT32_MAX;
        bool IsValid() const { return id != UINT32_MAX; }
    };

    /// @brief The blueprint for creating a Vulkan Image. 
    /// Used by the ResourceManager to allocate exact memory footprints.
    struct TextureDesc {
        std::string name;
        VkExtent3D extent{};
        VkFormat format{};
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspect{};
        VkImageCreateFlags flags = 0;
        uint32_t mipLevels{ 1 };
        uint32_t arrayLayers{ 1 };
        VkSampleCountFlagBits samples{ VK_SAMPLE_COUNT_1_BIT };
        bool isCube{ false };
    };

    /// @brief The blueprint for creating a Vulkan Buffer.
    struct BufferDesc {
        std::string name;
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        VmaMemoryUsage vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VmaAllocationCreateFlags vmaflags = 0;
    };
}
#endif