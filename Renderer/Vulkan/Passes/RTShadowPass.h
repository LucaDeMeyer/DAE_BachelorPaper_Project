#ifndef RT_SHADOWS_PASS_H
#define RT_SHADOWS_PASS_H

#include <memory>
#include <vulkan/vulkan.h>
#include "../RenderGraph/Pass.h"
#include "../Core/RayTracing/ShaderBindingTable.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "../Core/GraphicsContext.h"


namespace Render::Pass
{
	class RTShadowPass : public Graph::Pass
       {
       public:
           RTShadowPass(const std::string& name, Core::ResourceManager* resManager,VkExtent2D extent)
               : Pass(name), m_resourceManager(resManager),m_Extent(extent)
           {
               m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
                   .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR) // Depth
                   .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR) // Normal
                   .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)          // Shadow Mask
                   .build(Core::MAX_FRAMES_IN_FLIGHT);
               Core::RayTracingPipelineConfig rtConfig{};
               rtConfig.raygenShader = "shaders/RTShadow.rgen.spv";
               rtConfig.missShaders = { "shaders/RTShadow.rmiss.spv" };
               rtConfig.hitShaders = { "shaders/RTShadow.rchit.spv" };
               rtConfig.maxRayRecursionDepth = 1;
               rtConfig.descriptorLayouts = {
                   m_resourceManager->GetGlobalDescriptorSet().layout,
                   m_resourceManager->GetRTDescriptorSet().layout,
                   m_descriptorSet.layout
               };
               Core::PipelineBuilder pipelineBuilder(m_resourceManager->GetContext());
               m_pipeline = Core::PipelineFactory::CreateRayTracing(&pipelineBuilder, rtConfig);
           
               m_sbt = std::make_unique<Core::RT::ShaderBindingTable>(
                   m_resourceManager->GetContext(),
                   *m_resourceManager,
                   m_pipeline->pipeline,
                   m_pipeline->missGroupCount,
                   m_pipeline->hitGroupCount
               );
           }
           ~RTShadowPass() override {
               if (m_pipeline) m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
               m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());
               if (m_sbt) m_sbt->Shutdown();
           }
           void Setup(Graph::RenderGraphBuilder& builder) override;
           void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
           bool HasSideEffect() override { return false; }
           Graph::RGHandle GetShadowMask() const { return m_shadowMask; }

       private:

           Core::ResourceManager* m_resourceManager = nullptr;
           Graph::RGHandle m_shadowMask;
			Graph::RGHandle m_Depth;
          Graph::RGHandle m_Normal;

           std::unique_ptr<Core::PipelineBuilder::Pipeline> m_pipeline;
           Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
           std::unique_ptr<Core::RT::ShaderBindingTable> m_sbt;
           VkExtent2D m_Extent;
       };
   }

#endif