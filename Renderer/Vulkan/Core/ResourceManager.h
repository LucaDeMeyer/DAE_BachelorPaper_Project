#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <vector>
#include <unordered_map>
#include <string>

#include "Image.h"
#include "Buffer.h"
#include "DescriptorSet.h"
#include "Pipeline.h"
#include "ResourceTypes.h" 

namespace Core
{
    static constexpr uint32_t MAX_BINDLESS_TEXTURES = 5000;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
    static constexpr uint32_t MAX_POINT_LIGHTS = 10;

    struct GraphicsContext;

    /// @brief The central hub for all GPU memory allocation and tracking.
    /// Handles persistent assets (Mega-Buffers, global descriptors) and transient 
    /// frame-graph resources (Memory Aliasing / Pooling).
    class ResourceManager final
    {
    public:
        explicit ResourceManager(GraphicsContext& ctx);
        ~ResourceManager() = default;

        GraphicsContext& GetContext() const { return context; }

        /// @brief Allocates a permanent texture (e.g., loaded from disk) that lives indefinitely.
        TextureHandle CreateTexture(const TextureDesc& desc);

        /// @brief Requests a temporary texture for the Render Graph. 
        /// If an unused texture of the exact same size/format exists in the pool, it is recycled.
        /// Otherwise, a new physical texture is allocated.
        TextureHandle AcquireTransientTexture(const TextureDesc& desc);

        /// @brief Returns a transient texture to the pool so it can be reused by a later Render Pass.
        void ReleaseTransientTexture(TextureHandle handle, const TextureDesc& desc);

        /// @brief Resolves a virtual handle into the physical Vulkan Image object.
        ImageBuilder::Image* GetTexture(TextureHandle handle);

        
        ///@brief checks if a texture of a certain handle already exists in the pool
        bool HasTexture(TextureHandle handle);

        /// @brief Lazily creates and caches a Vulkan ImageView for a specific mip level of a 2D texture.
        VkImageView GetMipView(TextureHandle handle, uint32_t mipLevel);

        /// @brief Lazily creates and caches a Vulkan ImageView for a specific mip level of a Cubemap.
        VkImageView GetMipViewCube(TextureHandle handle, uint32_t mipLevel);

        /// @brief Explicitly destroys a persistent texture and releases its VRAM immediately.
		/// Leaves its handle slot empty to preserve overall handle alignment.
        void DestroyTexture(TextureHandle handle);
        //should look into a freelist so we dont leave tombstones behind, could be a problem as this project grows.

        /// @brief Allocates a permanent buffer (e.g., static SSBOs or persistent Uniforms).
        BufferHandle CreateBuffer(const BufferDesc& desc);

        /// @brief Requests a temporary buffer for the Render Graph, utilizing the free pool if possible.
        BufferHandle AcquireTransientBuffer(const BufferDesc& desc);

        /// @brief Returns a temporary buffer to the free pool for immediate reuse.
        void ReleaseTransientBuffer(BufferHandle handle, const BufferDesc& desc);

        /// @brief Resolves a virtual handle into the physical Vulkan Buffer object.
        BufferBuilder::Buffer* GetBuffer(BufferHandle handle);

        Core::BufferHandle GetExposureBuffer() const { return m_exposureBuffer; }


        /// @brief Explicitly destroys a persistent buffer and releases its memory immediately.
        void DestroyBuffer(BufferHandle handle);
        /// @brief Allocates the massive unified global buffers for all geometry in the scene.
        /// This allows us to bind the Vertex/Index buffers exactly once per frame.
        void InitGlobalGeometryBuffers(GraphicsContext& ctx, uint32_t maxVertices, uint32_t maxIndices);

        /// @brief Appends a mesh's data to the Mega-Buffer.
        /// @param outFirstIndex Returns the starting index offset for this specific mesh's vkCmdDrawIndexed call.
        /// @param outVertexOffset Returns the starting vertex offset to shift the indices correctly.
        void AppendMeshToGlobalBuffer(GraphicsContext& ctx, const std::vector<RenderTypes::Vertex>& vertices, const std::vector<uint32_t>& indices, uint32_t& outFirstIndex, int32_t& outVertexOffset);

        VkBuffer GetGlobalVertexBuffer();
        VkBuffer GetGlobalIndexBuffer();


        /// @brief Destroys all pooled transient resources. Usually called on window resize.
        void FlushFreePools();

        PipelineBuilder::Pipeline* CreateGraphicsPipeline(const std::string& name, const GraphicsPipelineConfig& config);
        PipelineBuilder::Pipeline* CreateComputePipeline(const std::string& name, const ComputePipelineConfig& config);
        PipelineBuilder::Pipeline* GetPipeline(const std::string& name) const;

        // Shared static samplers so we don't spam the GPU with duplicate sampler objects
        VkSampler GetPointSampler() const { return m_PointSampler; }
        VkSampler GetLinearSampler() const { return m_LinearSampler; }
        VkSampler GetShadowSampler() const { return m_ShadowSampler; }
        VkSampler GetPointRepeatSampler() const { return m_PointRepeatSampler; }

        /// @brief Allocates the standard UBOs and SSBOs needed by the global rendering system.
        void InitGlobalBuffers();

        /// @brief Binds the global buffers and bindless texture arrays into a single Descriptor Set 0.
        void InitGlobalDescriptorSet();

        Core::DescriptorBuilder::DescriptorSet& GetGlobalDescriptorSet() { return m_globalDescriptorSet; }

        std::vector<Core::BufferHandle> GetCameraUBO() { return m_cameraUBOs; }
        std::vector<Core::BufferHandle> GetInstanceSSBOs() { return m_instanceSSBOs; }
        std::vector<Core::BufferHandle> GetLightSSBOs() { return m_LightSSBOs; }
        std::vector<Core::BufferHandle> GetCascadeUBOs() { return m_CascadeUBOs; }
        std::vector < Core::BufferHandle> GetPointShadowUBOs() { return m_PointShadowUBOs; }

        void SetFrameIDX(uint32_t newidx) { currentFrameIDX = newidx; }
        uint32_t GetFrameIndex() { return currentFrameIDX; }

        void Shutdown();

        void UpdateDeltaTime(float newtime) { m_DeltaTime = newtime; }
        float GetDeltaTime() const { return m_DeltaTime; }

    private:
        GraphicsContext& context;

        uint32_t currentFrameIDX = 0;

        uint32_t m_pointLightMatrixCount = 0;

        // Core physical resource storage
        std::vector<ImageBuilder::Image> m_textures;
        std::vector<BufferBuilder::Buffer> m_buffers;

        // Cached specific Mip Level views
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, VkImageView>> m_mipViews;
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, VkImageView>> m_cubeMipViews;

        std::unordered_map<std::string, std::unique_ptr<PipelineBuilder::Pipeline>> m_pipelines;

        // Maps a unique string (derived from format/extent/usage) to a list of currently unused resources.
        std::unordered_map<std::string, std::vector<TextureHandle>> m_transientTexturePool;
        std::unordered_map<std::string, std::vector<BufferHandle>> m_transientBufferPool;

        BufferHandle m_globalVertexBuffer;
        BufferHandle m_globalIndexBuffer;
        BufferHandle m_exposureBuffer;

        uint32_t m_globalVertexCapacity = 0;
        uint32_t m_globalIndexCapacity = 0;
        uint32_t m_currentVertexCount = 0;
        uint32_t m_currentIndexCount = 0;

      
        std::vector<Core::BufferHandle> m_cameraUBOs;
        std::vector<Core::BufferHandle> m_instanceSSBOs;
        std::vector<Core::BufferHandle> m_LightSSBOs;
        std::vector<Core::BufferHandle> m_CascadeUBOs;
        std::vector<Core::BufferHandle> m_PointShadowUBOs;

        Core::DescriptorBuilder::DescriptorSet m_globalDescriptorSet;

        VkSampler m_PointSampler = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;
        VkSampler m_ShadowSampler = VK_NULL_HANDLE;
        VkSampler m_PointRepeatSampler = VK_NULL_HANDLE;
        float m_DeltaTime = 0;

        /// @brief Hashes resource properties into a string key to find matching free resources in the pool.
        std::string GenerateTextureKey(const TextureDesc& desc);
        std::string GenerateBufferKey(const BufferDesc& desc);
    };
}
#endif