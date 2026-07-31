#ifndef COMMON_HELPERS_GLSL
#define COMMON_HELPERS_GLSL
vec3 reconstructViewPos(float depth, vec2 uv, mat4 invProj)
{
    vec2 ndc_xy = uv * 2.0 - 1.0;
    float ndc_z = depth;

    vec4 clipPos = vec4(ndc_xy, ndc_z, 1.0);
   
    vec4 viewPos = invProj * clipPos; 

    return viewPos.xyz / viewPos.w;
}

vec3 reconstructWorldPos(float depth, vec2 uv, mat4 invViewProj)
{
   vec2 ndc_xy = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0));
    float ndc_z = depth; 

    vec4 clipPos = vec4(ndc_xy, ndc_z, 1.0);
    
    vec4 worldPosFull = invViewProj * clipPos;

    return worldPosFull.xyz / worldPosFull.w;
}
#endif