#include "SVGFSpatialPass.h"
#include "../RenderGraph/RenderGraph.h"

void Render::Pass::SVGFSpatialPass::Setup(Graph::RenderGraphBuilder& builder)
{
    m_inputImage = builder.FindResource(m_inputName);
    builder.AddDependency(m_inputImage, Graph::AccessType::ComputeShaderRead);

    m_Depth = builder.FindResource("SceneDepth");
    builder.AddDependency(m_Depth, Graph::AccessType::ComputeShaderRead);

    m_Normal = builder.FindResource("GBuffer_Normal");
    builder.AddDependency(m_Normal, Graph::AccessType::ComputeShaderRead);

    if (m_isRTAO == 3) {
        m_Material = builder.FindResource("GBuffer_Albedo");
    }
    else {
        m_Material = builder.FindResource("GBuffer_Material");
    }

    Core::TextureDesc outDesc{};
    outDesc.name = m_outputName; 
    outDesc.extent = { m_Extent.width, m_Extent.height, 1 };
	outDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    outDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    outDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    outDesc.arrayLayers = m_arrayLayers;
    m_output = builder.CreateTexture(outDesc,false);
    builder.AddDependency(m_output, Graph::AccessType::ComputeShaderWrite);
}

void Render::Pass::SVGFSpatialPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    auto* inputImg = context.resourceManager->GetTexture(graph.GetResource(m_inputImage).physicalTexture);
    auto* depthImg = context.resourceManager->GetTexture(graph.GetResource(m_Depth).physicalTexture);
    auto* normalImg = context.resourceManager->GetTexture(graph.GetResource(m_Normal).physicalTexture);
    auto* matImg = context.resourceManager->GetTexture(graph.GetResource(m_Material).physicalTexture);
    auto* outputImg = context.resourceManager->GetTexture(graph.GetResource(m_output).physicalTexture);

    VkSampler sampler = context.resourceManager->GetLinearSampler();

    Core::DescriptorWriter writer;
    writer.writeImage(0, inputImg->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, depthImg->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(2, normalImg->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(3, matImg->view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(4, outputImg->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .overwrite(m_descriptorSet.sets[context.currentFrameIndex], m_resourceManager->GetContext().getDevice());

    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->pipeline);

    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->layout, 0, 1, &m_descriptorSet.sets[context.currentFrameIndex], 0, nullptr);

    SpatialPushConstants pc{};
    pc.stepSize = m_stepSize;
    pc.isRTAO = m_isRTAO;
    vkCmdPushConstants(context.cmd, m_pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SpatialPushConstants), &pc);

    uint32_t groupX = (m_Extent.width + 15) / 16;
    uint32_t groupY = (m_Extent.height + 15) / 16;
    vkCmdDispatch(context.cmd, groupX, groupY, m_arrayLayers);
}