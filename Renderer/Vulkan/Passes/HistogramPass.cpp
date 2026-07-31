#include "HistogramPass.h"
#include "../Core/ResourceManager.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/GraphicsContext.h"
namespace Render::Pass {

    HistogramPass::HistogramPass(const std::string& name, Core::ResourceManager* resManager)
        : Pass(name), m_resourceManager(resManager)
    {
        auto& context = m_resourceManager->GetContext();

        Core::DescriptorBuilder builder(context);
        m_buildBlueprint = builder
            .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build(1);

        Core::PipelineBuilder pipeBuilder(context);
        Core::ComputePipelineConfig buildRef{};
        buildRef.computeShader = "shaders/BuildHistogram.comp.spv";
        buildRef.descriptorLayouts = { m_buildBlueprint.layout };

        VkPushConstantRange buildPush{};
        buildPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        buildPush.offset = 0;
        buildPush.size = sizeof(BuildHistogramParams);
        buildRef.pushConstants.push_back(buildPush);
        m_buildPipeline = Core::PipelineFactory::CreateCompute(&pipeBuilder, buildRef);

        m_averageBlueprint = builder
            .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build(1);

        Core::ComputePipelineConfig avgRef{};
        avgRef.computeShader = "shaders/AverageHistogram.comp.spv";
        avgRef.descriptorLayouts = { m_averageBlueprint.layout };
        VkPushConstantRange avgPush{};
        avgPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        avgPush.offset = 0;
        avgPush.size = sizeof(AverageHistogramParams);
        avgRef.pushConstants.push_back(avgPush);
        m_averagePipeline = Core::PipelineFactory::CreateCompute(&pipeBuilder, avgRef);
    }

    HistogramPass::~HistogramPass() {
        auto device = m_resourceManager->GetContext().getDevice();
        if (m_buildPipeline) m_buildPipeline->destroy(device);
        if (m_averagePipeline) m_averagePipeline->destroy(device);

        m_buildBlueprint.destroy(device);
        m_averageBlueprint.destroy(device);

        for (auto& descriptor : m_averageDescriptors)
            descriptor.destroy(device);
        for (auto& descriptor : m_buildDescriptors)
            descriptor.destroy(device);
    }

    void HistogramPass::Setup(Graph::RenderGraphBuilder& builder) {
        m_hdrInput = builder.FindResource("LightingOut");
        builder.AddDependency(m_hdrInput, Graph::AccessType::ComputeShaderRead);

        Core::BufferDesc histDesc{};
        histDesc.name = "HistogramBuffer";
        histDesc.size = 256 * sizeof(uint32_t);
        histDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; 
        m_histogramBuffer = builder.CreateBuffer(histDesc);
        builder.AddDependency(m_histogramBuffer, Graph::AccessType::ComputeShaderWrite);

        Core::BufferDesc expDesc{};
        expDesc.name = "ExposureBuffer";
        expDesc.size = sizeof(float);
        expDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        Core::BufferHandle expHandle = m_resourceManager->GetExposureBuffer();
        VkBuffer rawExpBuffer = m_resourceManager->GetBuffer(expHandle)->buffer;
        m_exposureBuffer = builder.RegisterImportedBuffer(rawExpBuffer, expDesc);
        builder.AddDependency(m_exposureBuffer, Graph::AccessType::ReadWrite);

        m_buildDescriptors.resize(1);
        m_averageDescriptors.resize(1);

        Core::DescriptorBuilder descBuilder(m_resourceManager->GetContext());
        m_buildDescriptors[0] = descBuilder
            .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build(Core::MAX_FRAMES_IN_FLIGHT);

        m_averageDescriptors[0] = descBuilder
            .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build(Core::MAX_FRAMES_IN_FLIGHT);
    }

    void HistogramPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) {
        VkDevice device = context.resourceManager->GetContext().getDevice();
        uint32_t frameIdx = context.resourceManager->GetFrameIndex();

        auto& hdrRes = graph.GetResource(m_hdrInput);
        VkImageView hdrView = context.resourceManager->GetTexture(hdrRes.physicalTexture)->view;
        VkExtent2D hdrExtent = context.resourceManager->GetTexture(hdrRes.physicalTexture)->extent;
        
        VkBuffer histBuf = graph.GetPhysicalBuffer(m_histogramBuffer, *context.resourceManager);
        VkBuffer expBuf = graph.GetPhysicalBuffer(m_exposureBuffer, *context.resourceManager);
       
        vkCmdFillBuffer(context.cmd, histBuf, 0, VK_WHOLE_SIZE, 0);

        VkBufferMemoryBarrier2 fillBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        fillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        fillBarrier.buffer = histBuf;
        fillBarrier.offset = 0;
        fillBarrier.size = VK_WHOLE_SIZE;

        VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &fillBarrier;
        vkCmdPipelineBarrier2(context.cmd, &depInfo);

        Core::DescriptorWriter writer;
        writer.writeImage(0, hdrView, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .writeBuffer(1, histBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .overwrite(m_buildDescriptors[0].sets[frameIdx], device);

        vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_buildPipeline->pipeline);
        vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_buildPipeline->layout, 0, 1, &m_buildDescriptors[0].sets[frameIdx], 0, nullptr);

        BuildHistogramParams buildParams{};
        vkCmdPushConstants(context.cmd, m_buildPipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BuildHistogramParams), &buildParams);
        // Dispatch one compute thread per 16x16 block of pixels.
		// This perfectly aligns with the GPU's hardware warp/wavefront size (32 or 64 threads)
		// ensuring maximum occupancy on the compute units.
        uint32_t groupX = (hdrExtent.width + 15) / 16;
        uint32_t groupY = (hdrExtent.height + 15) / 16;
        vkCmdDispatch(context.cmd, groupX, groupY, 1);

        VkBufferMemoryBarrier2 buildBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        buildBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        buildBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        buildBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        buildBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        buildBarrier.buffer = histBuf;
        buildBarrier.offset = 0;
        buildBarrier.size = VK_WHOLE_SIZE;

        depInfo.pBufferMemoryBarriers = &buildBarrier;
        vkCmdPipelineBarrier2(context.cmd, &depInfo);

        writer.writeBuffer(0, histBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, expBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .overwrite(m_averageDescriptors[0].sets[frameIdx], device);

        vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_averagePipeline->pipeline);
        vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_averagePipeline->layout, 0, 1, &m_averageDescriptors[0].sets[frameIdx], 0, nullptr);

        AverageHistogramParams avgParams{};
        avgParams.timeDelta = context.resourceManager->GetDeltaTime();
        avgParams.pixelCount = hdrExtent.width * hdrExtent.height;

        // Grab the live settings from ImGui!
        float aperture = context.cameraSettings.aperture;
        float shutterSpeed = context.cameraSettings.shutterSpeed;
        float iso = context.cameraSettings.iso;

        avgParams.manualEV100 = log2f((aperture * aperture) / shutterSpeed) - log2f(iso / 100.0f);

        vkCmdPushConstants(context.cmd, m_averagePipeline->layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0,
            sizeof(AverageHistogramParams), &avgParams);
     

        vkCmdDispatch(context.cmd, 1, 1, 1);
    }
}