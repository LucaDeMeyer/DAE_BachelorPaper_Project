#include "ShaderBindingTable.h"
#include "Vulkan/Core/GraphicsContext.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/Core/Buffer.h"
#include "Vulkan/Core/ResourceTypes.h"



namespace Core
{
	RT::ShaderBindingTable::ShaderBindingTable(GraphicsContext& context, ResourceManager& resManager, VkPipeline pipeline, uint32_t missCount, uint32_t hitCount)
        : m_context(context), m_resManager(resManager)
    {
        VkDevice device = m_context.getDevice();

        auto pfn_vkGetRayTracingShaderGroupHandlesKHR =
            (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR");

        if (!pfn_vkGetRayTracingShaderGroupHandlesKHR) {
            throw std::runtime_error("Failed to load vkGetRayTracingShaderGroupHandlesKHR!");
        }

        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
        rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &rtProps;
        vkGetPhysicalDeviceProperties2(m_context.getPhysicalDevice(), &props2);

        uint32_t handleSize = rtProps.shaderGroupHandleSize;
        uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
        uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;
        uint32_t handleSizeAligned = alignUp(handleSize, handleAlignment);

        uint32_t raygenCount = 1; 
        uint32_t groupCount = raygenCount + missCount + hitCount;
        uint32_t dataSize = groupCount * handleSize;

        std::vector<uint8_t> shaderHandleStorage(dataSize);

        if (pfn_vkGetRayTracingShaderGroupHandlesKHR(
            device, pipeline, 0, groupCount, dataSize, shaderHandleStorage.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to get ray tracing shader group handles");
        }

        uint32_t raygenRegionSize = alignUp(handleSizeAligned, baseAlignment);
        uint32_t missRegionSize = alignUp(handleSizeAligned * missCount, baseAlignment);
        uint32_t hitRegionSize = hitCount > 0 ? alignUp(handleSizeAligned * hitCount, baseAlignment) : 0;
        uint32_t sbtSize = raygenRegionSize + missRegionSize + hitRegionSize;

     
        Core::BufferDesc sbtDesc{};
        sbtDesc.name = "ShaderBindingTable";
        sbtDesc.size = sbtSize + baseAlignment;
        sbtDesc.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        sbtDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        sbtDesc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        m_bufferHandle = m_resManager.CreateBuffer(sbtDesc); 
            Core::BufferBuilder::Buffer* sbtBuf = m_resManager.GetBuffer(m_bufferHandle); 
            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = sbtBuf->buffer;
            VkDeviceAddress rawSbtAddress = vkGetBufferDeviceAddress(device, &addressInfo);

            VkDeviceAddress sbtAddress = (rawSbtAddress + baseAlignment - 1) & ~(static_cast<VkDeviceAddress>(baseAlignment) - 1);

          
            uint32_t alignmentOffset = static_cast<uint32_t>(sbtAddress - rawSbtAddress);

          
            uint8_t* pData = static_cast<uint8_t*>(sbtBuf->mapped) + alignmentOffset;
            uint32_t handleIdx = 0;

        memcpy(pData, shaderHandleStorage.data() + handleIdx * handleSize, handleSize);
        handleIdx++;

        // Miss
        for (uint32_t i = 0; i < missCount; ++i) {
            memcpy(pData + raygenRegionSize + i * handleSizeAligned,
                shaderHandleStorage.data() + handleIdx * handleSize, // source uses raw handleSize!
                handleSize); // copy only the true handle bytes
            handleIdx++;
        }

        // Hit
        for (uint32_t i = 0; i < hitCount; ++i) {
            memcpy(pData + raygenRegionSize + missRegionSize + i * handleSizeAligned,
                shaderHandleStorage.data() + handleIdx * handleSize, // source uses raw handleSize!
                handleSize); // copy only the true handle bytes
            handleIdx++;
        }

        m_raygenRegion.deviceAddress = sbtAddress;
        m_raygenRegion.stride = raygenRegionSize;
        m_raygenRegion.size = raygenRegionSize;

        m_missRegion.deviceAddress = sbtAddress + raygenRegionSize;
        m_missRegion.stride = handleSizeAligned;
        m_missRegion.size = missRegionSize;

        if (hitCount > 0) {
            m_hitRegion.deviceAddress = sbtAddress + raygenRegionSize + missRegionSize;
            m_hitRegion.stride = handleSizeAligned;
            m_hitRegion.size = hitRegionSize;
        }

        m_callableRegion = {};
    }

    void RT::ShaderBindingTable::Shutdown()
    {
        if (m_bufferHandle.IsValid()) {
            m_resManager.DestroyBuffer(m_bufferHandle); 
        }
    }
}
