#ifndef OBJECT_H
#define OBJECT_H
#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtx/hash.hpp>

#include "RenderTypes.h"
#include "ResourceTypes.h"

struct aiMesh;
struct aiScene;

namespace Core
{
	struct GraphicsContext;
	class ResourceManager;
    /// @brief Defines the visual surface properties for Physically Based Rendering (PBR).
	struct Material {
        std::string albedoPath;
        std::string normalPath;
        std::string metallicRoughnessPath;

        TextureHandle albedoMap;
        TextureHandle normalMap;
        TextureHandle metallicRoughnessMap;

        glm::vec3 baseColor = glm::vec3(1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
    };

    /// @brief The raw geometry data uploaded to the GPU Mega-Buffers.
    struct Mesh {
        uint32_t firstIndex = 0;      
        int32_t vertexOffset = 0;

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        uint32_t materialIndex = 0;

        std::vector<RenderTypes::Vertex> vertices;
        std::vector<uint32_t> indices;

        glm::vec3 minBounds{ 0.0f };
        glm::vec3 maxBounds{ 0.0f };

        Material material;
        std::string name;

        int bindlessID = -1;
        glm::mat4 builtInTransform{ 1.0f };

        void UpLoadToGPU(GraphicsContext& ctx, ResourceManager& resManager);
    };

    namespace MeshLoader
    {
        std::vector<Core::Mesh> LoadFromFile(GraphicsContext& ctx, const std::string& filename, Core::ResourceManager& resManager);
        Mesh ProcessAssimpMesh(GraphicsContext& ctx,aiMesh* assimpMesh,const aiScene* scene,const std::string& directory,ResourceManager& resManager);

        static std::string SanitizeTexturePath(const std::string& p)
        {
            std::string out = p;
            while (!out.empty() && (out[0] == '/' || out[0] == '\\'))
                out.erase(out.begin());
            std::replace(out.begin(), out.end(), '\\', '/');
            return out;
        }
    }

    /// @brief An instance of a mesh existing in the 3D world with its own transform.
    class Object final {
    public:
        Object() = default;
        explicit Object(Mesh* m, std::string _modelPath, const glm::mat4& t = glm::mat4(1.0f))
            : mesh(m), modelPath(_modelPath), transform(t), localTransform(t) {
        }

        Mesh* mesh = nullptr;

        glm::vec3 position{ 0.0f, 0.0f, 0.0f };
        glm::vec3 rotation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

        glm::mat4 localTransform;
        glm::mat4 transform{ 1.0f };

        std::string modelPath;
        std::string name;

        void UpdateLocalTransform() {
            glm::mat4 matTranslate = glm::translate(glm::mat4(1.0f), position);

            glm::mat4 matRotate = glm::mat4(1.0f);
            matRotate = glm::rotate(matRotate, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            matRotate = glm::rotate(matRotate, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            matRotate = glm::rotate(matRotate, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            glm::mat4 matScale = glm::scale(glm::mat4(1.0f), scale);
            localTransform = matTranslate * matRotate * matScale * mesh->builtInTransform;
        }

        Object* SetPosition(const glm::vec3& pos) { position = pos; UpdateLocalTransform(); return this; }
        Object* SetRotation(const glm::vec3& rot) { rotation = rot; UpdateLocalTransform(); return this; }
        Object* SetScale(const glm::vec3& s) { scale = s; UpdateLocalTransform(); return this; }
    };
}

#endif
