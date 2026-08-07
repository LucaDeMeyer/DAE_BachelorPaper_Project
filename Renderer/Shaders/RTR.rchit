#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) rayPayloadInEXT vec4 hitValue;
hitAttributeEXT vec2 attribs; // Barycentrics

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;
    vec3 tangent;
    vec3 biTangent;
};

// --- Your Global Geometry ---
layout(set = 2, binding = 4, std430) readonly buffer VertexBuffer { Vertex vertices[]; } globalVertices;
layout(set = 2, binding = 5, std430) readonly buffer IndexBuffer { uint indices[]; } globalIndices;

// --- Bindless Textures & Materials ---
layout(set = 0, binding = 4) uniform sampler2D bindlessTextures[]; 

// MUST MATCH C++ GPUMaterial EXACTLY
struct Material {
    vec4 baseColor;
    int albedoTexIdx;
    int normalTexIdx;
    int metallicRoughnessTexIdx;
    float metallic;
    float roughness;
    float padding1, padding2, padding3;
};
layout(set = 0, binding = 11, std430) readonly buffer MaterialBuffer { Material materials[]; } globalMaterials;

// MUST MATCH C++ InstanceData EXACTLY
struct InstanceData {
    mat4 model;
    uint materialID;
    uint padding1, padding2, padding3;
};
layout(set = 0, binding = 12, std430) readonly buffer InstanceBuffer { InstanceData instances[]; } globalInstances;

// --- Lighting Data ---
struct DirectionalLight {
    vec4 direction; 
    vec4 color;     
};
layout(set = 0, binding = 3) readonly buffer LightData {
    DirectionalLight dirLight;
} sceneLights;

void main()
{
    // 1. Get the Triangle
    uint indexOffset = gl_PrimitiveID * 3;
    uint i0 = globalIndices.indices[indexOffset + 0];
    uint i1 = globalIndices.indices[indexOffset + 1];
    uint i2 = globalIndices.indices[indexOffset + 2];

    Vertex v0 = globalVertices.vertices[i0];
    Vertex v1 = globalVertices.vertices[i1];
    Vertex v2 = globalVertices.vertices[i2];

    // 2. Barycentric Interpolation
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    vec3 hitNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    vec2 hitUV = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

    // 3. Look up the Material (Notice we use materialID to match the C++ struct)
    uint matIndex = globalInstances.instances[gl_InstanceCustomIndexEXT].materialID;
    Material mat = globalMaterials.materials[matIndex];

    // 4. Resolve Albedo safely!
    vec3 albedo = mat.baseColor.rgb;
    if (mat.albedoTexIdx >= 0) {
        // We only sample the texture if it actually exists!
        albedo *= texture(bindlessTextures[nonuniformEXT(mat.albedoTexIdx)], hitUV).rgb;
    }

    // 5. Cheap Directional Lighting
    vec3 L = normalize(-sceneLights.dirLight.direction.xyz);
    float NdotL = max(dot(hitNormal, L), 0.1); 
    
    vec3 radiance = sceneLights.dirLight.color.rgb * sceneLights.dirLight.direction.w;
    vec3 finalColor = albedo * radiance * NdotL;

    // Return the lit, textured color!
    hitValue = vec4(finalColor, 1.0);
}