#ifndef GRAPHICS_CONTEXT_H
#define GRAPHICS_CONTEXT_H

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h> 
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <memory>

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif


extern PFN_vkCmdBeginDebugUtilsLabelEXT ext_vkCmdBeginDebugUtilsLabelEXT;
extern PFN_vkCmdEndDebugUtilsLabelEXT ext_vkCmdEndDebugUtilsLabelEXT;

namespace Core
{
    class GraphicsContextBuilder;
    class Allocator;

    struct GraphicsContext final
    {

        // We explicitly delete copy and move semantics. Vulkan handles (like VkDevice) 
        // are pointers to physical hardware state. If this class were accidentally copied, 
        // two objects would try to destroy the exact same GPU resources, causing a fatal crash.
        GraphicsContext(const GraphicsContext&) = delete;
        GraphicsContext& operator=(const GraphicsContext&) = delete;
        GraphicsContext(const GraphicsContext&&) = delete;
        GraphicsContext& operator=(const GraphicsContext&&) = delete;

        ~GraphicsContext() = default;

        /// @brief The Vulkan library instance. Ties the application to the Vulkan runtime.
        VkInstance getInstance() const { return m_instance; }

        /// @brief The logical device. Used to allocate memory, create pipelines, and dispatch commands.
        VkDevice getDevice() const { return m_device; }

        /// @brief The physical graphics card. Used to query hardware capabilities and format support.
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }

        /// @brief The hardware queue used to submit drawing and compute commands.
        VkQueue getGraphicsQueue() const { return m_graphicsQueue; }

        /// @brief The hardware queue used to communicate with the OS window for Swapchain presentation.
        VkQueue getPresentQueue() const { return m_presentQueue; } // Note: Fixed the return variable here!

        /// @brief The memory pool used to allocate Command Buffers for this specific thread.
        VkCommandPool getCommandPool() const { return m_commandPool; }

        /// @brief The global Vulkan Memory Allocator (VMA) instance used to manage VRAM.
        Allocator* getAllocator() const { return m_allocator.get(); }

        /// @brief The OS-specific window surface (e.g., Win32, Wayland, X11) we are rendering into.
        VkSurfaceKHR getSurface() const { return m_surface; }

        /// @brief Safely waits for the GPU to idle, then destroys all core Vulkan handles.
        void Shutdown();
     
    private:
        friend class GraphicsContextBuilder;
        GraphicsContext() = default;

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_presentQueue = VK_NULL_HANDLE;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;

        std::unique_ptr<Allocator> m_allocator;
    };

    /// @brief Implements the Builder Pattern to construct the immutable GraphicsContext.
        /// Abstracts away the massive amount of Vulkan boilerplate required to initialize the API.
    class GraphicsContextBuilder // we are wasting 4 bytes of padding here -> since only 1 of these exist in any situation its not that big of a deal
    {
    public:
        /// @brief Binds the Vulkan rendering surface to a specific OS window.
        GraphicsContextBuilder& setWindow(GLFWwindow* window) {
            m_window = window;
            return *this;
        }

        /// @brief Sets the application name (Visible to GPU driver profiles and debugging tools).
        GraphicsContextBuilder& setAppName(const std::string& name) {
            m_appName = name;
            return *this;
        }

        /// @brief Requests a specific Vulkan validation layer (e.g., "VK_LAYER_KHRONOS_validation").
        GraphicsContextBuilder& addValidationLayer(const char* layerName) {
            m_validationLayers.push_back(layerName);
            return *this;
        }

        /// @brief Requests a specific device extension (e.g., VK_KHR_SWAPCHAIN_EXTENSION_NAME).
        GraphicsContextBuilder& addDeviceExtension(const char* extensionName) {
            m_deviceExtensions.push_back(extensionName);
            return *this;
        }

        /// @brief Sets the target Vulkan API version (e.g., VK_API_VERSION_1_3).
        GraphicsContextBuilder& SetApiVersion(uint32_t apiversion)
        {
            m_apiVersion = apiversion;
            return *this;
        }

        /// @brief Executes the initialization sequence and returns a locked-down GraphicsContext.
        std::unique_ptr<GraphicsContext> build();

    private:
        GLFWwindow* m_window = nullptr;
        std::string m_appName = "Vulkan Renderer";
        std::vector<const char*> m_validationLayers;
        std::vector<const char*> m_deviceExtensions;

        uint32_t m_apiVersion = VK_API_VERSION_1_3;

        /// @brief Verifies that the requested validation layers exist on the host machine.
        bool checkValidationLayerSupport() const;

        /// @brief Gathers mandatory extensions (like GLFW windowing and Debug Utils).
        std::vector<const char*> getRequiredExtensions() const;

        /// @brief Configures the severity and types of warnings the validation layers will catch.
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

        /// @brief Evaluates a physical GPU to ensure it supports the queues, swapchain, and features we need.
        bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) const;

        /// @brief Initializes the Vulkan library (VkInstance).
        void createInstance(GraphicsContext& ctx);

        /// @brief Hooks up the validation layer callbacks for debugging.
        void setupDebugMessenger(GraphicsContext& ctx);

        /// @brief Bridges the OS window (GLFW) to the Vulkan API.
        void createSurface(GraphicsContext& ctx);

        /// @brief Scores and selects the best available physical GPU.
        void pickPhysicalDevice(GraphicsContext& ctx);

        /// @brief Creates the logical device and retrieves the hardware queues.
        void createLogicalDevice(GraphicsContext& ctx);

        /// @brief Initializes the Vulkan Memory Allocator (VMA).
        void createAllocator(GraphicsContext& ctx);

        /// @brief Creates the command pool for allocating rendering command buffers.
        void createCommandPool(GraphicsContext& ctx);
    };
}
#endif