#ifndef UNGUIDED_SHADOW_PASS_H
#define UNGUIDED_SHADOW_PASS_H

#include <memory>
#include <vulkan/vulkan.h>
#include "../RenderGraph/Pass.h"
#include "../Core/RayTracing/ShaderBindingTable.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "../Core/GraphicsContext.h"

namespace Render::Pass
{
    class UnguidedShadowPass : public Graph::Pass
    {
    public:
        UnguidedShadowPass(const std::string& name, Core::ResourceManager* resManager, VkExtent2D extent);
        ~UnguidedShadowPass() override;

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return false; }
        Graph::RGHandle GetShadowMask() const { return m_shadowMask; }

    private:
        Core::ResourceManager* m_resourceManager = nullptr;
        Graph::RGHandle m_shadowMask;

        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
        Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
        std::unique_ptr<Core::RT::ShaderBindingTable> m_sbt;
        VkExtent2D m_Extent;
    };
}

#endif