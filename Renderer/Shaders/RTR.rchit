#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

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

// CORRECTED: Restored the 'Vertex' struct array and 'scalar' layout!
layout(set = 2, binding = 4, scalar) readonly buffer VertexBuffer { Vertex vertices[]; } globalVertices;
layout(set = 2, binding = 5, scalar) readonly buffer IndexBuffer { uint indices[]; } globalIndices;

layout(set = 0, binding = 4) uniform sampler2D bindlessTextures[]; 

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

struct InstanceData {
    mat4 model;
    uint materialID;
    uint firstIndex;
    uint vertexOffset;
    uint padding;
};

// CORRECTED: Binding 2 (Rasterizer Buffer)
layout(set = 0, binding = 2, std430) readonly buffer InstanceBuffer { InstanceData instances[]; } globalInstances;

struct PointLight {
    vec4 position; 
    vec4 color;    
};

struct DirectionalLight {
    vec4 direction;  
    vec4 color;      
};

layout(set = 0, binding = 3) readonly buffer LightData {
    DirectionalLight dirLight;
    uint pointLightCount;
    float padding[3];
    PointLight pointLights[];
} sceneLights;

void main()
{
    // 1. Get Instance & Material Data
    uint instID = gl_InstanceCustomIndexEXT;
    uint matIndex = globalInstances.instances[instID].materialID;
    uint firstIndex = globalInstances.instances[instID].firstIndex;

    // 2. Fetch Indices
    uint indexOffset = firstIndex + (gl_PrimitiveID * 3);
    uint i0 = globalIndices.indices[indexOffset + 0];
    uint i1 = globalIndices.indices[indexOffset + 1];
    uint i2 = globalIndices.indices[indexOffset + 2];

    // 3. Fetch Vertices 
    Vertex v0 = globalVertices.vertices[i0];
    Vertex v1 = globalVertices.vertices[i1];
    Vertex v2 = globalVertices.vertices[i2];

    // 4. Calculate UVs & Barycentrics
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 hitUV = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

    // 5. Calculate World Normals for Lighting
    vec3 objNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    vec3 worldNormal = normalize(vec3(objNormal * gl_WorldToObjectEXT)); 

    // 6. Fetch Material and Albedo Texture
    Material mat = globalMaterials.materials[matIndex];
    vec3 albedo = mat.baseColor.rgb;
    
    if (mat.albedoTexIdx >= 0) {
        albedo *= texture(bindlessTextures[nonuniformEXT(mat.albedoTexIdx)], hitUV).rgb;
    }

    // 7. LIGHTING!
    vec3 L_dir = sceneLights.dirLight.direction.xyz;
    
    // Safety fallback in case the sun is accidentally 0,0,0
    if (length(L_dir) < 0.0001) L_dir = vec3(0.5, -1.0, 0.5); 
    
    vec3 L = normalize(-L_dir);
    float NdotL = max(dot(worldNormal, L), 0.0); 
    
    // Fetch sun color and intensity
    vec3 radiance = sceneLights.dirLight.color.rgb * sceneLights.dirLight.direction.w;
    
    // If the sun is off, give it a bright default so we can see the reflections!
    if (length(radiance) < 0.001) radiance = vec3(5.0); 

    // Add a bright ambient baseline (0.15) so the dark green pops back to full color
    vec3 finalColor = albedo * ((radiance * NdotL) + vec3(0.15));
    
    // Safety Clamp for SVGF
    finalColor = clamp(finalColor, vec3(0.0), vec3(65000.0)); 

    hitValue = vec4(finalColor, 1.0);
}