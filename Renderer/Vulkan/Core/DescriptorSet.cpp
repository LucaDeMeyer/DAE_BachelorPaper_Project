
#include "GraphicsContext.h"
#include <stdexcept>
#include <iostream>
#include "DescriptorSet.h"

Core::DescriptorBuilder::DescriptorSet Core::DescriptorBuilder::build(uint32_t setCount) {
    DescriptorSet result;
    VkDevice device = context.getDevice();

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();


    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo; 
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    for (const auto& flag : bindingFlags) {
        if (flag & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) {
            layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            break;
        }
    }

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &result.layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }

    if (setCount > poolMaxSets) {
        poolMaxSets = setCount;
    }

    if (poolSizes.empty()) {
        if (poolSizeCounters.empty()) {
            VkDescriptorPoolSize defaultSize{};
            defaultSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            defaultSize.descriptorCount = setCount;
            poolSizes.push_back(defaultSize);
        }
        else {
            for (auto& [type, count] : poolSizeCounters) {
                VkDescriptorPoolSize poolSize{};
                poolSize.type = type;
                poolSize.descriptorCount = count * setCount;
                poolSizes.push_back(poolSize);
            }
        }
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = poolMaxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (layoutInfo.flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) {
        poolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    }

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &result.pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }

    std::vector<VkDescriptorSetLayout> layouts(setCount, result.layout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = result.pool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();

    result.sets.resize(setCount);
    if (vkAllocateDescriptorSets(device, &allocInfo, result.sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets!");
    }
    layoutBindings.clear();
    poolSizes.clear();
    bindingFlags.clear();
    poolSizeCounters.clear();
    poolMaxSets = 1000;

    return result;
}

Core::DescriptorBuilder::DescriptorSet Core::DescriptorBuilder::buildLayoutAndPool()
{
    DescriptorSet result;
    VkDevice device = context.getDevice();

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    for (const auto& flag : bindingFlags) {
        if (flag & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) {
            layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            break;
        }
    }

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &result.layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = poolMaxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (layoutInfo.flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) {
        poolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    }

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &result.pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }

    layoutBindings.clear();
    bindingFlags.clear();
    poolSizes.clear();
    poolSizeCounters.clear();
    poolMaxSets = 10;

    return result;
}