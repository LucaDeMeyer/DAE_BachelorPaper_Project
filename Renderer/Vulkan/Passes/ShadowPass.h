#ifndef SHADOW_PASS_H
#define SHADOW_PASS_H
#include "../RenderGraph/Pass.h"
#include "../Core/ResourceManager.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include "../RenderGraph/RenderGraph.h"
#include <vulkan/vulkan.h>

namespace Core
{
	class Scene;
}


namespace Render::Pass
{
    /// @brief Renders the scene from the directional light's perspective to generate Cascaded Shadow Maps (CSM).
    /// Utilizes Vulkan layered rendering and Instancing to draw all 4 cascades in a single draw pass.
    ///
    /// @note Inputs: None (Uses Global Vertex/Index buffers & Scene descriptor)
    /// @note Outputs: "ShadowMap" (DepthWrite - 4-Layer 2D Array Texture)
	class ShadowPass : public Graph::Pass
	{
	public:
        ShadowPass(const std::string& name,
            Core::ResourceManager* resManager,
            const Core::Scene* scene) : Pass(name),m_resourceManager(resManager),m_scene(scene)
	{
            m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
                .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT)
                .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT)
                .build(Core::MAX_FRAMES_IN_FLIGHT);

            Core::GraphicsPipelineConfig shadowConfig{};
            shadowConfig.vertexShader = "shaders/shadow.vert.spv";
            shadowConfig.descriptorLayouts = { m_descriptorSet.layout };
            shadowConfig.pushConstants = {
                { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RenderTypes::PassPushConstants) }
            };
            shadowConfig.colorFormats = {}; 
            shadowConfig.depthFormat = VK_FORMAT_D32_SFLOAT;
            shadowConfig.enableDepthTest = true;
            shadowConfig.enableDepthWrite = true;
            shadowConfig.depthCompareOp = VK_COMPARE_OP_LESS;
            // We cull FRONT faces instead of BACK faces. 
			// This pushes the shadow acne (self-shadowing artifacts) deep inside the solid 
			// geometry where the camera can't see it, effectively curing Peter-Panning!
            shadowConfig.cullMode = VK_CULL_MODE_FRONT_BIT;
            // Add a slight slope bias to catch grazing angles on the geometry
            shadowConfig.depthBiasEnable = true;
            shadowConfig.depthBiasConstant = 1.25f;
            shadowConfig.depthBiasSlopeFactor = 1.75f;
            Core::PipelineBuilder pipelineBuilder(m_resourceManager->GetContext());
            m_pipeline = Core::PipelineFactory::CreateGraphics(&pipelineBuilder, shadowConfig);
	}
	
        ~ShadowPass() override;



        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return true; }

      Graph::RGHandle GetShadowMap() { return m_shadowMap; }
	private:
        static constexpr uint32_t NUM_CASCADES = 4;
        static constexpr uint32_t SHADOW_MAP_SIZE = 2048;

        Core::ResourceManager* m_resourceManager = nullptr;
       const Core::Scene* m_scene = nullptr;

       Graph::RGHandle m_shadowMap;

        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
        Core::DescriptorBuilder::DescriptorSet     m_descriptorSet;
	};
}

#endif
