#ifndef SVGF_TEMPORAL_PASS_H
#define SVGF_TEMPORAL_PASS_H

#include "../RenderGraph/Pass.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "../Core/GraphicsContext.h"

namespace Render::Pass
{
    class SVGFTemporalPass : public Graph::Pass
    {
    public:
        SVGFTemporalPass(const std::string& name,
            Core::ResourceManager* resManager,
            VkExtent2D extent,
            Graph::RGHandle history0,
            Graph::RGHandle history1,
            Core::TextureHandle physHistory0,
            Core::TextureHandle physHistory1,
            const std::string& inputName,  
            const std::string& outputName, 
            int isRTAO, uint32_t arrayLayers = 1)
            : Pass(name), m_resourceManager(resManager), m_Extent(extent),
            m_history0(history0), m_history1(history1),
            m_physHistory0(physHistory0), m_physHistory1(physHistory1),m_inputName(inputName),m_outputName(outputName),m_isRTAO(isRTAO), m_arrayLayers(arrayLayers)
        {
            m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
                .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Noisy RTR
                .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Depth
                .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Normal
                .addLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Material (Roughness)
                .addLayoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // History Read (Previous Frame)
                .addLayoutBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)          // History Write (Persistent)
                .addLayoutBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)          // Graph Output (Transient)
                .build(Core::MAX_FRAMES_IN_FLIGHT);

            Core::ComputePipelineConfig config{};
            if (m_arrayLayers > 1) {
                config.computeShader = "shaders/svgf_temporal_array.comp.spv";
            }
            else {
                config.computeShader = "shaders/svgf_temporal.comp.spv";
            }

            config.descriptorLayouts = {
                m_resourceManager->GetGlobalDescriptorSet().layout,
                m_descriptorSet.layout
            };

            VkPushConstantRange pc{};
            pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pc.offset = 0;
            pc.size = sizeof(int);
            config.pushConstants = { pc };

            m_pipeline = m_resourceManager->CreateComputePipeline(name, config);
        }

        ~SVGFTemporalPass() override {

            if (m_pipeline) {
                m_pipeline->destroy(m_resourceManager->GetContext().getDevice());
            }
            m_descriptorSet.destroy(m_resourceManager->GetContext().getDevice());
        }

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;
        bool HasSideEffect() override { return false; }

        Graph::RGHandle GetOutput() const { return m_output; }

    private:
        Core::ResourceManager* m_resourceManager = nullptr;
        VkExtent2D m_Extent;
        uint32_t m_arrayLayers;
    
        Graph::RGHandle m_history0;
        Graph::RGHandle m_history1;
        Graph::RGHandle m_Depth;
        Graph::RGHandle m_Normal;
        Graph::RGHandle m_Material;

        Graph::RGHandle m_output;

        Core::TextureHandle m_physHistory0;
        Core::TextureHandle m_physHistory1;

        std::string m_inputName;
        std::string m_outputName;
        int m_isRTAO;
        Graph::RGHandle m_inputHandle;

        Core::PipelineBuilder::Pipeline* m_pipeline = nullptr;
        Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
    };
}
#endif