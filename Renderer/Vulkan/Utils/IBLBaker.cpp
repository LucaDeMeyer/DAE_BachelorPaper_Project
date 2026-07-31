#include "IBLBaker.h"
#include "../Core/TextureLoader.h"
#include "Utils.h"
#include "../Core/Allocator.h"
#include "../Core/GraphicsContext.h"


IBLBaker::IBLBaker(Core::GraphicsContext* context, Core::ResourceManager* resManager) : m_context(context),m_resourceManager(resManager)
{

    InitPipelines();
}

IBLBaker::~IBLBaker()
{
  if ( m_equirectangularPipeline)
  m_equirectangularPipeline->destroy(m_context->getDevice());
  if (m_irradiancePipeline)
      m_irradiancePipeline->destroy(m_context->getDevice());
	m_equirectSet.destroy(m_context->getDevice());
   m_irradianceSet.destroy(m_context->getDevice());

   if (m_preFilterdPipeline)
       m_preFilterdPipeline->destroy(m_context->getDevice());
   if (m_brdfLutPipline)
       m_brdfLutPipline->destroy(m_context->getDevice());

   m_prefilterSet.destroy(m_context->getDevice());
   m_brdfLUTSet.destroy(m_context->getDevice());
}

void IBLBaker::InitPipelines()
{
    m_equirectSet = Core::DescriptorBuilder(*m_context)
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(1);

    Core::ComputePipelineConfig config{};
    config.computeShader = "shaders/equirectangular.comp.spv";
    config.descriptorLayouts = { m_equirectSet.layout };

    m_equirectangularPipeline = m_resourceManager->CreateComputePipeline("Equirectangular", config);

    m_irradianceSet = Core::DescriptorBuilder(*m_context)
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(1);

    Core::ComputePipelineConfig irrConfig{};
    irrConfig.computeShader = "shaders/Irradiance.comp.spv";
    irrConfig.descriptorLayouts = { m_irradianceSet.layout };

    m_irradiancePipeline = m_resourceManager->CreateComputePipeline("Irradiance", irrConfig);

    m_prefilterSet = Core::DescriptorBuilder(*m_context)
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(5);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float); 

    Core::ComputePipelineConfig prefilterConfig{};
    prefilterConfig.computeShader = "shaders/prefilter.comp.spv";
    prefilterConfig.descriptorLayouts = { m_prefilterSet.layout };
    prefilterConfig.pushConstants = { pushRange };

    m_preFilterdPipeline = m_resourceManager->CreateComputePipeline("Prefilter", prefilterConfig);

    m_brdfLUTSet = Core::DescriptorBuilder(*m_context)
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(1);

    Core::ComputePipelineConfig brdfConfig{};
    brdfConfig.computeShader = "shaders/brdf_lut.comp.spv";
    brdfConfig.descriptorLayouts = { m_brdfLUTSet.layout };

    m_brdfLutPipline = m_resourceManager->CreateComputePipeline("BRDFLUT", brdfConfig);
}

IBLTextures IBLBaker::BakeEnvironment(const std::string& hdrFilePath, Core::TextureHandle existingBrdfLut)
{
    IBLTextures results{};

    //Load Data & Allocate Memory
    Core::TextureData hdrData = Core::TextureLoader::LoadPixels(hdrFilePath, VK_FORMAT_R32G32B32A32_SFLOAT);
    if (hdrData.pixels.empty() || hdrData.size == 0) {
        throw std::runtime_error("Failed to load HDR image via TextureLoader: " + hdrFilePath);
    }

    auto allocator = m_context->getAllocator();
    auto [stagingBuffer, stagingAlloc] = allocator->createStagingBuffer(hdrData.pixels.data(), hdrData.size);

    Core::TextureHandle hdr2DTexture = AllocateIBLTextures(hdrData, results,existingBrdfLut);

    //Fetch Image Pointers
    Core::ImageBuilder::Image* hdrImage = m_resourceManager->GetTexture(hdr2DTexture);
    Core::ImageBuilder::Image* cubeImage = m_resourceManager->GetTexture(results.environmentCubemap);
    Core::ImageBuilder::Image* irrImage = m_resourceManager->GetTexture(results.irradianceCubemap);
    Core::ImageBuilder::Image* prefilterImage = m_resourceManager->GetTexture(results.prefilteredCubemap);
    Core::ImageBuilder::Image* brdfImage = m_resourceManager->GetTexture(results.brdfLUT);

    uint32_t envMipLevels = static_cast<uint32_t>(std::floor(std::log2(1024))) + 1;
    uint32_t prefilterMips = 5;

    //Record GPU Commands
    VkCommandBuffer cmd = Utils::beginSingleTimeCommands(m_context->getDevice(), m_context->getCommandPool());

    CopyHDRToGPU(cmd, stagingBuffer, hdrImage, hdrData.width, hdrData.height);
    BakeEquirectangularToCubemap(cmd, hdrImage, cubeImage, envMipLevels);
    BakeIrradiance(cmd, cubeImage, irrImage);
    BakePrefilter(cmd, cubeImage, prefilterImage, results.prefilteredCubemap, prefilterMips);

    if (!existingBrdfLut.IsValid()) {
        BakeBRDFLUT(cmd, brdfImage);
    }

    Utils::endSingleTimeCommands(m_context->getDevice(), m_context->getCommandPool(), m_context->getGraphicsQueue(), cmd);

    m_resourceManager->DestroyTexture(hdr2DTexture);
    //Cleanup
    allocator->destroyBuffer(stagingBuffer, stagingAlloc);

    return results;
}

Core::TextureHandle IBLBaker::AllocateIBLTextures(const Core::TextureData& hdrData, IBLTextures& results, Core::TextureHandle existingBrdfLut)
{
    // Temporary 2D HDR
    Core::TextureDesc hdrDesc{};
    hdrDesc.name = "TempHDR2D";
    hdrDesc.extent = { hdrData.width, hdrData.height, 1 };
    hdrDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    hdrDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    hdrDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    Core::TextureHandle hdr2DTexture = m_resourceManager->CreateTexture(hdrDesc);

    // Environment Cubemap
    Core::TextureDesc cubeDesc{};
    cubeDesc.name = "EnvironmentCubemap";
    cubeDesc.extent = { 1024, 1024, 1 };
    cubeDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    cubeDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    cubeDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    cubeDesc.arrayLayers = 6;
    cubeDesc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cubeDesc.isCube = true;
    cubeDesc.mipLevels = static_cast<uint32_t>(std::floor(std::log2(1024))) + 1;
    results.environmentCubemap = m_resourceManager->CreateTexture(cubeDesc);

    // Irradiance Cubemap
    Core::TextureDesc irrDesc{};
    irrDesc.name = "IrradianceCubemap";
    irrDesc.extent = { 32, 32, 1 };
    irrDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    irrDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    irrDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    irrDesc.arrayLayers = 6;
    irrDesc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    results.irradianceCubemap = m_resourceManager->CreateTexture(irrDesc);

    // Prefiltered Cubemap
    Core::TextureDesc prefilterDesc{};
    prefilterDesc.name = "PrefilterCubemap";
    prefilterDesc.extent = { 128, 128, 1 };
    prefilterDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    prefilterDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    prefilterDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    prefilterDesc.arrayLayers = 6;
    prefilterDesc.mipLevels = 5;
    prefilterDesc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    prefilterDesc.isCube = true;
    results.prefilteredCubemap = m_resourceManager->CreateTexture(prefilterDesc);

    // BRDF LUT

    if (existingBrdfLut.IsValid()) {
        results.brdfLUT = existingBrdfLut;
    }
    else {
        Core::TextureDesc brdfDesc{};
        brdfDesc.name = "BRDFLUT";
        brdfDesc.extent = { 512, 512, 1 };
        brdfDesc.format = VK_FORMAT_R16G16_SFLOAT;
        brdfDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        brdfDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        results.brdfLUT = m_resourceManager->CreateTexture(brdfDesc);
    }

    return hdr2DTexture;
}

void IBLBaker::CopyHDRToGPU(VkCommandBuffer cmd, VkBuffer stagingBuffer, Core::ImageBuilder::Image* hdrImage, uint32_t width, uint32_t height)
{
    Utils::TransitionImageLayout(cmd, hdrImage->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    Utils::CopyBufferToImage(cmd, stagingBuffer, hdrImage->image, width, height);

    Utils::TransitionImageLayout(cmd, hdrImage->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}

void IBLBaker::BakeEquirectangularToCubemap(VkCommandBuffer cmd, Core::ImageBuilder::Image* hdrImage, Core::ImageBuilder::Image* cubeImage, uint32_t mipLevels)
{
    Utils::TransitionImageLayout(cmd, cubeImage->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, 6);

    Core::DescriptorWriter writer;
    writer.writeImage(0, hdrImage->view, m_resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, cubeImage->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .overwrite(m_equirectSet.sets[0], m_context->getDevice());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_equirectangularPipeline->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_equirectangularPipeline->layout, 0, 1, &m_equirectSet.sets[0], 0, nullptr);
    vkCmdDispatch(cmd, 1024 / 16, 1024 / 16, 6);

    Utils::TransitionImageLayout(cmd, cubeImage->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, 6);

    // Mipmap generation leaves the cubemap in SHADER_READ_ONLY_OPTIMAL
    Utils::generateMipmaps(m_context->getPhysicalDevice(), cmd, cubeImage->image, VK_FORMAT_R16G16B16A16_SFLOAT, 1024, 1024, mipLevels, 6);
}

void IBLBaker::BakeIrradiance(VkCommandBuffer cmd, Core::ImageBuilder::Image* cubeImage, Core::ImageBuilder::Image* irrImage)
{
    Utils::TransitionImageLayout(cmd, irrImage->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 6);

    Core::DescriptorWriter irrWriter;
    irrWriter.writeImage(0, cubeImage->view, m_resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, irrImage->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .overwrite(m_irradianceSet.sets[0], m_context->getDevice());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiancePipeline->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiancePipeline->layout, 0, 1, &m_irradianceSet.sets[0], 0, nullptr);
    vkCmdDispatch(cmd, 32 / 16, 32 / 16, 6);

    Utils::TransitionImageLayout(cmd, irrImage->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 6);
}

void IBLBaker::BakePrefilter(VkCommandBuffer cmd, Core::ImageBuilder::Image* cubeImage, Core::ImageBuilder::Image* prefilterImage, Core::TextureHandle prefilterHandle, uint32_t mipLevels)
{
    Utils::TransitionImageLayout(cmd, prefilterImage->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, 6);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_preFilterdPipeline->pipeline);

    for (uint32_t mip = 0; mip < mipLevels; mip++) {
        uint32_t mipSize = std::max(1u, 128u >> mip);
        float roughness = (float)mip / (float)(mipLevels - 1);

        VkImageView mipView = m_resourceManager->GetMipViewCube(prefilterHandle, mip);

        Core::DescriptorWriter prefilterWriter;
        prefilterWriter.writeImage(0, cubeImage->view, m_resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .writeImage(1, mipView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .overwrite(m_prefilterSet.sets[mip], m_context->getDevice());

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_preFilterdPipeline->layout, 0, 1, &m_prefilterSet.sets[mip], 0, nullptr);
        vkCmdPushConstants(cmd, m_preFilterdPipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);

        uint32_t groups = std::max(1u, mipSize / 16);
        vkCmdDispatch(cmd, groups, groups, 6);
    }

    Utils::TransitionImageLayout(cmd, prefilterImage->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, 6);
}

void IBLBaker::BakeBRDFLUT(VkCommandBuffer cmd, Core::ImageBuilder::Image* brdfImage)
{
    Utils::TransitionImageLayout(cmd, brdfImage->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

    Core::DescriptorWriter brdfWriter;
    brdfWriter.writeImage(0, brdfImage->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .overwrite(m_brdfLUTSet.sets[0], m_context->getDevice());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfLutPipline->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfLutPipline->layout, 0, 1, &m_brdfLUTSet.sets[0], 0, nullptr);
    vkCmdDispatch(cmd, 512 / 16, 512 / 16, 1);

    Utils::TransitionImageLayout(cmd, brdfImage->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}
