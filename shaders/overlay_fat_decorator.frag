#version 440

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 color;
    vec4 params;
} ub;

layout(location = 0) in float vSide;
layout(location = 0) out vec4 fragColor;

void main()
{
    float aa = max(fwidth(vSide), 1e-4);
    float alpha = 1.0 - smoothstep(1.0 - aa, 1.0, abs(vSide));
    fragColor = vec4(ub.color.rgb, ub.color.a * alpha);
    if (fragColor.a <= 0.001)
        discard;
}
