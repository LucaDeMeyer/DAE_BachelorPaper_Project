#ifndef HISTOGRAM_PASS_H
#define HISTOGRAM_PASS_H
#include "../RenderGraph/Pass.h"
#include "../Core/ResourceManager.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include "../RenderGraph/RenderGraph.h"
#include <vulkan/vulkan.h>


namespace Render::Pass {

    struct BuildHistogramParams {
        float minLogLum = -8.0f;
        float logLumRange = 24.0f;
    };

    struct AverageHistogramParams {
        float minLogLum = -8.0f;
        float logLumRange = 24.0f;
        float timeDelta;
        float adaptationRate = 1.5f;
        uint32_t pixelCount;
        float minEV100 = -16.0f;
        float maxEV100 = 16.0f;
        float manualEV100 = 0.0f;
    };

		/// @brief Computes the scene's average luminance to drive Automatic Exposure (Eye Adaptation).
       /// Uses a two-step Compute Shader pipeline: 
       /// 1. Builds a luminance histogram from the HDR input.
       /// 2. Averages the histogram over time to calculate the final Exposure buffer.
       ///
       /// @note Inputs: "LightingOut" (ComputeShaderRead)
       /// @note Outputs: Temporary Virtual Buffers for Histogram and Exposure data.
    class HistogramPass : public Graph::Pass {
    public:
       
        HistogramPass(const std::string& name, Core::ResourceManager* resManager);
        ~HistogramPass() override;

        void Setup(Graph::RenderGraphBuilder& builder) override;
        void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override;

        bool HasSideEffect() override { return true; }
    private:
        Core::ResourceManager* m_resourceManager;

        Graph::RGHandle m_hdrInput;
        Graph::RGHandle m_histogramBuffer;
        Graph::RGHandle m_exposureBuffer; 


        Core::DescriptorBuilder::DescriptorSet m_buildBlueprint;
        Core::DescriptorBuilder::DescriptorSet m_averageBlueprint;

        std::vector<Core::DescriptorBuilder::DescriptorSet> m_buildDescriptors;
        std::vector<Core::DescriptorBuilder::DescriptorSet> m_averageDescriptors;

        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_buildPipeline;
        std::unique_ptr<Core::PipelineBuilder::Pipeline> m_averagePipeline;
    };

}
#endif
