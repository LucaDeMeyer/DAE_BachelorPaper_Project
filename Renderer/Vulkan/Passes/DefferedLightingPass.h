#ifndef DEFFERED_LIGHTING_H
#define DEFFERED_LIGHTING_H
#include "../RenderGraph/Pass.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include <vulkan/vulkan.h>
#include "Vulkan/Core/ResourceManager.h"

namespace Core
{
	class Scene;
}

namespace Render::Pass
{
	/// @brief A fullscreen pass that reads the G-Buffer and applies Physically Based Rendering (PBR) lighting.
	/// Combines directional light shadows (CSM) and Image-Based Lighting (IBL) to calculate the final HDR pixel colors.
	///
	/// @note Inputs: "GBuffer_Albedo", "GBuffer_Normal", "GBuffer_Material", "SceneDepth", "ShadowMap", and IBL Cubemaps.
	/// @note Outputs: "LightingOut" (ColorAttachmentWrite)
	class DefferdLightingPass : public Graph::Pass
	{
	public:

		DefferdLightingPass(const std::string& name,
			VkExtent2D extent, Core::ResourceManager* resManager,
			Core::Scene* scene,
			Graph::RGHandle envMap, Graph::RGHandle irrMap, Graph::RGHandle prefiltermap, Graph::RGHandle brdflutmap,
			Core::TextureHandle envTex, Core::TextureHandle irrTex, Core::TextureHandle prefilterTex, Core::TextureHandle brdfTex)
			: Pass(name), m_extent(extent), m_resourceManager(resManager), m_scene(scene),
			m_envMap(envMap), m_irrMap(irrMap),m_prefilterMap(prefiltermap),m_brdflut(brdflutmap) ,m_envTex(envTex), m_irrTex(irrTex),m_prefilterTex(prefilterTex),m_brdfTex(brdfTex)
		{
			Core::DescriptorBuilder builder(resManager->GetContext());

			m_LightingDescriptors = builder
				.addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) 
				.addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) 
				.addLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) 
				.addLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) 
				.addLayoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.addLayoutBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.addLayoutBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.addLayoutBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.addLayoutBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.addLayoutBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.addLayoutBinding(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.addLayoutBinding(11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
					Core::MAX_POINT_LIGHTS, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT)
				.build(Core::MAX_FRAMES_IN_FLIGHT);


			Core::GraphicsPipelineConfig lightingConfig{};
			lightingConfig.vertexShader = "shaders/FullscreenVert.vert.spv";
			lightingConfig.fragmentShader = "shaders/lighting.frag.spv";
			lightingConfig.descriptorLayouts = {
				resManager->GetGlobalDescriptorSet().layout,
				m_LightingDescriptors.layout
			};
			lightingConfig.pushConstants = { {VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(RenderTypes::LightingDebugPushConstant)} };
			lightingConfig.colorFormats = {
			   VK_FORMAT_R32G32B32A32_SFLOAT
			};
			lightingConfig.depthFormat = VK_FORMAT_D32_SFLOAT;
			// We disable depth testing because we are rendering a single 2D fullscreen triangle
			// to evaluate the lighting math for every pixel on the screen.
			lightingConfig.enableDepthTest = false;
			lightingConfig.enableDepthWrite = false;
			lightingConfig.depthCompareOp = VK_COMPARE_OP_ALWAYS;
			lightingConfig.cullMode = VK_CULL_MODE_NONE;

			Core::PipelineBuilder Pipelinebuilder(m_resourceManager->GetContext());

			m_pipeline = Core::PipelineFactory::CreateGraphics(&Pipelinebuilder, lightingConfig);
		}
		~DefferdLightingPass() override;

		void Setup(Graph::RenderGraphBuilder& builder) override;
		void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
		bool HasSideEffect() override { return true; }

		Graph::RGHandle GetLightingOut() const { return m_LightingOut; }

	private: 
		Graph::RGHandle m_LightingOut;
		Render::Graph::RGHandle m_Albedo;
		Render::Graph::RGHandle m_Normal;
		Render::Graph::RGHandle m_Material;
		Render::Graph::RGHandle m_Depth;
		Render::Graph::RGHandle m_shadowMap;
		Render::Graph::RGHandle m_ssaoMap;
		std::vector<Render::Graph::RGHandle> m_pointShadowMaps;

		Render::Graph::RGHandle m_envMap;
		Render::Graph::RGHandle m_irrMap;
		Render::Graph::RGHandle m_prefilterMap;
		Render::Graph::RGHandle m_brdflut;


		Core::TextureHandle m_envTex;
		Core::TextureHandle m_irrTex;
		Core::TextureHandle m_prefilterTex;
		Core::TextureHandle m_brdfTex;

		VkExtent2D m_extent;
		std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
		Core::Scene* m_scene;
		Core::ResourceManager* m_resourceManager;

		Core::DescriptorBuilder::DescriptorSet m_LightingDescriptors;
	};

}
#endif	