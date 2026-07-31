#ifndef POINT_SHADOW_PASS_H
#define POINT_SHADOW_PASS_H
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
	class PointShadowPass : public Graph::Pass
	{
	public:
		PointShadowPass(const std::string& name,
			Core::ResourceManager* resManager,
			Core::Scene* scene) : Pass(name), m_resourceManager(resManager), m_scene(scene)
		{

			m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
				.addLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					VK_SHADER_STAGE_VERTEX_BIT)   // PointLightMatrices (6 faces * N lights)
				.addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					VK_SHADER_STAGE_VERTEX_BIT)  
				.build(Core::MAX_FRAMES_IN_FLIGHT);

			Core::GraphicsPipelineConfig shadowConfig{};
			shadowConfig.vertexShader = "shaders/pointShadow.vert.spv";
			shadowConfig.descriptorLayouts = { m_descriptorSet.layout };
			shadowConfig.pushConstants = {
				{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RenderTypes::PassPushConstants) }
			};
			shadowConfig.colorFormats = {};
			shadowConfig.depthFormat = VK_FORMAT_D32_SFLOAT;
			shadowConfig.enableDepthTest = true;
			shadowConfig.enableDepthWrite = true;
			shadowConfig.depthCompareOp = VK_COMPARE_OP_LESS;
			shadowConfig.cullMode = VK_CULL_MODE_FRONT_BIT;
			shadowConfig.depthBiasEnable = true;
			shadowConfig.depthBiasConstant = 1.25f;
			shadowConfig.depthBiasSlopeFactor = 1.75f;
			shadowConfig.viewMask = 0b00111111; //6-face multiview

			Core::PipelineBuilder pipelineBuilder(m_resourceManager->GetContext());
			m_pipeline = Core::PipelineFactory::CreateGraphics(&pipelineBuilder, shadowConfig);
		}

		~PointShadowPass() override;



		void Setup(Graph::RenderGraphBuilder& builder) override;
		void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
		bool HasSideEffect() override { return true; }

		std::vector<Graph::RGHandle> GetPointShadowMaps() const { return m_pointShadowMaps; }

	private:
		static constexpr uint32_t NUM_FACES = 6;
		static constexpr uint32_t POINT_SHADOW_SIZE = 512;

		Core::ResourceManager* m_resourceManager = nullptr;Core::Scene* m_scene = nullptr;
		std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
		Core::DescriptorBuilder::DescriptorSet     m_descriptorSet;

		std::vector<Graph::RGHandle> m_pointShadowMaps;
		uint32_t m_pointLightCount = 0;

	};


}
#endif