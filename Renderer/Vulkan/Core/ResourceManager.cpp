#include "ResourceManager.h"
#include "GraphicsContext.h"
#include "Allocator.h"
#include <sstream>
#include <cstring>


Core::ResourceManager::ResourceManager(GraphicsContext& ctx): context(ctx)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    vkCreateSampler(context.getDevice(), &samplerInfo, nullptr, &m_LinearSampler);

    VkSamplerCreateInfo pointSampler{};
    pointSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    pointSampler.magFilter = VK_FILTER_NEAREST;
    pointSampler.minFilter = VK_FILTER_NEAREST;
    pointSampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    pointSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pointSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pointSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pointSampler.mipLodBias = 0.0f;
    pointSampler.minLod = 0.0f;
    pointSampler.maxLod = VK_LOD_CLAMP_NONE;
    pointSampler.anisotropyEnable = VK_FALSE;
    vkCreateSampler(context.getDevice(), &pointSampler, nullptr, &m_PointSampler);

    VkSamplerCreateInfo shadowSamplerInfo{};
    shadowSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    shadowSamplerInfo.magFilter = VK_FILTER_LINEAR;
    shadowSamplerInfo.minFilter = VK_FILTER_LINEAR;
    shadowSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadowSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowSamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    shadowSamplerInfo.mipLodBias = 0.0f;
    shadowSamplerInfo.minLod = 0.0f;
    shadowSamplerInfo.maxLod = 1.0f;
    shadowSamplerInfo.anisotropyEnable = VK_FALSE;
    shadowSamplerInfo.compareEnable = VK_TRUE;
    shadowSamplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    vkCreateSampler(context.getDevice(), &shadowSamplerInfo, nullptr, &m_ShadowSampler);


    VkSamplerCreateInfo pointRepeatInfo{};
    pointRepeatInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    pointRepeatInfo.magFilter = VK_FILTER_NEAREST;
    pointRepeatInfo.minFilter = VK_FILTER_NEAREST;
    pointRepeatInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    pointRepeatInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    pointRepeatInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    pointRepeatInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    pointRepeatInfo.mipLodBias = 0.0f;
    pointRepeatInfo.minLod = 0.0f;
    pointRepeatInfo.maxLod = VK_LOD_CLAMP_NONE;
    pointRepeatInfo.anisotropyEnable = VK_FALSE;
    vkCreateSampler(context.getDevice(), &pointRepeatInfo, nullptr, &m_PointRepeatSampler);


}


std::string Core::ResourceManager::GenerateTextureKey(const TextureDesc& desc) {
    // Create a unique string based on the blueprint properties that matter for memory reuse
    std::stringstream ss;
    ss << desc.extent.width << "x" << desc.extent.height << "x" << desc.extent.depth
        << "_F" << desc.format
        << "_U" << desc.usage
        << "_M" << desc.mipLevels
        << "_L" << desc.arrayLayers
        << "_S" << desc.samples
		<< "_FL" << desc.flags;
    return ss.str();
}

std::string Core::ResourceManager::GenerateBufferKey(const BufferDesc& desc) {
    std::stringstream ss;
    ss << "S" << desc.size << "_U" << desc.usage << "_VMA" << desc.vmaUsage;
    return ss.str();
}

Core::TextureHandle Core::ResourceManager::CreateTexture(const TextureDesc& desc) {
    uint32_t newId = static_cast<uint32_t>(m_textures.size());

    auto image = ImageBuilder(context)
        .setExtent(desc.extent.width, desc.extent.height)
        .setFormat(desc.format)
        .setUsage(desc.usage)
        .setAspectMask(desc.aspect)
        .setMipLevels(desc.mipLevels)
        .setLayerCount(desc.arrayLayers)
        .setSamples(desc.samples)
        .createView(true)
        .setFlags(desc.flags)
        .build();

    if (!desc.name.empty()) image.setDebugName(desc.name);

    m_textures.push_back(std::move(image));

    return TextureHandle{ newId };
}

Core::TextureHandle Core::ResourceManager::AcquireTransientTexture(const TextureDesc& desc) {
    std::string key = GenerateTextureKey(desc);

    if (m_transientTexturePool.contains(key) && !m_transientTexturePool.at(key).empty()) {
        TextureHandle handle = m_transientTexturePool[key].back();
        m_transientTexturePool[key].pop_back();

        if (m_textures[handle.id].image == VK_NULL_HANDLE) {
            auto image = ImageBuilder(context)
                .setExtent(desc.extent.width, desc.extent.height)
                .setFormat(desc.format)
                .setUsage(desc.usage)
                .setAspectMask(desc.aspect)
                .setMipLevels(desc.mipLevels)
                .setLayerCount(desc.arrayLayers)
                .setSamples(desc.samples)
                .createView(true)
                .setFlags(desc.flags)
                .build();

            if (!desc.name.empty()) image.setDebugName(desc.name);

            m_textures[handle.id] = std::move(image);
        }
        return handle;
    }

    uint32_t newId = static_cast<uint32_t>(m_textures.size());

    auto image = ImageBuilder(context)
        .setExtent(desc.extent.width, desc.extent.height)
        .setFormat(desc.format)
        .setUsage(desc.usage)
        .setAspectMask(desc.aspect)
        .setMipLevels(desc.mipLevels)
        .setLayerCount(desc.arrayLayers)
        .setSamples(desc.samples)
        .createView(true)
        .setFlags(desc.flags)
        .build();

    if (!desc.name.empty()) image.setDebugName(desc.name);

    m_textures.push_back(std::move(image));

    return TextureHandle{ newId };
}

void Core::ResourceManager::ReleaseTransientTexture(TextureHandle handle, const TextureDesc& desc) {
    if (!handle.IsValid()) return;
    std::string key = GenerateTextureKey(desc);
    m_transientTexturePool[key].push_back(handle);
}

Core::ImageBuilder::Image* Core::ResourceManager::GetTexture(TextureHandle handle) {
    if (!handle.IsValid() || handle.id >= m_textures.size()) return nullptr;
    return &m_textures[handle.id];
}

VkImageView Core::ResourceManager::GetMipView(TextureHandle handle, uint32_t mipLevel) {
    if (!handle.IsValid()) return VK_NULL_HANDLE;

    if (m_mipViews.contains(handle.id) && m_mipViews[handle.id].contains(mipLevel)) {
        return m_mipViews[handle.id][mipLevel];
    }

    ImageBuilder::Image* img = GetTexture(handle);
    if (!img) return VK_NULL_HANDLE;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = img->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = img->format; 

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = mipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(context.getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    m_mipViews[handle.id][mipLevel] = view;
    return view;
}

VkImageView Core::ResourceManager::GetMipViewCube(TextureHandle handle, uint32_t mipLevel) {
    if (!handle.IsValid()) return VK_NULL_HANDLE;

    if (m_cubeMipViews.contains(handle.id) && m_cubeMipViews[handle.id].contains(mipLevel)) {
        return m_cubeMipViews[handle.id][mipLevel];
    }

    ImageBuilder::Image* img = GetTexture(handle);
    if (!img) return VK_NULL_HANDLE;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = img->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; 
    viewInfo.format = img->format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = mipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    VkImageView view;
    if (vkCreateImageView(context.getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    m_cubeMipViews[handle.id][mipLevel] = view;
    return view;
}

Core::BufferHandle Core::ResourceManager::CreateBuffer(const BufferDesc& desc) {
    uint32_t newId = static_cast<uint32_t>(m_buffers.size());
    auto buffer = BufferBuilder(context)
        .setSize(desc.size)
        .setUsage(desc.usage)
        .setVmaUsage(desc.vmaUsage)
        .setVmaFlags(desc.vmaflags)
        .build();

    if (!desc.name.empty()) buffer.setDebugName(desc.name);

    m_buffers.push_back(std::move(buffer));
    return BufferHandle{ newId };
}

Core::BufferHandle Core::ResourceManager::AcquireTransientBuffer(const BufferDesc& desc) {
    std::string key = GenerateBufferKey(desc);

    if (m_transientBufferPool.contains(key) && !m_transientBufferPool.at(key).empty()) {
        BufferHandle handle = m_transientBufferPool[key].back();
        m_transientBufferPool[key].pop_back();

        // Rebuild if it was flushed
        if (m_buffers[handle.id].buffer == VK_NULL_HANDLE) {
            auto buffer = BufferBuilder(context)
                .setSize(desc.size)
                .setUsage(desc.usage)
                .setVmaUsage(desc.vmaUsage)
                .setVmaFlags(desc.vmaflags)
                .build();

            if (!desc.name.empty()) buffer.setDebugName(desc.name);

            m_buffers[handle.id] = std::move(buffer);
        }
        return handle;
    }

    uint32_t newId = static_cast<uint32_t>(m_buffers.size());

    auto buffer = BufferBuilder(context)
        .setSize(desc.size)
        .setUsage(desc.usage)
        .setVmaUsage(desc.vmaUsage)
        .setVmaFlags(desc.vmaflags)
        .build();

    if (!desc.name.empty()) buffer.setDebugName(desc.name);

    m_buffers.push_back(std::move(buffer));

    return BufferHandle{ newId };
}

void Core::ResourceManager::ReleaseTransientBuffer(BufferHandle handle, const BufferDesc& desc) {
    if (!handle.IsValid()) return;
    std::string key = GenerateBufferKey(desc);
    m_transientBufferPool[key].push_back(handle);
}

Core::BufferBuilder::Buffer* Core::ResourceManager::GetBuffer(BufferHandle handle) {
    if (!handle.IsValid() || handle.id >= m_buffers.size()) return nullptr;
    return &m_buffers[handle.id];
}

void Core::ResourceManager::FlushFreePools() {
    for (auto& [key, handles] : m_transientTexturePool) {
        for (TextureHandle handle : handles) {
            m_textures[handle.id].destroy();
        }
    }

    for (auto& [key, handles] : m_transientBufferPool) {
        for (BufferHandle handle : handles) {
            m_buffers[handle.id].destroy();
        }
    }

    m_transientTexturePool.clear();
    m_transientBufferPool.clear();
}

void Core::ResourceManager::InitGlobalGeometryBuffers(GraphicsContext& ctx, uint32_t maxVertices, uint32_t maxIndices) {
    m_globalVertexCapacity = maxVertices;
    m_globalIndexCapacity = maxIndices;

    BufferDesc vDesc{};
    vDesc.name = "GlobalVertexSSBO";
    vDesc.size = maxVertices * sizeof(RenderTypes::Vertex);
    vDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;;
    vDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    m_globalVertexBuffer = CreateBuffer(vDesc);

    BufferDesc iDesc{};
    iDesc.name = "GlobalIndexBuffer";
    iDesc.size = maxIndices * sizeof(uint32_t);
    iDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;;
    iDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    m_globalIndexBuffer = CreateBuffer(iDesc);
}

void Core::ResourceManager::AppendMeshToGlobalBuffer(
    GraphicsContext& ctx,
    const std::vector<RenderTypes::Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t& outFirstIndex,
    int32_t& outVertexOffset)
{
    if (m_currentVertexCount + vertices.size() > m_globalVertexCapacity ||
        m_currentIndexCount + indices.size() > m_globalIndexCapacity) {
        throw std::runtime_error("Global Geometry Buffers are full! Increase capacity at startup.");
    }

    outVertexOffset = m_currentVertexCount;
    outFirstIndex = m_currentIndexCount;

    VkDeviceSize vertexSize = vertices.size() * sizeof(RenderTypes::Vertex);
    VkDeviceSize vertexOffsetBytes = m_currentVertexCount * sizeof(RenderTypes::Vertex);

    VkDeviceSize indexSize = indices.size() * sizeof(uint32_t);
    VkDeviceSize indexOffsetBytes = m_currentIndexCount * sizeof(uint32_t);

    auto allocator = ctx.getAllocator();
    auto [vStagingBuffer, vStagingAlloc] = allocator->createStagingBuffer((void*)vertices.data(), vertexSize);
    allocator->setAllocationName(vStagingAlloc, "Geometry_Staging_VBO");

    std::vector<uint32_t> rebasedIndices(indices.size());
    for (size_t i = 0; i < indices.size(); i++) {
        rebasedIndices[i] = indices[i] + m_currentVertexCount;
    }
    auto [iStagingBuffer, iStagingAlloc] = allocator->createStagingBuffer((void*)rebasedIndices.data(), indexSize);
    allocator->setAllocationName(iStagingAlloc, "Geometry_Staging_IBO"); 

    VkCommandBuffer cmd = Utils::beginSingleTimeCommands(ctx.getDevice(), ctx.getCommandPool());

    VkBufferCopy vCopyRegion{};
    vCopyRegion.srcOffset = 0;
    vCopyRegion.dstOffset = vertexOffsetBytes;
    vCopyRegion.size = vertexSize;
    vkCmdCopyBuffer(cmd, vStagingBuffer, GetBuffer(m_globalVertexBuffer)->buffer, 1, &vCopyRegion);

    VkBufferCopy iCopyRegion{};
    iCopyRegion.srcOffset = 0;
    iCopyRegion.dstOffset = indexOffsetBytes;
    iCopyRegion.size = indexSize;
    vkCmdCopyBuffer(cmd, iStagingBuffer, GetBuffer(m_globalIndexBuffer)->buffer, 1, &iCopyRegion);

    Utils::endSingleTimeCommands(ctx.getDevice(), ctx.getCommandPool(), ctx.getGraphicsQueue(), cmd);

    allocator->destroyBuffer(vStagingBuffer, vStagingAlloc);
    allocator->destroyBuffer(iStagingBuffer, iStagingAlloc);

    m_currentVertexCount += static_cast<uint32_t>(vertices.size());
    m_currentIndexCount += static_cast<uint32_t>(indices.size());
}

VkBuffer Core::ResourceManager::GetGlobalVertexBuffer()  {
    return GetBuffer(m_globalVertexBuffer)->buffer;
}

VkBuffer Core::ResourceManager::GetGlobalIndexBuffer() {
    return GetBuffer(m_globalIndexBuffer)->buffer;
}


Core::PipelineBuilder::Pipeline* Core::ResourceManager::CreateGraphicsPipeline(const std::string& name, const GraphicsPipelineConfig& config) {
    PipelineBuilder builder(context);
    m_pipelines[name] = PipelineFactory::CreateGraphics(&builder, config);

    return m_pipelines[name].get();
}

Core::PipelineBuilder::Pipeline* Core::ResourceManager::CreateComputePipeline(const std::string& name, const ComputePipelineConfig& config) {
    PipelineBuilder builder(context);
    m_pipelines[name] = PipelineFactory::CreateCompute(&builder, config);
    return m_pipelines[name].get();
}

Core::PipelineBuilder::Pipeline* Core::ResourceManager::CreateRayTracingPipeline(const std::string& name, const RayTracingPipelineConfig& config) {
    PipelineBuilder builder(context);
    m_pipelines[name] = PipelineFactory::CreateRayTracing(&builder, config);
    return m_pipelines[name].get();
}

Core::PipelineBuilder::Pipeline* Core::ResourceManager::GetPipeline(const std::string& name) const {
    auto it = m_pipelines.find(name);
    if (it != m_pipelines.end()) {
        return it->second.get();
    }
    return nullptr; 
}

void Core::ResourceManager::InitGlobalBuffers()
{
    m_cameraUBOs.resize(MAX_FRAMES_IN_FLIGHT);
    m_instanceSSBOs.resize(MAX_FRAMES_IN_FLIGHT);
    m_LightSSBOs.resize(MAX_FRAMES_IN_FLIGHT);
    m_CascadeUBOs.resize(MAX_FRAMES_IN_FLIGHT);
    m_PointShadowUBOs.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        Core::BufferDesc uboDesc{};
        uboDesc.name = "CameraUBO_Frame_" + std::to_string(i);
        uboDesc.size = sizeof(RenderTypes::CameraUBO);
        uboDesc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uboDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        uboDesc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        m_cameraUBOs[i] = CreateBuffer(uboDesc);

        Core::BufferDesc instanceDesc{};
        instanceDesc.name = "InstanceSSBO_Frame_" + std::to_string(i);
        instanceDesc.size = sizeof(RenderTypes::InstanceData) * 10000;
        instanceDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        instanceDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        instanceDesc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        m_instanceSSBOs[i] = CreateBuffer(instanceDesc);

        Core::BufferDesc lightDesc{};
        lightDesc.name = "LightSSBO_Frame_" + std::to_string(i);
        lightDesc.size = sizeof(RenderTypes::LightSSBO);
        lightDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        lightDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        lightDesc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        m_LightSSBOs[i] = CreateBuffer(lightDesc);

		Core::BufferDesc cascadeDesc{};
    	cascadeDesc.name = "CascadeUBO_Frame_" + std::to_string(i);
    	cascadeDesc.size = sizeof(RenderTypes::CascadeUBO);
    	cascadeDesc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    	cascadeDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    	cascadeDesc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    	m_CascadeUBOs[i] = CreateBuffer(cascadeDesc);

        Core::BufferDesc desc{};
        desc.name = "PointLightMatrixUBO_Frame_" + std::to_string(i);
        desc.size = sizeof(glm::mat4) * 6 * MAX_POINT_LIGHTS;
        desc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        desc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        desc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        m_PointShadowUBOs[i] = CreateBuffer(desc);
    }

    Core::BufferDesc expDesc{};
    expDesc.name = "ExposureBuffer";
    expDesc.size = sizeof(float);
    expDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    m_exposureBuffer = CreateBuffer(expDesc);

    float initialExposure = 0.0f;

  
    BufferBuilder::Buffer* expBuf = GetBuffer(m_exposureBuffer);
    if (expBuf->mapped != nullptr) {
        memcpy(expBuf->mapped, &initialExposure, sizeof(float));
    }




}

void Core::ResourceManager::InitGlobalDescriptorSet()
{
    m_globalDescriptorSet = Core::DescriptorBuilder(context)
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .addLayoutBinding(3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .addLayoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            MAX_BINDLESS_TEXTURES,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT)
        .build(MAX_FRAMES_IN_FLIGHT);

    VkBuffer globalVertexBuffer = GetGlobalVertexBuffer();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        Core::DescriptorWriter writer;

        VkBuffer uboBuffer = GetBuffer(m_cameraUBOs[i])->buffer;
        VkBuffer instanceBuffer = GetBuffer(m_instanceSSBOs[i])->buffer;
        VkBuffer lightBuffer = GetBuffer(m_LightSSBOs[i])->buffer;

        writer.writeBuffer(0, uboBuffer, 0, sizeof(RenderTypes::CameraUBO),
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .writeBuffer(1, globalVertexBuffer, 0, VK_WHOLE_SIZE,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, instanceBuffer, 0, VK_WHOLE_SIZE,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(3, lightBuffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .overwrite(m_globalDescriptorSet.sets[i], context.getDevice());
    }
}

void Core::ResourceManager::InitRTDescriptorSet()
{
    m_globalRTDescriptorSet = Core::DescriptorBuilder(context)
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT)
        .build(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        Core::DescriptorWriter writer;

        VkAccelerationStructureKHR tlas = GetGlobalTLAS();

        if (tlas != VK_NULL_HANDLE) {
            writer.writeAccelerationStructure(0, &tlas);
            writer.overwrite(m_globalRTDescriptorSet.sets[i], context.getDevice());
        }
    }
}

void Core::ResourceManager::Shutdown()
{
  
    m_pipelines.clear();
    for (auto& buffer : m_buffers) {
        if (buffer.buffer != VK_NULL_HANDLE) {
            buffer.destroy();
        }
    }
    m_buffers.clear();

    if (m_LinearSampler != VK_NULL_HANDLE)
        vkDestroySampler(context.getDevice(), m_LinearSampler, nullptr);
    if (m_PointSampler != VK_NULL_HANDLE)
        vkDestroySampler(context.getDevice(), m_PointSampler, nullptr);
    if (m_ShadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(context.getDevice(), m_ShadowSampler, nullptr);
    if (m_PointRepeatSampler != VK_NULL_HANDLE)
        vkDestroySampler(context.getDevice(), m_PointRepeatSampler, nullptr);
    for (auto& texture : m_textures) {
        if (texture.image != VK_NULL_HANDLE) {
            texture.destroy();
        }
    }
    m_textures.clear();

    for (auto& [id, mipMap] : m_mipViews) {
        for (auto& [level, view] : mipMap) {
            vkDestroyImageView(context.getDevice(), view, nullptr);
        }
    }
    m_mipViews.clear();

    for (auto& [id, mipMap] : m_cubeMipViews) {
        for (auto& [level, view] : mipMap) {
            vkDestroyImageView(context.getDevice(), view, nullptr);
        }
    }
    m_cubeMipViews.clear();

    m_transientTexturePool.clear();
    m_transientBufferPool.clear();

    m_globalVertexBuffer = BufferHandle{};
    m_globalIndexBuffer = BufferHandle{};
    m_currentVertexCount = 0;
    m_currentIndexCount = 0;

    m_globalDescriptorSet.destroy(context.getDevice());
    m_globalRTDescriptorSet.destroy(context.getDevice());
}

void Core::ResourceManager::DestroyTexture(TextureHandle handle)
{
    if (!handle.IsValid()) return;

    uint32_t index = handle.id;
    if (index >= m_textures.size()) return;

    auto& img = m_textures[index];
    VkDevice device = context.getDevice();
    auto* allocator = context.getAllocator();

    auto mipIt = m_mipViews.find(index);
    if (mipIt != m_mipViews.end()) {
        for (auto& [level, view] : mipIt->second) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        m_mipViews.erase(mipIt);
    }

    auto cubeIt = m_cubeMipViews.find(index);
    if (cubeIt != m_cubeMipViews.end()) {
        for (auto& [level, view] : cubeIt->second) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        m_cubeMipViews.erase(cubeIt);
    }

    if (img.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, img.view, nullptr);
        img.view = VK_NULL_HANDLE;
    }

    if (img.image != VK_NULL_HANDLE && img.allocation != VK_NULL_HANDLE) {
        allocator->destroyImage(img.image, img.allocation);
        img.image = VK_NULL_HANDLE;
        img.allocation = nullptr;
    }
}

void Core::ResourceManager::DestroyBuffer(BufferHandle handle)
{
    if (!handle.IsValid()) return;

    uint32_t index = handle.id;
    if (index >= m_buffers.size()) return;

    auto& buf = m_buffers[index];
    VkDevice device = context.getDevice();
    auto* allocator = context.getAllocator();

    if (buf.buffer != VK_NULL_HANDLE && buf.allocation != VK_NULL_HANDLE) {
        allocator->destroyBuffer(buf.buffer, buf.allocation);
        buf.buffer = VK_NULL_HANDLE;
        buf.allocation = nullptr;
    }
}