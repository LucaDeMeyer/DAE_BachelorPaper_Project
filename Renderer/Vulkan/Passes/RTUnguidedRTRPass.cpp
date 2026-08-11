#include "RTUnguidedRTRPass.h"
#include "../RenderGraph/RenderGraph.h"

Render::Pass::UnguidedRTRPass::UnguidedRTRPass(const std::string& name, Core::ResourceManager* resManager, VkExtent2D extent)
    : Pass(name), m_resourceManager(resManager), m_Extent(extent)
{
    // Binding 0: Output Image (No G-Buffer required)
    m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .build(Core::MAX_FRAMES_IN_FLIGHT);

    Core::RayTracingPipelineConfig rtConfig{};
    rtConfig.raygenShader = "shaders/UnguidedRTR.rgen.spv";
    rtConfig.missShaders = { "shaders/RTShadow.rmiss.spv" }; // Dummy miss shader to satisfy pipeline
    rtConfig.hitShaders = { "shaders/RTShadow.rchit.spv" };  // Dummy hit shader to satisfy pipeline
    rtConfig.maxRayRecursionDepth = 1;

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

Render::Pass::UnguidedRTRPass::~UnguidedRTRPass()
{
    if (m_pipeline) m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
    m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());
    if (m_sbt) m_sbt->Shutdown();
}

void Render::Pass::UnguidedRTRPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc outDesc{};
    outDesc.name = "RT_ReflectionOutput";
    outDesc.extent = { m_Extent.width, m_Extent.height, 1 };
    outDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT; 
    outDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    outDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    outDesc.arrayLayers = 1;

    m_rtrOut = builder.CreateTexture(outDesc, true);
    builder.AddDependency(m_rtrOut, Graph::AccessType::ComputeShaderWrite);
}

void Render::Pass::UnguidedRTRPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* outImage = context.resourceManager->GetTexture(graph.GetResource(m_rtrOut).physicalTexture);

    Core::DescriptorWriter writer;
    writer.writeImage(0, outImage->view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
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
        outImage->extent.width,
        outImage->extent.height,
        1
    );
}