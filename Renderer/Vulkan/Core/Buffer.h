#ifndef BUFFER_H
#define BUFFER_H

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <stdexcept>
#include <string>
namespace Core
{
    class Allocator;
    struct GraphicsContext;
    /// @brief Implements the Builder Pattern for allocating GPU memory Buffers (UBOs, SSBOs, VBOs).
    class BufferBuilder {
    public:
        explicit BufferBuilder(GraphicsContext& ctx) : context(ctx) {}

        BufferBuilder& setSize(VkDeviceSize size) {
            bufferSize = size;
            return *this;
        }

        BufferBuilder& setUsage(VkBufferUsageFlags usage) {
            bufferUsage = usage;
            return *this;
        }

        BufferBuilder& setVmaUsage(VmaMemoryUsage usage) {
            vmaUsage = usage;
            return *this;
        }

        BufferBuilder& setMemoryProperties(VkMemoryPropertyFlags props) {
            memoryProperties = props;
            return *this;
        }

        BufferBuilder& setSharingMode(VkSharingMode mode) {
            sharingMode = mode;
            return *this;
        }

        BufferBuilder& setVmaFlags(VmaAllocationCreateFlags flags) {
            vmaFlags = flags;
            return *this;
        }

        /// @brief The allocated Vulkan buffer and its associated VMA tracking data.
        struct Buffer {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            Allocator* allocator = nullptr;
            VkDeviceSize size = 0;
            void* mapped = nullptr;
            bool isPersistent = false;

            Buffer() = default;
            ~Buffer();

            // Move Constructor
            Buffer(Buffer&& other) noexcept :
                buffer(other.buffer), allocation(other.allocation),
                allocator(other.allocator), size(other.size), mapped(other.mapped), isPersistent(other.isPersistent)
            {
                other.buffer = VK_NULL_HANDLE;
                other.allocation = VK_NULL_HANDLE;
                other.mapped = nullptr;
                other.isPersistent = false;
            }

            // Move Assignment
            Buffer& operator=(Buffer&& other) noexcept {
                if (this != &other) {
                    destroy();

                    buffer = other.buffer;
                    allocation = other.allocation;
                    allocator = other.allocator;
                    size = other.size;
                    mapped = other.mapped;
                    isPersistent = other.isPersistent;

                    other.buffer = VK_NULL_HANDLE;
                    other.allocation = VK_NULL_HANDLE;
                    other.mapped = nullptr;
                    other.isPersistent = false;
                }
                return *this;
            }

            Buffer(const Buffer&) = delete;
            Buffer& operator=(const Buffer&) = delete;

            operator VkBuffer() const { return buffer; }

            void* map();
            void unmap();
            void destroy();

            template<typename T>
            void copyData(const std::vector<T>& data) {
                if (!mapped)
                    throw std::runtime_error("Attempted to copyData() to unmapped buffer. Persistent mapping expected.");
                memcpy(mapped, data.data(), sizeof(T) * data.size());
            }

            template <typename T>
            void copyData(const T* data, size_t count) {
                if (!mapped)
                    throw std::runtime_error("Attempted to copyData() to unmapped buffer. Persistent mapping expected.");
                memcpy(mapped, data, sizeof(T) * count);
            }

            void flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
            void setDebugName(const std::string& name);
        };

        Buffer build();

    private:
        GraphicsContext& context;
        VkDeviceSize bufferSize = 0;
        VkBufferUsageFlags bufferUsage = 0;
        VkMemoryPropertyFlags memoryProperties = 0;
        VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaMemoryUsage vmaUsage = VMA_MEMORY_USAGE_AUTO;
        VmaAllocationCreateFlags vmaFlags = 0;
    };
}
#endif // BUFFER_H