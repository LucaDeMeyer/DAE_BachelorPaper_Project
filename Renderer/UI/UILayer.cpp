#include "UILayer.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <stdexcept>
#include "Vulkan/Core/GraphicsContext.h"
#include "Vulkan/Utils/Utils.h"

void UI::UILayer::Init(Core::GraphicsContext& ctx, GLFWwindow* window,VkFormat swapChainFormat)
{
    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(ctx.getDevice(), &pool_info, nullptr, &m_imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; 

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = ctx.getInstance();
    init_info.PhysicalDevice = ctx.getPhysicalDevice();
    init_info.Device = ctx.getDevice();
    init_info.QueueFamily = Utils::findQueueFamilies(ctx.getPhysicalDevice(), ctx.getSurface()).graphicsFamily.value();
    init_info.Queue = ctx.getGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_imguiPool; 
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    init_info.UseDynamicRendering = true;

	VkPipelineRenderingCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    createInfo.colorAttachmentCount = 1;
    createInfo.pColorAttachmentFormats = &swapChainFormat;

    ImGui_ImplVulkan_PipelineInfo pipelineInfo{};
    pipelineInfo.PipelineRenderingCreateInfo = createInfo;
    pipelineInfo.RenderPass = nullptr;
    pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    init_info.PipelineInfoMain = pipelineInfo;
   
    ImGui_ImplVulkan_Init(&init_info);
}

void UI::UILayer::Shutdown(Core::GraphicsContext& ctx) {
    
    vkDeviceWaitIdle(ctx.getDevice());

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.getDevice(), m_imguiPool, nullptr);
        m_imguiPool = VK_NULL_HANDLE;
    }
}

void UI::UILayer::BeginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UI::UILayer::EndFrame() {
    ImGui::Render();
}

void UI::UILayer::RecordCommands(VkCommandBuffer cmd) {
    // Takes the generated draw data from EndFrame() and writes the actual 
    // vkCmdBindPipeline, vkCmdBindDescriptorSets, and vkCmdDrawIndexed calls
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}