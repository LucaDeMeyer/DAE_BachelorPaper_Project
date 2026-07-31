#ifndef TEXTURE_LOADER_H
#define TEXTURE_LOADER_H

#include <filesystem>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "ResourceTypes.h"


namespace Core {
    struct GraphicsContext;
    class ResourceManager;

    /// @brief A raw CPU-side container for loaded image pixels before they are uploaded to the GPU.
    struct TextureData {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        VkDeviceSize size = 0;
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
        uint32_t channels = 4;
    };

    /// @brief Utility namespace for loading images from disk using libraries like stb_image.
    namespace TextureLoader {
        /// @brief Reads an image file from disk and decodes it into a raw byte array.
        TextureData LoadPixels(const std::filesystem::path& path, VkFormat format);

        /// @brief High-level helper: Loads an image from disk and immediately registers it with the ResourceManager.
        TextureHandle LoadFromFile(GraphicsContext& ctx, ResourceManager& resManager, const std::string& path, VkFormat format);

        /// @brief Mathematically calculates the maximum number of mipmap levels possible for a given resolution.
        uint32_t CalculateMipLevels(uint32_t width, uint32_t height);
    }
}

#endif