#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "Common/LightingHelpers.glsl"
#include "Common/CommonHelpers.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;
    vec3 tangent;
    vec3 biTangent;
};

struct PointLight {
    vec4 position; // xyz = world pos, w = radius
    vec4 color;    // xyz = color,     w = luminance
};

struct DirectionalLight {
  vec4 direction;  // xyz = direction, w = lux
  vec4 color;      // xyz = color,     w = unused
};

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    mat4 invViewProj;
    mat4 invProjUnjittered;
    mat4 viewProj;
    mat4 prevViewProj;
} camera;

layout(set = 0, binding = 3) readonly buffer LightData {
    DirectionalLight dirLight;
    uint pointLightCount;
    float padding[3];
    PointLight pointLights[];
} sceneLights;


 layout(push_constant) uniform DebugPushConstBlock {
   int debugMode;
   int useRTShadows;
   int usePostDenoising;
}debugPushConst;

layout(set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 1) uniform sampler2D samplerNormal;
layout(set = 1, binding = 2) uniform sampler2D samplerMaterial;
layout(set = 1, binding = 3) uniform sampler2D samplerDepth;

layout(set = 1, binding = 4) uniform samplerCube environmentMap;
layout(set = 1, binding = 5) uniform samplerCube irradianceMap;

layout(set = 1, binding = 6) uniform samplerCube prefilterMap; 
layout(set = 1, binding = 7) uniform sampler2D brdfLUT;

layout(set = 1, binding = 8) uniform sampler2DArrayShadow shadowMap;

layout(set = 1, binding = 9) uniform sampler2D ssao;

layout(set = 1, binding = 10) uniform CascadeUBO {
    mat4 lightSpaceMatrices[4];
    vec4 splitDepths;
} cascades;

layout(set = 1, binding = 11) uniform samplerCubeShadow pointShadowMaps[10];

layout(set = 1, binding = 12) uniform sampler2D rtShadowMask;

layout(set = 1, binding =13) uniform sampler2DArray rtPointShadowMask;

layout(set = 1, binding = 14) uniform sampler2D ssrMask;
 
float CalculateShadow(vec3 worldPos, vec3 N, vec3 L,vec2 uv)
{

if (debugPushConst.useRTShadows == 1) {
        // Just fetch the pre-calculated ray-traced mask!
        return texture(rtShadowMask, uv).r;
    }

    float depth = -(camera.view * vec4(worldPos, 1.0)).z;
    // Determine which shadow map slice to use based on camera distance
    uint cascadeIndex = 0;
    for (uint i = 0; i < 3; i++) {
        if (depth > cascades.splitDepths[i])
            cascadeIndex = i + 1;
    }

    // Project into light space
    vec4 lightSpacePos = cascades.lightSpaceMatrices[cascadeIndex] * vec4(worldPos, 1.0);
    lightSpacePos.xyz /= lightSpacePos.w;

    // Vulkan NDC to [0,1]
    vec2 shadowUV = lightSpacePos.xy * 0.5 + 0.5;
    float currentDepth = lightSpacePos.z;

    // Clamp to valid range
    if (currentDepth > 1.0 || any(lessThan(shadowUV, vec2(0.0))) || any(greaterThan(shadowUV, vec2(1.0))))
        return 1.0;

    // Slope scaled bias
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.0005);

    // PCF — 3x3 kernel using sampler2DArrayShadow
    // By using 'sampler2DArrayShadow' instead of 'sampler2DArray', we tell the Vulkan 
    // driver to use the GPU's fixed-function texture filtering hardware. 
    // The 'texture()' call automatically performs the depth comparison (currentDepth < shadowDepth) 
    // in silicon and returns a smooth [0.0 to 1.0] visibility float, rather than returning a raw depth value.
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            // sampler2DArrayShadow automatically performs the depth comparison in hardware!
            shadow += texture(shadowMap, vec4(
                shadowUV + offset,
                float(cascadeIndex),
                currentDepth - bias));
        }
    }
    shadow /= 9.0;
    return shadow;
}

float CalculatePointShadow(vec3 worldPos, PointLight light, vec3 N, uint lightIndex,vec2 uv)
{

    if (debugPushConst.useRTShadows == 1) {
       
        return texture(rtPointShadowMask, vec3(uv, float(lightIndex))).r;
    }

    vec3 fragToLight = worldPos - light.position.xyz;
    float currentDist = length(fragToLight);
    
    float zFar = 25.0; 
    if (currentDist > zFar) {
        return 1.0; 
    }

    vec3 sampleDir = normalize(fragToLight);

   //Slope-Scaled Bias (World-space meters)
    vec3 L = -normalize(fragToLight);
    float NdotL = max(dot(N, L), 0.0);
    float bias = max(0.25 * (1.0 - NdotL), 0.05);
    
    vec3 absVec = abs(fragToLight);
    float planarDepth = max(max(absVec.x, absVec.y), absVec.z);
    float biasedPlanarDepth = planarDepth - bias;

  
    float zNear = 0.1; 
    float referenceDepth = (zFar / (zFar - zNear)) - ((zFar * zNear) / ((zFar - zNear) * biasedPlanarDepth));
    referenceDepth = clamp(referenceDepth, 0.0, 1.0); // Safety clamp
    
    float isLit = texture(pointShadowMaps[nonuniformEXT(lightIndex)], vec4(sampleDir, referenceDepth));

   float falloff = 1.0 - smoothstep(zFar * 0.9, zFar, currentDist);
    return mix(0.0, isLit, falloff);
}

void main()
{
    vec4 albedo = texture(samplerAlbedo, inUV);
    vec3 N = normalize(texture(samplerNormal, inUV).xyz);
    vec4 material = texture(samplerMaterial, inUV);
    float depth = texture(samplerDepth, inUV).r;
    float occlusion = texture(ssao,inUV).r;

    if (debugPushConst.debugMode == 1) {
        outColor = albedo;
        return; 
    }
    else if (debugPushConst.debugMode == 2) {
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    } 
    else if (debugPushConst.debugMode == 3) {
        outColor = vec4(material.r, material.g, 0.0, 1.0);
        return;
    }
    else if (debugPushConst.debugMode == 4) {
        //Reconstruct the position relative to the camera
        vec3 viewpos = reconstructViewPos(depth,inUV,camera.invProj);
        
        // Grab the true linear distance from the camera (in meters)
        // (View space looks down the -Z axis, so we invert it to be positive)
        float linearDepth = length(viewpos.xyz);
        // Divide by a "Max Visible Distance" to map it to a 0.0 -> 1.0 color range
        float maxDistance = 50.0; 
        float normalizedDepth = clamp(linearDepth / maxDistance, 0.0, 1.0);
        
        // Output as a grayscale gradient!
        outColor = vec4(vec3(normalizedDepth), 1.0);
        return;
    }
    else if(debugPushConst.debugMode == 5)
    {
        outColor = vec4(vec3(occlusion), 1.0);
        return;
    }

    else if (debugPushConst.debugMode == 6) {
    outColor = texture(ssrMask, inUV);
    return;
}

  if (depth >= 1.0) {
    vec2 ndc = inUV * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, 1.0, 1.0);
    vec4 viewPos = camera.invProjUnjittered * clipPos;
    viewPos.w = 0.0;
    vec3 worldDir = normalize((camera.invView * viewPos).xyz);
    
    vec3 envColor = texture(environmentMap, worldDir).rgb;
    
  
   outColor = vec4(envColor, 1.0);
    return;
}

    // We do NOT store World Position in the G-Buffer. Storing a vec4(x,y,z,w) of 
    // 32-bit floats for every pixel would cost an extra 16 bytes per pixel.
    // At 4K resolution, that wastes 132 Megabytes of VRAM bandwidth per frame
    // Instead, we mathematically reconstruct it from the Depth buffer and UV coordinates 
    // using the Inverse View-Projection matrix, trading cheap ALU cycles for expensive memory bandwidth.
    vec3 worldPos =  reconstructWorldPos(depth, inUV, camera.invViewProj);
    vec3 viewPos = (camera.invView * vec4(0, 0, 0, 1)).xyz;
    vec3 V = normalize(viewPos - worldPos);
  


    float roughness = material.g;
    float metallic  = material.r;

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo.rgb, metallic);

    vec3 finalRadiance = vec3(0.0);
    vec3 Lo = vec3(0.0);


    vec3 L = normalize(-sceneLights.dirLight.direction.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = sceneLights.dirLight.color.rgb * sceneLights.dirLight.direction.w;

    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;	  

    float NdotL = max(dot(N, L), 0.0);        
    float shadow = CalculateShadow(worldPos, N, L, inUV);
    Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL * shadow;

    for (uint i = 0; i < sceneLights.pointLightCount; i++) {
    PointLight light = sceneLights.pointLights[i];
        
        vec3 L_unnorm = light.position.xyz - worldPos;
        float trueDist = length(L_unnorm);
        
        // Safeguard normalization
        vec3 L = L_unnorm / max(trueDist, 0.0001); 
        vec3 H = normalize(V + L); 
        
        float lumen = light.color.w; 
        
        // Prevent division by zero at dead center
        float dist2 = max(trueDist * trueDist, 0.0001); 
        
        float I = lumen / (4.0 * PI); // Convert Lumens to Candelas (Intensity)

        float E = I / dist2;          // Convert Candelas to Lux (Illuminance)
       
        // Calculate shadows early and bake them directly into the radiance
        float shadow = CalculatePointShadow(worldPos, light,N, i,inUV);
        vec3 radiance = light.color.rgb * E * shadow;


        float safeRoughness = max(roughness, 0.045);
        float NDF = DistributionGGX(N, H, safeRoughness);   
        float G   = GeometrySmith(N, V, L, safeRoughness);      
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);            
        
        vec3 numerator    = NDF * G * F; 
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;        

        float NdotL = max(dot(N, L), 0.0);        

        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
}


    vec3 kS_IBL = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD_IBL = (1.0 - kS_IBL) * (1.0 - metallic);

    vec3 irradiance  = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL  = irradiance * albedo.rgb;

    vec3 R = reflect(-V, N);
    float MAX_REFLECTION_LOD = 4.0; 

    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    
    vec4 rtrResult = texture(ssrMask, inUV); 

    float rtWeight = 1.0 - smoothstep(0.2, 0.4, roughness);

    vec3 finalReflectionColor = mix(prefilteredColor, rtrResult.rgb, rtWeight);

    vec2 envBRDF = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = finalReflectionColor * (kS_IBL * envBRDF.x + envBRDF.y);

    vec3 ambient = (kD_IBL * diffuseIBL + specularIBL) * occlusion;

   vec3 color = Lo + ambient;

    if (debugPushConst.usePostDenoising == 1) {
     
        vec3 demodulatedLighting = color / max(albedo.rgb, vec3(0.005));
        outColor = vec4(demodulatedLighting, 1.0); 
    } else {
        outColor = vec4(color, 1.0);
    }
}