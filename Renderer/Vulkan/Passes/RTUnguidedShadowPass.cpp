#include "RTUnguidedShadowPass.h"
#include "../RenderGraph/RenderGraph.h"

Render::Pass::UnguidedShadowPass::UnguidedShadowPass(const std::string& name, Core::ResourceManager* resManager, VkExtent2D extent)
    : Pass(name), m_resourceManager(resManager), m_Extent(extent)
{
    m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .build(Core::MAX_FRAMES_IN_FLIGHT);

    Core::RayTracingPipelineConfig rtConfig{};
    rtConfig.raygenShader = "shaders/UnguidedShadow.rgen.spv";
    rtConfig.missShaders = { "shaders/UnguidedShadow.rmiss.spv" };
    rtConfig.hitShaders = { "shaders/UnguidedShadow.rchit.spv" };
    rtConfig.maxRayRecursionDepth = 2;
    rtConfig.pushConstants = { {VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RenderTypes::RTPushConstants)} };
    rtConfig.descriptorLayouts = {
        m_resourceManager->GetGlobalDescriptorSet().layout,
        m_resourceManager->GetRTDescriptorSet().layout,
        m_descriptorSet.layout
    };

    Core::PipelineBuilder pipelineBuilder(m_resourceManager->GetContext());
    m_pipeline = Core::PipelineFactory::CreateRayTracing(&pipelineBuilder, rtConfig);

    m_sbt = std::make_unique<Core::RT::ShaderBindingTable>(
        m_resourceManager->GetContext(),
        *m_resourceManager,
        m_pipeline->pipeline,
        m_pipeline->missGroupCount,
        m_pipeline->hitGroupCount
    );
}

Render::Pass::UnguidedShadowPass::~UnguidedShadowPass()
{
    if (m_pipeline) m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());
    if (m_sbt) m_sbt->Shutdown();
}

void Render::Pass::UnguidedShadowPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc maskDesc{};
    maskDesc.name = "RT_ShadowMask";
    maskDesc.extent = { m_Extent.width, m_Extent.height, 1 };
    maskDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    maskDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    maskDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    maskDesc.arrayLayers = 1;

    m_shadowMask = builder.CreateTexture(maskDesc, true);
    builder.AddDependency(m_shadowMask, Graph::AccessType::ComputeShaderWrite);
}

void Render::Pass::UnguidedShadowPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* maskImage = context.resourceManager->GetTexture(graph.GetResource(m_shadowMask).physicalTexture);

    Core::DescriptorWriter writer;
    writer.writeImage(0, maskImage->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
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
        maskImage->extent.width,
        maskImage->extent.height,
        1
    );
}