#ifndef RTAO_PASS_H
#define RTAO_PASS_H

#include <memory>
#include <vulkan/vulkan.h>
#include "../RenderGraph/Pass.h"
#include "../Core/RayTracing/ShaderBindingTable.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "../Core/GraphicsContext.h"

namespace Render::Pass
{
    class RTAOPass : public Graph::Pass
    {
    public:
        RTAOPass(const std::string& name, Core::ResourceManager* resManager, VkExtent2D extent)
            : Pass(name), m_resourceManager(resManager), m_Extent(extent)
        {
            m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
                .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
                .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
                .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
                .build(Core::MAX_FRAMES_IN_FLIGHT);

            Core::RayTracingPipelineConfig rtConfig{};
            rtConfig.raygenShader = "shaders/RTAO.rgen.spv";
            rtConfig.missShaders = { "shaders/RTShadow.rmiss.spv" }; // Reuse shadow miss!
            rtConfig.hitShaders = { "shaders/RTShadow.rchit.spv" };  // Reuse shadow hit!
            rtConfig.maxRayRecursionDepth = 1;

            // Add push constant for the random seed
            rtConfig.pushConstants = { {VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(uint32_t)} };

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

        ~RTAOPass() override {
            if (m_pipeline) m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
            m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());
            if (m_sbt) m_sbt->Shutdown();
        }

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return false; }
        Graph::RGHandle GetAOMask() const { return m_aoMask; }

    private:
        Core::ResourceManager* m_resourceManager = nullptr;
        Graph::RGHandle m_aoMask;
        Graph::RGHandle m_Depth;
        Graph::RGHandle m_Normal;

        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
        Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
        std::unique_ptr<Core::RT::ShaderBindingTable> m_sbt;
        VkExtent2D m_Extent;
    };
}
#endif