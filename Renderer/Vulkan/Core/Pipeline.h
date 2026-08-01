#ifndef PIPELINE_H
#define PIPELINE_H
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>
#include <iostream>
#include <memory>
#include "Vulkan/Utils/Utils.h" 
#include "RenderTypes.h"

namespace Core
{
    struct GraphicsContext;

    /// @brief A Builder pattern implementation abstracting the verbose creation of Vulkan pipelines and pipeline layouts.
    class PipelineBuilder final {
    public:
        explicit PipelineBuilder(GraphicsContext& ctx) : context(ctx) {}

        /// @brief Compiles and adds a shader stage (e.g., Vertex, Fragment) to the pipeline.
        PipelineBuilder& addShaderStage(VkShaderStageFlagBits stage,
            const std::string& code,
            const char* entryPoint = "main") {

            std::vector<char> _code = Utils::readFile(code);
            shaderStages.push_back({ stage, _code, entryPoint });

            std::cout << "Added shader stage (raw enum: " << stage << ") from " << code << std::endl;

            return *this;
        }

        PipelineBuilder& setVertexInput(
            const std::optional<VkVertexInputBindingDescription>& binding,
            const std::vector<VkVertexInputAttributeDescription>& attributes)
        {
            vertexBinding = binding;
            vertexAttributes = attributes;
            return *this;
        }

        PipelineBuilder& setTopology(VkPrimitiveTopology topo) {
            topology = topo;
            return *this;
        }

        PipelineBuilder& setPolygonMode(VkPolygonMode mode) {
            polygonMode = mode;
            return *this;
        }

        PipelineBuilder& setCullMode(VkCullModeFlags mode) {
            cullMode = mode;
            return *this;
        }

        PipelineBuilder& setFrontFace(VkFrontFace face) {
            frontFace = face;
            return *this;
        }

        PipelineBuilder& enableDepthTest(bool enable = true) {
            depthTestEnable = enable;
            return *this;
        }

        PipelineBuilder& enableDepthWrite(bool enable = true) {
            depthWriteEnable = enable;
            return *this;
        }

        PipelineBuilder& setDepthCompareOp(VkCompareOp op) {
            depthCompareOp = op;
            return *this;
        }

        PipelineBuilder& enableBlending(bool enable = true) {
            blendEnable = enable;
            return *this;
        }

        PipelineBuilder& setMSAASamples(VkSampleCountFlagBits samples) {
            msaaSamples = samples;
            return *this;
        }

        PipelineBuilder& setColorFormat(VkFormat format) {
            colorFormats.clear();
            colorFormats.push_back(format);
            return *this;
        }

        PipelineBuilder& setColorAttachmentFormats(const std::vector<VkFormat>& formats) {
            colorFormats = formats;
            return *this;
        }

        PipelineBuilder& setColorAttachmentCount(uint32_t count) {
            if (count == 0) {
                colorFormats.clear();
            }
            return *this;
        }

        PipelineBuilder& setDepthFormat(VkFormat format) {
            depthFormat = format;
            return *this;
        }

        PipelineBuilder& addDescriptorSetLayout(const VkDescriptorSetLayout& layout) {
            descriptorLayouts.push_back(layout);
            return *this;
        }

        PipelineBuilder& addPushConstantRange(VkShaderStageFlags stages, uint32_t offset, uint32_t size) {
            pushConstantRanges.push_back({ stages, offset, size });
            return *this;
        }

        PipelineBuilder& setBindPoint(VkPipelineBindPoint bp) {
            this->bindPoint = bp;
            return *this;
        }

        PipelineBuilder& setDepthBias(bool enable, float constant = 0.0f, float slope = 0.0f) {
            depthBiasEnable = enable;
            depthBiasConstant = constant;
            depthBiasSlopeFactor = slope;
            return *this;
        }

        PipelineBuilder& SetViewMask(uint32_t mask)
        {
            viewMask = mask;
            return *this;
        }

        PipelineBuilder& addRaygenShader(const std::string& code, const char* entryPoint = "main") {
            addShaderStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, code, entryPoint);

            VkRayTracingShaderGroupCreateInfoKHR group{};
            group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            group.generalShader = static_cast<uint32_t>(shaderStages.size() - 1);
            group.closestHitShader = VK_SHADER_UNUSED_KHR;
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
            rtShaderGroups.push_back(group);
            return *this;
        }

        PipelineBuilder& addMissShader(const std::string& code, const char* entryPoint = "main") {
            addShaderStage(VK_SHADER_STAGE_MISS_BIT_KHR, code, entryPoint);

            VkRayTracingShaderGroupCreateInfoKHR group{};
            group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            group.generalShader = static_cast<uint32_t>(shaderStages.size() - 1);
            group.closestHitShader = VK_SHADER_UNUSED_KHR;
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
            rtShaderGroups.push_back(group);
            missGroupCount++;
            return *this;
        }

        PipelineBuilder& addHitShader(const std::string& closestHitCode, const char* entryPoint = "main") {
            addShaderStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, closestHitCode, entryPoint);

            VkRayTracingShaderGroupCreateInfoKHR group{};
            group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            group.generalShader = VK_SHADER_UNUSED_KHR;
            group.closestHitShader = static_cast<uint32_t>(shaderStages.size() - 1);
            group.anyHitShader = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
            rtShaderGroups.push_back(group);
            hitGroupCount++;
            return *this;
        }

        PipelineBuilder& setMaxRayRecursionDepth(uint32_t depth) {
            maxRayRecursionDepth = depth;
            return *this;
        }

        /// @brief An encapsulated Vulkan pipeline and its associated layout.
        struct Pipeline {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;

            uint32_t missGroupCount = 0;
            uint32_t hitGroupCount = 0;

            Pipeline() = default;

            /// @brief Safely destroys the underlying Vulkan pipeline objects.
            void destroy(VkDevice device) {
                if (pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
                if (layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, layout, nullptr);
                    layout = VK_NULL_HANDLE;
                }
            }
        };

        /// @brief Compiles the configured state into a Vulkan Pipeline and Pipeline Layout.
        Pipeline build();
    private:
        void Reset();
        GraphicsContext& context;

        struct ShaderStage {
            VkShaderStageFlagBits stage;
            std::vector<char> code;
            const char* entryPoint;
        };

        std::vector<ShaderStage> shaderStages;
        std::optional<VkVertexInputBindingDescription> vertexBinding;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;

        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        bool blendEnable = false;
        bool  depthBiasEnable = false;
        float depthBiasConstant = 0.0f;
        float depthBiasSlopeFactor = 0.0f;
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        uint32_t viewMask = 0;
        std::vector<VkFormat> colorFormats;
        std::optional<VkFormat> depthFormat;
        std::vector<VkDescriptorSetLayout> descriptorLayouts;
        std::vector<VkPushConstantRange> pushConstantRanges;
        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
        uint32_t maxRayRecursionDepth = 1;
        uint32_t missGroupCount = 0;
        uint32_t hitGroupCount = 0;

    };

    /// @brief Data-driven configuration struct containing all state needed to build a graphics pipeline.
    struct GraphicsPipelineConfig {
        std::string vertexShader;
        std::string fragmentShader;
        std::string geometryShader;

        std::optional<VkVertexInputBindingDescription> vertexBinding;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;

        std::vector<VkDescriptorSetLayout> descriptorLayouts;
        std::vector<VkPushConstantRange> pushConstants;
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        bool enableDepthTest = true;
        bool enableDepthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        bool  depthBiasEnable = false;
        float depthBiasConstant = 0.0f;
        float depthBiasSlopeFactor = 0.0f;

        uint32_t viewMask = 0;
    };

    /// @brief Data-driven configuration struct containing all state needed to build a compute pipeline.
    struct ComputePipelineConfig {
        std::string computeShader;
        std::vector<VkDescriptorSetLayout> descriptorLayouts;
        std::vector<VkPushConstantRange> pushConstants;
    };

    struct RayTracingPipelineConfig {
        std::string raygenShader;
        std::vector<std::string> missShaders;
        std::vector<std::string> hitShaders; 

        uint32_t maxRayRecursionDepth = 1;

        std::vector<VkDescriptorSetLayout> descriptorLayouts;
        std::vector<VkPushConstantRange> pushConstants;
    };

    /// @brief Factory class to orchestrate the generation of Pipelines using the builder and config structs.
    class PipelineFactory {
    public:
        /// @brief Generates a complete graphics pipeline based on the provided configuration.
        static std::unique_ptr<PipelineBuilder::Pipeline> CreateGraphics(
            PipelineBuilder* builder,
            const GraphicsPipelineConfig& config)
        {
            builder->setBindPoint(VK_PIPELINE_BIND_POINT_GRAPHICS)
                .addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, config.vertexShader);

            if (!config.fragmentShader.empty()) {
                builder->addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, config.fragmentShader);
            }
            if (!config.geometryShader.empty()) {
                builder->addShaderStage(VK_SHADER_STAGE_GEOMETRY_BIT, config.geometryShader);
            }

            builder->setVertexInput(config.vertexBinding, config.vertexAttributes)
                .setTopology(config.topology)
                .setPolygonMode(VK_POLYGON_MODE_FILL)
                .setCullMode(config.cullMode)
                .setFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
                .enableBlending(false)
                .setColorAttachmentFormats(config.colorFormats)
                .setDepthFormat(config.depthFormat)
                .enableDepthTest(config.enableDepthTest)
                .enableDepthWrite(config.enableDepthWrite)
                .setDepthCompareOp(config.depthCompareOp)
                .setDepthBias(config.depthBiasEnable, config.depthBiasConstant, config.depthBiasSlopeFactor)
                .SetViewMask(config.viewMask);


            for (const auto& layout : config.descriptorLayouts) builder->addDescriptorSetLayout(layout);
            for (const auto& pc : config.pushConstants) builder->addPushConstantRange(pc.stageFlags, pc.offset, pc.size);

            return std::make_unique<PipelineBuilder::Pipeline>(builder->build());
        }

        /// @brief Generates a complete compute pipeline based on the provided configuration.
        static std::unique_ptr<PipelineBuilder::Pipeline> CreateCompute(
            PipelineBuilder* builder,
            const ComputePipelineConfig& config)
        {
            builder->setBindPoint(VK_PIPELINE_BIND_POINT_COMPUTE)
                .addShaderStage(VK_SHADER_STAGE_COMPUTE_BIT, config.computeShader);

            for (const auto& layout : config.descriptorLayouts) builder->addDescriptorSetLayout(layout);
            for (const auto& pc : config.pushConstants) builder->addPushConstantRange(pc.stageFlags, pc.offset, pc.size);

            return std::make_unique<PipelineBuilder::Pipeline>(builder->build());
        }

        static std::unique_ptr<PipelineBuilder::Pipeline> CreateRayTracing(
            PipelineBuilder* builder,
            const RayTracingPipelineConfig& config)
        {
            builder->setBindPoint(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
                .addRaygenShader(config.raygenShader)
                .setMaxRayRecursionDepth(config.maxRayRecursionDepth);

            for (const auto& miss : config.missShaders) {
                builder->addMissShader(miss);
            }

            for (const auto& hit : config.hitShaders) {
                builder->addHitShader(hit);
            }

            for (const auto& layout : config.descriptorLayouts) builder->addDescriptorSetLayout(layout);
            for (const auto& pc : config.pushConstants) builder->addPushConstantRange(pc.stageFlags, pc.offset, pc.size);

            return std::make_unique<PipelineBuilder::Pipeline>(builder->build());
        }
    };


}

#endif // PIPELINE_H