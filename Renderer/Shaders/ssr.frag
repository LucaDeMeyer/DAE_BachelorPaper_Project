#version 460

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outSSR;

// --- Bindings ---
layout(set = 0, binding = 0) uniform sampler2D samplerDepth;
layout(set = 0, binding = 1) uniform sampler2D samplerNormal;
layout(set = 0, binding = 2) uniform sampler2D samplerMaterial; // R=Metallic, G=Roughness
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

// --- Helpers ---
vec3 getViewPos(vec2 uv, float depth) {
    vec4 clipSpace = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewSpace = camera.invProj * clipSpace;
    return viewSpace.xyz / viewSpace.w;
}

void main() 
{
    float depth = texture(samplerDepth, inUV).r;
    
    // 1. Skip the Skybox
    if (depth >= 0.9999) { 
        outSSR = vec4(0.0);
        return;
    }

    vec4 material = texture(samplerMaterial, inUV);
    float metallic = material.r;
    float roughness = material.g;

    // 2. Early out for rough surfaces (Massive performance save!)
    if (roughness > 0.4) {
        outSSR = vec4(0.0);
        return;
    }

    // 3. Reconstruct View-Space Data
    vec3 viewPos = getViewPos(inUV, depth);
    
    // Convert World Normal to View Normal
    vec3 worldNormal = normalize(texture(samplerNormal, inUV).xyz);
    vec3 viewNormal = normalize(mat3(camera.view) * worldNormal);

    // Calculate Reflection Vector
    vec3 viewDir = normalize(viewPos); // Ray from camera to pixel
    vec3 reflectDir = normalize(reflect(viewDir, viewNormal));

    // Prevent rays from pointing back at the camera (self-intersection)
    if (reflectDir.z > 0.0) {
        outSSR = vec4(0.0);
        return;
    }

    // 4. Raymarching Parameters
    const int maxSteps = 60;
    const float stepSize = 0.25;
    const float thickness = 0.5; // How thick a surface is assumed to be for collisions

    vec3 rayPos = viewPos;
    vec2 hitUV = vec2(0.0);
    float hitMask = 0.0;

    // 5. The Raymarch Loop
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

        // Calculate absolute depths (Vulkan view Z is negative, so absolute is easier to read)
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

    // 6. Output Color & Blend Mask
    if (hitMask > 0.0) {
        vec3 reflectionColor = texture(samplerPrevColor, hitUV).rgb;
        
        // Fade out naturally based on material roughness
        float roughnessFade = 1.0 - smoothstep(0.2, 0.4, roughness);
        
        // Output RGB color, and Alpha as the blend factor
        outSSR = vec4(reflectionColor, hitMask * roughnessFade);
    } else {
        outSSR = vec4(0.0);
    }
}