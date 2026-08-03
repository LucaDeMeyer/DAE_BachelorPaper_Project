#include "TAAPass.h"
#include "../Core/Image.h"

namespace Render::Pass
{
    void TAAPass::Setup(Graph::RenderGraphBuilder& builder)
    {
        Core::TextureDesc outDesc{};
        outDesc.name = "TAA_Output";
        outDesc.extent = { m_extent.width, m_extent.height, 1 };
        outDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        outDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        outDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        outDesc.mipLevels = 1;
        outDesc.arrayLayers = 1;

        m_taaOut = builder.CreateTexture(outDesc);

        m_depthIn = builder.FindResource("SceneDepth");
    	builder.AddDependency(m_depthIn, Graph::AccessType::ComputeShaderRead);
        m_lightingIn = builder.FindResource("LightingOut");
        builder.AddDependency(m_lightingIn, Graph::AccessType::ComputeShaderRead);
        m_velocityIn = builder.FindResource("VelocityMap");
        builder.AddDependency(m_velocityIn, Graph::AccessType::ComputeShaderRead);
      

        builder.AddDependency(m_history0, Graph::AccessType::ReadWrite);
        builder.AddDependency(m_history1, Graph::AccessType::ReadWrite);
        builder.AddDependency(m_taaOut, Graph::AccessType::ComputeShaderWrite);
    }

    void TAAPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
    {
       
        bool isEvenFrame = (context.currentFrameIndex % 2 == 0);
        Graph::RGHandle readHistoryHandle = isEvenFrame ? m_history1 : m_history0;
        Graph::RGHandle writeHistoryHandle = isEvenFrame ? m_history0 : m_history1;

        Core::TextureHandle readTexHandle = isEvenFrame ? m_histTex1 : m_histTex0;
        Core::TextureHandle writeTexHandle = isEvenFrame ? m_histTex0 : m_histTex1;

        auto* imgLightingIn = m_resourceManager->GetTexture(graph.GetResource(m_lightingIn).physicalTexture);
        auto* imgVelocityIn = m_resourceManager->GetTexture(graph.GetResource(m_velocityIn).physicalTexture);
        auto* imgDepthIn = m_resourceManager->GetTexture(graph.GetResource(m_depthIn).physicalTexture);
      
        auto* imgHistoryRead = m_resourceManager->GetTexture(readTexHandle);
        auto* imgHistoryWrite = m_resourceManager->GetTexture(writeTexHandle);

        auto* imgTaaOut = m_resourceManager->GetTexture(graph.GetResource(m_taaOut).physicalTexture);

        Core::DescriptorWriter writer;
        writer.writeImage(0, imgLightingIn->view, m_resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .writeImage(1, imgVelocityIn->view, m_resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .writeImage(2, imgDepthIn->view, m_resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .writeImage(3, imgHistoryRead->view, m_resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .writeImage(4, imgTaaOut->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .writeImage(5, imgHistoryWrite->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .overwrite(m_descriptors.sets[context.currentFrameIndex], m_resourceManager->GetContext().getDevice());

        vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->pipeline);

        std::vector<VkDescriptorSet> boundSets = {
            m_resourceManager->GetGlobalDescriptorSet().sets[context.currentFrameIndex],
            m_descriptors.sets[context.currentFrameIndex]
        };

        vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->layout,
            0, static_cast<uint32_t>(boundSets.size()), boundSets.data(), 0, nullptr);

        uint32_t groupX = (m_extent.width + 15) / 16;
        uint32_t groupY = (m_extent.height + 15) / 16;
        vkCmdDispatch(context.cmd, groupX, groupY, 1);
    }
}