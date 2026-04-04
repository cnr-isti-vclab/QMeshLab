#version 440

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
    vec4 bboxColor;
    vec4 pointColor;
    vec4 pointParams;
    vec4 wireColor;
    vec4 wireParams;
} ub;

layout(location = 0) in vec3 vBarycentric;
layout(location = 0) out vec4 fragColor;

void main()
{
    float edgeDistance = min(vBarycentric.x, min(vBarycentric.y, vBarycentric.z));
    float edgeWidth = max(fwidth(edgeDistance) * ub.wireParams.x, 0.001);
    float wire = 1.0 - smoothstep(0.0, edgeWidth, edgeDistance);

    if (wire < 0.05)
        discard;

    fragColor = vec4(ub.wireColor.rgb, wire * ub.wireColor.a);
}
