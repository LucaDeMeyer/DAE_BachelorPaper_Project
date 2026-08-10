#ifndef RENDERGRAPH_TYPES_H
#define RENDERGRAPH_TYPES_H
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "Vulkan/Core/ResourceTypes.h"

namespace Render::Graph
{
    class Pass;

    /// @brief High-level abstraction for Vulkan memory access types.
    /// Passes use these to declare their intent. The Render Graph translates 
    /// these into exact Vulkan Pipeline Stages, Access Masks, and Image Layouts.
    enum class AccessType {
        ShaderRead,
        ColorAttachmentWrite,
        ReadWrite,
        ComputeShaderRead,
        ComputeShaderWrite,
        TransferRead,
        TransferWrite,
        DepthWrite,
        DepthRead,
        DepthShaderRead
    };

    /// @brief Hazard Detection Helper: Checks if an access type mutates the resource.
    /// Used by the Render Graph to detect RAW (Read-After-Write) and WAW (Write-After-Write) hazards.
    inline bool IsWriteAccess(AccessType access) {
        return access == AccessType::ColorAttachmentWrite ||
            access == AccessType::ReadWrite ||
            access == AccessType::ComputeShaderWrite ||
            access == AccessType::TransferWrite ||
            access == AccessType::DepthWrite;
    }

    /// @brief Hazard Detection Helper: Checks if an access type only reads the resource.
    inline bool IsReadAccess(AccessType access) {
        return access == AccessType::ShaderRead ||
            access == AccessType::ReadWrite ||
            access == AccessType::ComputeShaderRead ||
            access == AccessType::TransferRead ||
            access == AccessType::DepthRead ||
            access == AccessType::DepthShaderRead;
    }

    /// @brief A virtual identifier for a resource managed by the Render Graph.
    /// @note This is NOT a physical Vulkan handle. It allows the graph to track dependencies 
    /// before actually allocating physical GPU memory (Memory Aliasing).
    struct RGHandle {
        uint32_t id = UINT32_MAX;
        [[nodiscard]] bool IsValid() const noexcept { return id != UINT32_MAX; }
    };

    /// @brief Links a virtual resource to how a specific pass intends to use it.
    struct PassDependency {
        RGHandle resource;
        AccessType access;
    };

    /// @brief The final output of the Render Graph compilation phase.
    /// Bundles the execution logic of a pass with the exact Vulkan synchronization 
    /// barriers required immediately before it runs.
    struct CompiledPass {
        Pass* pass;
        std::vector<VkImageMemoryBarrier2> imageBarriers;
        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    };

    /// @brief Low-level Vulkan synchronization parameters.
    struct ResourceAccessInfo {
        VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2 accessMask = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    /// @brief The "Fat Node" tracking the entire lifecycle of a virtual resource.
    struct RGPhysicalResource
    {
        Core::TextureDesc textureDesc;
        Core::BufferDesc bufferDesc;

        bool isBuffer = false;

        /// @brief If true, the graph will automatically allocate and free this resource 
        /// to recycle GPU memory. If false, it is an external resource (like the Swapchain).
        bool isTransient = true;
        bool isImported = false;

        // Handles to the physical memory allocated by the ResourceManager
        Core::TextureHandle physicalTexture;
        Core::BufferHandle physicalBuffer;

        // Direct Vulkan handles for imported external resources
        VkImage importedImage = VK_NULL_HANDLE;
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkBuffer importedBuffer = VK_NULL_HANDLE;
    };

    /// @brief The core Translation Table. Converts high-level engine intent into 
    /// strict Vulkan Synchronization2 parameters.
    /// @param access The intent of the pass.
    /// @param desc Resource metadata (used to determine defaults).
    /// @return The Vulkan Stage, Access Mask, and required Image Layout.
    inline ResourceAccessInfo GetResourceAccessInfo(AccessType access, const Core::TextureDesc& desc) {
        ResourceAccessInfo info{};

        switch (access) {
        case AccessType::ShaderRead:
            info.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;
        case AccessType::ColorAttachmentWrite:
            info.accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            break;
        case AccessType::ReadWrite:
            info.accessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            info.layout = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case AccessType::ComputeShaderRead:
            info.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;

        case AccessType::ComputeShaderWrite:
            info.accessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            info.layout = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case AccessType::TransferRead:
            info.accessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            info.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;

        case AccessType::TransferWrite:
            info.accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            info.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            break;
        case AccessType::DepthWrite:
            info.accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            info.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            break;
        case AccessType::DepthRead:
            info.accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            info.layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            break;
        case AccessType::DepthShaderRead:
            info.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
            info.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            info.layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            break;

        }

        if (info.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            info.layout = VK_IMAGE_LAYOUT_GENERAL;
        }

        return info;
    }
}
#endif