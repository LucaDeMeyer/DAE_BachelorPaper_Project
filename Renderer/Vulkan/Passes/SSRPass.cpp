#include "SSRPass.h"
#include "../RenderGraph/RenderGraph.h"

void Render::Pass::SSRPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc ssrDesc{};
    ssrDesc.name = "SSR_Output";
    ssrDesc.extent = { m_extent.width, m_extent.height, 1 };
    ssrDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ssrDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ssrDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ssrDesc.mipLevels = 1;
    ssrDesc.arrayLayers = 1;

    m_ssrOut = builder.CreateTexture(ssrDesc);
    builder.AddDependency(m_ssrOut, Graph::AccessType::ColorAttachmentWrite);

    m_Depth = builder.FindResource("SceneDepth");
    builder.AddDependency(m_Depth, Graph::AccessType::ShaderRead);

    m_Normal = builder.FindResource("GBuffer_Normal");
    builder.AddDependency(m_Normal, Graph::AccessType::ShaderRead);

    m_Material = builder.FindResource("GBuffer_Material");
    builder.AddDependency(m_Material, Graph::AccessType::ShaderRead);

    if (m_taaHist0.IsValid()) builder.AddDependency(m_taaHist0, Graph::AccessType::ShaderRead);
    if (m_taaHist1.IsValid()) builder.AddDependency(m_taaHist1, Graph::AccessType::ShaderRead);
}

void Render::Pass::SSRPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* ssrOutImage = context.resourceManager->GetTexture(graph.GetResource(m_ssrOut).physicalTexture);
    Core::ImageBuilder::Image* depthImage = context.resourceManager->GetTexture(graph.GetResource(m_Depth).physicalTexture);
    Core::ImageBuilder::Image* normalImage = context.resourceManager->GetTexture(graph.GetResource(m_Normal).physicalTexture);
    Core::ImageBuilder::Image* materialImage = context.resourceManager->GetTexture(graph.GetResource(m_Material).physicalTexture);


    uint32_t prevFrameIdx = (context.currentFrameIndex + 1) % 2;
    Core::TextureHandle activePrevTex = (prevFrameIdx == 0) ? m_texHist0 : m_texHist1;

    Core::ImageBuilder::Image* prevColorImage = materialImage;
    if (activePrevTex.IsValid()) {
        prevColorImage = context.resourceManager->GetTexture(activePrevTex);
    }

    Core::DescriptorWriter writer;
    writer.writeImage(0, depthImage->view, context.resourceManager->GetPointSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, normalImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(2, materialImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(3, prevColorImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeBuffer(4, context.resourceManager->GetBuffer(context.resourceManager->GetCameraUBO()[context.currentFrameIndex])->buffer, 0, sizeof(RenderTypes::CameraUBO), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .overwrite(m_descriptorSet.sets[context.currentFrameIndex], m_resourceManager->GetContext().getDevice());

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = ssrOutImage->view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = { .color = { 0.0f, 0.0f, 0.0f, 0.0f } };

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

    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout, 0, 1, &m_descriptorSet.sets[context.currentFrameIndex], 0, nullptr);
    vkCmdDraw(context.cmd, 3, 1, 0, 0);


    vkCmdEndRendering(context.cmd);

    Utils::TransitionImageLayout(
        context.cmd, ssrOutImage->image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 1
    );
}