#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in flat uint inMaterialID;

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
    
    if (mat.albedoTexIdx >= 0) {
        vec4 albedo = texture(textures[nonuniformEXT(mat.albedoTexIdx)], inTexCoord);
        if (albedo.a < 0.1) {
            discard;
        }
    }
}