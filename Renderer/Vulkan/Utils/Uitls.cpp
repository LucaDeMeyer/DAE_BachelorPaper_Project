#include <stdexcept>

#include "Utils.h"
#include <fstream>
#include <glm/glm.hpp>
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

#include "Vulkan/Core/TextureLoader.h"
#include "Vulkan/Core/GraphicsContext.h"


// Vulkan separates GPU operations into different Queue Families.
// We need to find specific hardware queues that allow us to render 3D graphics AND present them to the OS window.
QueueFamilyIndices Utils::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        // Find a queue that supports drawing (Graphics)
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        // Find a queue that supports talking to the OS window system (Present)
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
        i++;
    }

    return indices;
}

// A GPU might support Vulkan, but the physical monitor might not support the specific color format or V-Sync mode we want.
// This function queries the surface (window) to see exactly what extents, colors, and presentation modes are legal.
SwapChainSupportDetails Utils::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

// We use this to verify the physical device supports the VK_KHR_swapchain extension before picking a device.
bool Utils::checkDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char*>& requiredExtensions) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());

    for (const auto& extension : availableExtensions) {
        required.erase(extension.extensionName);
    }

    return required.empty();
}


VkFormat Utils::findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported format!");
}

// Depth buffers differ wildly between AMD, NVIDIA, and Intel. 
// We iterate through a list of preferred formats (e.g., D32_SFLOAT) and ask the GPU which one it natively supports.
VkFormat Utils::findDepthFormat(VkPhysicalDevice physicalDevice) {
    return findSupportedFormat(
        physicalDevice,
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

//gotta start using filesystem for this shi
 std::vector<char> Utils::readFile(const std::filesystem::path& filename) {

    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error("File does not exist: " + filename.string());
    }
    uintmax_t size = std::filesystem::file_size(filename);
    std::vector<char> buffer(size);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename.string());
    }

    file.read(buffer.data(), size);

    return buffer;
}

 // Vulkan requires all operations to be recorded into a command buffer. 
// These helpers generate a temporary buffer for immediate synchronous tasks (like copying textures from CPU to GPU).
 VkCommandBuffer Utils::beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool) {
     VkCommandBufferAllocateInfo allocInfo{};
     allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
     allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
     allocInfo.commandPool = commandPool;
     allocInfo.commandBufferCount = 1;

     VkCommandBuffer commandBuffer;
     vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

     VkCommandBufferBeginInfo beginInfo{};
     beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
     beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

     vkBeginCommandBuffer(commandBuffer, &beginInfo);

     return commandBuffer;
 }

 void Utils::endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer) {
     vkEndCommandBuffer(commandBuffer);

     VkSubmitInfo submitInfo{};
     submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
     submitInfo.commandBufferCount = 1;
     submitInfo.pCommandBuffers = &commandBuffer;

     vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
     vkQueueWaitIdle(queue);

     vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
 }

 // Vulkan stores images in opaque memory layouts optimized for specific tasks. 
// An image optimized as a TRANSFER_DST cannot be read by a shader. We use pipeline barriers to explicitly 
// morph the memory layout and force the GPU pipeline to wait until the transition is complete.
 void Utils::TransitionImageLayout(
     VkCommandBuffer cmd,Core::ImageBuilder::Image* image,VkImageLayout newLayout,VkAccessFlags2 srcAccessMask,VkAccessFlags2 dstAccessMask,VkPipelineStageFlags2 srcStageMask,
     VkPipelineStageFlags2 dstStageMask,VkImageAspectFlags aspectMask,uint32_t mipLevels,uint32_t layerCount)
 {
     VkImageMemoryBarrier2 barrier = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .pNext = nullptr,
         .srcStageMask = srcStageMask,
         .srcAccessMask = srcAccessMask,
         .dstStageMask = dstStageMask,
         .dstAccessMask = dstAccessMask,
         .oldLayout = image->layout, 
         .newLayout = newLayout,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = image->image,
         .subresourceRange = {
             .aspectMask = aspectMask,
             .baseMipLevel = 0,
             .levelCount = mipLevels,
             .baseArrayLayer = 0,
             .layerCount = layerCount
         }
     };

     VkDependencyInfo dependencyInfo = {
         .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
         .pNext = nullptr,
         .dependencyFlags = 0,
         .memoryBarrierCount = 0,
         .pMemoryBarriers = nullptr,
         .bufferMemoryBarrierCount = 0,
         .pBufferMemoryBarriers = nullptr,
         .imageMemoryBarrierCount = 1,
         .pImageMemoryBarriers = &barrier
     };

     vkCmdPipelineBarrier2(cmd, &dependencyInfo);
     image->layout = newLayout;
 }

 void Utils::TransitionImageLayout(VkCommandBuffer cmd,VkImage image,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags2 srcAccessMask,VkAccessFlags2 dstAccessMask,
     VkPipelineStageFlags2 srcStageMask,VkPipelineStageFlags2 dstStageMask,VkImageAspectFlags aspectMask,uint32_t mipLevels,uint32_t layerCount)
 {
     VkImageMemoryBarrier2 barrier = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .pNext = nullptr,
         .srcStageMask = srcStageMask,
         .srcAccessMask = srcAccessMask,
         .dstStageMask = dstStageMask,
         .dstAccessMask = dstAccessMask,
         .oldLayout = oldLayout, 
         .newLayout = newLayout,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = image,        
         .subresourceRange = {
             .aspectMask = aspectMask,
             .baseMipLevel = 0,
             .levelCount = mipLevels,
             .baseArrayLayer = 0,
             .layerCount = layerCount
         }
     };

     VkDependencyInfo dependencyInfo = {
         .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
         .imageMemoryBarrierCount = 1,
         .pImageMemoryBarriers = &barrier
     };

     vkCmdPipelineBarrier2(cmd, &dependencyInfo);
 }

 void Utils::copyBuffer(Core::GraphicsContext& ctx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
     VkCommandBuffer commandBuffer = beginSingleTimeCommands(ctx.getDevice(), ctx.getCommandPool());

     VkBufferCopy copyRegion{};
     copyRegion.srcOffset = 0;
     copyRegion.dstOffset = 0;
     copyRegion.size = size;

     vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

     endSingleTimeCommands(ctx.getDevice(), ctx.getCommandPool(), ctx.getGraphicsQueue(), commandBuffer);
 
}

 void Utils::CopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
 {
 
     VkBufferImageCopy region{};
     region.bufferOffset = 0;
     region.bufferRowLength = 0;
     region.bufferImageHeight = 0;
     region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
     region.imageSubresource.mipLevel = 0;
     region.imageSubresource.baseArrayLayer = 0;
     region.imageSubresource.layerCount = 1;
     region.imageOffset = { 0, 0, 0 };
     region.imageExtent = { width, height, 1 };

     vkCmdCopyBufferToImage(cmd,buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 }

 // We generate mipmaps on the GPU by repeatedly "blitting" (copying and scaling) an image onto itself.
 // To do this, we have to transition Mip Level [i-1] into a SRC layout, and Mip Level [i] into a DST layout, 
 // copy the data down, and then transition them both into SHADER_READ_ONLY.
 void Utils::generateMipmaps(VkPhysicalDevice physicalDevice, VkCommandBuffer cmd, VkImage image, VkFormat format, int32_t texWidth, int32_t texHeight, uint32_t mipLevels, uint32_t layerCount)
 {
     // Ensure the physical device supports linear filtering for this specific format
     VkFormatProperties formatProperties;
     vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);

     if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
         throw std::runtime_error("Texture image format does not support linear blitting!");
     }

     VkImageMemoryBarrier barrier{};
     barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
     barrier.image = image;
     barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
     barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
     barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
     barrier.subresourceRange.baseArrayLayer = 0;
     barrier.subresourceRange.layerCount = layerCount;
     barrier.subresourceRange.levelCount = 1;

     int32_t mipWidth = texWidth;
     int32_t mipHeight = texHeight;

     for (uint32_t i = 1; i < mipLevels; i++) {
         // Transition previous mip level to SRC
         barrier.subresourceRange.baseMipLevel = i - 1;
         barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
         barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
         vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
         // Transition current mip level to DST
         VkImageMemoryBarrier barrierDst = barrier;
         barrierDst.subresourceRange.baseMipLevel = i;
         barrierDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
         barrierDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         barrierDst.srcAccessMask = 0;
         barrierDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierDst);
         // Setup the blit regions (Halving the resolution each step)
         VkImageBlit blit{};
         blit.srcOffsets[0] = { 0, 0, 0 };
         blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
         blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         blit.srcSubresource.mipLevel = i - 1;
         blit.srcSubresource.baseArrayLayer = 0;
         blit.srcSubresource.layerCount = layerCount;

         blit.dstOffsets[0] = { 0, 0, 0 };
         blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
         blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         blit.dstSubresource.mipLevel = i;
         blit.dstSubresource.baseArrayLayer = 0;
         blit.dstSubresource.layerCount = layerCount;
         // Perform the hardware blit
         vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
         // Transition the SRC level back to SHADER_READ_ONLY so we can use it in rendering
         barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
         barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
         barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
         barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
         vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

         if (mipWidth > 1) mipWidth /= 2;
         if (mipHeight > 1) mipHeight /= 2;
     }
     // Handle the final mip level transition
     barrier.subresourceRange.baseMipLevel = mipLevels - 1;
     barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
     barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
     barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
     barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

     vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
 }
 RenderTypes::CascadeData Utils::CalculateCascades(const glm::mat4& view, float fov, float aspect, float nearClip, float farClip, const glm::vec3& lightDir, uint32_t numCascades)
 {
     RenderTypes::CascadeData data{};

     // Calculate split depths based on a practical mix of Logarithmic and Uniform distributions.
     // This gives us more resolution up close (where the player looks) and stretches it out further away.

     float lambda = 0.95f;
     float range = farClip - nearClip;
     float ratio = farClip / nearClip;

     std::array<float, 4> splits;
     for (uint32_t i = 0; i < numCascades; i++) {
         float p = (i + 1) / (float)numCascades;
         float log = nearClip * std::pow(ratio, p);
         float uniform = nearClip + range * p;
         float d = lambda * (log - uniform) + uniform;
         splits[i] = d;
     }

     float prevSplit = nearClip;
     for (uint32_t i = 0; i < numCascades; i++) {
         float splitNear = prevSplit;
         float splitFar = splits[i];
         prevSplit = splitFar;

         // Create a projection matrix for this specific cascade slice.
         // We strictly use perspectiveZO because Vulkan's clip space Z ranges from 0.0 to 1.0 (Unlike OpenGL's -1 to 1)
         glm::mat4 proj = glm::perspectiveZO(glm::radians(fov), aspect, splitNear, splitFar);
         proj[1][1] *= -1.0f; // Vulkan Y-Flip (Y points down in Vulkan)
         // Inverse matrix to move from Clip Space back into World Space
         glm::mat4 invCam = glm::inverse(proj * view);

         std::array<glm::vec3, 8> corners;
         uint32_t idx = 0;
         for (int x = 0; x < 2; x++) {
             for (int y = 0; y < 2; y++) {
                 for (int z = 0; z < 2; z++) {

                     glm::vec4 pt = invCam * glm::vec4(2.0f * x - 1.0f,2.0f * y - 1.0f,(float)z, // Vulkan Z is 0 to 1
                     1.0f);
                     corners[idx++] = glm::vec3(pt) / pt.w; // Perspective divide
                 
                 }
             }
         }

         // We wrap the frustum in a sphere instead of a tight box. 
         // This prevents the shadow map from resizing (and therefore shimmering) when the camera rotates.
         glm::vec3 center = glm::vec3(0);
         for (auto& c : corners) center += c;
         center /= 8.0f;

         float radius = 0.0f;
         for (auto& c : corners) {
             float distance = glm::length(c - center);
             radius = std::max(radius, distance);
         }
         radius = std::ceil(radius * 16.0f) / 16.0f; // Round up to stabilize floating point math

         // Create the square ortho bounds based on the sphere radius
         float minX = -radius;
         float maxX = radius;
         float minY = -radius;
         float maxY = radius;

         glm::vec3 lightDirNorm = glm::normalize(lightDir);
         glm::mat4 lightView = glm::lookAt(center - lightDirNorm, center, glm::vec3(0, 1, 0));

         // Calculate exactly where the geometry starts and ends in light space to prevent 
     	// extreme depth precision loss (which causes Peter-Panning).
         float minZ = FLT_MAX;
         float maxZ = -FLT_MAX;
         for (auto& c : corners) {
             glm::vec3 ls = glm::vec3(lightView * glm::vec4(c, 1.0f));
             minZ = std::min(minZ, ls.z);
             maxZ = std::max(maxZ, ls.z);
         }

         // Pad the Z bounds safely to catch shadow casters that are behind the camera
         float zNear = -(maxZ + 1000.0f);
         float zFar = -(minZ - 50.0f);

         // Build the Ortho projection. Again, using ZO for Vulkan's 0 to 1 depth range.
         glm::mat4 lightOrtho = glm::orthoZO(minX, maxX, minY, maxY, zNear, zFar);

         // If the camera moves, the shadow map camera moves with it. To prevent shadow pixels from 
         // sliding across geometry (swimming), we force the projection matrix to snap to nearest texel increments.
         float shadowMapRes = 2048.0f;
         glm::mat4 shadowMatrix = lightOrtho * lightView;
         glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
         shadowOrigin *= (shadowMapRes / 2.0f); // Scale to shadow map pixel space

         glm::vec4 roundedOrigin = glm::round(shadowOrigin);
         glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
         roundOffset *= (2.0f / shadowMapRes); // Scale back to NDC

         // Apply the sub-pixel shift directly into the projection matrix translation column
         lightOrtho[3][0] += roundOffset.x;
         lightOrtho[3][1] += roundOffset.y;

         lightOrtho[1][1] *= -1.0f; // Final Vulkan Y-flip for the projection

         data.lightSpaceMatrices[i] = lightOrtho * lightView;
         data.splitDepths[i] = splitFar;
     }
     return data;
 }