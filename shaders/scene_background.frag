#version 440

layout(std140, binding = 0) uniform bg {
    vec4 bottomColor;
    vec4 topColor;
} ub;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

void main()
{
    const float t = clamp((vNdc.y + 1.0) * 0.5, 0.0, 1.0);
    const vec3 rgb = mix(ub.bottomColor.rgb, ub.topColor.rgb, t);
    fragColor = vec4(rgb, 1.0);
}
