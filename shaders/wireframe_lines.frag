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
    vec4 fillColor;
    vec4 lightingParams;
} ub;

layout(location = 0) in vec3 vBarycentric;
layout(location = 1) in vec3 vViewPos;
layout(location = 0) out vec4 fragColor;

void main()
{
    float edgeDistance = min(vBarycentric.x, min(vBarycentric.y, vBarycentric.z));
    float edgeWidth = max(fwidth(edgeDistance) * ub.wireParams.x, 0.001);
    float wire = 1.0 - smoothstep(0.0, edgeWidth, edgeDistance);

    if (wire < 0.05)
        discard;

    vec3 color = ub.wireColor.rgb;
    if (ub.lightingParams.z > 0.5) {
        vec3 N = normalize(cross(dFdx(vViewPos), dFdy(vViewPos)));
        vec3 lightDir = normalize(-vViewPos);
        float diff = clamp(abs(dot(N, lightDir)), 0.0, 1.0);
        float ambient = 0.25;
        color *= ambient + (1.0 - ambient) * diff;
    }

    fragColor = vec4(color, wire * ub.wireColor.a);
}
