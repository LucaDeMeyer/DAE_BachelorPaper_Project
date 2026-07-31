#include "ToneMappingPass.h"
#include "../Core/ResourceTypes.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/ResourceManager.h"
#include "../Core/GraphicsContext.h"

Render::Pass::ToneMappingPass::~ToneMappingPass()
{
    if (m_pipeline) {
        m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    }

    m_descriptors.destroy(m_resourceManager->GetContext().getDevice());

}
void Render::Pass::ToneMappingPass::Setup(Graph::RenderGraphBuilder& builder)
{
	m_hdrInput = builder.FindResource("LightingOut");
	builder.AddDependency(m_hdrInput, Graph::AccessType::ComputeShaderRead);

    Core::TextureDesc ldrDesc{};
    ldrDesc.name = "ToneMap_OUT";
    ldrDesc.extent = { m_extent.width, m_extent.height, 1 };
    ldrDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
    ldrDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ldrDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    m_ToneMapOut = builder.CreateTexture(ldrDesc);
    builder.AddDependency(m_ToneMapOut, Graph::AccessType::ComputeShaderWrite);

    Core::BufferDesc expDesc{};
    expDesc.name = "ExposureBuffer";
    expDesc.size = sizeof(float);
    expDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    Core::BufferHandle expHandle = m_resourceManager->GetExposureBuffer();
    VkBuffer rawExpBuffer = m_resourceManager->GetBuffer(expHandle)->buffer;

    m_exposureBuffer = builder.RegisterImportedBuffer(rawExpBuffer, expDesc);
    builder.AddDependency(m_exposureBuffer, Graph::AccessType::ComputeShaderRead);
}

void Render::Pass::ToneMappingPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
   VkImageView hdrView = context.resourceManager->GetTexture(graph.GetResource(m_hdrInput).physicalTexture)->view;
   VkImageView ldrView = context.resourceManager->GetTexture(graph.GetResource(m_ToneMapOut).physicalTexture)->view;
   VkBuffer expBuf = graph.GetPhysicalBuffer(m_exposureBuffer, *context.resourceManager);
    Core::DescriptorWriter writer;
    writer.writeImage(0, hdrView, context.resourceManager->GetLinearSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(1, ldrView, VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .writeBuffer(2, expBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .overwrite(m_descriptors.sets[context.currentFrameIndex], context.resourceManager->GetContext().getDevice());

    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->pipeline);

    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->layout,
        0, 1, &m_descriptors.sets[context.currentFrameIndex], 0, nullptr);

   
    vkCmdPushConstants(context.cmd, m_pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RenderTypes::PhysicalCameraSettings), &context.cameraSettings);

    uint32_t groupX = (m_extent.width + 15) / 16;
    uint32_t groupY = (m_extent.height + 15) / 16;

    vkCmdDispatch(context.cmd, groupX, groupY, 1);
}
