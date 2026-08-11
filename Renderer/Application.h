#ifndef APPLICATION_H
#define APPLICATION_H
#include <memory>
#include <vector>
#include <chrono>
#include <GLFW/glfw3.h>

#include "Vulkan/Core/Scene.h"
#include "Vulkan/Core/Camera.h"
#include "Vulkan/Core/Object.h"

class Renderer;

/// @brief The central hub of the engine. Manages the main game loop, window creation, input state, and high-level scene ownership.
class Application final
{
public:
    /// @brief Initializes the GLFW window, creates the Renderer, and sets up the initial scene and UI state.
    Application();

    /// @brief Cleans up engine resources and terminates GLFW.
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// @brief Starts the main application loop. Handles window events, input routing, and frame dispatch.
    void Run();

    /// @brief Retrieves the active camera for input handling and rendering.
    /// @return Pointer to the current Core::Camera.
    Core::Camera* GetCamera() const { return m_Camera.get(); }

    /// @brief Checks if the engine is currently in UI mode (cursor enabled, camera movement locked).
    /// @return True if UI mode is active, false otherwise.
    bool IsUIModeActive() const { return m_uiModeActive; }

private:
    /// @brief Initializes the GLFW window with the specified dimensions and title.
    void InitWindow(uint32_t width, uint32_t height, const char* title);

    /// @brief Binds GLFW input callbacks (mouse, keyboard, window resize) to the application instance.
    void SetupCallbacks();

    /// @brief Loads the default scene layout, including meshes, lights, and camera configuration.
    void InitScene();

    /// @brief Instantiates and registers all ImGui panels with the UI Layer.
    void InitUI();

    GLFWwindow* m_window = nullptr;
    std::unique_ptr<Renderer> m_renderer;

    // --- State Ownership ---
    /// @brief The active 3D scene containing all objects and lights.
    std::unique_ptr<Core::Scene> m_Scene;

    /// @brief The primary camera used to view the scene.
    std::unique_ptr<Core::Camera> m_Camera;

    /// @brief Container for all loaded 3D geometry.
    std::vector<Core::Mesh> m_LoadedMeshes;

    std::chrono::high_resolution_clock::time_point m_lastTime;

    // --- App/Input State ---
    bool m_uiModeActive = false;
    bool m_tabKeyWasPressed = false;
    bool m_f2WasPressed = false;
    // UI Panel Visibility Toggles
    bool m_showCameraSettings = false;
    bool m_showLightingControls = false;
    bool m_showRenderGraphTimeline = false;
    bool m_showMemoryStats = false;
};
#endif