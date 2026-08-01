
set(SOURCE_FILES
main.cpp
Renderer.cpp
Application.cpp
Vulkan/Core/Allocator.cpp 
Vulkan/Core/Buffer.cpp 
Vulkan/Core/GraphicsContext.cpp 
Vulkan/Core/Image.cpp 
Vulkan/Core/Pipeline.cpp 
Vulkan/Core/Swapchain.cpp 
Vulkan/Utils/Uitls.cpp
Vulkan/Core/DescriptorSet.cpp
Vulkan/RenderGraph/RenderGraph.cpp
Vulkan/Core/ResourceManager.cpp 
Vulkan/Core/Object.cpp 
Vulkan/Core/TextureLoader.cpp
Vulkan/Passes/DepthPrePass.cpp
Vulkan/Passes/GeometryPass.cpp
Vulkan/Core/Camera.cpp
Vulkan/Passes/DefferedLightingPass.cpp
Vulkan/Passes/ToneMappingPass.cpp
Vulkan/Passes/HistogramPass.cpp
Vulkan/Utils/IBLBaker.cpp
Vulkan/Passes/ShadowPass.cpp
UI/UILayer.cpp
UI/UIPanels.cpp
Vulkan/Passes/SSAOPass.cpp
Vulkan/Passes/PointShadowPass.cpp
Vulkan/Core/AccelerationStructure.cpp)

set(HEADER_FILES
Renderer.h
Application.h
Vulkan/Core/Allocator.h
Vulkan/Core/Buffer.h 
Vulkan/Core/DescriptorSet.h
Vulkan/Core/GraphicsContext.h 
Vulkan/Core/Image.h 
Vulkan/Core/Pipeline.h 
Vulkan/Core/Swapchain.h 
Vulkan/Utils/Utils.h
Vulkan/Core/FrameData.h 
Vulkan/RenderGraph/Pass.h 
Vulkan/RenderGraph/RenderGraph.h
Vulkan/RenderGraph/RenderGraphTypes.h Vulkan/Core/ResourceManager.h 
Vulkan/Core/ResourceTypes.h
Vulkan/Core/Object.h
Vulkan/Core/Scene.h 
Vulkan/Core/TextureLoader.h
Vulkan/Passes/ClearPass.h 
Vulkan/Passes/DepthPrePass.h
Vulkan/Passes/BlitToSwapchainPass.h
Vulkan/Passes/GeometryPass.h
Vulkan/Core/Camera.h
Vulkan/Passes/DefferedLightingPass.h
Vulkan/Passes/ToneMappingPass.h
Vulkan/Passes/HistogramPass.h
Vulkan/Utils/IBLBaker.h
Vulkan/Passes/ShadowPass.h
UI/UILayer.h
UI/UIPanels.h
Vulkan/Passes/SSAOPass.h
Vulkan/Passes/PointShadowPass.h
Vulkan/Core/AccelerationStructure.h)



add_executable(Renderer ${SOURCE_FILES} ${HEADER_FILES} )



if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Renderer PROPERTY CXX_STANDARD 20)
endif()

include (FetchContent)

FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm
	GIT_TAG 1.0.3
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw
	GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    stb 
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG master 
)

FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

FetchContent_Declare(
    vma 
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(vma) 

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp
    GIT_TAG v6.0.4
)
FetchContent_MakeAvailable(assimp) 

FetchContent_Declare(
    asset_repo
    GIT_REPOSITORY https://github.com/LucaDeMeyer/GP2_Resources
    GIT_TAG main
)
FetchContent_GetProperties(asset_repo)
if(NOT asset_repo_POPULATED)
    FetchContent_Populate(asset_repo)
endif()

message(STATUS "Asset Repo successfully downloaded to: ${asset_repo_SOURCE_DIR}")

find_package(Vulkan REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG v1.92.8
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PRIVATE Vulkan::Vulkan glfw)

target_include_directories(
Renderer
PRIVATE
${Vulkan_INCLUDE_DIRS}
${CMAKE_CURRENT_SOURCE_DIR}
${glm_SOURCE_DIR}
${glfw_SOURCE_DIR}
${stb_SOURCE_DIR}
${vma_SOURCE_DIR}/include
${assimp_SOURCE_DIR}/include
)

target_link_libraries(Renderer PRIVATE glm glfw Vulkan::Vulkan assimp imgui)

find_program(GLSLC glslc PATHS $ENV{VULKAN_SDK}/Bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found!")
endif()


set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Shaders)

file(GLOB_RECURSE SHADER_FILES
    "${SHADER_DIR}/*.vert"
    "${SHADER_DIR}/*.frag"
    "${SHADER_DIR}/*.comp"
    "${SHADER_DIR}/*.geom"
)
set(SPIRV_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_DIR})
 
set(COMPILED_SHADERS "")
foreach(SHADER IN LISTS SHADER_FILES)
    get_filename_component(FILE_NAME ${SHADER} NAME)
    
    set(SPIRV_FILE "${SPIRV_DIR}/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${GLSLC} --target-env=vulkan1.3 ${SHADER} -o ${SPIRV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader: ${FILE_NAME} -> .spv"
    )
    list(APPEND COMPILED_SHADERS ${SPIRV_FILE})
endforeach()

add_custom_command(TARGET Renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${asset_repo_SOURCE_DIR}" 
        "$<TARGET_FILE_DIR:Renderer>/models"
        
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${asset_repo_SOURCE_DIR}/Textures" 
        "$<TARGET_FILE_DIR:Renderer>/textures"
        
  #  COMMAND ${CMAKE_COMMAND} -E copy_directory 
  #      "${asset_repo_SOURCE_DIR}/HDRI" 
  #      "$<TARGET_FILE_DIR:GP2_Renderer>/hdris"
        
    COMMENT "Copying all external assets next to the executable..."
)
add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADERS})
add_dependencies(Renderer CompileShaders)

target_compile_definitions(Renderer PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)

