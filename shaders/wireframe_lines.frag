#version 440

layout(location = 0) in vec3 vBarycentric;
layout(location = 0) out vec4 fragColor;

void main()
{
    float edgeDistance = min(vBarycentric.x, min(vBarycentric.y, vBarycentric.z));
    float edgeWidth = max(fwidth(edgeDistance) * 1.5, 0.001);
    float wire = 1.0 - smoothstep(0.0, edgeWidth, edgeDistance);

    if (wire < 0.05)
        discard;

    vec3 lineColor = vec3(0.06, 0.06, 0.08);
    fragColor = vec4(lineColor, wire);
}