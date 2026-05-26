#version 440

layout(std140, binding = 0) uniform rasterBackplate {
    vec4 rect;
    vec4 params; // opacity, unused
} ub;

layout(binding = 1) uniform sampler2D rasterTex;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 c = texture(rasterTex, vUv);
    fragColor = vec4(c.rgb, clamp(ub.params.x, 0.0, 1.0));
}
