#include "Renderer.h"
#include "Vulkan/Utils/Utils.h"
#include <stdexcept>
#include "imgui.h"
#include "UI/UILayer.h"
#include "Vulkan/Core/Allocator.h"
#include "Vulkan/Passes/BlitToSwapchainPass.h"
#include "Vulkan/Passes/ClearPass.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/Passes/DefferedLightingPass.h"
#include "Vulkan/Passes/DepthPrePass.h"
#include "Vulkan/Passes/GeometryPass.h"
#include "Vulkan/Passes/HistogramPass.h"
#include "Vulkan/Passes/ShadowPass.h"
#include "Vulkan/Passes/ToneMappingPass.h"
#include "Vulkan/Passes/PointShadowPass.h"
#include "Vulkan/Passes/SSAOPass.h"
#include "Vulkan/Passes/TAAPass.h"

/*TODO: 
 *
    X add comments as per feeback        

	X make graphics context a struct makes more sense since it just holds data 

	X revisit the auto exposure -> think we can improve on that -> camera settings being hardcoded atm overal readability could be better

	X refactor the IBL baker -> 1 big bake function is kind of hard to debug even though it works rn

	X move all of the render code to its seperate file from main / maybe add an app class like I have in grad work project
	
    X Add comments and documentation 

	full refactor of renderer again -> pull out UI

---------------------------------------------------------------
TODO: STRETCH GOALS

		soft shadows? => for directional light we add blocker search we are already doing a 3x3 pcf 
	X	SSAO
		parallax mapping => sponza doesnt have a height map :/
		lighting via compute
		Multithreading -> model loading, maybe graph building and ibl baking? => resource manager is not thread safe atm, will need mutexes around textures and buffers, and command pools per thread 
	X   IMGUI
	X	Point light shadows
		Texture compression
	
*/      
	
Renderer::Renderer(GLFWwindow* window) : window(window) {
    initVulkan();
    m_UiLayer = std::make_unique<UI::UILayer>();
}

void Renderer::InitUI(GLFWwindow* window)
{
    m_UiLayer->Init(*context.get(), window, swapchain->format);
}

Renderer::~Renderer() {
   
}

void Renderer::waitIdle() {
    vkDeviceWaitIdle(context->getDevice());
}

void Renderer::initVulkan() {
    Core::GraphicsContextBuilder ctxBuilder;
    context = ctxBuilder.setWindow(window)
        .setAppName("Vulkan Engine")
        .SetApiVersion(VK_API_VERSION_1_3)
        .addValidationLayer("VK_LAYER_KHRONOS_validation")
        .addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .addDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)
        .addDeviceExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)
        .addDeviceExtension(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)
		.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
		.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
        .addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        .build();

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    swapchain = std::make_unique<Core::SwapChainBuilder::SwapChain>(
        Core::SwapChainBuilder(*context).setExtent(width, height).build()
    );

    frameData.resize(Core::MAX_FRAMES_IN_FLIGHT);
    for (auto& fd : frameData) fd.init(context->getDevice(), context->getCommandPool());

    size_t imageCount = swapchain->images.size();
    renderFinishedSemaphores.resize(imageCount);
    VkSemaphoreCreateInfo semInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto& sem : renderFinishedSemaphores) {
        vkCreateSemaphore(context->getDevice(), &semInfo, nullptr, &sem);
    }

    resourceManager = std::make_unique<Core::ResourceManager>(*context);
    resourceManager->InitGlobalGeometryBuffers(*context, 1'000'000, 3'000'000);

    m_IBLBaker = std::make_unique<IBLBaker>(context.get(), resourceManager.get());
    m_iblTextures = m_IBLBaker->BakeEnvironment("textures/circus_arena_4k.hdr");

    resourceManager->InitGlobalBuffers();
    resourceManager->InitGlobalDescriptorSet();
}

void Renderer::SetupScene(Core::Scene* scene, const std::vector<Core::Mesh>& meshes) {
    RegisterBindlessTextures(meshes);

    VkCommandBuffer cmd = Utils::beginSingleTimeCommands(context->getDevice(), context->getCommandPool());
    m_accelerationStructure = Core::RT::RTAccelerationStructureBuilder(*context, *resourceManager)
        .buildFromScene(cmd, scene)
        .allowTLASUpdates(true)
        .build();
    Utils::endSingleTimeCommands(context->getDevice(), context->getCommandPool(), context->getGraphicsQueue(),cmd);

    resourceManager->SetGlobalTLAS(m_accelerationStructure->getTLASHandle());

    resourceManager->InitRTDescriptorSet();

    for (int i = 0; i < 2; i++) {
        if (m_taaHistory[i].IsValid()) {
            resourceManager->DestroyTexture(m_taaHistory[i]);
        }
    }

    Core::TextureDesc taaDesc{};
    taaDesc.name = "TAA_History";
    taaDesc.extent = { swapchain->extent.width, swapchain->extent.height, 1 };
    taaDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    taaDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    taaDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    taaDesc.mipLevels = 1;
    taaDesc.arrayLayers = 1;

    m_taaHistory[0] = resourceManager->CreateTexture(taaDesc);
    m_taaHistory[1] = resourceManager->CreateTexture(taaDesc);

    BuildRenderGraph(scene);
    renderGraph->InitProfiling(context->getDevice(), context->getPhysicalDevice(), Core::MAX_FRAMES_IN_FLIGHT);
}

void Renderer::BuildRenderGraph(Core::Scene* scene)
{
    if (!renderGraph)
        renderGraph = std::make_unique<Render::Graph::RenderGraph>();
    else
        renderGraph->Reset(*resourceManager.get());

    VkExtent3D ext = { swapchain->extent.width, swapchain->extent.height, 1 };

    m_swapchainImageHandles.resize(swapchain->images.size());

    for (size_t i = 0; i < swapchain->images.size(); i++)
    {
        m_swapchainImageHandles[i] =
            renderGraph->RegisterImportedImage(
                swapchain->images[i],
                swapchain->format,
                ext,VK_IMAGE_LAYOUT_UNDEFINED
            );
    }


    Core::ImageBuilder::Image* taaImg0 = resourceManager->GetTexture(m_taaHistory[0]);
    Core::ImageBuilder::Image* taaImg1 = resourceManager->GetTexture(m_taaHistory[1]);

    VkExtent3D exttaa00 = { taaImg0->extent.width, taaImg0->extent.height, 1 };
    VkExtent3D exttaa01 = { taaImg1->extent.width, taaImg1->extent.height, 1 };
    Render::Graph::RGHandle rgTaaHistory0 = renderGraph->RegisterImportedImage(
        taaImg0->image, taaImg0->format, exttaa00, VK_IMAGE_LAYOUT_UNDEFINED
    );
    Render::Graph::RGHandle rgTaaHistory1 = renderGraph->RegisterImportedImage(
        taaImg1->image, taaImg1->format, exttaa01, VK_IMAGE_LAYOUT_UNDEFINED
    );


    Core::ImageBuilder::Image* envImage = resourceManager->GetTexture(m_iblTextures.environmentCubemap);
    Core::ImageBuilder::Image* irrImage = resourceManager->GetTexture(m_iblTextures.irradianceCubemap);
    Core::ImageBuilder::Image* prefilterImage = resourceManager->GetTexture(m_iblTextures.prefilteredCubemap);
    Core::ImageBuilder::Image* brdfimage = resourceManager->GetTexture(m_iblTextures.brdfLUT);


    Render::Graph::RGHandle rgEnvMap = renderGraph->RegisterImportedImage(
        envImage->image,
        envImage->format,
        { envImage->extent.width, envImage->extent.height, 1 }, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    Render::Graph::RGHandle rgIrrMap = renderGraph->RegisterImportedImage(
        irrImage->image,
        irrImage->format,
        { irrImage->extent.width, irrImage->extent.height, 1 }, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    Render::Graph::RGHandle prefiltermap = renderGraph->RegisterImportedImage(
        prefilterImage->image,
        prefilterImage->format,
        { prefilterImage->extent.width, prefilterImage->extent.height, 1 }, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    Render::Graph::RGHandle brdfMap = renderGraph->RegisterImportedImage(
        brdfimage->image,
        brdfimage->format,
        { brdfimage->extent.width, brdfimage->extent.height, 1 }, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    auto& shadowPass = renderGraph->AddPass<Render::Pass::ShadowPass>(
        "Shadow Pass",
        resourceManager.get(),
        scene);

    auto& pointShadowPass = renderGraph->AddPass<Render::Pass::PointShadowPass>(
        "Point-Shadow Pass",
        resourceManager.get(),
        scene);

    auto& depthPrePass = renderGraph->AddPass<Render::Pass::DepthPrePass>(
        "Depth Pre Pass",
        swapchain->extent,
        resourceManager.get(),
        scene
    );

    auto& geomPass = renderGraph->AddPass<Render::Pass::GeometryPass>(
        "Geometry Pass",
        swapchain->extent,
        resourceManager.get(),
        scene
    );

    auto& ssaoPass = renderGraph->AddPass<Render::Pass::SSAOPass>(
        "SSAO Pass",
        swapchain->extent,
        resourceManager.get(),
        scene);

    auto& lightingPass = renderGraph->AddPass<Render::Pass::DefferdLightingPass>("Deffered lighting pass",
        swapchain->extent,
        resourceManager.get(),
        scene, rgEnvMap, rgIrrMap, prefiltermap, brdfMap, m_iblTextures.environmentCubemap, m_iblTextures.irradianceCubemap,m_iblTextures.prefilteredCubemap,m_iblTextures.brdfLUT);

    auto& histogramPass = renderGraph->AddPass<Render::Pass::HistogramPass>("Histogram Pass",
        resourceManager.get());


    auto& taaPass = renderGraph->AddPass<Render::Pass::TAAPass>(
        "TAA Pass",
        swapchain->extent,
        resourceManager.get(),
        rgTaaHistory0,
        rgTaaHistory1, m_taaHistory[0], 
        m_taaHistory[1]
    );

    auto& ToneMappingPass = renderGraph->AddPass<Render::Pass::ToneMappingPass>("Tone Mapping Pass",
        swapchain->extent,
        resourceManager.get());

    m_blitPass = &renderGraph->AddPass<BlitToSwapchainPass>(
        "Blit Pass",
        resourceManager.get(),
       ToneMappingPass.GetToneMapOutput(),
        m_swapchainImageHandles[0],
        swapchain->extent
    );

    renderGraph->Compile(*resourceManager);

   

}


void Renderer::recreateSwapchain(Core::Scene* scene, Core::Camera* camera) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }
    waitIdle();

    swapchain->destroy(context->getDevice());
    swapchain = std::make_unique<Core::SwapChainBuilder::SwapChain>(
        Core::SwapChainBuilder(*context).setExtent(width, height).build()
    );

    for (int i = 0; i < 2; i++) {
        if (m_taaHistory[i].IsValid()) {
            resourceManager->DestroyTexture(m_taaHistory[i]);
        }
    }

    Core::TextureDesc taaDesc{};
    taaDesc.name = "TAA_History";
    taaDesc.extent = { swapchain->extent.width, swapchain->extent.height, 1 };
    taaDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    taaDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    taaDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    taaDesc.mipLevels = 1;
    taaDesc.arrayLayers = 1;

    m_taaHistory[0] = resourceManager->CreateTexture(taaDesc);
    m_taaHistory[1] = resourceManager->CreateTexture(taaDesc);


    BuildRenderGraph(scene);

    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    camera->InitCamera(aspectRatio, camera->GetFov(), camera->GetNear(), camera->GetFar(), camera->GetPosition(), 10.f);
}

void Renderer::drawFrame(Core::Scene* scene, Core::Camera* camera, bool uiModeActive) {

    VkDevice device = context->getDevice();
    auto& fd = frameData[currentFrame];

    resourceManager->SetFrameIDX(currentFrame);
    vkWaitForFences(device, 1, &fd.inFlightFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain->swapchain, UINT64_MAX, fd.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (m_graphNeedsRebuild) {
        waitIdle();
        renderGraph->Reset(*resourceManager);
        BuildRenderGraph(scene);
        m_graphNeedsRebuild = false;
    }

    UpdateBuffers(currentFrame, scene, camera);
    scene->globalDescriptorSet = resourceManager->GetGlobalDescriptorSet().sets[currentFrame];

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(scene, camera);
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

 
    m_UiLayer->BeginFrame();
    if (uiModeActive) {
        m_UiLayer->DrawPanels();
    }
    m_UiLayer->EndFrame();

    vkResetFences(device, 1, &fd.inFlightFence);
    vkResetCommandBuffer(fd.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(fd.commandBuffer, &beginInfo);

    Utils::TransitionImageLayout(fd.commandBuffer, swapchain->images[imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    m_blitPass->SetBackbuffer(m_swapchainImageHandles[imageIndex]);

    RenderTypes::RenderContext rendercontext{};
    rendercontext.cmd = fd.commandBuffer;
    rendercontext.resourceManager = resourceManager.get();
    rendercontext.currentFrameIndex = currentFrame;
    rendercontext.debugViewMode = m_debugView;
    rendercontext.cameraSettings = m_CameraSettings;

    renderGraph->Execute(rendercontext);

   
    Utils::TransitionImageLayout(fd.commandBuffer, swapchain->images[imageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);


    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain->imageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.offset = { 0, 0 };
    renderInfo.renderArea.extent = swapchain->extent;
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(fd.commandBuffer, &renderInfo);

    m_UiLayer->RecordCommands(fd.commandBuffer);
    
   
    vkCmdEndRendering(fd.commandBuffer);

    Utils::TransitionImageLayout(fd.commandBuffer, swapchain->images[imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(fd.commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = { fd.imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &fd.commandBuffer;

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(context->getGraphicsQueue(), 1, &submitInfo, fd.inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = { swapchain->swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(context->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapchain(scene, camera);
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % Core::MAX_FRAMES_IN_FLIGHT;
}


void Renderer::UpdateBuffers(uint32_t frameIDX, Core::Scene* scene, Core::Camera* camera)
{
    auto* allocator = context->getAllocator();

    m_absoluteFrameCount++;
    
    camera->UpdateJitter(m_absoluteFrameCount, swapchain->extent.width, swapchain->extent.height);

    RenderTypes::CameraUBO ubo{};
    ubo.view = camera->GetViewMatrix();
    ubo.proj = camera->GetProjectionMatrix();
    ubo.invView = glm::inverse(ubo.view);
    ubo.invProj = glm::inverse(ubo.proj);

    glm::mat4 viewProj = ubo.proj * ubo.view;
    ubo.invViewProj = glm::inverse(viewProj);
    ubo.invProjUnjittered = glm::inverse(camera->GetUnjitteredProjectionMatrix());

    ubo.viewProj = viewProj;
    ubo.prevViewProj = m_prevViewProj;

    auto* uboBuffer = resourceManager->GetBuffer(resourceManager->GetCameraUBO()[frameIDX]);
    void* data = allocator->map<void>(uboBuffer->allocation);
    memcpy(data, &ubo, sizeof(ubo));
    allocator->unmap(uboBuffer->allocation);

    uint32_t meshIdx = 0;
    std::vector<RenderTypes::InstanceData> instances;
    instances.reserve(scene->GetObjects().size());
    for (const auto& obj : scene->GetObjects()) {
        RenderTypes::InstanceData inst{};
        inst.model = obj.transform;
        inst.materialID = meshIdx;
        instances.push_back(inst);
        meshIdx++;
    }

    auto* instanceBuffer = resourceManager->GetBuffer(resourceManager->GetInstanceSSBOs()[frameIDX]);
    data = allocator->map<void>(instanceBuffer->allocation);
    memcpy(data, instances.data(), instances.size() * sizeof(RenderTypes::InstanceData));
    allocator->unmap(instanceBuffer->allocation);

    RenderTypes::LightSSBO lightSSBO{};
    lightSSBO.sun = scene->GetDirectionalLight();

    auto& scenePointLights = scene->GetPointLights();
    lightSSBO.pointLightCount = static_cast<uint32_t>(scenePointLights.size());

    uint32_t copyCount = std::min(lightSSBO.pointLightCount, 1024u);
    if (copyCount > 0) {
        memcpy(lightSSBO.pointLights, scenePointLights.data(), copyCount * sizeof(RenderTypes::PointLight));
    }

    auto* lightBuffer = resourceManager->GetBuffer(resourceManager->GetLightSSBOs()[frameIDX]);
    data = allocator->map<void>(lightBuffer->allocation);
    memcpy(data, &lightSSBO, sizeof(RenderTypes::LightSSBO));
    allocator->unmap(lightBuffer->allocation);


    RenderTypes::CascadeData cascades = Utils::CalculateCascades(
        ubo.view,
        camera->GetFov(),
        camera->GetAspectRatio(),
        camera->GetNear(),
        camera->GetFar(),
        glm::normalize(scene->GetDirectionalLight().direction),
        4
    );

    RenderTypes::CascadeUBO cascadeUBO{};
    memcpy(cascadeUBO.lightSpaceMatrices,
        cascades.lightSpaceMatrices,
        sizeof(cascades.lightSpaceMatrices));
    cascadeUBO.splitDepths = glm::vec4(
        cascades.splitDepths[0],
        cascades.splitDepths[1],
        cascades.splitDepths[2],
        cascades.splitDepths[3]);

    auto* cascadeBuffer = resourceManager->GetBuffer(
        resourceManager->GetCascadeUBOs()[frameIDX]);
    data = allocator->map<void>(cascadeBuffer->allocation);
    memcpy(data, &cascadeUBO, sizeof(RenderTypes::CascadeUBO));
    allocator->unmap(cascadeBuffer->allocation);

    static const glm::vec3 faceDirections[6] = {
        { 1,  0,  0}, {-1,  0,  0},
        { 0,  1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1}
    };
    static const glm::vec3 faceUps[6] = {
        { 0, -1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1},
        { 0, -1,  0}, { 0, -1,  0}
    };

     glm::mat4 proj = glm::perspective(
        glm::radians(90.0f), 1.0f, 0.1f, 25.0f);

    std::vector<glm::mat4> allMatrices;
    allMatrices.reserve(6 * scene->GetPointLights().size());

    for (uint32_t l = 0; l < scene->GetPointLights().size(); l++)
    {
        glm::vec3 lightPos = glm::vec3(scene->GetPointLights()[l].position);
        for (uint32_t f = 0; f < 6; f++)
            allMatrices.push_back(
                proj * glm::lookAt(lightPos, lightPos + faceDirections[f], faceUps[f]));
    }

    Core::BufferBuilder::Buffer* buf = resourceManager->GetBuffer(resourceManager->GetPointShadowUBOs()[frameIDX]);
    if (buf->mapped)
        memcpy(buf->mapped, allMatrices.data(), allMatrices.size() * sizeof(glm::mat4));

    m_prevViewProj = camera->GetUnjitteredProjectionMatrix() * ubo.view;

}

void Renderer::RegisterBindlessTextures(const std::vector<Core::Mesh>& meshes)
{
    std::vector<VkDescriptorImageInfo> imageInfos;

    for (auto& mesh : meshes)
    {
        auto writeSlot = [&](Core::TextureHandle handle) {
            VkDescriptorImageInfo info{};
            if (handle.IsValid()) {
                Core::ImageBuilder::Image* img = resourceManager->GetTexture(handle);
                info.imageView = img->view;
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else {
                info.imageView = VK_NULL_HANDLE;
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            info.sampler = resourceManager->GetLinearSampler();
            imageInfos.push_back(info);
            };

        writeSlot(mesh.material.albedoMap);
        writeSlot(mesh.material.normalMap);
        writeSlot(mesh.material.metallicRoughnessMap);
    }

    for (int i = 0; i < Core::MAX_FRAMES_IN_FLIGHT; i++)
    {
        Core::DescriptorWriter writer;
        writer.writeImageArray(4, imageInfos, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .overwrite(resourceManager->GetGlobalDescriptorSet().sets[i], context->getDevice());
    }
}

void Renderer::LoadNewHDREnv(const std::string& hdrFilepath, Core::Scene* scene) {
    waitIdle();
    if (m_iblTextures.environmentCubemap.IsValid()) {
        resourceManager->DestroyTexture(m_iblTextures.environmentCubemap);
        resourceManager->DestroyTexture(m_iblTextures.irradianceCubemap);
        resourceManager->DestroyTexture(m_iblTextures.prefilteredCubemap);
    }
    m_iblTextures = m_IBLBaker->BakeEnvironment(hdrFilepath, m_iblTextures.brdfLUT);

    if (renderGraph && resourceManager) renderGraph->Reset(*resourceManager);
    BuildRenderGraph(scene);
}

void Renderer::Shutdown()
{
    waitIdle();

    VkDevice device = context->getDevice();

    m_IBLBaker.reset();

    if (renderGraph && resourceManager)
    {
        renderGraph->DestroyProfiling(device);
        renderGraph->Reset(*resourceManager);
        
    }

    m_accelerationStructure->Shutdown();

    resourceManager->Shutdown();

    resourceManager.reset();
   
    if (swapchain)
        swapchain->destroy(device);

  
    for(auto& fd : frameData)
        fd.destroy(device);

    for (auto& sem : renderFinishedSemaphores)
        vkDestroySemaphore(device, sem, nullptr);
   
    m_UiLayer->Shutdown(*context.get());

    context->Shutdown();
}

