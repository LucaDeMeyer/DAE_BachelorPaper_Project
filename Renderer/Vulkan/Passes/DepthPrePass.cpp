#include "DepthPrePass.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/Pipeline.h"
#include "../Core/Scene.h"
#include "../Core/Object.h"
#include "Vulkan/Core/GraphicsContext.h"


Render::Pass::DepthPrePass::DepthPrePass(const std::string& name,
    VkExtent2D extent,
    Core::ResourceManager* resManager,
    const Core::Scene* scene)
    : Pass(name), m_extent(extent), m_resourceManager(resManager), m_scene(scene)
{
    Core::GraphicsPipelineConfig depthConfig{};
    depthConfig.vertexShader = "shaders/depth.vert.spv";
    depthConfig.fragmentShader = "shaders/depth.frag.spv";
    depthConfig.descriptorLayouts = { m_resourceManager->GetGlobalDescriptorSet().layout };
    depthConfig.pushConstants = {
        { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RenderTypes::PassPushConstants) }
    };
    depthConfig.colorFormats = {};
    depthConfig.depthFormat = VK_FORMAT_D32_SFLOAT;
    depthConfig.enableDepthTest = true;
    depthConfig.enableDepthWrite = true;
    depthConfig.depthCompareOp = VK_COMPARE_OP_LESS;
    depthConfig.cullMode = VK_CULL_MODE_NONE;

    Core::PipelineBuilder builder(m_resourceManager->GetContext());

    m_pipeline = Core::PipelineFactory::CreateGraphics(&builder, depthConfig);
}

Render::Pass::DepthPrePass::~DepthPrePass()
{
    if (m_pipeline && m_resourceManager) {
        m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    }
}

void Render::Pass::DepthPrePass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc depthDesc{};
    depthDesc.name = "SceneDepth";
    depthDesc.extent = { m_extent.width, m_extent.height, 1 };
    depthDesc.format = VK_FORMAT_D32_SFLOAT;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthDesc.mipLevels = 1;
    depthDesc.arrayLayers = 1;
    m_DepthTexture = builder.CreateTexture(depthDesc,true);
    builder.AddDependency(m_DepthTexture, Graph::AccessType::DepthWrite);
}
void Render::Pass::DepthPrePass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
   
    const auto& physicalResource = graph.GetResource(m_DepthTexture);
    Core::ImageBuilder::Image* depthImage = context.resourceManager->GetTexture(physicalResource.physicalTexture);
    VkImageView depthView = depthImage->view;

    VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, 
        .clearValue = {.depthStencil = { 1.0f, 0 } }
    };
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0, 0}, m_extent },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pColorAttachments = nullptr,
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
	VkDeviceAddress VertexBufferAddress =    vkGetBufferDeviceAddress(context.resourceManager->GetContext().getDevice(), &deviceAdressInfo);

    VkBuffer globalIndexBuffer = context.resourceManager->GetGlobalIndexBuffer();
    vkCmdBindIndexBuffer(context.cmd, globalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);


    uint32_t instanceID = 0;
    for (const auto& obj : m_scene->GetObjects())
    {
	    RenderTypes::PassPushConstants push{};
        push.vertexAddress = VertexBufferAddress;
        push.instanceID = instanceID;

        vkCmdPushConstants(
            context.cmd, m_pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(RenderTypes::PassPushConstants), &push
        );

        vkCmdDrawIndexed(context.cmd, obj.mesh->indexCount, 1, obj.mesh->firstIndex, 0, 0);
        instanceID++;
    }

    vkCmdEndRendering(context.cmd);
}