#ifndef SVGF_SPATIAL_PASS_H
#define SVGF_SPATIAL_PASS_H

#include "../RenderGraph/Pass.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "../Core/GraphicsContext.h"

namespace Render::Pass
{

    struct SpatialPushConstants {
        int stepSize;
        int isRTAO;
    };

    class SVGFSpatialPass : public Graph::Pass
    {
    public:
        SVGFSpatialPass(const std::string& name,
            Core::ResourceManager* resManager,
            VkExtent2D extent,
            const std::string& inputName,  
            const std::string& outputName,  
            int stepSize,
            int isRTAO, uint32_t arrayLayers = 1)
            : Pass(name), m_resourceManager(resManager), m_Extent(extent),
            m_inputName(inputName), m_outputName(outputName), 
            m_stepSize(stepSize), m_isRTAO(isRTAO),m_arrayLayers(arrayLayers)
        {
            m_descriptorSet = Core::DescriptorBuilder(m_resourceManager->GetContext())
                .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Noisy Input
                .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Depth
                .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Normal
                .addLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Material
                .addLayoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)          // Output
                .build(Core::MAX_FRAMES_IN_FLIGHT);

            Core::ComputePipelineConfig config{};
            if (m_arrayLayers > 1) {
                config.computeShader = "shaders/svgf_spatial_array.comp.spv";
            }
            else {
                config.computeShader = "shaders/svgf_spatial.comp.spv";
            }
            config.descriptorLayouts = { m_descriptorSet.layout };

            // Define the Push Constant for stepSize
            VkPushConstantRange pushConstant{};
            pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushConstant.offset = 0;
            pushConstant.size = sizeof(SpatialPushConstants);
            config.pushConstants = { pushConstant };

            m_pipeline = m_resourceManager->CreateComputePipeline(name, config);

        }

        ~SVGFSpatialPass() override {

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

        std::string m_inputName;
        std::string m_outputName;
        int m_stepSize = 1;
        int m_isRTAO = 0;
        uint32_t m_arrayLayers;


        Graph::RGHandle m_inputImage;
        Graph::RGHandle m_Depth;
        Graph::RGHandle m_Normal;
        Graph::RGHandle m_Material;
        Graph::RGHandle m_output;

 
        Core::PipelineBuilder::Pipeline* m_pipeline = nullptr;
        Core::DescriptorBuilder::DescriptorSet m_descriptorSet;
    };
}
#endif