#version 450
#include "Common/CommonHelpers.glsl"
layout(location = 0) out float FragColor;
layout(location = 0) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    mat4 invViewProj;
    mat4 invProjUnjitterd;
    mat4 viewProj;
    mat4 prevViewProj;
} camera;

layout(set = 0, binding = 1) uniform SSAOKernel {
    vec4 samples[64]; 
} kernel;

layout(set = 0, binding = 2) uniform sampler2D samplerDepth;
layout(set = 0, binding = 3) uniform sampler2D samplerNormal;
layout(set = 0, binding = 4)uniform sampler2D samplerNoise;

layout(push_constant) uniform PushConstants {
    vec2 screenRes; 
    int sampleCount; 
} pc;


//https://learnopengl.com/Advanced-Lighting/SSAO

void main()
{
    float depth = texture(samplerDepth, inTexCoord).r;
    vec3 fragPos = reconstructViewPos(depth, inTexCoord, camera.invProj);
    vec3 worldNormal = normalize(texture(samplerNormal, inTexCoord).rgb);
    vec3 normal = normalize(mat3(camera.view) * worldNormal);

  
    vec2 noiseUV = gl_FragCoord.xy / 4.0;
    vec3 randomVec = texture(samplerNoise, noiseUV).xyz;

    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    float radius = 0.5;
    float bias   = 0.025;

    for (int i = 0; i < pc.sampleCount; ++i)
    {
        vec3 samplePos = TBN * kernel.samples[i].xyz;
        samplePos = fragPos + samplePos * radius;

        vec4 offset = camera.proj * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        float sampleDepth = texture(samplerDepth, offset.xy).r;
        vec3 occluderViewPos = reconstructViewPos(sampleDepth, offset.xy, camera.invProj);

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - occluderViewPos.z));
        occlusion += (occluderViewPos.z >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    FragColor = 1.0 - (occlusion / float(pc.sampleCount));
}