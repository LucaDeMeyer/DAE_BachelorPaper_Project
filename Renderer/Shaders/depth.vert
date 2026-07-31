#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
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

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    mat4 invViewProj;
} camera;

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(set = 0, binding = 2) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(push_constant) uniform PushConstants {
    uint64_t vertexAddress;
      uint instanceID;
}pushConstants;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out flat uint outMaterialID;

void main()
{
    VertexBuffer vb = VertexBuffer(pushConstants.vertexAddress);
    Vertex v = vb.vertices[gl_VertexIndex];
    InstanceData i = instances[pushConstants.instanceID];

    gl_Position    = camera.proj * camera.view * i.model * vec4(v.position, 1.0);
    outTexCoord    = v.texCoord;
    outMaterialID  = i.materialID;
}