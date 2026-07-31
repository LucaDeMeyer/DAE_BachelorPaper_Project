#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include "Image.h"

namespace Core
{
    struct GraphicsContext;
    /// @brief Implements the Builder Pattern to safely configure and create the Vulkan Swapchain.
    class SwapChainBuilder final {
    public:
        explicit SwapChainBuilder(GraphicsContext& ctx) : context(ctx) {}

        SwapChainBuilder& setExtent(uint32_t width, uint32_t height) {
            extent = VkExtent2D{ width, height };
            return *this;
        }

        SwapChainBuilder& setPreferredFormat(VkFormat format) {
            preferredFormat = format;
            return *this;
        }

        SwapChainBuilder& setPreferredPresentMode(VkPresentModeKHR mode) {
            preferredPresentMode = mode;
            return *this;
        }

        SwapChainBuilder& setImageCount(uint32_t count) {
            imageCount = count;
            return *this;
        }
        /// @brief The finalized Swapchain object containing the OS backbuffers and Depth target.
        struct SwapChain {
            SwapChain() = default;

            SwapChain(const SwapChain&) = delete;
            SwapChain& operator=(const SwapChain&) = delete;
            SwapChain(SwapChain&&) = default;
            SwapChain& operator=(SwapChain&&) = default;
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;

            std::vector<VkImage> images;
            std::vector<VkImageView> imageViews;
            VkFormat format = VK_FORMAT_UNDEFINED;

            std::optional<ImageBuilder> depthImageBuilder;
            std::optional<ImageBuilder::Image> depthImage;

            VkFormat depthFormat = VK_FORMAT_UNDEFINED;
            VkExtent2D extent = { 0, 0 };
            void destroy(VkDevice device) {
                for (auto imageView : imageViews) {
                    if (imageView != VK_NULL_HANDLE) {
                        vkDestroyImageView(device, imageView, nullptr);
                    }
                }
                imageViews.clear();

                if (swapchain != VK_NULL_HANDLE) {
                    vkDestroySwapchainKHR(device, swapchain, nullptr);
                    swapchain = VK_NULL_HANDLE;
                }

                depthImage.reset();
            }
        };

        SwapChain build();

    private:
        GraphicsContext& context;
        std::optional<VkExtent2D> extent;

        VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_SRGB;
        VkPresentModeKHR preferredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        uint32_t imageCount = 3;
    };
}
#endif