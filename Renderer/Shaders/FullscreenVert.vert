#version 450

layout(location = 0) out vec2 outUV;

const vec2 verts[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 pos = verts[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);

    outUV = pos * 0.5 + 0.5;
}