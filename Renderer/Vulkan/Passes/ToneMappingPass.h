#ifndef TONE_MAPPING_PASS_H
#define TONE_MAPPING_PASS_H
#include "../RenderGraph/Pass.h"
#include "../Core/ResourceManager.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include "../RenderGraph/RenderGraph.h"
#include <vulkan/vulkan.h>
namespace RenderTypes {
	struct RenderContext;
}
namespace Core {
    class Scene;
}

namespace Render::Pass
{


	/// @brief Converts the High Dynamic Range (HDR) scene into Low Dynamic Range (LDR) for display.
	/// Applies physical camera exposure (calculated dynamically by the HistogramPass) and a tonemapping curve.
	///
	/// @note Inputs: "LightingOut" (ComputeShaderRead), "ExposureBuffer" (StorageBuffer Read)
	/// @note Outputs: "ToneMap_OUT" (ComputeShaderWrite)
	class ToneMappingPass  : public Graph::Pass 
	{
	public:
		ToneMappingPass(const std::string& name,VkExtent2D extent,Core::ResourceManager* resManager) : Pass(name),m_extent(extent),m_resourceManager(resManager)
		{
			Core::DescriptorBuilder builder(resManager->GetContext());
			m_descriptors = builder
				.addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) 
				.addLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)         
				.addLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)       
				.build(Core::MAX_FRAMES_IN_FLIGHT);

			Core::ComputePipelineConfig config{};
			config.computeShader = "shaders/ToneMapping.comp.spv";
			config.descriptorLayouts = { m_descriptors.layout };

			VkPushConstantRange physicalcamerasettings{};
			physicalcamerasettings.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			physicalcamerasettings.offset = 0;
			physicalcamerasettings.size = sizeof(RenderTypes::PhysicalCameraSettings);
			config.pushConstants.push_back(physicalcamerasettings);

			Core::PipelineBuilder Pipelinebuilder(m_resourceManager->GetContext());
			m_pipeline = Core::PipelineFactory::CreateCompute(&Pipelinebuilder, config);
		}

		~ToneMappingPass() override;

		void Setup(Graph::RenderGraphBuilder& builder) override;
		void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
		bool HasSideEffect() override { return true; }

		Graph::RGHandle GetToneMapOutput() const { return m_ToneMapOut; }
	private:

		VkExtent2D m_extent;
		std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
	
		Core::ResourceManager* m_resourceManager;

		Core::DescriptorBuilder::DescriptorSet m_descriptors;

		Graph::RGHandle m_hdrInput;
		Graph::RGHandle m_ToneMapOut;
		Graph::RGHandle m_exposureBuffer;
	};
}
#endif
