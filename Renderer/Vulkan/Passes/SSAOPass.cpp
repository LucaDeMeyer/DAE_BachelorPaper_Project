#include "SSAOPass.h"
#include "vulkan/Core/GraphicsContext.h"
#include <random>

float lerp(float a, float b, float f) { return a + f * (b - a); }

Render::Pass::SSAOPass::SSAOPass(const std::string& name, VkExtent2D extent, Core::ResourceManager* resManager, const Core::Scene* scene)
    : Graph::Pass(name), m_extent(extent),m_resourceManager(resManager), m_scene(scene)
{
    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;

    for (unsigned int i = 0; i < 64; ++i)
    {
        glm::vec3 sample(
            randomFloats(generator) * 2.0 - 1.0, // X: -1.0 to 1.0
            randomFloats(generator) * 2.0 - 1.0, // Y: -1.0 to 1.0
            randomFloats(generator)              // Z:  0.0 to 1.0 (Hemisphere)
        );
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);

        // Distribute more samples closer to the origin
        float scale = (float)i / 64.0f;
        scale = lerp(0.1f, 1.0f, scale * scale);
        sample *= scale;

        m_ssaoKernel.push_back(glm::vec4(sample, 0.0f)); // Pad to vec4 for Vulkan alignment
    }

    size_t bufferSize = m_ssaoKernel.size() * sizeof(glm::vec4);

    Core::BufferDesc kerneldisc{};
    kerneldisc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    kerneldisc.size = bufferSize;
    kerneldisc.name = "SAAOKernelUBO";
    kerneldisc.vmaUsage = VMA_MEMORY_USAGE_AUTO;
    kerneldisc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	m_ssaoKernelBuffer = m_resourceManager->CreateBuffer(kerneldisc);

    Core::BufferBuilder::Buffer* kernelbuf = m_resourceManager->GetBuffer(m_ssaoKernelBuffer);
    if (kernelbuf->mapped != nullptr) {
        memcpy(kernelbuf->mapped, m_ssaoKernel.data(), bufferSize);
    }

    for (unsigned int i = 0; i < 16; i++)
    {
        glm::vec3 noise(
            randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator) * 2.0 - 1.0,
            0.0f // Z is exactly zero so it rotates around the Z axis
        );
        m_ssaoNoise.push_back(glm::vec4(noise, 0.0f));
    }

    Core::TextureDesc ssaoNoiseDesc{};
    ssaoNoiseDesc.extent = { 4,4,1 };
    ssaoNoiseDesc.arrayLayers = 1;
    ssaoNoiseDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ssaoNoiseDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ssaoNoiseDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	m_ssaoNoiseTex =  m_resourceManager->CreateTexture(ssaoNoiseDesc);

    size_t noiseSize = m_ssaoNoise.size() * sizeof(glm::vec4);
    Core::BufferDesc stagingDesc{};
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; // Source of a transfer!
    stagingDesc.size = noiseSize;
    stagingDesc.name = "SSAONoiseStaging";
    stagingDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO;
    stagingDesc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    Core::BufferHandle stagingHandle = m_resourceManager->CreateBuffer(stagingDesc);

    Core::BufferBuilder::Buffer* stagingBuf = m_resourceManager->GetBuffer(stagingHandle);
    if (stagingBuf->mapped != nullptr) {
        memcpy(stagingBuf->mapped, m_ssaoNoise.data(), noiseSize);
    }
    auto& ctx = resManager->GetContext();
    VkCommandBuffer cmd = Utils::beginSingleTimeCommands(ctx.getDevice(), ctx.getCommandPool());

   
    Core::ImageBuilder::Image* noiseImg = m_resourceManager->GetTexture(m_ssaoNoiseTex);

    Utils::TransitionImageLayout(
        cmd,
        noiseImg,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,                                  
        VK_ACCESS_2_TRANSFER_WRITE_BIT,      
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,   
        VK_IMAGE_ASPECT_COLOR_BIT,
        1, 1
    );

    Utils::CopyBufferToImage(cmd, stagingBuf->buffer, noiseImg->image, 4, 4);

    Utils::TransitionImageLayout(
        cmd,
        noiseImg,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,         
        VK_ACCESS_2_SHADER_READ_BIT,            
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,       
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 
        VK_IMAGE_ASPECT_COLOR_BIT,
        1, 1
    );

    Utils::endSingleTimeCommands(ctx.getDevice(), ctx.getCommandPool(), ctx.getGraphicsQueue(), cmd);

    m_resourceManager->DestroyBuffer(stagingHandle);

    Core::DescriptorBuilder builder(resManager->GetContext());

    m_ssaoDescriptorSet = builder
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT) // Camera
        .addLayoutBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT) // SSAO Kernel (UBO)
        .addLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // Depth
        .addLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // Normal
        .addLayoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // Noise Tex
        .build(Core::MAX_FRAMES_IN_FLIGHT);

    m_ssaoBlurDescriptorSet = builder
        .addLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)// raw ssao output
        .build(Core::MAX_FRAMES_IN_FLIGHT);
    
    Core::GraphicsPipelineConfig ssaoConfig{};
    ssaoConfig.vertexShader = "shaders/FullscreenVert.vert.spv"; 
    ssaoConfig.fragmentShader = "shaders/ssao.frag.spv";
    ssaoConfig.descriptorLayouts = { m_ssaoDescriptorSet.layout };
    ssaoConfig.pushConstants = { {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec2)} };
    ssaoConfig.colorFormats = { VK_FORMAT_R8_UNORM };
    ssaoConfig.enableDepthTest = false;
    ssaoConfig.enableDepthWrite = false;
    ssaoConfig.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    ssaoConfig.cullMode = VK_CULL_MODE_NONE;

    Core::GraphicsPipelineConfig blurConfig{};
    blurConfig.vertexShader = "shaders/FullscreenVert.vert.spv"; 
    blurConfig.fragmentShader = "shaders/ssaoBlur.frag.spv";
    blurConfig.descriptorLayouts = { m_ssaoBlurDescriptorSet.layout };
    blurConfig.colorFormats = { VK_FORMAT_R8_UNORM };
    blurConfig.enableDepthTest = false;
    blurConfig.enableDepthWrite = false;
    blurConfig.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    blurConfig.cullMode = VK_CULL_MODE_NONE;

    Core::PipelineBuilder Pipelinebuilder(m_resourceManager->GetContext());
    m_ssaoPipeline = Core::PipelineFactory::CreateGraphics(&Pipelinebuilder, ssaoConfig);
    m_ssaoBlurPipeline = Core::PipelineFactory::CreateGraphics(&Pipelinebuilder, blurConfig);

}

Render::Pass::SSAOPass::~SSAOPass()
{
    if (m_resourceManager)
    {
        m_ssaoBlurPipeline->destroy(m_resourceManager->GetContext().getDevice());
        m_ssaoPipeline->destroy(m_resourceManager->GetContext().getDevice());
        m_ssaoBlurDescriptorSet.destroy(m_resourceManager->GetContext().getDevice());
        m_ssaoDescriptorSet.destroy(m_resourceManager->GetContext().getDevice());
    }
}
void Render::Pass::SSAOPass::Setup(Graph::RenderGraphBuilder& builder)
{
    Core::TextureDesc rawDesc{};
    rawDesc.name = "SSAO_Raw";
    rawDesc.extent = { m_extent.width, m_extent.height, 1 };
    rawDesc.format = VK_FORMAT_R8_UNORM;
    rawDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    rawDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    rawDesc.mipLevels = 1;
    rawDesc.arrayLayers = 1;

    m_ssaoRawMap = builder.CreateTexture(rawDesc);
    builder.AddDependency(m_ssaoRawMap, Graph::AccessType::ColorAttachmentWrite);

    Core::TextureDesc blurDesc = rawDesc;
    blurDesc.name = "SSAO_Blur";

    m_ssaoFinal = builder.CreateTexture(blurDesc);
    builder.AddDependency(m_ssaoFinal, Graph::AccessType::ColorAttachmentWrite);

    m_Depth = builder.FindResource("SceneDepth");
    builder.AddDependency(m_Depth, Graph::AccessType::DepthShaderRead);

    m_Normal = builder.FindResource("GBuffer_Normal");
    builder.AddDependency(m_Normal, Graph::AccessType::ShaderRead);


}

void Render::Pass::SSAOPass::Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph)
{
    Core::ImageBuilder::Image* depthImage = context.resourceManager->GetTexture(graph.GetResource(m_Depth).physicalTexture);
    Core::ImageBuilder::Image* normalImage = context.resourceManager->GetTexture(graph.GetResource(m_Normal).physicalTexture);

    Core::ImageBuilder::Image* rawSSAO = context.resourceManager->GetTexture(graph.GetResource(m_ssaoRawMap).physicalTexture);
    Core::ImageBuilder::Image* blurSSAO = context.resourceManager->GetTexture(graph.GetResource(m_ssaoFinal).physicalTexture);
    Core::ImageBuilder::Image* noiseTex = context.resourceManager->GetTexture(m_ssaoNoiseTex);

   
    Core::DescriptorWriter rawWriter;
    rawWriter.writeBuffer(0, context.resourceManager->GetBuffer(m_resourceManager->GetCameraUBO()[m_resourceManager->GetFrameIndex()])->buffer, 0, sizeof(RenderTypes::CameraUBO), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .writeBuffer(1, m_resourceManager->GetBuffer(m_ssaoKernelBuffer)->buffer, 0, m_ssaoKernel.size() * sizeof(glm::vec4), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .writeImage(2, depthImage->view, context.resourceManager->GetPointSampler(), VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(3, normalImage->view, context.resourceManager->GetLinearSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .writeImage(4, noiseTex->view, context.resourceManager->GetPointRepeatSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .overwrite(m_ssaoDescriptorSet.sets[context.currentFrameIndex], context.resourceManager->GetContext().getDevice());

    VkRenderingAttachmentInfo colorAttachment{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = rawSSAO->view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = {.color = { 1.0f, 1.0f, 1.0f, 1.0f } } };
    VkRenderingInfo renderInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = { {0, 0}, m_extent }, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachment };

    glm::vec2 screenres{};
    screenres.x = static_cast<float>( m_extent.width);
    screenres.y = static_cast<float>(m_extent.height);

    vkCmdBeginRendering(context.cmd, &renderInfo);
    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline->pipeline);
    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline->layout, 0, 1, &m_ssaoDescriptorSet.sets[context.currentFrameIndex], 0, nullptr);

    vkCmdPushConstants(
        context.cmd, m_ssaoPipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(glm::vec2), &screenres
    );

    vkCmdDraw(context.cmd, 3, 1, 0, 0);
    vkCmdEndRendering(context.cmd);

    Utils::TransitionImageLayout(
        context.cmd,
        rawSSAO->image,                         
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1, 1
    );
    VkRenderingAttachmentInfo blurAttachment = colorAttachment;
    blurAttachment.imageView = blurSSAO->view;
    VkRenderingInfo blurRenderInfo = renderInfo;
    blurRenderInfo.pColorAttachments = &blurAttachment;

    Core::DescriptorWriter blurWriter;
    blurWriter.writeImage(0, rawSSAO->view, context.resourceManager->GetPointSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .overwrite(m_ssaoBlurDescriptorSet.sets[context.currentFrameIndex],
            context.resourceManager->GetContext().getDevice());

    vkCmdBeginRendering(context.cmd, &blurRenderInfo);
    vkCmdBindPipeline(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipeline->pipeline);
    vkCmdBindDescriptorSets(context.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipeline->layout,
        0, 1, &m_ssaoBlurDescriptorSet.sets[context.currentFrameIndex], 0, nullptr);
    vkCmdDraw(context.cmd, 3, 1, 0, 0);



    vkCmdEndRendering(context.cmd);

}
