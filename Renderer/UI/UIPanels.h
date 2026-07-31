#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <functional>

namespace Core {
	class Camera;
	class Scene;
    class Allocator;
    class ResourceManager;
    struct GraphicsContext;
    struct TextureDesc;
}

namespace RenderTypes {
    struct PhysicalCameraSettings;
}

namespace Render::Graph {
    class RenderGraph;
    struct RGHandle;
}

namespace UI {

    /// @brief Abstract base interface for all ImGui-based UI panels.
    class IPanel {
    public:
        virtual ~IPanel() = default;

        /// @brief Executes ImGui rendering commands for this specific panel.
        virtual void Draw() = 0;
    };

    /// @brief Renders the global top menu bar, handling window toggles and debug view selection.
    class MainMenuBarPanel : public IPanel {
    public:
        /// @param showCameraSettings Pointer to the application's camera toggle state.
        /// @param showLightingControls Pointer to the application's lighting toggle state.
        /// @param showMemoryStats Pointer to the application's memory stats toggle state.
        /// @param showRenderGraphTimeline Pointer to the application's render graph toggle state.
        /// @param debugViewMode Pointer to the renderer's debug view state (e.g., Albedo, Final).
        MainMenuBarPanel(
            bool* showCameraSettings,
            bool* showLightingControls,
            bool* showMemoryStats,
            bool* showRenderGraphTimeline,
            int* debugViewMode
        ) : m_showCameraSettings(showCameraSettings),
            m_showLightingControls(showLightingControls),
            m_showMemoryStats(showMemoryStats),
            m_showRenderGraphTimeline(showRenderGraphTimeline),
            m_debugViewMode(debugViewMode) {
        }

        void Draw() override;
    private:
        bool* m_showCameraSettings;
        bool* m_showLightingControls;
        bool* m_showMemoryStats;
        bool* m_showRenderGraphTimeline;
        int* m_debugViewMode;
    };

    /// @brief Renders controls for physically-based camera parameters (Aperture, ISO, Shutter Speed).
    class CameraSettingsPanel : public IPanel {
    public:
        CameraSettingsPanel(bool* show, RenderTypes::PhysicalCameraSettings* settings, Core::Camera* camera)
            : m_show(show), m_settings(settings), m_camera(camera) {
        }

        void Draw() override;
    private:
        bool* m_show;
        RenderTypes::PhysicalCameraSettings* m_settings;
        Core::Camera* m_camera; 
    };

    /// @brief Renders controls for scene lighting, point light management, and IBL/HDR environment loading.
    class LightingControlsPanel : public IPanel {
    public:
        LightingControlsPanel(
            bool* show,
            Core::Scene* scene,
            bool* graphNeedsRebuild,
            RenderTypes::PhysicalCameraSettings* settings,
            std::function<void(const std::string&)> onHdrLoad
        ) : m_show(show), m_scene(scene), m_graphNeedsRebuild(graphNeedsRebuild),
            m_settings(settings), m_onHdrLoad(onHdrLoad) {
        }

        void Draw() override;
    private:
        bool* m_show;
        Core::Scene* m_scene;
        bool* m_graphNeedsRebuild;
        RenderTypes::PhysicalCameraSettings* m_settings;
        std::function<void(const std::string&)> m_onHdrLoad;
    };

    /// @brief Visualizes Vulkan Memory Allocator (VMA) statistics and device memory usage.
    class MemoryStatsPanel : public IPanel {
    public:
        MemoryStatsPanel(bool* show, Core::Allocator* allocator)
            : m_show(show), m_allocator(allocator) {
        }

        void Draw() override;
    private:
        bool* m_show;
        Core::Allocator* m_allocator;
    };

    /// @brief Visualizes the DAG structure of the Render Graph, pass timings (CPU/GPU), and resource dependencies.
    class RenderGraphTimelinePanel : public IPanel {
    public:
        RenderGraphTimelinePanel(
            bool* show,
            Render::Graph::RenderGraph* renderGraph,
            Core::ResourceManager* resourceManager,
            Core::GraphicsContext* context,
            VkDescriptorSet* viewedTextureID,
            Core::TextureDesc* viewedTextureDesc
        ) : m_show(show), m_renderGraph(renderGraph), m_resourceManager(resourceManager),
            m_context(context), m_viewedTextureID(viewedTextureID), m_viewedTextureDesc(viewedTextureDesc) {
        }

        void Draw() override;
    private:
        /// @brief Helper to draw collapsible nodes for a specific render graph resource handle.
        void DrawResourceNode(Render::Graph::RGHandle resourceHandle);

        bool* m_show;
        Render::Graph::RenderGraph* m_renderGraph;
        Core::ResourceManager* m_resourceManager;
        Core::GraphicsContext* m_context;
        VkDescriptorSet* m_viewedTextureID;
        Core::TextureDesc* m_viewedTextureDesc;
    };

} // namespace UI