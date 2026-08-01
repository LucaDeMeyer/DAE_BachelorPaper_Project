#ifndef DESCRIPTOR_SET_H
#define DESCRIPTORSET_H
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace Core
{
    struct GraphicsContext;
	

    /// @brief Implements the Builder Pattern to create Descriptor Set Layouts and allocate Descriptor Pools.
	class DescriptorBuilder final
	{
	public:
        explicit DescriptorBuilder(GraphicsContext& ctx) : context(ctx) {}

        DescriptorBuilder& addLayoutBinding(uint32_t binding,
            VkDescriptorType type,
            VkShaderStageFlags stages,
            uint32_t count = 1,
            VkDescriptorBindingFlags flags = 0)
        {
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding;
            layoutBinding.descriptorType = type;
            layoutBinding.descriptorCount = count;
            layoutBinding.stageFlags = stages;
            layoutBinding.pImmutableSamplers = nullptr;

            layoutBindings.push_back(layoutBinding);
            bindingFlags.push_back(flags);

            poolSizeCounters[type] += count;

            return *this;
        }

        DescriptorBuilder& setPoolSize(uint32_t maxSets) {
            poolMaxSets = maxSets;
            return *this;
        }

        DescriptorBuilder& addPoolSize(VkDescriptorType type, uint32_t count) {
            VkDescriptorPoolSize poolSize{};
            poolSize.type = type;
            poolSize.descriptorCount = count;
            poolSizes.push_back(poolSize);
            return *this;
        }
        /// @brief A generated Descriptor Set alongside its governing Layout and parent Pool.
        struct DescriptorSet {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            std::vector<VkDescriptorSet> sets;

            void destroy(VkDevice device) {
                if (pool != VK_NULL_HANDLE) {
                    vkDestroyDescriptorPool(device, pool, nullptr);
                    pool = VK_NULL_HANDLE;
                }
                if (layout != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device, layout, nullptr);
                    layout = VK_NULL_HANDLE;
                }
                sets.clear();
            }
        };

        DescriptorSet build(uint32_t setCount = 1);
        DescriptorSet buildLayoutAndPool();

    private:
        GraphicsContext& context;
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
        std::vector<VkDescriptorPoolSize> poolSizes;
        std::vector<VkDescriptorBindingFlags> bindingFlags;

        uint32_t poolMaxSets = 500;
        std::unordered_map<VkDescriptorType, uint32_t> poolSizeCounters;
    };
    /// @brief A utility class that queues up Vulkan Descriptor Writes and executes them all at once.
    /// Uses std::deque to ensure memory addresses of Image/Buffer infos remain perfectly stable.
    class DescriptorWriter {
    public:
        DescriptorWriter& writeImage(uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
            VkDescriptorImageInfo info{};
            info.sampler = sampler;
            info.imageView = view;
            info.imageLayout = layout;

            // Use deque to keep pointer valid
            auto& storedInfo = imageInfos.emplace_back(info);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = binding;
            write.descriptorType = type;
            write.descriptorCount = 1;
            write.pImageInfo = &storedInfo;

            writes.push_back(write);
            return *this;
        }

        DescriptorWriter& writeImageArray(uint32_t binding, const std::vector<VkDescriptorImageInfo>& infos, VkDescriptorType type) {
            auto& storedArray = imageArrayInfos.emplace_back(infos);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = binding;
            write.descriptorType = type;
            write.descriptorCount = static_cast<uint32_t>(storedArray.size());
            write.pImageInfo = storedArray.data(); 

            writes.push_back(write);
            return *this;
        }

        DescriptorWriter& writeBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, VkDescriptorType type) {
            VkDescriptorBufferInfo info{};
            info.buffer = buffer;
            info.offset = offset;
            info.range = range;

            auto& storedInfo = bufferInfos.emplace_back(info);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = binding;
            write.descriptorType = type;
            write.descriptorCount = 1;
            write.pBufferInfo = &storedInfo;

            writes.push_back(write);
            return *this;
        }

        DescriptorWriter& writeAccelerationStructure(uint32_t binding, const VkAccelerationStructureKHR* as)
        {
            VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
            asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = as;

            m_asWrites.push_back(asWrite);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = &m_asWrites.back();
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

            writes.push_back(write);
            return *this;
        }

        /// @brief Commits all queued writes to the actual GPU Descriptor Set.
        void overwrite(VkDescriptorSet set, VkDevice device) {
            for (auto& write : writes) {
                write.dstSet = set;
            }

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            writes.clear();
            imageInfos.clear();
            bufferInfos.clear();
            imageArrayInfos.clear(); 
        }

    private:
        std::deque<VkDescriptorImageInfo> imageInfos;
        std::deque<VkDescriptorBufferInfo> bufferInfos;
        std::deque<std::vector<VkDescriptorImageInfo>> imageArrayInfos;

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> m_asWrites;
    };
}

#endif
