#ifndef CLEAR_PASS_H
#define CLEAR_PASS_H
#include <string>
#include "../RenderGraph/RenderGraph.h"
#include "../RenderGraph/Pass.h"
#include "../Core/ResourceManager.h"


/// @brief A utility/debug pass that allocates a temporary texture and clears it to a solid color (Magenta).
/// Useful for testing the Render Graph's memory allocation and visual debugging.
///
/// @note Inputs: None
/// @note Outputs: "TestPinkTexture" (TransferWrite)
class ClearPass : public Render::Graph::Pass {
public:
    ClearPass(const std::string& name, Core::ResourceManager* resManager)
        : Pass(name), m_resManager(resManager) {
    }

    virtual void Setup(Render::Graph::RenderGraphBuilder& builder) override {
        Core::TextureDesc desc{};
        desc.name = "TestPinkTexture";
        desc.extent = { 1920, 1080, 1 };
        desc.format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT| VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;

        m_pinkTexture = builder.CreateTexture(desc);
        builder.AddDependency(m_pinkTexture, Render::Graph::AccessType::TransferWrite);
    }

     void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override {

        VkImage pinkImage = graph.GetPhysicalImage(m_pinkTexture, *context.resourceManager);

        VkClearColorValue clearColor = { { 1.0f, 0.0f, 1.0f, 1.0f } };
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdClearColorImage(context.cmd, pinkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
    }

    Render::Graph::RGHandle GetPinkTextureHandle() const { return m_pinkTexture; }

private:
    Render::Graph::RGHandle m_pinkTexture;
    Core::ResourceManager* m_resManager;
};
#endif