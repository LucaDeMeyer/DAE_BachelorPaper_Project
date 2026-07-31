#include "Swapchain.h"
#include "Vulkan/Utils/Utils.h" 
#include "GraphicsContext.h"
#include <algorithm>
#include <stdexcept>
#include <limits>

Core::SwapChainBuilder::SwapChain Core::SwapChainBuilder::build()
{
    SwapChain result;

    VkPhysicalDevice physicalDevice = context.getPhysicalDevice();
    VkSurfaceKHR surface = context.getSurface();
    VkDevice device = context.getDevice();

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    if (formatCount != 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount != 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    }

    VkSurfaceFormatKHR chosenFormat = surfaceFormats[0];
    for (const auto& format : surfaceFormats) {
        if (format.format == preferredFormat && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = format;
            break;
        }
    }
    result.format = chosenFormat.format;

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : presentModes) {
        if (mode == preferredPresentMode) {
            chosenPresentMode = mode;
            break;
        }
    }

    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        result.extent = surfaceCapabilities.currentExtent;
    }
    else {
        result.extent = extent.value();
        result.extent.width = std::clamp(result.extent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
        result.extent.height = std::clamp(result.extent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    }

    uint32_t minImageCount = std::max(imageCount, surfaceCapabilities.minImageCount);
    if (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface; 
    createInfo.minImageCount = minImageCount;
    createInfo.imageFormat = chosenFormat.format;
    createInfo.imageColorSpace = chosenFormat.colorSpace;
    createInfo.imageExtent = result.extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; 
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;

    createInfo.preTransform = surfaceCapabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; 
    createInfo.presentMode = chosenPresentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &result.swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain!");
    }

    uint32_t realImageCount;
    vkGetSwapchainImagesKHR(device, result.swapchain, &realImageCount, nullptr);
    result.images.resize(realImageCount);
    vkGetSwapchainImagesKHR(device, result.swapchain, &realImageCount, result.images.data());

    result.imageViews.resize(result.images.size());
    for (size_t i = 0; i < result.images.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = result.images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = result.format;

        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &result.imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain image view!");
        }
    }

    result.depthFormat = Utils::findDepthFormat(physicalDevice);

    result.depthImageBuilder.emplace(context);

    result.depthImageBuilder->setExtent(result.extent.width, result.extent.height)
        .setFormat(result.depthFormat)
        .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        .setAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT);

    result.depthImage.emplace(result.depthImageBuilder->build());

    return result;
}