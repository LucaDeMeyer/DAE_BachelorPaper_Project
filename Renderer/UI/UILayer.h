#ifndef UI_LAYER_H
#define UI_LAYER_H
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "UIPanels.h"

namespace Core
{
    struct GraphicsContext;
}
namespace UI
{
    /// @brief A self-contained subsystem that manages the Dear ImGui lifecycle.
    class UILayer final {
    public:
        UILayer() = default;
        ~UILayer() = default;

        UILayer(const UILayer&) = delete;
        UILayer& operator=(const UILayer&) = delete;

        /// @brief Initializes ImGui for Dynamic Rendering.
        /// @param colorFormat The VkFormat of the swapchain image ImGui will draw over.
        void Init(Core::GraphicsContext& ctx, GLFWwindow* window,VkFormat swapchainFormat);

        void BeginFrame();
        void EndFrame();

        /// @brief Records ImGui draw commands. Must be called inside vkCmdBeginRendering
        void RecordCommands(VkCommandBuffer cmd);
        void Shutdown(Core::GraphicsContext& ctx);

        void AddPanel(std::unique_ptr<IPanel> panel) { m_panels.push_back(std::move(panel)); }
        void DrawPanels() { for (auto& p : m_panels) p->Draw(); }

    private:
        VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
        std::vector<std::unique_ptr<IPanel>> m_panels;
    };
}
#endif