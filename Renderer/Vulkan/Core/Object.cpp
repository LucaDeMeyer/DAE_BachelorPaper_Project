#include "Object.h"
#include <assimp/Importer.hpp>
#include <assimp/cimport.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <iostream>

#include "Allocator.h"
#include "TextureLoader.h"
#include "Vulkan/Utils/Utils.h"
#include "Vulkan/Core/GraphicsContext.h"
#include "Vulkan/Core/ResourceManager.h"

std::vector<Core::Mesh> Core::MeshLoader::LoadFromFile(GraphicsContext& ctx, const std::string& filename, Core::ResourceManager& resManager)
{
   Assimp::Importer importer;
   const aiScene* scene = importer.ReadFile(filename.c_str(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals  | aiProcess_CalcTangentSpace);
   if (!scene || !scene->mRootNode) {
       throw std::runtime_error("Assimp Error: " + std::string(importer.GetErrorString()));
   }
   std::string directory = Utils::GetDirectoryPath(filename);
   std::vector<Mesh> allMeshes;
   for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
       allMeshes.push_back(ProcessAssimpMesh(ctx, scene->mMeshes[i], scene, directory, resManager));
       allMeshes.back().UpLoadToGPU(ctx, resManager);
   }
   std::function<void(aiNode*, glm::mat4)> traverseNodes = [&](aiNode* node, glm::mat4 parentTransform) {
       glm::mat4 nodeTransform;
       for (int r = 0; r < 4; ++r)
           for (int c = 0; c < 4; ++c)
               nodeTransform[c][r] = node->mTransformation[r][c];

       glm::mat4 globalTransform = parentTransform * nodeTransform;
       for (unsigned int i = 0; i < node->mNumMeshes; i++) {
           uint32_t meshIdx = node->mMeshes[i];
           allMeshes[meshIdx].builtInTransform = globalTransform;
       }

       for (unsigned int i = 0; i < node->mNumChildren; i++) {
           traverseNodes(node->mChildren[i], globalTransform);
       }
       };

   traverseNodes(scene->mRootNode, glm::mat4(1.0f));

   return allMeshes;
}

Core::Mesh Core::MeshLoader::ProcessAssimpMesh(
	Core::GraphicsContext& ctx,aiMesh* assimpMesh,const aiScene* scene,const std::string& directory,ResourceManager& resManager)
    {
        Mesh mesh;

        mesh.vertices.reserve(assimpMesh->mNumVertices);
        mesh.indices.reserve(assimpMesh->mNumFaces * 3);
        mesh.minBounds = glm::vec3(std::numeric_limits<float>::max());
        mesh.maxBounds = glm::vec3(std::numeric_limits<float>::lowest());

        if (assimpMesh->mName.length > 0) {
            mesh.name = assimpMesh->mName.C_Str();
        }
        else {
            mesh.name = "Unnamed_Mesh";
        }
        for (unsigned int i = 0; i < assimpMesh->mNumVertices; i++) {
            RenderTypes::Vertex vertex{};

            vertex.position = { assimpMesh->mVertices[i].x, assimpMesh->mVertices[i].y, assimpMesh->mVertices[i].z };

            if (assimpMesh->HasNormals()) {
                vertex.normal = { assimpMesh->mNormals[i].x, assimpMesh->mNormals[i].y, assimpMesh->mNormals[i].z };
            }
            else {
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }


            mesh.minBounds = glm::min(mesh.minBounds, vertex.position);
            mesh.maxBounds = glm::max(mesh.maxBounds, vertex.position);

            if (assimpMesh->HasVertexColors(0)) {
                vertex.color = { assimpMesh->mColors[0][i].r, assimpMesh->mColors[0][i].g, assimpMesh->mColors[0][i].b };
            }
            else {
                vertex.color = glm::vec3(1.0f);
            }

            if (assimpMesh->HasTextureCoords(0)) {
                vertex.texCoord = { assimpMesh->mTextureCoords[0][i].x, assimpMesh->mTextureCoords[0][i].y };
            }
            else {
                vertex.texCoord = glm::vec2(0.0f);
            }

            if (assimpMesh->HasTangentsAndBitangents())
            {
                vertex.tangent = { assimpMesh->mTangents[i].x,assimpMesh->mTangents[i].y,assimpMesh->mTangents[i].z };
                vertex.biTangent = { assimpMesh->mBitangents[i].x,assimpMesh->mBitangents[i].y,assimpMesh->mBitangents[i].z };
            }
            else
            {
                vertex.tangent = { 1.0f, 0.0f, 0.0f };
                vertex.biTangent = { 0.0f, 1.0f, 0.0f }; // we dont want to zero these since it will mess up lighting if no tangents/bitangents are present
            }

            mesh.vertices.push_back(vertex);
        }
        for (unsigned int i = 0; i < assimpMesh->mNumFaces; i++) {
            const aiFace& face = assimpMesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                mesh.indices.push_back(face.mIndices[j]);
            }
        }

        mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
        mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());

        if (assimpMesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[assimpMesh->mMaterialIndex];

            // ALBEDO
            if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
                aiString texPath;
                material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
                std::string tex = SanitizeTexturePath(texPath.C_Str());
                std::filesystem::path fullPath = std::filesystem::path(directory) / tex;
                fullPath = fullPath.lexically_normal();

                mesh.material.albedoPath = fullPath.string();
                mesh.material.albedoMap = TextureLoader::LoadFromFile(ctx, resManager, mesh.material.albedoPath, VK_FORMAT_R8G8B8A8_SRGB);
            }

            // NORMALS
            if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
                aiString texPath;
                material->GetTexture(aiTextureType_NORMALS, 0, &texPath);
                std::string tex = SanitizeTexturePath(texPath.C_Str());
                std::filesystem::path fullPath = std::filesystem::path(directory) / tex;
                fullPath = fullPath.lexically_normal();

                mesh.material.normalPath = fullPath.string();
                mesh.material.normalMap = TextureLoader::LoadFromFile(ctx, resManager, mesh.material.normalPath, VK_FORMAT_R8G8B8A8_UNORM);
            }

            // METALLIC ROUGHNESS
            if (material->GetTextureCount(aiTextureType_METALNESS) > 0) {
                aiString texPath;
                material->GetTexture(aiTextureType_METALNESS, 0, &texPath);
                std::string tex = SanitizeTexturePath(texPath.C_Str());
                std::filesystem::path fullPath = std::filesystem::path(directory) / tex;
                fullPath = fullPath.lexically_normal();

                mesh.material.metallicRoughnessPath = fullPath.string();
                mesh.material.metallicRoughnessMap = TextureLoader::LoadFromFile(ctx, resManager, mesh.material.metallicRoughnessPath, VK_FORMAT_R8G8B8A8_UNORM);
            }
            aiColor3D color;
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
                mesh.material.baseColor = glm::vec3(color.r, color.g, color.b);
            }

            float metallic = 0.0f;
            material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
            mesh.material.metallic = metallic;

            float roughness = 0.5f;
            material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
            mesh.material.roughness = roughness;
        }

        return mesh;
    }
        void Core::Mesh::UpLoadToGPU(Core::GraphicsContext & ctx, Core::ResourceManager & resManager)
    {
        if (vertices.empty()) {
            throw std::runtime_error("Attempted to upload empty mesh!");
        }
        vertexCount = static_cast<uint32_t>(vertices.size());
        indexCount = static_cast<uint32_t>(indices.size());
        resManager.AppendMeshToGlobalBuffer(ctx,vertices,indices,this->firstIndex,  this->vertexOffset );

        vertices.clear();
        vertices.shrink_to_fit();
        indices.clear();
        indices.shrink_to_fit();
    }
