#include "Pipeline.h"
#include "GraphicsContext.h"
#include <stdexcept>
#include <iostream>

Core::PipelineBuilder::Pipeline Core::PipelineBuilder::build() {
    Pipeline result;
    VkDevice device = context.getDevice();

   
    std::vector<VkShaderModule> localShaderModules;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos;
    shaderStageInfos.reserve(shaderStages.size());

    for (const auto& stage : shaderStages) {
        if (stage.code.empty()) {
            throw std::runtime_error("Shader code is empty - file may not have loaded correctly");
        }

        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = stage.code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(stage.code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }

        localShaderModules.push_back(shaderModule);

        VkPipelineShaderStageCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderInfo.stage = stage.stage;
        shaderInfo.module = shaderModule;
        shaderInfo.pName = stage.entryPoint;

        shaderStageInfos.push_back(shaderInfo);
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorLayouts.size());
    layoutInfo.pSetLayouts = descriptorLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &result.layout) != VK_SUCCESS) {
        throw std::runtime_error("FATAL: Failed to create Pipeline Layout. Check descriptor set layouts.");
    }

   
    if (bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
        // === COMPUTE PIPELINE ===
        if (shaderStageInfos.size() != 1 || shaderStages[0].stage != VK_SHADER_STAGE_COMPUTE_BIT) {
            throw std::runtime_error("Compute pipeline must have exactly one compute shader stage.");
        }

        VkComputePipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        createInfo.stage = shaderStageInfos[0];
        createInfo.layout = result.layout;
        
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &result.pipeline) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pipeline!");
        }
    }
    else {
        // === GRAPHICS PIPELINE ===
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (vertexBinding.has_value()) {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &vertexBinding.value();
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
            vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();
        }
        else {
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport / scissor dynamic
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = polygonMode;
        rasterizer.cullMode = cullMode;
        rasterizer.frontFace = frontFace;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.lineWidth = 1.0f;
        
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = msaaSamples;
        multisampling.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = depthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = depthCompareOp;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
        if (!colorFormats.empty()) {
            colorBlendAttachments.resize(colorFormats.size());
            for (auto& attachment : colorBlendAttachments) {
                attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                attachment.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;

                if (blendEnable) {
                    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    attachment.colorBlendOp = VK_BLEND_OP_ADD;
                    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
                }
            }
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data();

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.empty() ? nullptr : colorFormats.data();
        renderingInfo.viewMask = viewMask;

        if (depthFormat.has_value()) {
            if (depthFormat.value() == VK_FORMAT_UNDEFINED) {
                throw std::runtime_error("Invalid depth format for pipeline");
            }
            renderingInfo.depthAttachmentFormat = depthFormat.value();
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo; 
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStageInfos.size());
        pipelineInfo.pStages = shaderStageInfos.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = result.layout;
       
        std::cout << "Color attachments: " << colorFormats.size() << std::endl;
        std::cout << "Descriptor layouts: " << descriptorLayouts.size() << std::endl;
        std::cout << "Push constants: " << pushConstantRanges.size() << std::endl;
        std::cout << "Pipeline Layout Handle: " << (void*)result.layout << std::endl;
        std::cout << "Shader Stage Count: " << shaderStageInfos.size() << std::endl;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result.pipeline) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create graphics pipeline!");
        }
    }

    for (VkShaderModule module : localShaderModules) {
        vkDestroyShaderModule(device, module, nullptr);
    }


    Reset();

    return result;
}

void Core::PipelineBuilder::Reset()
{
    shaderStages.clear();
    vertexBinding.reset();
    vertexAttributes.clear();
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    polygonMode = VK_POLYGON_MODE_FILL;
    cullMode = VK_CULL_MODE_BACK_BIT;
    frontFace = VK_FRONT_FACE_CLOCKWISE; 
    depthTestEnable = true;
    depthWriteEnable = true;
    depthCompareOp = VK_COMPARE_OP_LESS;
    blendEnable = false;
    msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    colorFormats.clear();
    depthFormat.reset();
    descriptorLayouts.clear();
    pushConstantRanges.clear();
}