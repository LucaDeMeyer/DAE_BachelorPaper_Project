#ifndef RTR_PASS_H
#define RTR_PASS_H

#include <memory>
#include <vulkan/vulkan.h>
#include "../RenderGraph/Pass.h"
#include "../Core/RayTracing/ShaderBindingTable.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "../Core/GraphicsContext.h"

namespace Render::Pass
{
    class RTRPass : public Graph::Pass
    {
    public:
        RTRPass(const std::string& name, Core::ResourceManager* resManager, VkExtent2D extent)
            : Pass(name), m_resourceManager(resManager), m_Extent(extent)
        {
            m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
                .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR) // Depth
                .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR) // Normal
                .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR) // Material (for roughness check)
                .addLayoutBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)          // RTR Output
                .addLayoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)    // Global Vertex Buffer
                .addLayoutBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)    // Global Index Buffer
                .build(Core::MAX_FRAMES_IN_FLIGHT);

            Core::RayTracingPipelineConfig rtConfig{};
            rtConfig.raygenShader = "shaders/RTR.rgen.spv";
            rtConfig.missShaders = { "shaders/RTR.rmiss.spv" };
            rtConfig.hitShaders = { "shaders/RTR.rchit.spv" };
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

        ~RTRPass() override {
            if (m_pipeline) m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
            m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());
            if (m_sbt) m_sbt->Shutdown();
        }

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return false; }

        Graph::RGHandle GetRTROutput() const { return m_rtrMask; }

    private:
        Core::ResourceManager* m_resourceManager = nullptr;
        Graph::RGHandle m_rtrMask;
        Graph::RGHandle m_Depth;
        Graph::RGHandle m_Normal;
        Graph::RGHandle m_Material;

        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
        Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
        std::unique_ptr<Core::RT::ShaderBindingTable> m_sbt;
        VkExtent2D m_Extent;
    };
}
#endif