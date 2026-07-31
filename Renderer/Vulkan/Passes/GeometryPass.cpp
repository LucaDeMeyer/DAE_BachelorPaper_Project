#include "GeometryPass.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/Pipeline.h"
#include "../Core/Scene.h"
#include "../Core/Object.h"
#include "../Core/GraphicsContext.h"

Render::Pass::GeometryPass::~GeometryPass()
{
    if (m_pipeline && m_resourceManager) {
        m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    }
}

void Render::Pass::GeometryPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc albedoDesc{};
    albedoDesc.name = "GBuffer_Albedo";
    albedoDesc.extent = { m_extent.width, m_extent.height, 1 };
    albedoDesc.format = VK_FORMAT_R8G8B8A8_SRGB; 
    albedoDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    albedoDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    albedoDesc.mipLevels = 1;
    albedoDesc.arrayLayers = 1;
    m_Albedo = builder.CreateTexture(albedoDesc);
    builder.AddDependency(m_Albedo, Graph::AccessType::ColorAttachmentWrite);

    Core::TextureDesc normalDesc{};
    normalDesc.name = "GBuffer_Normal";
    normalDesc.extent = { m_extent.width, m_extent.height, 1 };
    normalDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    normalDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    normalDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    normalDesc.mipLevels = 1;
    normalDesc.arrayLayers = 1;
    m_Normal = builder.CreateTexture(normalDesc);
    builder.AddDependency(m_Normal, Graph::AccessType::ColorAttachmentWrite);

    Core::TextureDesc materialDesc{};
    materialDesc.name = "GBuffer_Material";
    materialDesc.extent = { m_extent.width, m_extent.height, 1 };
    materialDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
    materialDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    materialDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    materialDesc.mipLevels = 1;
    materialDesc.arrayLayers = 1;
    m_Material = builder.CreateTexture(materialDesc);
    builder.AddDependency(m_Material, Graph::AccessType::ColorAttachmentWrite);

    m_Depth = builder.FindResource("SceneDepth");
    builder.AddDependency(m_Depth, Graph::AccessType::DepthRead);
}
void Render::Pass::GeometryPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* albedoImage = context.resourceManager->GetTexture(graph.GetResource(m_Albedo).physicalTexture);
    Core::ImageBuilder::Image* normalImage = context.resourceManager->GetTexture(graph.GetResource(m_Normal).physicalTexture);
    Core::ImageBuilder::Image* materialImage = context.resourceManager->GetTexture(graph.GetResource(m_Material).physicalTexture);
    Core::ImageBuilder::Image* depthImage = context.resourceManager->GetTexture(graph.GetResource(m_Depth).physicalTexture);

    VkRenderingAttachmentInfo colorAttachments[3]{};

    colorAttachments[0] = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = albedoImage->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = { 0.0f, 0.0f, 0.0f, 1.0f } }
    };

    colorAttachments[1] = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = normalImage->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = { 0.0f, 0.0f, 0.0f, 0.0f } }
    };

    colorAttachments[2] = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = materialImage->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = { 0.0f, 0.5f, 0.0f, 0.0f } }  
    };

    VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImage->view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0, 0}, m_extent },
        .layerCount = 1,
        .colorAttachmentCount = 3,
        .pColorAttachments = colorAttachments,
        .pDepthAttachment = &depthAttachment
    };

    vkCmdBeginRendering(context.cmd, &renderingInfo);
    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);

    VkViewport viewport{ 0.0f, 0.0f, (float)m_extent.width, (float)m_extent.height, 0.0f, 1.0f };
    vkCmdSetViewport(context.cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, m_extent };
    vkCmdSetScissor(context.cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(
        context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout,
        0, 1, &m_scene->globalDescriptorSet, 0, nullptr
    );

    VkBufferDeviceAddressInfo deviceAdressInfo =
    {
    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    .buffer = context.resourceManager->GetGlobalVertexBuffer()

    };
    VkDeviceAddress VertexBufferAddress = vkGetBufferDeviceAddress(context.resourceManager->GetContext().getDevice(), &deviceAdressInfo);


    VkBuffer globalIndexBuffer = context.resourceManager->GetGlobalIndexBuffer();
    vkCmdBindIndexBuffer(context.cmd, globalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    uint32_t instanceID = 0;
    for (const auto& obj : m_scene->GetObjects())
    {
        RenderTypes::PassPushConstants push{};
        push.vertexAddress = VertexBufferAddress;
        push.instanceID = instanceID;


        vkCmdPushConstants(
            context.cmd, m_pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(RenderTypes::PassPushConstants), &push
        );

        vkCmdDrawIndexed(context.cmd, obj.mesh->indexCount, 1, obj.mesh->firstIndex, 0, 0);
        instanceID++;
    }

    vkCmdEndRendering(context.cmd);
}
