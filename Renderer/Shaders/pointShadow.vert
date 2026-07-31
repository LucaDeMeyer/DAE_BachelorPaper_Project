#version 450
#extension GL_EXT_multiview : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;
    vec3 tangent;
    vec3 biTangent;
};

struct InstanceData {
    mat4 model;
    uint materialID;
};

layout(set = 0, binding = 0) uniform PointShadowUBO {
    mat4 viewProj[10][6]; // [MAX_LIGHTS][NUM_FACES] -> should not be hardcoded in both shader and the rendertypes, fine for now
} ubo;

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(set = 0, binding = 1) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(push_constant) uniform PushConstants {
    uint64_t vertexAddress;
    uint instanceID;
    uint lightIndex;
} push;

void main() {
    VertexBuffer vb = VertexBuffer(push.vertexAddress);
    Vertex v = vb.vertices[gl_VertexIndex];
    InstanceData instance = instances[push.instanceID];

    mat4 currentLightViewProj = ubo.viewProj[push.lightIndex][gl_ViewIndex];

    gl_Position = currentLightViewProj * instance.model * vec4(v.position, 1.0);
}