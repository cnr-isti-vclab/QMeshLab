#version 440

layout(std140, binding = 0) uniform rasterBackplate {
    vec4 rect;   // center.xy, halfSize.xy in NDC
    vec4 params; // opacity, unused
} ub;

layout(location = 0) out vec2 vUv;

void main()
{
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0));

    const vec2 uvs[6] = vec2[](
        vec2(0.0, 1.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(1.0, 0.0));

    const vec2 p = ub.rect.xy + corners[gl_VertexIndex] * ub.rect.zw;
    vUv = uvs[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);
}
