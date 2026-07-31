#include "Buffer.h"
#include "Vulkan/Utils/Utils.h"
#include "GraphicsContext.h"
#include "Allocator.h"

Core::BufferBuilder::Buffer::~Buffer() {
    destroy();
}

void Core::BufferBuilder::Buffer::destroy() {
    if (buffer != VK_NULL_HANDLE && allocator != nullptr) {
        allocator->destroyBuffer(buffer, allocation);
    }
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

void* Core::BufferBuilder::Buffer::map() {
    if (isPersistent && mapped) {
        return mapped;
    }

    if (!mapped && allocator) {
        mapped = allocator->map(allocation);
    }
    return mapped;
}

void Core::BufferBuilder::Buffer::unmap() {
    if (isPersistent) {
        return; 
    }

    if (mapped && allocator) {
        allocator->unmap(allocation);
        mapped = nullptr;
    }
}

void Core::BufferBuilder::Buffer::flush(VkDeviceSize offset, VkDeviceSize size) {
    if (allocator && allocation) {
        allocator->flush(allocation, offset, size);
    }
}

void Core::BufferBuilder::Buffer::setDebugName(const std::string& name) {
    if (allocator && allocation) {
        allocator->setAllocationName(allocation, name);
        allocator->setObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, name);
    }
}

Core::BufferBuilder::Buffer Core::BufferBuilder::build() {
    Buffer result;
    result.size = bufferSize;
    result.allocator = context.getAllocator();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = bufferUsage;
    bufferInfo.sharingMode = sharingMode;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = vmaUsage;
    allocInfo.flags = vmaFlags;

    if (vmaUsage == VMA_MEMORY_USAGE_AUTO) {
        if (memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
    }

    VmaAllocationInfo resultInfo;

    result.buffer = result.allocator->createBuffer(
        bufferInfo, allocInfo, result.allocation, &resultInfo);
    if (resultInfo.pMappedData) {
        result.mapped = resultInfo.pMappedData;
        result.isPersistent = true;
    }

    return result;
}