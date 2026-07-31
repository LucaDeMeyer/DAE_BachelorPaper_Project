#include "ShadowPass.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/Pipeline.h"
#include "../Core/Scene.h"
#include "../Core/GraphicsContext.h"

Render::Pass::ShadowPass::~ShadowPass()
{
    if (m_pipeline) {
        m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    }
    m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());

}

void Render::Pass::ShadowPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc shadowDesc{};
    shadowDesc.name = "ShadowMap";
    shadowDesc.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1 };
    shadowDesc.format = VK_FORMAT_D32_SFLOAT;
    shadowDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT;
    shadowDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    shadowDesc.arrayLayers = NUM_CASCADES;

    m_shadowMap = builder.CreateTexture(shadowDesc);
    builder.AddDependency(m_shadowMap, Graph::AccessType::DepthWrite);

    for (int i = 0; i < Core::MAX_FRAMES_IN_FLIGHT; i++) {
        Core::DescriptorWriter writer;
        writer
            .writeBuffer(0,
                m_resourceManager->GetBuffer(
                    m_resourceManager->GetCascadeUBOs()[i])->buffer,
                0, sizeof(RenderTypes::CascadeUBO),
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


void Render::Pass::ShadowPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* shadowImage = context.resourceManager->GetTexture(graph.GetResource(m_shadowMap).physicalTexture);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = shadowImage->view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { {0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE} };
    renderingInfo.layerCount = NUM_CASCADES;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.pDepthAttachment = &depthAttachment;

    VkViewport viewport{};
    viewport.width = (float)SHADOW_MAP_SIZE;
    viewport.height = (float)SHADOW_MAP_SIZE;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };


    vkCmdBeginRendering(context.cmd, &renderingInfo);
    vkCmdSetViewport(context.cmd, 0, 1, &viewport);
    vkCmdSetScissor(context.cmd, 0, 1, &scissor);
    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);

    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeline->layout, 0, 1,
        &m_descriptorSet.sets[context.currentFrameIndex], 0, nullptr);

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = context.resourceManager->GetGlobalVertexBuffer();
    VkDeviceAddress vertexAddress = vkGetBufferDeviceAddress(
        context.resourceManager->GetContext().getDevice(), &addressInfo);

    vkCmdBindIndexBuffer(context.cmd, context.resourceManager->GetGlobalIndexBuffer(),
        0, VK_INDEX_TYPE_UINT32);

    uint32_t instanceID = 0;
    for (const auto& obj : m_scene->GetObjects()) {
        RenderTypes::PassPushConstants push{};
        push.vertexAddress = vertexAddress;
        push.instanceID = instanceID;

        vkCmdPushConstants(context.cmd, m_pipeline->layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(RenderTypes::PassPushConstants), &push);

        vkCmdDrawIndexed(context.cmd,
            obj.mesh->indexCount,
            NUM_CASCADES,
            obj.mesh->firstIndex,
            0, 0);

        instanceID++;
    }

    vkCmdEndRendering(context.cmd);
}
