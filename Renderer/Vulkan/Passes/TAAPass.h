#ifndef TAA_PASS_H
#define TAA_PASS_H

#include "../RenderGraph/Pass.h"
#include "../Core/ResourceManager.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Core/GraphicsContext.h"
#include <vulkan/vulkan.h>

namespace RenderTypes {
    struct RenderContext;
}

namespace Render::Pass
{
    /// @brief Temporal Anti-Aliasing (TAA) Pass.
    /// Accumulates noisy frames over time using a Velocity buffer to resolve sub-pixel jitter.
    class TAAPass : public Graph::Pass
    {
    public:
        TAAPass(const std::string& name, VkExtent2D extent, Core::ResourceManager* resManager,
            Graph::RGHandle history0, Graph::RGHandle history1,
            Core::TextureHandle histTex0, Core::TextureHandle histTex1,const std::string& input)
            : Pass(name), m_extent(extent), m_resourceManager(resManager),
            m_history0(history0), m_history1(history1),
            m_histTex0(histTex0), m_histTex1(histTex1), m_inputname(input)
        {
            Core::DescriptorBuilder builder(resManager->GetContext());
            m_descriptors = builder
                .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Noisy In
                .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Velocity
                .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Depth
                .addLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // History Read
                .addLayoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)          // TAA Out (Transient)
                .addLayoutBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)          // History Write
                .build(Core::MAX_FRAMES_IN_FLIGHT);

            Core::ComputePipelineConfig config{};
            config.computeShader = "shaders/TAA.comp.spv";

            config.descriptorLayouts = {
                m_resourceManager->GetGlobalDescriptorSet().layout,
                m_descriptors.layout
            };

            Core::PipelineBuilder Pipelinebuilder(m_resourceManager->GetContext());
            m_pipeline = Core::PipelineFactory::CreateCompute(&Pipelinebuilder, config);
        }

        ~TAAPass() override {
            if (m_pipeline) m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
            m_descriptors.destroy(m_resourceManager->GetContext().getDevice());
        }

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return true; }

        Graph::RGHandle GetTAAOutput() const { return m_taaOut; }

    private:
        VkExtent2D m_extent;
        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
        Core::ResourceManager* m_resourceManager;
        Core::DescriptorBuilder::DescriptorSet m_descriptors;

        Graph::RGHandle m_lightingIn;
        Graph::RGHandle m_velocityIn;
        Graph::RGHandle m_depthIn;

        Graph::RGHandle m_history0;
        Graph::RGHandle m_history1;

        Graph::RGHandle m_taaOut;

        Core::TextureHandle m_histTex0;
        Core::TextureHandle m_histTex1;

        std::string m_inputname;
    };
}
#endif