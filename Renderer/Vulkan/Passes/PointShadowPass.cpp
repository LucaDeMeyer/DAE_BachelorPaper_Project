#include "PointShadowPass.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/Pipeline.h"
#include "../Core/Scene.h"
#include "../Core/GraphicsContext.h"

Render::Pass::PointShadowPass::~PointShadowPass()
{
    if (m_pipeline) {
        m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    }
    m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());

}

void Render::Pass::PointShadowPass::Setup(Graph::RenderGraphBuilder& builder)
{
    m_pointLightCount = static_cast<uint32_t>(m_scene->GetPointLights().size());

    if (m_pointLightCount == 0)
        return;

    for (uint32_t i = 0; i < m_pointLightCount; i++)
    {
        Core::TextureDesc desc{};
        desc.name = "PointShadow_" + std::to_string(i);
        desc.extent = { POINT_SHADOW_SIZE, POINT_SHADOW_SIZE, 1 };
        desc.format = VK_FORMAT_D32_SFLOAT;
        desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        desc.arrayLayers = NUM_FACES;
        desc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        desc.isCube = true;
        
        auto handle = builder.CreateTexture(desc,true);
        builder.AddDependency(handle, Graph::AccessType::DepthWrite);
        m_pointShadowMaps.push_back(handle);
    }

    
    for (int i = 0; i < Core::MAX_FRAMES_IN_FLIGHT; i++)
    {
        Core::DescriptorWriter writer;
        writer
            .writeBuffer(0,
                m_resourceManager->GetBuffer(m_resourceManager->GetPointShadowUBOs()[i])->buffer,
                0, sizeof(RenderTypes::PointShadowUBO),
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .writeBuffer(1,
                m_resourceManager->GetBuffer(
                    m_resourceManager->GetInstanceSSBOs()[i])->buffer,
                0, VK_WHOLE_SIZE,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .overwrite(m_descriptorSet.sets[i],
                m_resourceManager->GetContext().getDevice());
    }
}

void Render::Pass::PointShadowPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    if (m_pointLightCount == 0)
        return;

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = context.resourceManager->GetGlobalVertexBuffer();
    VkDeviceAddress vertexAddress = vkGetBufferDeviceAddress(
        context.resourceManager->GetContext().getDevice(), &addressInfo);

    vkCmdBindIndexBuffer(context.cmd,
        context.resourceManager->GetGlobalIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    VkViewport viewport{};
    viewport.width = (float)POINT_SHADOW_SIZE;
    viewport.height = (float)POINT_SHADOW_SIZE;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = { POINT_SHADOW_SIZE, POINT_SHADOW_SIZE };

    for (uint32_t lightIdx = 0; lightIdx < m_pointLightCount; lightIdx++)
    {
        Core::ImageBuilder::Image* shadowImage = context.resourceManager->GetTexture(
            graph.GetResource(m_pointShadowMaps[lightIdx]).physicalTexture);

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = shadowImage->view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.viewMask = 0b00111111; // 6 faces, replaces layerCount
        renderingInfo.layerCount = 1;          // ignored when viewMask != 0
        renderingInfo.renderArea = { {0, 0}, {POINT_SHADOW_SIZE, POINT_SHADOW_SIZE} };
        renderingInfo.colorAttachmentCount = 0;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(context.cmd, &renderingInfo);
        vkCmdSetViewport(context.cmd, 0, 1, &viewport);
        vkCmdSetScissor(context.cmd, 0, 1, &scissor);
        vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);
        vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipeline->layout, 0, 1,
            &m_descriptorSet.sets[context.currentFrameIndex], 0, nullptr);

        uint32_t instanceID = 0;
        for (const auto& obj : m_scene->GetObjects())
        {
            RenderTypes::PassPushConstants push{};
            push.vertexAddress = vertexAddress;
            push.instanceID = instanceID;
            push.lightIndex = lightIdx;

            vkCmdPushConstants(context.cmd, m_pipeline->layout,
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RenderTypes::PassPushConstants), &push);
            vkCmdDrawIndexed(context.cmd,
                obj.mesh->indexCount, 1,
                obj.mesh->firstIndex, 0, 0);

            instanceID++;
        }

        vkCmdEndRendering(context.cmd);
    }
}