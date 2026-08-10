#include "SVGFTemporalPass.h"
#include "../RenderGraph/RenderGraph.h"

void Render::Pass::SVGFTemporalPass::Setup(Graph::RenderGraphBuilder& builder)
{
    m_inputHandle = builder.FindResource(m_inputName);
    builder.AddDependency(m_inputHandle, Graph::AccessType::ComputeShaderRead);

  
    builder.AddDependency(m_history0, Graph::AccessType::ComputeShaderRead);
    builder.AddDependency(m_history1, Graph::AccessType::ComputeShaderRead);

    m_Depth = builder.FindResource("SceneDepth");
    builder.AddDependency(m_Depth, Graph::AccessType::ComputeShaderRead);

    m_Normal = builder.FindResource("GBuffer_Normal");
    builder.AddDependency(m_Normal, Graph::AccessType::ComputeShaderRead);

    m_Material = builder.FindResource("GBuffer_Material");
    builder.AddDependency(m_Material, Graph::AccessType::ComputeShaderRead);

    Core::TextureDesc outDesc{};
    outDesc.name = m_outputName; 
    outDesc.extent = { m_Extent.width, m_Extent.height, 1 };

	outDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;

    outDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    outDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    m_output = builder.CreateTexture(outDesc);
    builder.AddDependency(m_output, Graph::AccessType::ComputeShaderWrite);
}

void Render::Pass::SVGFTemporalPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    auto* inputimg = context.resourceManager->GetTexture(graph.GetResource(m_inputHandle).physicalTexture);
    auto* depthImage = context.resourceManager->GetTexture(graph.GetResource(m_Depth).physicalTexture);
    auto* normalImage = context.resourceManager->GetTexture(graph.GetResource(m_Normal).physicalTexture);
    auto* materialImage = context.resourceManager->GetTexture(graph.GetResource(m_Material).physicalTexture);

    // Transient graph output
    auto* outputImage = context.resourceManager->GetTexture(graph.GetResource(m_output).physicalTexture);

    // Ping-Pong history buffers
    bool pingpong = (context.currentFrameIndex % 2) == 0;
    auto* readHistory = context.resourceManager->GetTexture(pingpong ? m_physHistory1 : m_physHistory0);
    auto* writeHistory = context.resourceManager->GetTexture(pingpong ? m_physHistory0 : m_physHistory1);

    // Transition writeHistory to GENERAL layout
    Utils::TransitionImageLayout(
        context.cmd, writeHistory->image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );

    VkSampler sampler = context.resourceManager->GetLinearSampler();

    Core::DescriptorWriter writer;
    writer.writeImage(0, inputimg->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, depthImage->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(2, normalImage->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(3, materialImage->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(4, readHistory->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(5, writeHistory->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .writeImage(6, outputImage->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .overwrite(m_descriptorSet.sets[context.currentFrameIndex], m_resourceManager->GetContext().getDevice());

    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->pipeline);

    std::vector<VkDescriptorSet> boundSets = {
        m_resourceManager->GetGlobalDescriptorSet().sets[context.currentFrameIndex],
        m_descriptorSet.sets[context.currentFrameIndex]
    };

    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->layout, 0, 2, boundSets.data(), 0, nullptr);

    // Push isRTAO flag (int)
    vkCmdPushConstants(context.cmd, m_pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &m_isRTAO);

    uint32_t groupX = (m_Extent.width + 15) / 16;
    uint32_t groupY = (m_Extent.height + 15) / 16;
    vkCmdDispatch(context.cmd, groupX, groupY, 1);
}