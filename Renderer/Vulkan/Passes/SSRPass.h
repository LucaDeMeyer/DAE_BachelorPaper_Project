#ifndef SSR_PASS_H
#define SSR_PASS_H

#include "../RenderGraph/Pass.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include "Vulkan/Core/ResourceManager.h"
#include "../Core/GraphicsContext.h"
#include <vulkan/vulkan.h>

namespace Core { class Scene; }

namespace Render::Pass
{
    class SSRPass : public Graph::Pass
    {
    public:
        SSRPass(const std::string& name, VkExtent2D extent, Core::ResourceManager* resManager,
            Graph::RGHandle taaHist0, Graph::RGHandle taaHist1,
            Core::TextureHandle texHist0, Core::TextureHandle texHist1)
            : Pass(name), m_extent(extent), m_resourceManager(resManager),
            m_taaHist0(taaHist0), m_taaHist1(taaHist1),
            m_texHist0(texHist0), m_texHist1(texHist1)
        {
            Core::DescriptorBuilder builder(resManager->GetContext());

            m_descriptorSet = builder
                .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addLayoutBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .build(Core::MAX_FRAMES_IN_FLIGHT);

            Core::GraphicsPipelineConfig ssrConfig{};
            ssrConfig.vertexShader = "shaders/FullscreenVert.vert.spv";
            ssrConfig.fragmentShader = "shaders/ssr.frag.spv";
            ssrConfig.descriptorLayouts = { m_descriptorSet.layout };
            ssrConfig.pushConstants = {};
            ssrConfig.colorFormats = { VK_FORMAT_R16G16B16A16_SFLOAT };
            ssrConfig.depthFormat = VK_FORMAT_D32_SFLOAT;
            ssrConfig.enableDepthTest = false;
            ssrConfig.enableDepthWrite = false;
            ssrConfig.depthCompareOp = VK_COMPARE_OP_ALWAYS;
            ssrConfig.cullMode = VK_CULL_MODE_NONE;

            Core::PipelineBuilder pipelineBuilder(m_resourceManager->GetContext());
            m_pipeline = Core::PipelineFactory::CreateGraphics(&pipelineBuilder, ssrConfig);
        }

        ~SSRPass() override {
            if (m_pipeline) m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
            m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());
        }

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return false; }

        Graph::RGHandle GetSSROutput() const { return m_ssrOut; }

    private:
        Graph::RGHandle m_ssrOut;
        Graph::RGHandle m_Depth;
        Graph::RGHandle m_Normal;
        Graph::RGHandle m_Material;

        Graph::RGHandle m_taaHist0;
        Graph::RGHandle m_taaHist1;
        Core::TextureHandle m_texHist0;
        Core::TextureHandle m_texHist1;

        VkExtent2D m_extent;
        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
        Core::ResourceManager* m_resourceManager;
        Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
    };
}
#endif