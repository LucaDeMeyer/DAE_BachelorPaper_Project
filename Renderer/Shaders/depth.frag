#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in flat uint inMaterialID;

layout(set = 0, binding = 4) uniform sampler2D textures[];

void main()
{
    uint albedoIdx = inMaterialID * 3 + 0;
    vec4 albedo = texture(textures[nonuniformEXT(albedoIdx)], inTexCoord);
    if (albedo.a < 0.5f)
        discard;

}