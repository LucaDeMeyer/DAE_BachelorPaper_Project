#version 460

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outSSR;

layout(set = 0, binding = 0) uniform sampler2D samplerDepth;
layout(set = 0, binding = 1) uniform sampler2D samplerNormal;
layout(set = 0, binding = 2) uniform sampler2D samplerMaterial; 
layout(set = 0, binding = 3) uniform sampler2D samplerPrevColor;

layout(set = 0, binding = 4) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    mat4 invViewProj;
    mat4 invProjUnjittered;
    mat4 viewProj;
    mat4 prevViewProj;
} camera;

vec3 getViewPos(vec2 uv, float depth) {
    vec4 clipSpace = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewSpace = camera.invProj * clipSpace;
    return viewSpace.xyz / viewSpace.w;
}

layout(push_constant) uniform PushConstants {
    int sampleCount; 
} pc;


void main() 
{
    float depth = texture(samplerDepth, inUV).r;
    
    if (depth >= 0.9999) { 
        outSSR = vec4(0.0);
        return;
    }

    vec4 material = texture(samplerMaterial, inUV);
    float metallic = material.r;
    float roughness = material.g;

    if (roughness > 0.4) {
        outSSR = vec4(0.0);
        return;
    }

    vec3 viewPos = getViewPos(inUV, depth);
    
    vec3 worldNormal = normalize(texture(samplerNormal, inUV).xyz);
    vec3 viewNormal = normalize(mat3(camera.view) * worldNormal);

    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, viewNormal));

    if (reflectDir.z > 0.0) {
        outSSR = vec4(0.0);
        return;
    }

    const int maxSteps =pc.sampleCount;
    const float MAX_DISTANCE = 8.0; 
    float stepSize = MAX_DISTANCE / float(pc.sampleCount);
    const float thickness = 0.5; // How thick a surface is assumed to be for collisions

    vec3 rayPos = viewPos;
    vec2 hitUV = vec2(0.0);
    float hitMask = 0.0;

    for (int i = 0; i < maxSteps; i++) {
        // Step forward in view space
        rayPos += reflectDir * stepSize;

        // Project ray back to screen space to read the depth buffer
        vec4 clipPos = camera.proj * vec4(rayPos, 1.0);
        vec3 ndcPos = clipPos.xyz / clipPos.w;
        vec2 screenUV = ndcPos.xy * 0.5 + 0.5;

        // Stop if the ray goes off-screen
        if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0) {
            break;
        }

        // Fetch the depth buffer at this UV
        float sampleDepth = texture(samplerDepth, screenUV).r;
        vec3 sampleViewPos = getViewPos(screenUV, sampleDepth);

        float rayZ = abs(rayPos.z);
        float sampleZ = abs(sampleViewPos.z);

        // Check for collision: Is our ray deeper than the depth buffer, but within the thickness threshold?
        if (rayZ > sampleZ && (rayZ - sampleZ) < thickness) {
            hitUV = screenUV;
            
            // Fade out the reflection smoothly at the edges of the screen
            vec2 edgeFade = smoothstep(vec2(0.0), vec2(0.1), hitUV) * 
                            (1.0 - smoothstep(vec2(0.9), vec2(1.0), hitUV));
            
            hitMask = edgeFade.x * edgeFade.y;
            break;
        }
    }

    if (hitMask > 0.0) {
        vec3 reflectionColor = texture(samplerPrevColor, hitUV).rgb;
        
        // Fade out  based on  roughness
        float roughnessFade = 1.0 - smoothstep(0.2, 0.4, roughness);
 
        outSSR = vec4(reflectionColor, hitMask * roughnessFade);
    } else {
        outSSR = vec4(0.0);
    }
}