#ifndef RT_UNGUIDED_AO_PASS_H
#define RT_UNGUIDED_AO_PASS_H

#include <memory>
#include <vulkan/vulkan.h>
#include "../RenderGraph/Pass.h"
#include "../Core/RayTracing/ShaderBindingTable.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "../Core/GraphicsContext.h"

namespace Render::Pass
{
    class UnguidedRTAOPass : public Graph::Pass
    {
    public:
        UnguidedRTAOPass(const std::string& name, Core::ResourceManager* resManager, VkExtent2D extent);
        ~UnguidedRTAOPass() override;

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return false; }
        Graph::RGHandle GetAOMask() const { return m_aoMask; }

    private:
        Core::ResourceManager* m_resourceManager = nullptr;
        Graph::RGHandle m_aoMask;

        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
        Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
        std::unique_ptr<Core::RT::ShaderBindingTable> m_sbt;
        VkExtent2D m_Extent;
    };
}

#endif