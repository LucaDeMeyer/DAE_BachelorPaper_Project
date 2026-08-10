#include "UIPanels.h"
#include <imgui.h>
#include <fstream>
#include <vulkan/vk_enum_string_helper.h>

#include "imgui_impl_vulkan.h"
#include "Vulkan/RenderGraph/RenderGraph.h"
#include "Vulkan/Utils/Utils.h"
#include "Vulkan/Core/Scene.h"
#include "Vulkan/Core/Allocator.h"
#include "Vulkan/Core/ResourceManager.h"
#include "Vulkan/Core/GraphicsContext.h"
#include "Vulkan/RenderGraph/Pass.h"
#include "Vulkan/Core/Camera.h"
namespace UI {



    void MainMenuBarPanel::Draw() {
        if (ImGui::BeginMainMenuBar()) {
            ImGui::TextDisabled("Vulkan Engine | %.1f FPS", ImGui::GetIO().Framerate);
            ImGui::Separator();

            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Lighting Controls", nullptr, m_showLightingControls);
                ImGui::MenuItem("Camera Settings", nullptr, m_showCameraSettings);
                ImGui::MenuItem("VRAM Stats", nullptr, m_showMemoryStats);
                ImGui::MenuItem("Render Graph Timeline", nullptr, m_showRenderGraphTimeline);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Shader Views")) {
                ImGui::RadioButton("Final Lighting", m_debugViewMode, 0);
                ImGui::RadioButton("Albedo", m_debugViewMode, 1);
                ImGui::RadioButton("Normals", m_debugViewMode, 2);
                ImGui::RadioButton("Roughness/Metallic", m_debugViewMode, 3);
                ImGui::RadioButton("Depth", m_debugViewMode, 4);
                ImGui::RadioButton("SSAO", m_debugViewMode, 5);
                ImGui::RadioButton("SSR", m_debugViewMode, 6);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

    void CameraSettingsPanel::Draw() {
        if (!*m_show) return;

        ImGui::Begin("Camera Settings", m_show, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::SliderFloat("Aperture (f-stop)", &m_settings->aperture, 1.2f, 22.0f);
        ImGui::SliderFloat("ISO", &m_settings->iso, 100.0f, 6400.0f);

        static float shutterDenominator = 60.0f;
        if (ImGui::SliderFloat("Shutter Speed (1/x)", &shutterDenominator, 1.0f, 4000.0f)) {
            m_settings->shutterSpeed = 1.0f / shutterDenominator;
        }

        float ev100 = log2f((m_settings->aperture * m_settings->aperture) / m_settings->shutterSpeed)
            - log2f(m_settings->iso / 100.0f);
        ImGui::TextDisabled("Calculated EV100: %.2f", ev100);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Gameplay Camera");

      
        float currentFov = m_camera->GetFov();
        if (ImGui::SliderFloat("Field of View", &currentFov, 1.0f, 120.0f)) {
            m_camera->SetFOV(currentFov);
        }

        float currentSpeed = m_camera->GetSpeed();
        if (ImGui::SliderFloat("Movement Speed", &currentSpeed, 0.05f, 200.0f)) {
            m_camera->SetSpeed(currentSpeed);
        }

        ImGui::End();
    }

    void LightingControlsPanel::Draw() {
        if (!*m_show) return;

        ImGui::Begin("Lighting & Scene Controls", m_show);
        ImGui::Spacing();

        if (ImGui::TreeNodeEx("Environment Skybox (IBL)", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char hdrPathBuffer[256] = "textures/circus_arena_4k.hdr";
            ImGui::InputText("HDR File Path", hdrPathBuffer, IM_ARRAYSIZE(hdrPathBuffer));

            if (ImGui::Button("Load & Re-Bake HDR", ImVec2(-1, 0))) {
                if (m_onHdrLoad) m_onHdrLoad(hdrPathBuffer);
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Quick Presets:");
            // using snprintf because ImGui is written in C->uses char arrays instead of std::string. strcpy_s is not cross - compatible, and strcpy is deprecated,which gives us compiler warnings.
            if (ImGui::Button("Morning Sky")) {
                snprintf(hdrPathBuffer,sizeof(hdrPathBuffer), "textures/qwantani_mid_morning_puresky_4k.hdr");
                if (m_onHdrLoad) m_onHdrLoad(hdrPathBuffer);

                m_settings->aperture = 8.0f;
                m_settings->iso = 100.0f;
                m_settings->shutterSpeed = 1.0f / 350.0f;

                auto& sun = m_scene->GetDirectionalLight();
                sun.direction = glm::vec4(-0.5f, -0.707f, -0.25f, 110000.0f);
                sun.color = glm::vec4(1.0f, 0.96f, 0.91f, 1.0f);
            }

            ImGui::SameLine();

            if (ImGui::Button("Circus Arena")) {
                snprintf(hdrPathBuffer, sizeof(hdrPathBuffer), "textures/circus_arena_4k.hdr");
                if (m_onHdrLoad) m_onHdrLoad(hdrPathBuffer);

                m_settings->aperture = 2.8f;
                m_settings->iso = 800.0f;
                m_settings->shutterSpeed = 1.0f / 60.0f;

                auto& sun = m_scene->GetDirectionalLight();
                sun.direction = glm::vec4(-1.0f, -1.0f, 0.3f, 500.0f);
                sun.color = glm::vec4(0.4f, 0.45f, 0.6f, 1.0f);

                auto& pointLights = m_scene->GetPointLights();
                pointLights.clear();

                m_scene->AddPointLight({ 0.0f, 5.0f, 0.0f }, { 1.0f, 0.9f, 0.7f }, 8000.0f, 12.0f);
                m_scene->AddPointLight({ 0.0f, 1.5f, 3.0f }, { 1.0f, 0.1f, 0.1f }, 4000.0f, 6.0f);
                *m_graphNeedsRebuild = true;
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Directional Light (Sun)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& dirLight = m_scene->GetDirectionalLight();
            ImGui::DragFloat3("Direction", &dirLight.direction.x, 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color", &dirLight.color.x);
            ImGui::DragFloat("Intensity (Lux)", &dirLight.direction.w, 10.0f, 0.0f, 100000.0f);
        }
        auto& pointLights = m_scene->GetPointLights();
        std::string pointsHeader = "Point Lights Management (" + std::to_string(pointLights.size()) + ")";

        if (ImGui::CollapsingHeader(pointsHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Add Point Light", ImVec2(-1, 0))) {
                m_scene->AddPointLight({ 0.0f, 2.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 5000.0f, 5.0f);
                *m_graphNeedsRebuild = true;
            }
            ImGui::Separator();

            int lightToDelete = -1;
            for (size_t i = 0; i < pointLights.size(); i++) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode(("Light #" + std::to_string(i)).c_str())) {
                    auto& light = pointLights[i];
                    ImGui::DragFloat3("Position", &light.position.x, 0.05f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        *m_graphNeedsRebuild = true;
                    }
                    ImGui::ColorEdit3("Color", &light.color.x);
                    ImGui::DragFloat("Luminance", &light.color.w, 50.0f, 0.0f, 100000.0f);

                    if (ImGui::Button("Delete Light")) {
                        lightToDelete = static_cast<int>(i);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (lightToDelete != -1) {
                pointLights.erase(pointLights.begin() + lightToDelete);
                *m_graphNeedsRebuild = true;
            }
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Shadow Settings", ImGuiTreeNodeFlags_DefaultOpen)) {

            bool useRT = (*m_enableRTShadows == 1);
            if (ImGui::Checkbox("Enable Ray Traced Shadows", &useRT)) {
                *m_enableRTShadows = useRT ? 1 : 0;
                *m_graphNeedsRebuild = true;
            }
        }
        ImGui::End();
    }
    void MemoryStatsPanel::Draw() {
        if (!*m_show) return;

        ImGui::Begin("VRAM Memory Statistics (VMA)", m_show, ImGuiWindowFlags_AlwaysAutoResize);
        auto stats = m_allocator->GetStats();

        float usedMB = static_cast<float>(stats.usedBytes) / (1024.0f * 1024.0f);
        float allocatedMB = static_cast<float>(stats.allocatedBytes) / (1024.0f * 1024.0f);

        ImGui::TextDisabled("Physical Device Memory");
        ImGui::Separator();

        float usageRatio = allocatedMB > 0.0f ? (usedMB / allocatedMB) : 0.0f;
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f MB / %.2f MB", usedMB, allocatedMB);
        ImGui::ProgressBar(usageRatio, ImVec2(-1.f, 0.f), overlay);

        ImGui::Spacing();
        ImGui::Text("Active Allocations: %u", stats.allocationCount);
        ImGui::Text("Vulkan Memory Blocks: %u", stats.blockCount);
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Dump Detailed JSON to Console")) {
            char* detailedStats = m_allocator->GetStatsString();
            printf("%s\n", detailedStats);
            m_allocator->FreeStatsString(detailedStats);
        }

        if (ImGui::Button("Dump Detailed JSON to File")) {
            char* data = m_allocator->GetStatsString();
            std::ofstream f("VmaDump.json");
            if (f.is_open()) {
                f << data;
                printf("Successfully saved VMA stats to VmaDump.json\n");
            }
            m_allocator->FreeStatsString(data);
        }
        ImGui::End();
    }

  
    void RenderGraphTimelinePanel::Draw() {
        if (!*m_show) return;

        ImGui::Begin("Render Graph Timeline", m_show);
        const auto& executionOrder = m_renderGraph->GetExecutionOrder();

        float totalCpu = 0.0f, totalGpu = 0.0f;
        for (uint32_t passIndex : executionOrder) {
            totalCpu += m_renderGraph->GetCPUTimeMs(passIndex);
            totalGpu += m_renderGraph->GetGPUTimeMs(passIndex);
        }

        ImGui::Text("Active Passes: %zu", executionOrder.size());
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Total CPU: %.3f ms | Total GPU: %.3f ms", totalCpu, totalGpu);
        ImGui::Separator();

        static ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("TimelineTable", 3, flags)) {
            ImGui::TableSetupColumn("Pass Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (uint32_t i = 0; i < executionOrder.size(); ++i) {
                uint32_t passIndex = executionOrder[i];
                const auto* pass = m_renderGraph->GetPass(passIndex);
                const auto& dependencies = m_renderGraph->GetPassDependencies(passIndex);

                ImGui::PushID(passIndex);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                bool nodeOpen = ImGui::TreeNodeEx(pass->GetName().c_str(), ImGuiTreeNodeFlags_SpanFullWidth);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", m_renderGraph->GetCPUTimeMs(passIndex));

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", m_renderGraph->GetGPUTimeMs(passIndex));

                if (nodeOpen) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    // Inputs (Reads)
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Reads (Inputs):");
                    bool hasReads = false;
                    for (const auto& dep : dependencies) {
                        if (!Render::Graph::IsWriteAccess(dep.access)) {
                            DrawResourceNode(dep.resource);
                            hasReads = true;
                        }
                    }
                    if (!hasReads) ImGui::TextDisabled("  None");

                    // Outputs (Writes)
                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Writes (Outputs):");
                    bool hasWrites = false;
                    for (const auto& dep : dependencies) {
                        if (Render::Graph::IsWriteAccess(dep.access)) {
                            DrawResourceNode(dep.resource);
                            hasWrites = true;
                        }
                    }
                    if (!hasWrites) ImGui::TextDisabled("  None");

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::End();

        // Texture Inspector Window handling
        if (*m_viewedTextureID != VK_NULL_HANDLE) {
            ImGui::Begin("Texture Inspector", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Viewing: %s", m_viewedTextureDesc->name.c_str());
            ImGui::Text("Resolution: %d x %d", m_viewedTextureDesc->extent.width, m_viewedTextureDesc->extent.height);
            ImGui::Separator();

            float maxWindowSize = 512.0f;
            float aspect = (float)m_viewedTextureDesc->extent.width / (float)m_viewedTextureDesc->extent.height;
            ImVec2 displaySize = ImVec2(maxWindowSize, maxWindowSize / aspect);

            if (!ImGui::Button("Close Inspector")) {
                ImGui::Image((ImTextureID)*m_viewedTextureID, displaySize);
            }
            else {
                vkDeviceWaitIdle(m_context->getDevice());
                ImGui_ImplVulkan_RemoveTexture(*m_viewedTextureID);
                *m_viewedTextureID = VK_NULL_HANDLE;
            }
            ImGui::End();
        }
    }

    void RenderGraphTimelinePanel::DrawResourceNode(Render::Graph::RGHandle resourceHandle) {
        const auto& res = m_renderGraph->GetPhysicalResource(resourceHandle);
        std::string resName = m_renderGraph->GetResourceName(res);

        if (ImGui::TreeNode(resName.c_str())) {
            if (res.isBuffer) {
                ImGui::Text("Size: %zu bytes", res.bufferDesc.size);
            }
            else {
                ImGui::TextDisabled("Type: %s", res.isTransient ? "Transient" : "Persistent");
                ImGui::Text("Format: %s", string_VkFormat(res.textureDesc.format));
                bool isFinalOutput = (resName == "ToneMap_OUT");

                if (!Utils::IsDepthFormat(res.textureDesc.format) && !isFinalOutput) {
                    if (ImGui::Button("Inspect Texture")) {
                        if (*m_viewedTextureID != VK_NULL_HANDLE) {
                            vkDeviceWaitIdle(m_context->getDevice());
                            ImGui_ImplVulkan_RemoveTexture(*m_viewedTextureID);
                        }
                        *m_viewedTextureID = ImGui_ImplVulkan_AddTexture(
                            m_resourceManager->GetLinearSampler(),
                            m_resourceManager->GetTexture(res.physicalTexture)->view,
                            res.currentLayout
                        );
                        *m_viewedTextureDesc = res.textureDesc;
                    }
                }
            }
            ImGui::TreePop();
        }
    }

} 