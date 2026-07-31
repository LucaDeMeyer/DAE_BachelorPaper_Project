#include "TextureLoader.h"
#include "Vulkan/Core/ResourceManager.h"
#include "GraphicsContext.h"
#include "Vulkan/Utils/Utils.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include "Allocator.h"

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif

uint32_t Core::TextureLoader::CalculateMipLevels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

Core::TextureData Core::TextureLoader::LoadPixels(const std::filesystem::path& path, VkFormat format) {

    TextureData data;
    std::string pathStr = path.string();
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == ".hdr") {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        float* pixels = stbi_loadf(pathStr.c_str(), &width, &height, &channels, STBI_rgb_alpha);

        if (!pixels) {
            std::cerr << "Failed to load HDR texture: " << pathStr << "\n";
            return data;
        }

        data.width = static_cast<uint32_t>(width);
        data.height = static_cast<uint32_t>(height);
        data.channels = 4;

        data.size = data.width * data.height * 4 * sizeof(float);

        data.format = VK_FORMAT_R32G32B32A32_SFLOAT;

        uint8_t* bytePtr = reinterpret_cast<uint8_t*>(pixels);
        data.pixels.assign(bytePtr, bytePtr + data.size);

        stbi_image_free(pixels);
    }

    else {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(false);
        stbi_uc* pixels = stbi_load(pathStr.c_str(), &width, &height, &channels, STBI_rgb_alpha);

        if (!pixels) {
            std::cerr << "Failed to load standard texture: " << pathStr << "\n";
            return data;
        }

        data.width = static_cast<uint32_t>(width);
        data.height = static_cast<uint32_t>(height);
        data.channels = 4;
        data.size = data.width * data.height * 4;
        data.format = format;

        data.pixels.assign(pixels, pixels + data.size);
        stbi_image_free(pixels);
    }

    return data;
}


Core::TextureHandle Core::TextureLoader::LoadFromFile(GraphicsContext& ctx, ResourceManager& resManager, const std::string& path, VkFormat format) {

    TextureData data = LoadPixels(path, format);
    if (data.pixels.empty()) {
        return TextureHandle{}; 
    }

    uint32_t mipLevels = CalculateMipLevels(data.width, data.height);

    TextureDesc desc{};
    desc.name = path;
    desc.extent = { data.width, data.height, 1 };
    desc.format = data.format;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.mipLevels = mipLevels;
    desc.arrayLayers = 1;

    TextureHandle handle = resManager.CreateTexture(desc);

    ImageBuilder::Image* allocatedImage = resManager.GetTexture(handle);

    auto allocator = ctx.getAllocator();
    auto [stagingBuffer, stagingAlloc] = allocator->createStagingBuffer(data.pixels.data(), data.size);

   VkCommandBuffer cmd = Utils::beginSingleTimeCommands(ctx.getDevice(),ctx.getCommandPool());

   Utils::TransitionImageLayout(cmd,allocatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_IMAGE_ASPECT_COLOR_BIT,mipLevels);

    Utils::CopyBufferToImage(cmd, stagingBuffer, allocatedImage->image, data.width, data.height);
    Utils::generateMipmaps(ctx.getPhysicalDevice(),cmd, allocatedImage->image, data.format, data.width, data.height, mipLevels,1);

    Utils::endSingleTimeCommands(ctx.getDevice(),ctx.getCommandPool(),ctx.getGraphicsQueue(),cmd);

    allocator->destroyBuffer(stagingBuffer, stagingAlloc);

    return handle;
}