#ifndef IMAGE_BUILDER_H
#define IMAGE_BUILDER_H

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <utility>
namespace Core
{
    class Allocator;
    struct GraphicsContext;

    /// @brief A Builder pattern implementation abstracting the creation of Vulkan Images, allocations, and Image Views.
    class ImageBuilder final {
    public:
        explicit ImageBuilder(GraphicsContext& ctx) : context(ctx) {}

        ImageBuilder& setExtent(uint32_t width, uint32_t height) {
            extent = VkExtent2D{ width, height };
            return *this;
        }

        ImageBuilder& setFormat(VkFormat fmt) {
            format = fmt;
            return *this;
        }

        ImageBuilder& setUsage(VkImageUsageFlags usage) {
            imageUsage = usage;
            return *this;
        }

        ImageBuilder& setTiling(VkImageTiling tiling) {
            imageTiling = tiling;
            return *this;
        }

        ImageBuilder& setSamples(VkSampleCountFlagBits samples) {
            sampleCount = samples;
            return *this;
        }

        ImageBuilder& setMipLevels(uint32_t levels) {
            mipLevels = levels;
            return *this;
        }

        ImageBuilder& setMemoryProperties(VkMemoryPropertyFlags props) {
            memoryProperties = props;
            return *this;
        }

        /// @brief Determines whether a VkImageView is automatically created alongside the VkImage.
        ImageBuilder& createView(bool create = true) {
            createImageView = create;
            return *this;
        }

        ImageBuilder& setAspectMask(VkImageAspectFlags aspect) {
            aspectMask = aspect;
            return *this;
        }

        ImageBuilder& setLayerCount(uint32_t layers) {
            layerCount = layers;
            return *this;
        }

        ImageBuilder& setFlags(VkImageCreateFlags f) {
            flags = f;
            return *this;
        }


        /// @brief An encapsulated Vulkan Image containing its View, VMA Allocation, and metadata.
        struct Image {
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            Allocator* allocator = nullptr;
            VkDevice device = VK_NULL_HANDLE;
            VkExtent2D extent = { 0, 0 };
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkFormat format = VK_FORMAT_UNDEFINED;
            uint32_t mipLevels = 1;
            uint32_t layerCount = 1;

            Image() = default;
            ~Image();

            Image(Image&& other) noexcept
                : image(other.image), view(other.view),
                allocation(other.allocation), allocator(other.allocator), device(other.device),
                extent(other.extent), layout(other.layout), format(other.format), mipLevels(other.mipLevels)
            {
                other.image = VK_NULL_HANDLE;
                other.view = VK_NULL_HANDLE;
                other.allocation = VK_NULL_HANDLE;
            }

            Image& operator=(Image&& other) noexcept {
                if (this != &other) {
                    destroy();

                    image = other.image;
                    view = other.view;
                    allocation = other.allocation;
                    allocator = other.allocator;
                    device = other.device;
                    extent = other.extent;
                    layout = other.layout;
                    format = other.format;
                    mipLevels = other.mipLevels;
                    layerCount = other.layerCount;

                    other.image = VK_NULL_HANDLE;
                    other.view = VK_NULL_HANDLE;
                    other.allocation = VK_NULL_HANDLE;
                }
                return *this;
            }

            Image(const Image&) = delete;
            Image& operator=(const Image&) = delete;
            operator VkImage() const { return image; }

            /// @brief Safely destroys the image view and frees the VMA allocation.
            void destroy();

            /// @brief Assigns a Vulkan debug label to the image for easier identification in RenderDoc/Nsight.
            void setDebugName(const std::string& name);
        };

        /// @brief Allocates and builds the configured Vulkan Image and optional View.
        Image build();

    private:
        GraphicsContext& context;

        uint32_t layerCount = 1;
        VkExtent2D extent = { 0, 0 };

        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
        VkImageUsageFlags imageUsage = 0;
        VkImageTiling imageTiling = VK_IMAGE_TILING_OPTIMAL;
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
        uint32_t mipLevels = 1;
        VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VkImageCreateFlags flags = 0;
        bool createImageView = true;
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    };
}
#endif