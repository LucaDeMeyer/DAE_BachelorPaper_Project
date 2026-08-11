#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require

#include "Common/LightingHelpers.glsl"


layout(location = 0) rayPayloadInEXT vec3 hitPayload;

layout(location = 1) rayPayloadEXT float shadowFactor;

layout(set = 1, binding = 0) uniform accelerationStructureEXT topLevelAS;

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
    vec3 worldPos =
        gl_WorldRayOriginEXT +
        gl_WorldRayDirectionEXT * gl_HitTEXT;

    vec3 normal = vec3(0.0, 1.0, 0.0);

    vec3 lightDir =
        normalize(-sceneLights.dirLight.direction.xyz);

    float NoL = dot(normal, lightDir);

    float shadowVal = 1.0;

    if (NoL > 0.0)
    {
        vec3 origin =
            worldPos +
            normal * 0.002 +
            lightDir * 0.005;

        shadowFactor = 1.0;

        uint rayFlags =
            gl_RayFlagsTerminateOnFirstHitEXT |
            gl_RayFlagsOpaqueEXT |
            gl_RayFlagsSkipClosestHitShaderEXT;

        traceRayEXT(
            topLevelAS,
            rayFlags,
            0xFF,
            0, 0, 0,
            origin,
            0.001,
            lightDir,
            1000.0,
            1
        );

        shadowVal = shadowFactor;
    }
    else
    {
        shadowVal = 0.0;
    }

    hitPayload.x = shadowVal;
}