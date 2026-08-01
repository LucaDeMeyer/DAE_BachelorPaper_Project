#ifndef SHADER_BINDING_TABLE_H
#define SHADER_BINDING_TABLE_H

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>
#include "Vulkan/Core/ResourceTypes.h"

namespace Core
{
    class GraphicsContext;
    class ResourceManager;
}
namespace Core::RT
{


    class ShaderBindingTable
    {
    public:
        ShaderBindingTable(GraphicsContext& context, ResourceManager& resManager, VkPipeline pipeline, uint32_t missCount, uint32_t hitCount);
        ~ShaderBindingTable() = default;

        // Cleans up the buffer handle via the ResourceManager
        void Shutdown();

        const VkStridedDeviceAddressRegionKHR* GetRaygenRegion() const { return &m_raygenRegion; }
        const VkStridedDeviceAddressRegionKHR* GetMissRegion() const { return &m_missRegion; }
        const VkStridedDeviceAddressRegionKHR* GetHitRegion() const { return &m_hitRegion; }
        const VkStridedDeviceAddressRegionKHR* GetCallableRegion() const { return &m_callableRegion; }

    private:
        GraphicsContext& m_context;
        ResourceManager& m_resManager;

        BufferHandle m_bufferHandle;

        VkStridedDeviceAddressRegionKHR m_raygenRegion{};
        VkStridedDeviceAddressRegionKHR m_missRegion{};
        VkStridedDeviceAddressRegionKHR m_hitRegion{};
        VkStridedDeviceAddressRegionKHR m_callableRegion{};

        uint32_t alignUp(uint32_t size, uint32_t alignment) const {
            return (size + alignment - 1) & ~(alignment - 1);
        }
    };
}

#endif