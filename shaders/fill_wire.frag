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
} ub;

layout(location = 0) in vec3 vBarycentric;
layout(location = 1) in vec3 vViewPos;
layout(location = 0) out vec4 fragColor;

void main()
{
    float edgeDistance = min(vBarycentric.x, min(vBarycentric.y, vBarycentric.z));
    float edgeWidth = max(fwidth(edgeDistance) * ub.wireParams.x, 0.001);
    float wire = 1.0 - smoothstep(0.0, edgeWidth, edgeDistance);

    vec3 faceNormal = normalize(cross(dFdy(vViewPos), dFdx(vViewPos)));
    vec3 lightDir = normalize(-vViewPos);
    float diffuse = clamp(abs(dot(faceNormal, lightDir)), 0.0, 1.0);

    vec3 litFill = ub.fillColor.rgb * (0.25 + 0.75 * diffuse);
    vec3 color = mix(litFill, ub.wireColor.rgb, wire);

    fragColor = vec4(color, 1.0);
}
