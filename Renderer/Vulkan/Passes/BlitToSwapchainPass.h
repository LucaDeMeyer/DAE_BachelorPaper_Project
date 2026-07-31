#ifndef BLIT_PASS_H
#define BLIT_PASS_H
#include "../RenderGraph/RenderGraph.h"
#include "../RenderGraph/Pass.h"

/// @brief Copies the final rendered LDR image directly to the OS window's Swapchain for presentation.
/// Uses hardware-accelerated image blitting. Because this pass writes to the physical screen, 
/// it acts as the ultimate "Root" dependency that forces the rest of the Render Graph to execute.
///
/// @note Inputs: "ToneMap_OUT" (TransferRead)
/// @note Outputs: Swapchain Backbuffer (TransferWrite)
class BlitToSwapchainPass : public Render::Graph::Pass {
public:
    BlitToSwapchainPass(const std::string& name,
        Core::ResourceManager* resManager,
        Render::Graph::RGHandle inputTex,
        Render::Graph::RGHandle backbuffer,
        VkExtent2D extent)
        : Pass(name), m_resManager(resManager), m_inputTex(inputTex),
        m_backbuffer(backbuffer), m_extent(extent) {
    }

    void SetBackbuffer(Render::Graph::RGHandle handle)
    {
        m_backbuffer = handle;
    }

    void Setup(Render::Graph::RenderGraphBuilder& builder) override
    {
        m_inputTex = builder.FindResource("ToneMap_OUT");

        builder.AddDependency(m_inputTex, Render::Graph::AccessType::TransferRead);
    }

    void Execute(const RenderTypes::RenderContext& context, Render::Graph::RenderGraph& graph) override
    {
        VkImage srcImage = graph.GetPhysicalImage(m_inputTex, *context.resourceManager);
        VkImage dstImage = graph.GetPhysicalImage(m_backbuffer, *context.resourceManager);

        VkImageBlit blitRegion{};
        blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blitRegion.srcOffsets[0] = { 0, 0, 0 };
        blitRegion.srcOffsets[1] = { (int32_t)m_extent.width, (int32_t)m_extent.height, 1 };
        blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blitRegion.dstOffsets[0] = { 0, 0, 0 };
        blitRegion.dstOffsets[1] = { (int32_t)m_extent.width, (int32_t)m_extent.height, 1 };

        vkCmdBlitImage(context.cmd,
            srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blitRegion,
            VK_FILTER_LINEAR);
    }

    bool HasSideEffect() override { return true; }

private:
    Render::Graph::RGHandle m_inputTex;
    Render::Graph::RGHandle m_backbuffer;
    Core::ResourceManager* m_resManager;
    VkExtent2D m_extent;
};
#endif
