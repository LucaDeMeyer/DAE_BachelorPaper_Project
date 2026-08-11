#ifndef RENDERER_H
#define RENDERER_H

#include <map>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

#include "Vulkan/Core/RayTracing/AccelerationStructure.h"
#include "Vulkan/Core/GraphicsContext.h"
#include "Vulkan/Core/Swapchain.h"
#include "Vulkan/Core/Pipeline.h"
#include "Vulkan/Core/Buffer.h"
#include "Vulkan/Core/Camera.h"
#include "Vulkan/Core/Image.h"
#include "Vulkan/Core/FrameData.h"
#include "Vulkan/Core/Scene.h"
#include "Vulkan/RenderGraph/RenderGraphTypes.h"
#include "Vulkan/Utils/IBLBaker.h"

namespace UI { class UILayer; }
class BlitToSwapchainPass;
namespace Render::Graph { class RenderGraph; }

/// @brief A pure Vulkan graphics renderer responsible for managing GPU resources, executing the Render Graph, and presenting frames.
class Renderer final
{
public:
    /// @brief Initializes the Vulkan context, swapchain, global buffers, and core rendering systems.
    /// @param window The active GLFW window surface to render to.
    explicit Renderer(GLFWwindow* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// @brief Prepares the renderer for drawing by registering bindless textures and compiling the initial Render Graph.
    /// @param scene The active scene containing lighting and object data.
    /// @param meshes The loaded geometry to be registered in the bindless descriptor arrays.
    void SetupScene(Core::Scene* scene, const std::vector<Core::Mesh>& meshes);

    /// @brief Executes a single frame: acquires an image, updates buffers, executes the Render Graph, and presents to the screen.
    /// @param scene The active scene to draw.
    /// @param camera The active camera providing view/projection matrices.
    /// @param uiModeActive Flag determining if the UI layer should be drawn over the frame.
    void drawFrame(Core::Scene* scene, Core::Camera* camera, bool uiModeActive);

    /// @brief Blocks the CPU until all Vulkan device operations have finished.
    void waitIdle();

    /// @brief Initializes the ImGui Vulkan backend.
    /// @param window The active GLFW window.
    void InitUI(GLFWwindow* window);

    /// @brief Safely tears down and destroys all Vulkan resources, rendering passes, and memory allocations.
    void Shutdown();

    /// @brief Loads a new HDR environment map, rebakes IBL textures, and triggers a Render Graph rebuild.
    /// @param hdrFilepath The path to the new .hdr file.
    /// @param scene The current scene to be re-linked to the new Render Graph.
    void LoadNewHDREnv(const std::string& hdrFilepath, Core::Scene* scene);

    /// @brief Flag indicating the window has been resized and the swapchain needs recreation.
    bool framebufferResized = false;

    // --- Getters for Application and UI Dependency Injection ---

    /// @brief Retrieves the base Vulkan graphics context.
    Core::GraphicsContext* GetContext() const { return context.get(); }
    /// @brief Retrieves the central resource allocator and manager.
    Core::ResourceManager* GetResourceManager() const { return resourceManager.get(); }
    /// @brief Retrieves the UI subsystem.
    UI::UILayer* GetUILayer() const { return m_UiLayer.get(); }
    /// @brief Retrieves the active Render Graph.
    Render::Graph::RenderGraph* GetRenderGraph() const { return renderGraph.get(); }

    /// @brief Retrieves a pointer to the active debug view mode (e.g., Albedo, Final Lighting).
    int* GetDebugViewMode() { return &m_debugView; }
    int* GetRTShadowMode() { return &m_RTShadowMode; }
    int* GetRTRMode() { return &m_RTRMode; }
    int* GetRTAOMode() { return &m_RTAOMode; }

    int* GetRTSPP() { return &m_rtSPP; }

    /// @brief Retrieves a pointer to the physical camera settings (Aperture, Shutter Speed, ISO).
    RenderTypes::PhysicalCameraSettings* GetCameraSettings() { return &m_CameraSettings; }
    /// @brief Retrieves a pointer to the Render Graph rebuild flag.
    bool* GetGraphNeedsRebuildFlag() { return &m_graphNeedsRebuild; }
    /// @brief Retrieves the descriptor set ID for the currently inspected texture in the UI.
    VkDescriptorSet* GetViewedTextureID() { return &m_ViewedTextureID; }
    /// @brief Retrieves the Vulkan texture descriptor metadata for the currently inspected UI texture.
    Core::TextureDesc* GetViewedTextureDesc() { return &m_ViewedTextureDesc; }


    void Screenshot() { m_takeScreenshot = true; }
    void StartBenchmark() {
        if (m_isBenchmarking) return;
        m_isBenchmarking = true;
        m_benchmarkFrame = 0;
        m_benchmarkDataGpu.clear();
        printf("Benchmark Started! (100 Frames)\n");
    }
private:
    /// @brief Bootstraps the Vulkan instance, physical device selection, logical device, and global managers.
    void initVulkan();

    /// @brief Safely destroys and recreates the swapchain and its dependent render passes upon window resize.
    void recreateSwapchain(Core::Scene* scene, Core::Camera* camera);

    /// @brief Loads texture handles from the scene geometry into the global bindless descriptor set.
    void RegisterBindlessTextures(const std::vector<Core::Mesh>& meshes);

    /// @brief Updates Uniform Buffers and Storage Buffers (Camera matrices, Light data, Instance data, Cascades) for the current frame.
    void UpdateBuffers(uint32_t frameIDX, Core::Scene* scene, Core::Camera* camera);

    /// @brief Registers all rendering passes and image resources, and compiles the directed acyclic graph (DAG).
    void BuildRenderGraph(Core::Scene* scene);


    void DumpBenchmark(const std::string& filepath);

    GLFWwindow* window;
    std::unique_ptr<Core::GraphicsContext> context;
    std::unique_ptr<Core::SwapChainBuilder::SwapChain> swapchain;
    std::unique_ptr<Core::ResourceManager> resourceManager;
    std::vector<Core::FrameData> frameData;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    uint32_t currentFrame = 0;

    std::unique_ptr<Render::Graph::RenderGraph> renderGraph;
    BlitToSwapchainPass* m_blitPass = nullptr;
    std::vector<Render::Graph::RGHandle> m_swapchainImageHandles;

    std::unique_ptr<IBLBaker> m_IBLBaker;
    IBLTextures m_iblTextures;

    std::unique_ptr<UI::UILayer> m_UiLayer;

    int m_debugView = 0;
    int m_RTShadowMode = 0;
    int m_RTRMode = 0;
    int m_RTAOMode = 0;
    bool m_graphNeedsRebuild = false;
    RenderTypes::PhysicalCameraSettings m_CameraSettings;

    Core::TextureDesc m_ViewedTextureDesc{};
    VkDescriptorSet m_ViewedTextureID = VK_NULL_HANDLE;

    std::unique_ptr<Core::RT::RTAccelerationStructure> m_accelerationStructure;

    glm::mat4 m_prevViewProj{ 1.0f };

    Core::TextureHandle m_taaHistory[2];
    Core::TextureHandle m_svgfHistory[2];
    Core::TextureHandle m_rtaoHistory[2];
    Core::TextureHandle m_pointShadowHistory[2];
    Core::TextureHandle m_shadowHistory[2];
    uint32_t m_absoluteFrameCount = 0;

    int m_rtSPP = 1;

    bool m_takeScreenshot = false;

    bool m_isBenchmarking = false;
    int m_benchmarkFrame = 0;
    std::map<std::string, std::vector<float>> m_benchmarkDataGpu;
};

#endif