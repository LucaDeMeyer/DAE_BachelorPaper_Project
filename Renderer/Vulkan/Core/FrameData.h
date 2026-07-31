#ifndef FRAME_DATA_H
#define FRAME_DATA_H
#include <vulkan/vulkan.h>
#include <stdexcept>

namespace Core {
    /// @brief Holds the synchronization primitives and command buffers for a single frame.
     /// In a triple-buffered engine (MAX_FRAMES_IN_FLIGHT = 3), you will have three of these.
     /// This prevents the CPU from overwriting data that the GPU is currently rendering.
    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;

        /// @brief Allocates the command buffer and creates the sync primitives.
        void init(VkDevice device, VkCommandPool commandPool) {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate command buffer!");
            }

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create synchronization objects!");
            }
        }

        /// @brief Safely destroys the sync primitives.
        void destroy(VkDevice device) {
            if (imageAvailableSemaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
                imageAvailableSemaphore = VK_NULL_HANDLE;
            }

            if (inFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device, inFlightFence, nullptr);
                inFlightFence = VK_NULL_HANDLE;
            }
        }
    };
}
#endif