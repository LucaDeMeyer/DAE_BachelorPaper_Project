#include "RTRPass.h"
#include "../RenderGraph/RenderGraph.h"


void Render::Pass::RTRPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc maskDesc{};
    maskDesc.name = "RT_ReflectionOutput";
    maskDesc.extent = { m_Extent.width, m_Extent.height, 1 };
    maskDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    maskDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    maskDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    maskDesc.arrayLayers = 1;

    m_rtrMask = builder.CreateTexture(maskDesc,true);

    builder.AddDependency(m_rtrMask, Graph::AccessType::ComputeShaderWrite);

    m_Depth = builder.FindResource("SceneDepth");
    builder.AddDependency(m_Depth, Graph::AccessType::ComputeShaderRead);

    m_Normal = builder.FindResource("GBuffer_Normal");
    builder.AddDependency(m_Normal, Graph::AccessType::ComputeShaderRead);

    m_Material = builder.FindResource("GBuffer_Material");
    builder.AddDependency(m_Material, Graph::AccessType::ComputeShaderRead);
}

void Render::Pass::RTRPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* rtrOutImage = context.resourceManager->GetTexture(graph.GetResource(m_rtrMask).physicalTexture);
    Core::ImageBuilder::Image* depthImage = context.resourceManager->GetTexture(graph.GetResource(m_Depth).physicalTexture);
    Core::ImageBuilder::Image* normalImage = context.resourceManager->GetTexture(graph.GetResource(m_Normal).physicalTexture);
    Core::ImageBuilder::Image* materialImage = context.resourceManager->GetTexture(graph.GetResource(m_Material).physicalTexture);

    auto* vertexBuffer = context.resourceManager->GetGlobalVertexBuffer();
    auto* indexBuffer = context.resourceManager->GetGlobalIndexBuffer();

    Core::DescriptorWriter writer;
    writer.writeImage(0, depthImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, normalImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(2, materialImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(3, rtrOutImage->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .writeBuffer(4, vertexBuffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(5, indexBuffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .overwrite(m_descriptorSet.sets[context.currentFrameIndex], m_resourceManager->GetContext().getDevice());

    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->pipeline);

    std::vector<VkDescriptorSet> boundSets = {
        m_resourceManager->GetGlobalDescriptorSet().sets[context.currentFrameIndex],
        m_resourceManager->GetRTDescriptorSet().sets[context.currentFrameIndex],
        m_descriptorSet.sets[context.currentFrameIndex]
    };

    vkCmdBindDescriptorSets(
        context.cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->layout,
        0, static_cast<uint32_t>(boundSets.size()), boundSets.data(), 0, nullptr
    );

    RenderTypes::RTPushConstants pushData{};
    pushData.frameCount = context.resourceManager->GetFrameIndex();
    pushData.spp = context.m_spp;
    vkCmdPushConstants(context.cmd, m_pipeline->layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RenderTypes::RTPushConstants), &pushData);


    auto pfn_vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(m_resourceManager->GetContext().getDevice(), "vkCmdTraceRaysKHR");

    pfn_vkCmdTraceRaysKHR(
        context.cmd,
        m_sbt->GetRaygenRegion(),
        m_sbt->GetMissRegion(),
        m_sbt->GetHitRegion(),
        m_sbt->GetCallableRegion(),
        rtrOutImage->extent.width,
        rtrOutImage->extent.height,
        1
    );
}