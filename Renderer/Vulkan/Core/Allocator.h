#ifndef ALLOCATOR_H
#define ALLOCATOR_H
#include <vulkan/vulkan.h> 
#include <vk_mem_alloc.h>
#include <string>
#include <stdexcept>

namespace Core
{

    struct GraphicsContext;
    /// @brief A high-level wrapper around the Vulkan Memory Allocator (VMA) library.
    /// Manages physical device memory, sub-allocation, and CPU-to-GPU memory mapping.
    class Allocator {
    public:
        struct MemoryStats {
            uint64_t usedBytes;
            uint64_t allocatedBytes;
            uint32_t allocationCount;
            uint32_t blockCount;
        };
        /// @brief An RAII helper for temporarily mapping GPU memory to the CPU.
        /// Automatically maps the memory on creation and safely unmaps it when destroyed.
        template<typename T>
        struct ScopedMap {
            Allocator& allocator;
            VmaAllocation allocation;
            T* data;

            ScopedMap(Allocator& alloc, VmaAllocation allocHandle)
                : allocator(alloc), allocation(allocHandle) {
                data = allocator.map<T>(allocation);
            }

            ~ScopedMap() {
                if (allocation) {
                    allocator.unmap(allocation);
                }
            }

            ScopedMap(const ScopedMap&) = delete;
            ScopedMap& operator=(const ScopedMap&) = delete;

            T* get() { return data; }
            T* operator->() { return data; }
        };

        explicit Allocator(GraphicsContext& context);
        ~Allocator() = default;

        Allocator(const Allocator&) = delete;
        Allocator& operator=(const Allocator&) = delete;
        Allocator(Allocator&&) = delete;
        Allocator& operator=(Allocator&&) = delete;

        VmaAllocator getHandle() const { return allocator; }
        VmaPool createPool(uint32_t memoryTypeIndex, VmaPoolCreateFlags flags = 0);
        void destroyPool(VmaPool pool);

        VkBuffer createBuffer(
            const VkBufferCreateInfo& bufferInfo,
            const VmaAllocationCreateInfo& allocInfo,
            VmaAllocation& outAllocation,
            VmaAllocationInfo* outAllocInfo = nullptr
        );

        std::pair<VkBuffer, VmaAllocation> createStagingBuffer(const void* data, VkDeviceSize size);

        void destroyBuffer(VkBuffer buffer, VmaAllocation allocation);

        VkImage createImage(
            const VkImageCreateInfo& imageInfo,
            const VmaAllocationCreateInfo& allocInfo,
            VmaAllocation& outAllocation
        );

        void destroyImage(VkImage image, VmaAllocation allocation);

        template<typename T = void>
        T* map(VmaAllocation allocation) {
            void* data = nullptr;
            VkResult result = vmaMapMemory(allocator, allocation, &data);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to map memory!");
            }
            return static_cast<T*>(data);
        }

        void unmap(VmaAllocation allocation);
        void flush(VmaAllocation allocation, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
        void invalidate(VmaAllocation allocation, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

        void setAllocationName(VmaAllocation allocation, const std::string& name);
        void setObjectName(VkObjectType type, uint64_t handle, const std::string& name);

        MemoryStats GetStats() const;

        char* GetStatsString();
        void FreeStatsString(char* str);

        VmaAllocator allocator = VK_NULL_HANDLE;

        void Shutdown()
        {
            if (allocator != VK_NULL_HANDLE) {
                vmaDestroyAllocator(allocator);
                allocator = VK_NULL_HANDLE;
            }
        }
    private:
        GraphicsContext& context;
    };
}

#endif