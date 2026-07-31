#ifndef RENDERPASS_H
#define RENDERPASS_H
#include <string>
#include <vulkan/vulkan.h>

namespace RenderTypes
{
    struct RenderContext;
}
namespace Core {
    class ResourceManager;
}

namespace Render::Graph
{
    class RenderGraphBuilder;
    class RenderGraph;

    /// @brief The abstract base class for all stages in the rendering pipeline.
    /// To create a new rendering effect (e.g., ShadowMapping, PostProcessing), 
    /// inherit from this class and register it with the RenderGraph.
    class Pass
    {
    public:
        /// @brief Constructs a Render Pass with a descriptive name.
        /// @param name The name used for Vulkan Debug Labels and profiling (e.g., RenderDoc).
        explicit Pass(const std::string& name) : m_Name(name) {}

        virtual ~Pass() = default;

        /// @brief The Declarative Phase. Called once during graph compilation.
        /// @param builder Use the builder to declare which virtual textures/buffers this pass 
        /// will create, read from, or write to. 
        /// @note Do NOT execute Vulkan commands or bind pipelines in this function!
        virtual void Setup(RenderGraphBuilder& builder) = 0;

        /// @brief The Execution Phase. Called by the RenderGraph during command recording.
        /// @param cmd The active Vulkan command buffer. Record your draw calls and dispatches here.
        /// @param graph The executing graph, used to retrieve actual physical Vulkan images/buffers.
        /// @param resManager Global resource manager for accessing shaders, pipelines, and meshes.
        virtual void Execute(const RenderTypes::RenderContext& context,RenderGraph& graph) = 0;

        /// @brief Retrieves the debug name of the pass.
        const std::string& GetName() const { return m_Name; }

        /// @brief Determines if this pass can be safely culled by the Render Graph.
        /// @return True if this pass writes to the screen or has external side effects. 
        /// False if it only generates intermediate data. 
        /// @note If False, and no subsequent pass reads this pass's output, the Render Graph 
        /// will completely skip allocating memory for it and will not call its Execute() function!
        virtual bool HasSideEffect() {
            return true; // Default to true so new passes aren't accidentally culled
        }

    private:
        std::string m_Name;
    };
}
#endif