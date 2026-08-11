#include "Application.h"
#include "Renderer.h"
#include "UI/UILayer.h"
#include "UI/UIPanels.h"
#include <stdexcept>
#include <iostream>

Application::Application() {
    InitWindow(1280, 720, "Vulkan Render Engine");

    m_renderer = std::make_unique<Renderer>(m_window);

    SetupCallbacks();

    InitScene();

    m_renderer->SetupScene(m_Scene.get(), m_LoadedMeshes);

    m_renderer->InitUI(m_window);
    InitUI();

    m_lastTime = std::chrono::high_resolution_clock::now();
}

Application::~Application() {
    if (m_renderer) {
        m_renderer->Shutdown();
    }
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

void Application::InitWindow(uint32_t width, uint32_t height, const char* title) {
    if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) throw std::runtime_error("Failed to create GLFW window");
}

void Application::SetupCallbacks() {
   
    glfwSetWindowUserPointer(m_window, this);

    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* win, int width, int height) {
        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win));
        if (app && app->m_renderer) {
            app->m_renderer->framebufferResized = true;
        }
        });

    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glfwSetCursorPosCallback(m_window, [](GLFWwindow* win, double xpos, double ypos) {
        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win));
        if (!app || app->IsUIModeActive()) return;

        static double lastX = xpos; static double lastY = ypos;
        static bool firstMouse = true;
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }

        float xoffset = static_cast<float>(xpos - lastX);
        float yoffset = static_cast<float>(lastY - ypos);
        lastX = xpos; lastY = ypos;

        app->GetCamera()->ProcessMouseMovement(xoffset, yoffset, true);
        });

    glfwSetScrollCallback(m_window, [](GLFWwindow* win, double xoffset, double yoffset) {
        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win));
        if (app && !app->IsUIModeActive()) {
            app->GetCamera()->ProcessMouseScroll(static_cast<float>(yoffset));
        }
        });
}

void Application::InitScene() {
    m_Scene = std::make_unique<Core::Scene>();
    m_Camera = std::make_unique<Core::Camera>();

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_Camera->InitCamera(aspectRatio, 45.f, 0.1f, 50.f, glm::vec3(0.0f, 2.0f, 0.f), 10.f);

  
    m_LoadedMeshes = Core::MeshLoader::LoadFromFile(*m_renderer->GetContext(), "models/Sponza/Sponza.gltf", *m_renderer->GetResourceManager());
    for (auto& mesh : m_LoadedMeshes) {
        Core::Object model(&mesh, "models/Sponza/Sponza.gltf");
        model.SetPosition({ 0.0f, 0.0f, 0.0f });
        m_Scene->AddObject(model);
    }
    m_Scene->UpdateSceneTransform();

    m_Scene->AddDirectionalLight({ -1.0f, -1.0f, 0.3f }, { 1.0f, 0.55f, 0.7f }, 1000.f);
    m_Scene->AddPointLight({ 0, 1.5, 3 }, { 1.0f, 0.0f, 0.0f }, 5000.f, 2.f);
    m_Scene->AddPointLight({ 0, 5, 0 }, { 1.0f, 1.0f, 1.0f }, 5000.f, 2.f);
}

void Application::InitUI() {
    auto* uiLayer = m_renderer->GetUILayer();

    uiLayer->AddPanel(std::make_unique<UI::MainMenuBarPanel>(
        &m_showCameraSettings,
        &m_showLightingControls,
        &m_showMemoryStats,
        &m_showRenderGraphTimeline,
        m_renderer->GetDebugViewMode()
    ));

    auto onHdrLoad = [this](const std::string& path) {
        m_renderer->LoadNewHDREnv(path, m_Scene.get());
        };

    uiLayer->AddPanel(std::make_unique<UI::LightingControlsPanel>(
        &m_showLightingControls,
        m_Scene.get(),
        m_renderer->GetGraphNeedsRebuildFlag(),
        m_renderer->GetCameraSettings(),
        m_renderer->GetRTShadowMode(),
        m_renderer->GetRTRMode(),
        m_renderer->GetRTAOMode(),
        m_renderer->GetRTSPP(),
        m_renderer->GetAOSPP(),
        m_renderer->GetUsePostDenoising(),
        onHdrLoad
    ));

    uiLayer->AddPanel(std::make_unique<UI::CameraSettingsPanel>(
        &m_showCameraSettings,
        m_renderer->GetCameraSettings(),
        m_Camera.get()
    ));

    uiLayer->AddPanel(std::make_unique<UI::MemoryStatsPanel>(
        &m_showMemoryStats,
        m_renderer->GetContext()->getAllocator()
    ));

    auto onBenchmark = [this]() {
        m_renderer->StartBenchmark();
        };

    uiLayer->AddPanel(std::make_unique<UI::RenderGraphTimelinePanel>(
        &m_showRenderGraphTimeline,
        m_renderer->GetRenderGraph(),
        m_renderer->GetResourceManager(),
        m_renderer->GetContext(),
        m_renderer->GetViewedTextureID(),
        m_renderer->GetViewedTextureDesc(),onBenchmark
    ));

    
}

void Application::Run() {
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - m_lastTime).count();
        m_lastTime = currentTime;

        m_renderer->GetResourceManager()->UpdateDeltaTime(deltaTime);

        bool tabIsPressed = glfwGetKey(m_window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tabIsPressed && !m_tabKeyWasPressed) {
            m_uiModeActive = !m_uiModeActive;
            glfwSetInputMode(m_window, GLFW_CURSOR, m_uiModeActive ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        }
        m_tabKeyWasPressed = tabIsPressed;

        bool f2IsPressed = glfwGetKey(m_window, GLFW_KEY_F2) == GLFW_PRESS;
        if (f2IsPressed && !m_f2WasPressed) {
            m_renderer->Screenshot();
        }
        m_f2WasPressed = f2IsPressed;

        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(m_window, true);
        }

        if (!m_uiModeActive) {
            m_Camera->ProcessKeyboard(m_window, deltaTime);
        }

        m_renderer->drawFrame(m_Scene.get(), m_Camera.get(), m_uiModeActive);
    }

    m_renderer->waitIdle();
}