#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;   
layout(location = 3) in vec3 inBitangent;  
layout(location = 4) in flat uint inMaterialID;

layout(location = 5) in vec4 inCurrClipPos;
layout(location = 6) in vec4 inPrevClipPos;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec2 outVelocity;


layout(set = 0, binding = 4) uniform sampler2D textures[];

struct Material {
    vec4 baseColor;
    int albedoTexIdx;
    int normalTexIdx;
    int metallicRoughnessTexIdx;
    float metallic;
    float roughness;
    float padding1, padding2, padding3;
};

layout(set = 0, binding = 11, std430) readonly buffer MaterialBuffer { 
    Material materials[]; 
} globalMaterials;

void main()
{

    Material mat = globalMaterials.materials[inMaterialID];

  
    vec4 albedo = mat.baseColor;
    if (mat.albedoTexIdx >= 0) {
        albedo *= texture(textures[nonuniformEXT(mat.albedoTexIdx)], inTexCoord);
    }
    
    if (albedo.a < 0.1) discard;
    outAlbedo = albedo;


    if (mat.normalTexIdx >= 0) {
        vec3 localNormal = texture(textures[nonuniformEXT(mat.normalTexIdx)], inTexCoord).rgb;
        localNormal = normalize(localNormal * 2.0 - 1.0); 
        vec3 T = normalize(inTangent);
        vec3 B = normalize(inBitangent);
        vec3 N = normalize(inNormal);
        mat3 TBN = mat3(T, B, N);
        outNormal = vec4(normalize(TBN * localNormal), 1.0);
    } else {
  
        outNormal = vec4(normalize(inNormal), 1.0);
    }

   
    float finalMetallic = mat.metallic;
    float finalRoughness = mat.roughness; 

    if (mat.metallicRoughnessTexIdx >= 0) {
        vec4 mr = texture(textures[nonuniformEXT(mat.metallicRoughnessTexIdx)], inTexCoord);
        finalRoughness *= mr.g; 
        finalMetallic *= mr.b; 
    }
    outMaterial = vec4(finalMetallic, finalRoughness, 0.0, 1.0); 


    vec2 currNDC = inCurrClipPos.xy / inCurrClipPos.w;
    vec2 prevNDC = inPrevClipPos.xy / inPrevClipPos.w;
    vec2 currUV = currNDC * 0.5 + 0.5;
    vec2 prevUV = prevNDC * 0.5 + 0.5;
    outVelocity = currUV - prevUV;
}