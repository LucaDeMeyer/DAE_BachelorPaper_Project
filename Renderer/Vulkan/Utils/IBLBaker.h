#ifndef IBL_BAKER_H
#define IBL_BAKER_H
#include "../Core/ResourceManager.h"
#include "../RenderGraph/RenderGraphTypes.h"
#include "../Core/Pipeline.h"
#include <vulkan/vulkan.h>

namespace Core
{
    struct TextureData;
}

/// @brief A container holding the virtual handles for a complete Image-Based Lighting setup.
/// These four textures contain pre-calculated lighting data required to shade PBR materials accurately.
struct IBLTextures {
    /// @brief The base HDR environment mapped onto a 3D Cubemap (used for the Skybox).
    Core::TextureHandle environmentCubemap;

    /// @brief A heavily blurred cubemap containing pre-calculated Diffuse lighting (Lambertian).
    Core::TextureHandle irradianceCubemap;

    /// @brief A cubemap with mip-levels containing varying levels of blur for Specular reflections.
    /// Mip 0 is sharp (smooth mirror), Mip 4 is highly blurred (rough concrete).
    Core::TextureHandle prefilteredCubemap;

    /// @brief A 2D Look-Up Texture that stores the BRDF integration map (The "Split-Sum Approximation").
    Core::TextureHandle brdfLUT;
};

/// @brief A dedicated Compute utility for generating Image-Based Lighting (IBL) assets.
/// It takes a standard 2D Equirectangular HDR panorama (.hdr) and dispatches a sequence of 
/// compute shaders to bake the complex mathematical integrals required for real-time PBR.
class IBLBaker final
{
public:
    /// @brief Initializes the baker and compiles the necessary Compute Pipelines.
    IBLBaker(Core::GraphicsContext* context, Core::ResourceManager* resManager);

    ~IBLBaker();

    /// @brief The main execution sequence. Loads a 2D HDR file from disk and bakes all PBR maps.
    /// @param hdrFilePath The relative path to the .hdr file (e.g., "assets/textures/sky.hdr").
    /// @return A struct containing the handles to the newly generated GPU textures.
    IBLTextures BakeEnvironment(const std::string& hdrFilePath, Core::TextureHandle existingBrdfLut = {});

private:
    // These functions act as individual "Passes" recorded into a single command buffer.

    /// @brief Allocates the raw Vulkan memory and Image Views for the cubemaps and LUT.
    Core::TextureHandle AllocateIBLTextures(const Core::TextureData& hdrData, IBLTextures& results, Core::TextureHandle existingBrdfLut);

    /// @brief Copies the raw 2D pixel data from the CPU staging buffer into the GPU's memory.
    void CopyHDRToGPU(VkCommandBuffer cmd, VkBuffer stagingBuffer, Core::ImageBuilder::Image* hdrImage, uint32_t width, uint32_t height);

    /// @brief Compute Pass 1: Warps the 2D Equirectangular image onto the 6 faces of a 3D Cubemap.
    void BakeEquirectangularToCubemap(VkCommandBuffer cmd, Core::ImageBuilder::Image* hdrImage, Core::ImageBuilder::Image* cubeImage, uint32_t mipLevels);

    /// @brief Compute Pass 2: Solves the Diffuse integral by taking thousands of samples across the hemisphere.
    void BakeIrradiance(VkCommandBuffer cmd, Core::ImageBuilder::Image* cubeImage, Core::ImageBuilder::Image* irrImage);

    /// @brief Compute Pass 3: Solves the Specular integral for varying roughness levels, storing them in mip-maps.
    void BakePrefilter(VkCommandBuffer cmd, Core::ImageBuilder::Image* cubeImage, Core::ImageBuilder::Image* prefilterImage, Core::TextureHandle prefilterHandle, uint32_t mipLevels);

    /// @brief Compute Pass 4: Pre-calculates the geometry and Fresnel terms of the BRDF equation into a 2D texture.
    void BakeBRDFLUT(VkCommandBuffer cmd, Core::ImageBuilder::Image* brdfImage);

    /// @brief Sets up Descriptor Layouts and compiles the SPIR-V compute shaders.
    void InitPipelines();

    Core::GraphicsContext* m_context;
    Core::ResourceManager* m_resourceManager;

    Core::PipelineBuilder::Pipeline* m_equirectangularPipeline;
    Core::PipelineBuilder::Pipeline* m_irradiancePipeline;
    Core::PipelineBuilder::Pipeline* m_preFilterdPipeline;
    Core::PipelineBuilder::Pipeline* m_brdfLutPipline;

    Core::DescriptorBuilder::DescriptorSet m_equirectSet;
    Core::DescriptorBuilder::DescriptorSet m_irradianceSet;
    Core::DescriptorBuilder::DescriptorSet m_prefilterSet;
    Core::DescriptorBuilder::DescriptorSet m_brdfLUTSet;
};
#endif