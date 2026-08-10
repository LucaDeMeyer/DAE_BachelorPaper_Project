#version 450
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
    uint firstIndex;
    uint vertexOffset;
    uint padding;
};
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

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};


layout(set = 0, binding = 2) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(push_constant) uniform PushConstants {
    uint64_t vertexAdress;
      uint instanceID;
}pushConstants;


layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outTangent;   
layout(location = 3) out vec3 outBitangent;
layout(location = 4) out flat uint outMaterialID;
layout(location = 5) out vec4 outCurrClipPos;
layout(location = 6) out vec4 outPrevClipPos;

void main()
{
    VertexBuffer vb = VertexBuffer(pushConstants.vertexAdress);
    Vertex v = vb.vertices[gl_VertexIndex];
    InstanceData i = instances[pushConstants.instanceID];

    mat4 unjitteredProj = inverse(camera.invProjUnjitterd);
    mat4 unjitteredViewProj = unjitteredProj * camera.view;

    vec4 worldPos = i.model * vec4(v.position, 1.0);

    outCurrClipPos = unjitteredViewProj * worldPos;
    outPrevClipPos = camera.prevViewProj * worldPos;

   gl_Position = camera.proj * camera.view * i.model * vec4(v.position, 1.0);

    outTexCoord  = v.texCoord;

    // If a 3D model is scaled non-uniformly (e.g., squashed into a pancake), simply 
    // multiplying the Normal by the Model Matrix will cause the normal to skew incorrectly, 
    // ruining the lighting. We must multiply normals by the Inverse-Transpose of the 
    // Model Matrix to preserve perpendicularity. 
    // (Optimization Note: In a production engine, this matrix is usually pre-calculated 
    // on the CPU and passed in the InstanceData to save GPU cycles)
    mat3 normalMatrix = transpose(inverse(mat3(i.model)));

    vec3 T = normalize(normalMatrix * v.tangent);
    vec3 B = normalize(normalMatrix * v.biTangent);
    vec3 N = normalize(normalMatrix * v.normal);

    // When 3D modeling software exports a mesh, averaging vertex tangents across 
    // curved surfaces often causes the Tangent and Normal vectors to drift so they 
    // are no longer perfectly 90-degrees perpendicular. 
    // We use the Gram-Schmidt process to forcefully re-orthogonalize the Tangent 
    // with respect to the Normal. Without this, normal maps on smooth curves will 
    // look warped and mathematically incorrect.
    vec3 orthogonalizedT = T - dot(T, N) * N;
    if (length(orthogonalizedT) > 0.0001) {
        T = normalize(orthogonalizedT);
    }

    outNormal = N;
    outTangent = T;
    outBitangent = B;

    outMaterialID = i.materialID;
}