#ifndef DEPTH_PRE_PASS_H
#define DEPTH_PRE_PASS_H
#include "../RenderGraph/Pass.h"
#include "../Core/ResourceManager.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include "../RenderGraph/RenderGraph.h"
#include <vulkan/vulkan.h>

namespace Core {
    class Scene;
}



namespace Render::Pass
{
    /// @brief Renders the scene geometry without color outputs to pre-populate the Depth Buffer.
    /// This drastically improves the performance of the subsequent Geometry Pass by enabling Early-Z testing,
    /// ensuring expensive G-Buffer fragment shaders only run on visible pixels.
    ///
    /// @note Inputs: None (Uses Global Vertex/Index buffers & Scene descriptor)
    /// @note Outputs: "SceneDepth" (DepthWrite)
    class DepthPrePass : public Graph::Pass
    {
    public:
        DepthPrePass(const std::string& name,
            VkExtent2D extent,
            Core::ResourceManager* resManager,
            const Core::Scene* scene);
        ~DepthPrePass() override;

        void Setup(Graph::RenderGraphBuilder& builder) override;

        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;

    	bool HasSideEffect() override { return true; }

    private:
        Render::Graph::RGHandle m_DepthTexture;

        VkExtent2D m_extent;
       std::unique_ptr< Core::PipelineBuilder::Pipeline> m_pipeline;
        const Core::Scene* m_scene;
        Core::ResourceManager* m_resourceManager;
    };
}
#endif