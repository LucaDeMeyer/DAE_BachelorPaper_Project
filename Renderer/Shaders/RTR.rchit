#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_ray_query : require // REQUIRED FOR INLINE SHADOWS

layout(location = 0) rayPayloadInEXT vec4 hitValue;
hitAttributeEXT vec2 attribs; // Barycentrics

// We need the BVH to cast our shadow ray!
layout(set = 1, binding = 0) uniform accelerationStructureEXT topLevelAS;

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;
    vec3 tangent;    
    vec3 biTangent;
};

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
    uint instID = gl_InstanceCustomIndexEXT;
    uint matIndex = globalInstances.instances[instID].materialID;
    uint firstIndex = globalInstances.instances[instID].firstIndex;

    uint indexOffset = firstIndex + (gl_PrimitiveID * 3);
    uint i0 = globalIndices.indices[indexOffset + 0];
    uint i1 = globalIndices.indices[indexOffset + 1];
    uint i2 = globalIndices.indices[indexOffset + 2];

    Vertex v0 = globalVertices.vertices[i0];
    Vertex v1 = globalVertices.vertices[i1];
    Vertex v2 = globalVertices.vertices[i2];

    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 hitUV = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

    vec3 objNormal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    float nLen = length(objNormal);
    if (nLen > 0.001) {
        objNormal = objNormal / nLen;
    } else {
        objNormal = vec3(0.0, 1.0, 0.0);
    }
    vec3 worldNormal = normalize(vec3(objNormal * gl_WorldToObjectEXT)); 

    Material mat = globalMaterials.materials[matIndex];
    vec3 albedo = mat.baseColor.rgb;
    
    if (mat.albedoTexIdx >= 0) {
        albedo *= texture(bindlessTextures[nonuniformEXT(mat.albedoTexIdx)], hitUV).rgb;
    }

    vec3 L_dir = sceneLights.dirLight.direction.xyz;
    if (length(L_dir) < 0.0001) L_dir = vec3(0.5, -1.0, 0.5); 
    
    vec3 L = normalize(-L_dir);
    float NdotL = max(dot(worldNormal, L), 0.0); 

    // --- NEW: INLINE SHADOW RAY ---
    float shadowFactor = 1.0;
    
    // Only cast a shadow ray if the surface actually faces the sun
    if (NdotL > 0.0) {
        vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
        vec3 shadowOrigin = hitPos + (worldNormal * 0.01); // Bias to prevent self-shadowing

        rayQueryEXT shadowQuery;
        rayQueryInitializeEXT(
            shadowQuery, 
            topLevelAS, 
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 
            0xFF, 
            shadowOrigin, 
            0.001, 
            L, 
            10000.0
        );

        while (rayQueryProceedEXT(shadowQuery)) {}

        if (rayQueryGetIntersectionTypeEXT(shadowQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
            shadowFactor = 0.0; // We hit something, the sun is blocked!
        }
    }
    
    vec3 radiance = sceneLights.dirLight.color.rgb * sceneLights.dirLight.direction.w;
    if (length(radiance) < 0.001) radiance = vec3(5.0); 

    // Multiply the radiance by our new shadowFactor
    vec3 finalColor = albedo * ((radiance * NdotL * shadowFactor) + vec3(0.15));

    float maxSafeHDR = 100.0; 
    finalColor = clamp(finalColor, vec3(0.0), vec3(maxSafeHDR)); 

    hitValue = vec4(finalColor, 1.0);
}