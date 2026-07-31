#include "Allocator.h"
#include "GraphicsContext.h" 
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

Core::Allocator::Allocator(GraphicsContext& ctx) : context(ctx) {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = ctx.getPhysicalDevice();
    allocatorInfo.device = ctx.getDevice();
    allocatorInfo.instance = ctx.getInstance();
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3; 
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA Allocator");
    }
}


VmaPool Core::Allocator::createPool(uint32_t memoryTypeIndex, VmaPoolCreateFlags flags) {
    VmaPoolCreateInfo poolInfo = {};
    poolInfo.memoryTypeIndex = memoryTypeIndex;
    poolInfo.flags = flags;

    VmaPool pool;
    if (vmaCreatePool(allocator, &poolInfo, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA Pool");
    }
    return pool;
}

void Core::Allocator::destroyPool(VmaPool pool) {
    vmaDestroyPool(allocator, pool);
}

VkBuffer Core::Allocator::createBuffer(
    const VkBufferCreateInfo& bufferInfo,
    const VmaAllocationCreateInfo& allocInfo,
    VmaAllocation& outAllocation,
    VmaAllocationInfo* outAllocInfo)
{
    VkBuffer rawBuffer = VK_NULL_HANDLE;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &rawBuffer, &outAllocation, outAllocInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer via VMA");
    }
    return rawBuffer;
}

void Core::Allocator::destroyBuffer(VkBuffer buffer, VmaAllocation allocation) {
    if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }
}
VkImage Core::Allocator::createImage(
    const VkImageCreateInfo& imageInfo,
    const VmaAllocationCreateInfo& allocInfo,
    VmaAllocation& outAllocation)
{
    VkImage rawImage = VK_NULL_HANDLE;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &rawImage, &outAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image via VMA");
    }
    return rawImage;
}

void Core::Allocator::destroyImage(VkImage image, VmaAllocation allocation) {
    if (image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image, allocation);
    }
}

std::pair<VkBuffer, VmaAllocation> Core::Allocator::createStagingBuffer(const void* data, VkDeviceSize size) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};

    VkBuffer buffer = createBuffer(bufferInfo, allocInfo, allocation, &allocationInfo);

    if (allocationInfo.pMappedData) {
        memcpy(allocationInfo.pMappedData, data, static_cast<size_t>(size));
    }
    else {
        void* mappedData = map(allocation);
        memcpy(mappedData, data, static_cast<size_t>(size));
        unmap(allocation);
    }

    return { buffer, allocation };
}

void Core::Allocator::flush(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size) {
    vmaFlushAllocation(allocator, allocation, offset, size);
}

void Core::Allocator::invalidate(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size) {
    vmaInvalidateAllocation(allocator, allocation, offset, size);
}

void Core::Allocator::setAllocationName(VmaAllocation allocation, const std::string& name) {
    vmaSetAllocationName(allocator, allocation, name.c_str());
}

void Core::Allocator::setObjectName(VkObjectType type, uint64_t handle, const std::string& name) {
    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(context.getDevice(), "vkSetDebugUtilsObjectNameEXT");

    if (func != nullptr) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = type;
        nameInfo.objectHandle = handle;
        nameInfo.pObjectName = name.c_str();

        func(context.getDevice(), &nameInfo);
    }
}

void Core::Allocator::unmap(VmaAllocation allocation) {
    vmaUnmapMemory(allocator, allocation);
}

Core::Allocator::MemoryStats Core::Allocator::GetStats() const {
    VmaTotalStatistics stats;
    vmaCalculateStatistics(allocator, &stats);

    return MemoryStats{
        stats.total.statistics.allocationBytes,
        stats.total.statistics.blockBytes,
        stats.total.statistics.allocationCount,
        stats.total.statistics.blockCount
    };
}

char* Core::Allocator::GetStatsString() {
    char* statsString = nullptr;
    vmaBuildStatsString(allocator, &statsString, true);
    return statsString;
}

void Core::Allocator::FreeStatsString(char* str) {
    vmaFreeStatsString(allocator, str);
}