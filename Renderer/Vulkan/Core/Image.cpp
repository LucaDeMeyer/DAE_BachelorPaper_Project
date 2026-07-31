#include "Image.h"
#include "GraphicsContext.h"
#include "Allocator.h"
#include <stdexcept>

Core::ImageBuilder::Image::~Image() {
    destroy();
}

void Core::ImageBuilder::Image::setDebugName(const std::string& name) {
    if (allocator && allocation) {
        allocator->setAllocationName(allocation, name);

        allocator->setObjectName(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, name);

        if (view != VK_NULL_HANDLE) {
            allocator->setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)view, name + "_View");
        }
    }
}

void Core::ImageBuilder::Image::destroy() {
    if (view != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }

    if (image != VK_NULL_HANDLE && allocator != nullptr) {
        allocator->destroyImage(image, allocation);
        image = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}

Core::ImageBuilder::Image Core::ImageBuilder::build() {
    Image result;
    result.allocator = context.getAllocator();
    result.device = context.getDevice(); 

    result.format = format;
    result.extent = extent;
    result.mipLevels = mipLevels;

    VkImageViewType finalViewType = VK_IMAGE_VIEW_TYPE_2D;

    if (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
        finalViewType = (layerCount > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
    }
    else if (layerCount > 1) {
        finalViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = flags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { extent.width, extent.height, 1 };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = layerCount;
    imageInfo.samples = sampleCount;
    imageInfo.tiling = imageTiling;
    imageInfo.usage = imageUsage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    result.image = result.allocator->createImage(imageInfo, allocInfo, result.allocation);

    if (createImageView) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = result.image;
        viewInfo.viewType = finalViewType;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = layerCount;

        if (vkCreateImageView(result.device, &viewInfo, nullptr, &result.view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image view!");
        }
    }

    return result;
}