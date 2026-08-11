#include "DefferedLightingPass.h"

#include "../Core/ResourceTypes.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/ResourceManager.h"
#include "../Core/Scene.h"
#include "../Core/GraphicsContext.h"

Render::Pass::DefferdLightingPass::~DefferdLightingPass()
{
    if (m_pipeline && m_resourceManager) {
        m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    }

    m_LightingDescriptors.destroy(m_resourceManager->GetContext().getDevice());
}

void Render::Pass::DefferdLightingPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc lightingOutDesc{};
    lightingOutDesc.name = "LightingOut";
    lightingOutDesc.extent = { m_extent.width,m_extent.height,1 };
    lightingOutDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    lightingOutDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    lightingOutDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    lightingOutDesc.mipLevels = 1;
    lightingOutDesc.arrayLayers = 1;
    m_LightingOut = builder.CreateTexture(lightingOutDesc, true);
    builder.AddDependency(m_LightingOut, Graph::AccessType::ColorAttachmentWrite);

    m_Albedo = builder.FindResource("GBuffer_Albedo");
    builder.AddDependency(m_Albedo, Graph::AccessType::ShaderRead);

    m_Normal = builder.FindResource("GBuffer_Normal");
    builder.AddDependency(m_Normal, Graph::AccessType::ShaderRead);

    m_Material = builder.FindResource("GBuffer_Material");
    builder.AddDependency(m_Material, Graph::AccessType::ShaderRead);

    m_Depth = builder.FindResource("SceneDepth");
    builder.AddDependency(m_Depth, Graph::AccessType::DepthShaderRead);

    m_envMap = builder.FindResource("EnvironmentCubemap");
    builder.AddDependency(m_envMap, Graph::AccessType::ShaderRead);

    m_irrMap = builder.FindResource("IrradianceCubemap");
    builder.AddDependency(m_irrMap, Graph::AccessType::ShaderRead);

    m_prefilterMap = builder.FindResource("PrefilterCubemap");
    builder.AddDependency(m_prefilterMap, Graph::AccessType::ShaderRead);

    m_brdflut = builder.FindResource("BRDFLUT");
    builder.AddDependency(m_brdflut, Graph::AccessType::ShaderRead);

    m_shadowMap = builder.FindResource("ShadowMap");
    if (m_shadowMap.IsValid()) {
        builder.AddDependency(m_shadowMap, Graph::AccessType::ShaderRead);
    }

    m_pointShadowMaps.clear();
    uint32_t activeLights = std::min((uint32_t)m_scene->GetPointLights().size(), Core::MAX_POINT_LIGHTS);
    for (uint32_t i = 0; i < activeLights; i++) {
        auto handle = builder.FindResource("PointShadow_" + std::to_string(i));
        if (handle.IsValid()) {
            builder.AddDependency(handle, Graph::AccessType::DepthShaderRead);
            m_pointShadowMaps.push_back(handle);
        }
    }

    m_ssaoMap = builder.FindResource("SSAO_Blur");
    if (m_ssaoMap.IsValid()) {
        builder.AddDependency(m_ssaoMap, Graph::AccessType::ShaderRead);
    }

    m_ssrOut = builder.FindResource("SSR_Output");
    if (m_ssrOut.IsValid()) {
        builder.AddDependency(m_ssrOut, Graph::AccessType::ShaderRead);
    }


    m_rtShadowMask = builder.FindResource((m_usePostDenoising == 1) ? "RT_ShadowMask" : "SVGF_Shadow_Final");
    if (m_rtShadowMask.IsValid()) builder.AddDependency(m_rtShadowMask, Graph::AccessType::ShaderRead);

    m_rtPointShadowMask = builder.FindResource((m_usePostDenoising == 1) ? "RT_PointShadowMask" : "SVGF_PointShadow_Final");
    if (m_rtPointShadowMask.IsValid()) builder.AddDependency(m_rtPointShadowMask, Graph::AccessType::ShaderRead);

    m_rtAOGuided = builder.FindResource((m_usePostDenoising == 1) ? "RT_AOMask" : "SVGF_RTAO_Final");
    if (m_rtAOGuided.IsValid()) builder.AddDependency(m_rtAOGuided, Graph::AccessType::ShaderRead);

    m_rtAOUnguided = builder.FindResource((m_usePostDenoising == 1) ? "RT_AOMask" : "SVGF_RTAO_Final");
    if (m_rtAOUnguided.IsValid()) builder.AddDependency(m_rtAOUnguided, Graph::AccessType::ShaderRead);

    m_rtReflectionGuided = builder.FindResource((m_usePostDenoising == 1) ? "RT_ReflectionOutput" : "SVGF_RTR_Final");
    if (m_rtReflectionGuided.IsValid()) builder.AddDependency(m_rtReflectionGuided, Graph::AccessType::ShaderRead);

    m_rtReflectionUnguided = builder.FindResource((m_usePostDenoising == 1) ? "RT_ReflectionOutput" : "SVGF_RTR_Final");
    if (m_rtReflectionUnguided.IsValid()) builder.AddDependency(m_rtReflectionUnguided, Graph::AccessType::ShaderRead);
}

void Render::Pass::DefferdLightingPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* albedoImage = context.resourceManager->GetTexture(graph.GetResource(m_Albedo).physicalTexture);
    Core::ImageBuilder::Image* normalImage = context.resourceManager->GetTexture(graph.GetResource(m_Normal).physicalTexture);
    Core::ImageBuilder::Image* materialImage = context.resourceManager->GetTexture(graph.GetResource(m_Material).physicalTexture);
    Core::ImageBuilder::Image* depthImage = context.resourceManager->GetTexture(graph.GetResource(m_Depth).physicalTexture);
    Core::ImageBuilder::Image* lightingOUT = context.resourceManager->GetTexture(graph.GetResource(m_LightingOut).physicalTexture);

    // Dynamic Binding Resolution
    VkImageView rasterShadowView = VK_NULL_HANDLE;
    if (m_shadowMap.IsValid()) {
        rasterShadowView = context.resourceManager->GetTexture(graph.GetResource(m_shadowMap).physicalTexture)->view;
    }

    VkImageView activeAOView = VK_NULL_HANDLE;
    if (m_rtAOGuided.IsValid()) {
        activeAOView = context.resourceManager->GetTexture(graph.GetResource(m_rtAOGuided).physicalTexture)->view;
    }
    else if (m_rtAOUnguided.IsValid()) {
        activeAOView = context.resourceManager->GetTexture(graph.GetResource(m_rtAOUnguided).physicalTexture)->view;
    }
    else if (m_ssaoMap.IsValid()) {
        activeAOView = context.resourceManager->GetTexture(graph.GetResource(m_ssaoMap).physicalTexture)->view;
    }

    VkImageView activeReflectionView = VK_NULL_HANDLE;
    if (m_rtReflectionGuided.IsValid()) {
        activeReflectionView = context.resourceManager->GetTexture(graph.GetResource(m_rtReflectionGuided).physicalTexture)->view;
    }
    else if (m_rtReflectionUnguided.IsValid()) {
        activeReflectionView = context.resourceManager->GetTexture(graph.GetResource(m_rtReflectionUnguided).physicalTexture)->view;
    }
    else if (m_ssrOut.IsValid()) {
        activeReflectionView = context.resourceManager->GetTexture(graph.GetResource(m_ssrOut).physicalTexture)->view;
    }

    VkImageView rtShadowView = VK_NULL_HANDLE;
    if (m_rtShadowMask.IsValid()) {
        rtShadowView = context.resourceManager->GetTexture(graph.GetResource(m_rtShadowMask).physicalTexture)->view;
    }

    VkImageView rtPointShadowView = VK_NULL_HANDLE;
    if (m_rtPointShadowMask.IsValid()) {
        rtPointShadowView = context.resourceManager->GetTexture(graph.GetResource(m_rtPointShadowMask).physicalTexture)->view;
    }

    Core::ImageBuilder::Image* envImage = context.resourceManager->GetTexture(m_envTex);
    Core::ImageBuilder::Image* irrImage = context.resourceManager->GetTexture(m_irrTex);
    Core::ImageBuilder::Image* prefilterimage = context.resourceManager->GetTexture(m_prefilterTex);
    Core::ImageBuilder::Image* brdfimage = context.resourceManager->GetTexture(m_brdfTex);

    std::vector<VkDescriptorImageInfo> shadowImageInfos;
    shadowImageInfos.reserve(m_pointShadowMaps.size());
    for (auto handle : m_pointShadowMaps) {
        Core::ImageBuilder::Image* img = context.resourceManager->GetTexture(graph.GetResource(handle).physicalTexture);
        VkDescriptorImageInfo info{};
        info.imageView = img->view;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.sampler = context.resourceManager->GetShadowSampler();
        shadowImageInfos.push_back(info);
    }

    Core::DescriptorWriter writer;
    writer.writeImage(0, albedoImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, normalImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(2, materialImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(3, depthImage->view, context.resourceManager->GetPointSampler(), VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(4, envImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(5, irrImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(6, prefilterimage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(7, brdfimage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(8, rasterShadowView, context.resourceManager->GetShadowSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(9, activeAOView, context.resourceManager->GetPointSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeBuffer(10, context.resourceManager->GetBuffer(context.resourceManager->GetCascadeUBOs()[context.resourceManager->GetFrameIndex()])->buffer, 0, sizeof(RenderTypes::CascadeUBO), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .writeImage(12, rtShadowView, context.resourceManager->GetPointSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(13, rtPointShadowView, context.resourceManager->GetPointSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(14, activeReflectionView, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .overwrite(m_LightingDescriptors.sets[context.resourceManager->GetFrameIndex()], context.resourceManager->GetContext().getDevice());

    if (!shadowImageInfos.empty()) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_LightingDescriptors.sets[context.resourceManager->GetFrameIndex()];
        write.dstBinding = 11;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = static_cast<uint32_t>(shadowImageInfos.size());
        write.pImageInfo = shadowImageInfos.data();
        vkUpdateDescriptorSets(context.resourceManager->GetContext().getDevice(), 1, &write, 0, nullptr);
    }

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = lightingOUT->view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0, 0}, m_extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = nullptr
    };

    vkCmdBeginRendering(context.cmd, &renderingInfo);
    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);

    VkViewport viewport{ 0.0f, 0.0f, (float)m_extent.width, (float)m_extent.height, 0.0f, 1.0f };
    vkCmdSetViewport(context.cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, m_extent };
    vkCmdSetScissor(context.cmd, 0, 1, &scissor);

    VkDescriptorSet globalSet = m_scene->globalDescriptorSet;
    VkDescriptorSet lightingSet = m_LightingDescriptors.sets[context.currentFrameIndex];
    VkDescriptorSet setsToBind[] = { globalSet, lightingSet };
    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout, 0, 2, setsToBind, 0, nullptr);

    RenderTypes::LightingDebugPushConstant push{};
    push.debugMode = context.debugViewMode;
    push.useRTShadows = context.m_enableRTShadows > 0 ? 1 : 0;
    push.usePostDenoising = context.m_usePostDenoising;
    vkCmdPushConstants(
        context.cmd, m_pipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(RenderTypes::LightingDebugPushConstant), &push
    );

    vkCmdDraw(context.cmd, 3, 1, 0, 0);
    vkCmdEndRendering(context.cmd);
}