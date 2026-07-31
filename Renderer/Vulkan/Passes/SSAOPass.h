#ifndef SSAO_PASS_H
#define SSAO_PASS_H
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
	class SSAOPass : public Graph::Pass
	{
	public:
		SSAOPass(const std::string& name, VkExtent2D extent, Core::ResourceManager* resManager, const Core::Scene* scene);


		~SSAOPass() override;


		void Setup(Graph::RenderGraphBuilder& builder) override;
		void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
		bool HasSideEffect() override { return true; }

		Graph::RGHandle GetSSAOMap() const { return m_ssaoFinal; }

	private:
		Core::ResourceManager* m_resourceManager = nullptr;
		const Core::Scene* m_scene = nullptr;
		VkExtent2D m_extent;

		Graph::RGHandle m_ssaoRawMap; 
		Graph::RGHandle m_ssaoFinal;

		Render::Graph::RGHandle m_Depth;
		Render::Graph::RGHandle m_Normal;

		Core::TextureHandle m_ssaoNoiseTex;
		std::unique_ptr<Core::PipelineBuilder::Pipeline> m_ssaoPipeline;
		std::unique_ptr<Core::PipelineBuilder::Pipeline> m_ssaoBlurPipeline;

		Core::DescriptorBuilder::DescriptorSet m_ssaoDescriptorSet;    
		Core::DescriptorBuilder::DescriptorSet m_ssaoBlurDescriptorSet;

		std::vector<glm::vec4> m_ssaoKernel;
		std::vector<glm::vec4> m_ssaoNoise;

		Core::BufferHandle m_ssaoKernelBuffer;
	};

}

#endif
