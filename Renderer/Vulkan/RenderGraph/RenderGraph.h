#ifndef RENDERGRAPH_H
#define RENDERGRAPH_H
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "RenderGraphTypes.h"
#include "../Core/ResourceTypes.h"
#include <vulkan/vulkan.h>


//Render graph Sources used -> mainly based on Alie Morsey's article
//https://logins.github.io/graphics/2021/05/31/RenderGraphs.html
//https://poniesandlight.co.uk/reflect/island_rendergraph_1/
//https://www.youtube.com/watch?v=FNtMryLkQ5U -> DirectX tutorial

namespace RenderTypes
{
	struct RenderContext;
}

//https://alielmorsy.github.io/the-art-of-render-graphs/ following this article
namespace Core { class ResourceManager; }

namespace Render::Graph
{
    class RenderGraph;

    /// @brief A transient utility object passed to each RenderPass during the Setup phase.
    /// It allows passes to declare the resources they need to read from or write to.
    class RenderGraphBuilder
    {
        friend class RenderGraph;

    public:
        RenderGraphBuilder(RenderGraph* graph, uint32_t passIndex)
            : m_parentGraph(graph), m_passIndex(passIndex) {
        }

        /// @brief Declares a new virtual texture that the graph will manage.
        /// @return A virtual handle to the texture.
        [[nodiscard]] RGHandle CreateTexture(const Core::TextureDesc& desc, bool isTransient = true) const;

        /// @brief Declares a new virtual buffer that the graph will manage.
        /// @return A virtual handle to the buffer.
        [[nodiscard]] RGHandle CreateBuffer(const Core::BufferDesc& desc) const;

        /// @brief Looks up a resource that was declared by a previous pass.
        /// @param name The exact name the resource was created with.
        [[nodiscard]] RGHandle FindResource(const std::string& name) const;

        /// @brief Registers an external Vulkan buffer into the graph (e.g., from an external library).
        RGHandle RegisterImportedBuffer(VkBuffer buffer, const Core::BufferDesc& desc) const;

        /// @brief Declares this pass's intent to interact with a resource, generating a dependency.
        /// @param handle The virtual resource handle.
        /// @param access How the pass intends to use it (e.g., ShaderRead, ColorAttachmentWrite).
        void AddDependency(RGHandle handle, AccessType access);

    private:
        RenderGraph* m_parentGraph;
        uint32_t m_passIndex;
        std::vector<std::pair<RGHandle, AccessType>> _dependencies;
    };


    /// @brief The core orchestrator of the rendering pipeline. 
    /// Automatically handles pass sorting, dead code elimination, memory aliasing, and Vulkan barriers.
    class RenderGraph
    {
        friend class RenderGraphBuilder;
    public:

        /// @brief Adds a new RenderPass to the graph.
        /// @tparam PassType The specific pass class (e.g., GeometryPass, ShadowPass).
        /// @param name A debug name for the pass (shows up in RenderDoc).
        template <typename PassType, typename... Args>
        PassType& AddPass(const std::string& name, Args&&... args)
        {
            auto pass = std::make_unique<PassType>(name, std::forward<Args>(args)...);
            PassType& passRef = *pass;
            m_passes.push_back(std::move(pass));
            return passRef;
        }

        /// @brief Compiles the graph: Resolves dependencies, culls unused passes, 
        /// allocates transient memory, and generates Vulkan synchronization barriers.
        void Compile(Core::ResourceManager& resourceManager);

        /// @brief Records the compiled sequence of passes and barriers into the provided command buffer.
        void Execute(const RenderTypes::RenderContext& context);

        /// @brief Injects an existing Vulkan Image into the graph (e.g., the Swapchain image).
        RGHandle RegisterImportedImage(VkImage image, VkFormat format, VkExtent3D extent, VkImageLayout currentLayout, uint32_t mipLevels = 1, uint32_t arrayLayers = 1);

        /// @brief Injects an existing Vulkan Buffer into the graph.
        RGHandle RegisterImportedBuffer(VkBuffer buffer, const Core::BufferDesc& desc);

        /// @brief Resolves a virtual handle into the actual Vulkan Image memory.
        VkImage GetPhysicalImage(RGHandle handle, Core::ResourceManager& resManager) const;

        /// @brief Resolves a virtual handle into the actual Vulkan Buffer memory.
        VkBuffer GetPhysicalBuffer(RGHandle handle, Core::ResourceManager& resManager) const;

        /// @brief Retrieves the metadata and tracking info for a resource.
        const RGPhysicalResource& GetResource(RGHandle handle) const {
            return m_physicalResources[handle.id];
        }

        /// @brief Clears the entire graph, freeing transient memory. Used during Window Resizing.
        void Reset(Core::ResourceManager& resManager);

        //---------------------------------
        // IMGUI DEBUG RENDERING
        //---------------------------------

        /// @brief Returns the topological execution order of surviving passes
        const std::vector<uint32_t>&GetExecutionOrder() const { return m_passOrder; }

        /// @brief Fetches a specific Pass by its original index
        const Pass* GetPass(uint32_t index) const { return m_passes[index].get(); }

        /// @brief Fetches all resource dependencies (reads/writes) for a specific pass
        const std::vector<PassDependency>& GetPassDependencies(uint32_t passIndex) const {
            return m_passDependencies[passIndex];
        }

        /// @brief Fetches the underlying physical resource metadata (including its name)
        const RGPhysicalResource& GetPhysicalResource(RGHandle handle) const {
            return m_physicalResources[handle.id];
        }

        std::string GetResourceName(const RGPhysicalResource& res) {
            if (res.isBuffer) {
                return res.bufferDesc.name;
            }
            else {
                return res.textureDesc.name;
            }
        }

        void InitProfiling(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxFramesInFlight);
        void DestroyProfiling(VkDevice device);

        float GetCPUTimeMs(uint32_t passIndex) const { return m_cpuTimes[passIndex]; }
        float GetGPUTimeMs(uint32_t passIndex) const { return m_gpuTimes[passIndex]; }

    private:
        /// @brief Step 1: Discovers all virtual resources created or accessed by every registered pass.
        void setupPassesAndRecordDependencies(std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList);

        /// @brief Step 2: Calculates the exact 'birth' and 'death' pass indices for every transient resource.
        void computeResourceLifetimes(const std::vector<std::vector<std::pair<uint32_t, AccessType> > >& resourceTouchList);

        /// @brief Step 3: Evaluates Read/Write hazards to build the Directed Acyclic Graph (DAG) edges.
        void buildAdjacencyGraph(const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList, std::vector<std::unordered_set<uint32_t>>& adj, std::vector<uint32_t>& indeg);

        /// @brief Step 4: Traverses the DAG backwards from Root passes to cull dead branches.
        std::vector<bool> CullPasses(const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList);

        /// @brief Step 5: Uses Kahn's Algorithm to sort the DAG into a linear, hazard-free execution order.
        void buildTopologicalOrder(const std::vector<bool>& needed, const std::vector<std::unordered_set<uint32_t>>& adj, std::vector<uint32_t>& indeg);

        /// @brief Step 6: Recycles physical GPU memory by overlapping resources that don't share a lifespan.
        void AllocatePhysicalResources(Core::ResourceManager& resourceManager, const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList, const std::vector<bool>& needed);

        /// @brief Step 7: Compares sequential resource states to automatically generate Vulkan Synchronization2 barriers.
        void buildBarriers(Core::ResourceManager& resourceManager, const std::vector<std::vector<std::pair<uint32_t, AccessType> > >& resourceTouchList, const std::vector<bool>& needed);

      
        /// @brief Generates an Image Memory Barrier based on state transitions.
        VkImageMemoryBarrier2 MakeBarrierForResourceTransition(const RGPhysicalResource& res, VkImage actualImage, AccessType prevAccess, AccessType curAccess);

        /// @brief Generates a Buffer Memory Barrier based on access hazards.
        VkBufferMemoryBarrier2 MakeBufferBarrier(VkBuffer buffer, AccessType prevAccess, AccessType curAccess);

        /// @brief Owns the memory of all registered abstract passes.
        std::vector<std::unique_ptr<Pass>> m_passes;

        /// @brief The final, optimized execution list (contains only surviving passes and their barriers).
        std::vector<CompiledPass> m_compiledPasses;

        /// @brief "Fat Nodes" containing the metadata and physical handles for every resource.
        std::vector<RGPhysicalResource> m_physicalResources;

        /// @brief Maps string names (e.g., "GBuffer_Albedo") to virtual integer handles.
        std::unordered_map<std::string, RGHandle> m_resourceRegistry;

        /// @brief Tracks what resources each pass requested during Setup().
        std::vector<std::vector<PassDependency>> m_passDependencies;

        /// @brief Timelines for memory aliasing: The topological index where a resource is first used.
        std::vector<uint32_t> m_resourceFirstUse;

        /// @brief Timelines for memory aliasing: The topological index where a resource is last used.
        std::vector<uint32_t> m_resourceLastUse;

        /// @brief A cached list of Image Barriers specifically required *before* a specific pass index runs.
        std::vector<std::vector<VkImageMemoryBarrier2>> m_barriersPerPass;

        /// @brief The final sequence of pass indices determined by the Topological Sort.
        std::vector<uint32_t> m_passOrder;


        VkQueryPool m_queryPool = VK_NULL_HANDLE;
        float m_timestampPeriod = 1.0f;
        uint32_t m_maxFramesInFlight = 2;
        uint32_t m_maxQueriesPerFrame = 100;

        std::vector<float> m_cpuTimes;
        std::vector<float> m_gpuTimes;
        std::vector<bool> m_frameQueriesValid;
    };
}
#endif