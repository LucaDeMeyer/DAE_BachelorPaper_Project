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

void main()
{
    uint albedoIdx   = inMaterialID * 3 + 0;
    uint normalIdx   = inMaterialID * 3 + 1;
    uint materialIdx = inMaterialID * 3 + 2;

    vec4 albedo = texture(textures[nonuniformEXT(albedoIdx)], inTexCoord);
    if (albedo.a < 0.1)
        discard;
    outAlbedo = albedo;
    // Normal maps are authored in "Tangent Space" (where Z is always pointing straight 
    // out from the surface, meaning blue = vec3(0.5, 0.5, 1.0)).
    // We construct a TBN (Tangent, Bitangent, Normal) matrix to rotate that flat normal 
    // map out into 3D World Space so the lighting pass can calculate reflections correctly.
  vec3 localNormal = texture(textures[nonuniformEXT(normalIdx)], inTexCoord).rgb;
    localNormal = normalize(localNormal * 2.0 - 1.0); // Unpack [0, 1] texture to [-1, 1] vector
    vec3 T = normalize(inTangent);
    vec3 B = normalize(inBitangent);
    vec3 N = normalize(inNormal);
    mat3 TBN = mat3(T, B, N);

    vec3 worldNormal = normalize(TBN * localNormal);

    outNormal = vec4(worldNormal, 1.0);

    // GLTF standard packs Roughness into Green and Metallic into Blue
    vec4 mr = texture(textures[nonuniformEXT(materialIdx)], inTexCoord);
    outMaterial = vec4(mr.b, mr.g, 0.0, 1.0); 

    vec2 currNDC = inCurrClipPos.xy / inCurrClipPos.w;
    vec2 prevNDC = inPrevClipPos.xy / inPrevClipPos.w;

    vec2 currUV = currNDC * 0.5 + 0.5;
    vec2 prevUV = prevNDC * 0.5 + 0.5;

    outVelocity = currUV - prevUV;
  
}