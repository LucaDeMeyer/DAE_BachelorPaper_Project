#ifndef UTILS_H
#define UTILS_H
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <set>
#include <string>
#include "Vulkan/Core/Image.h"
#include "Vulkan/Core/RenderTypes.h"
#include <filesystem>
namespace Core
{
    struct GraphicsContext;
    class ResourceManager;
}

/// @brief Stores the physical hardware queue indices for the GPU.
/// Vulkan separates GPU operations into different physical hardware queues.
/// We must find a queue that supports 3D Graphics, and a queue that supports Presenting to the OS.
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

/// @brief Stores the capabilities of the physical monitor/window surface.
/// Used to determine the max resolution, supported color spaces (e.g., SRGB, HDR), 
/// and supported V-Sync modes (e.g., Mailbox, FIFO).
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

/// @brief A collection of static helper functions for Vulkan boilerplate.
class Utils {
public:
    /// @brief Scans a physical GPU to find queues supporting Graphics and Presentation.
    static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

    /// @brief Queries the OS window surface to see what rendering formats and extents it supports.
    static SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    /// @brief Verifies that a physical GPU supports a specific list of extensions (e.g., VK_KHR_swapchain).
    static bool checkDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char*>& requiredExtensions);

    /// @brief Finds the most optimal image format supported by the physical GPU from a list of candidates.
    static VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    /// @brief AMD, NVIDIA, and Intel GPUs prefer different depth buffer formats. 
    /// This queries the specific hardware to find the best available 32-bit/24-bit depth format.
    static VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

   static bool IsDepthFormat(VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            format == VK_FORMAT_D24_UNORM_S8_UINT ||
            format == VK_FORMAT_D16_UNORM ||
            format == VK_FORMAT_D16_UNORM_S8_UINT;
    }

    /// @brief Reads a compiled SPIR-V shader file from disk into a byte array.
    static std::vector<char> readFile(const std::filesystem::path& );

    /// @brief Allocates and begins a temporary command buffer for immediate, synchronous GPU operations.
    static VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);

    /// @brief Submits a temporary command buffer and STALLS the CPU until the GPU finishes executing it.
    /// @note This is highly inefficient for runtime rendering. Use only during loading/initialization.
    static void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer);

    /// @brief Injects a Vulkan 1.3 Memory Barrier to morph an image's underlying memory layout.
    /// Used to safely transition an image from states like TRANSFER_DST to SHADER_READ_ONLY.
    static void TransitionImageLayout(VkCommandBuffer cmd, Core::ImageBuilder::Image* image, VkImageLayout newLayout, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 srcStageMask,
        VkPipelineStageFlags2 dstStageMask, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mipLevels = 1, uint32_t layerCount = 1);

    /// @brief Injects a Vulkan 1.3 Memory Barrier using a raw VkImage handle.
    static void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 srcStageMask,
        VkPipelineStageFlags2 dstStageMask, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mipLevels = 1, uint32_t layerCount = 1);

    /// @brief Utility to extract the directory path from a full file path string.
    static inline std::string GetDirectoryPath(const std::string& filePath) {
        size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash == std::string::npos) {
            return "";
        }
        return filePath.substr(0, lastSlash + 1);
    }

    /// @brief Records a command to copy data from a CPU-visible buffer into a GPU-optimal image.
    static void CopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    /// @brief Synchronously copies data from one buffer to another using a single-time command buffer.
    static void copyBuffer(Core::GraphicsContext& ctx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    /// @brief Uses the GPU's hardware blitting to automatically generate downsampled mipmap chains for a texture.
    static void generateMipmaps(VkPhysicalDevice physicalDevice, VkCommandBuffer cmd, VkImage image, VkFormat format, int32_t texWidth, int32_t texHeight, uint32_t mipLevels, uint32_t layercount);

    /// @brief Calculates the sub-frustum bounding boxes and Vulkan projection matrices for Cascaded Shadow Mapping.
    /// Automatically applies sub-pixel texel snapping to prevent shadows from shimmering when the camera moves.
    /// @param view The current Main Camera view matrix.
    /// @param fov The main camera's field of view (in degrees).
    /// @param aspect The screen aspect ratio.
    /// @param nearClip The main camera near plane.
    /// @param farClip The main camera far plane.
    /// @param lightDir The directional vector of the sun/light source.
    /// @param numCascades The number of shadow map slices to generate.
    /// @return A structured array of projection matrices and split depth distances.
    static RenderTypes::CascadeData CalculateCascades(const glm::mat4& view,
        float fov,
        float aspect,
        float nearClip,
        float farClip,
        const glm::vec3& lightDir,
        uint32_t numCascades);
};

#endif