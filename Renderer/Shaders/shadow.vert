#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_shader_viewport_layer_array : require


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

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(set = 0, binding = 0) uniform CascadeUBO {
    mat4 lightSpaceMatrices[4];
    vec4 splitDepths;
} cascades;

layout(set = 0, binding = 1) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(push_constant) uniform PushConstants {
    uint64_t vertexAddress;
    uint32_t instanceID;
};

void main() {
    VertexBuffer vb = VertexBuffer(vertexAddress);
    Vertex v        = vb.vertices[gl_VertexIndex];
    InstanceData i  = instances[instanceID];

    // We issue a draw call with an instance count of 4 (one for each cascade).
    // The GL_ARB_shader_viewport_layer_array extension allows us to write directly to 
    // gl_Layer in the Vertex Shader. This routes the triangle to the correct slice of 
    // the shadow map array, completely eliminating the severe performance penalty of 
    // using a Geometry Shader to duplicate triangles!
    gl_Layer    = gl_InstanceIndex;
    gl_Position = cascades.lightSpaceMatrices[gl_InstanceIndex]
                * i.model
                * vec4(v.position, 1.0);
}