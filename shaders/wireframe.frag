#version 440

layout(location = 0) in vec3 vBarycentric;
layout(location = 1) in vec3 vViewPos;
layout(location = 0) out vec4 fragColor;

void main()
{
    float edgeDistance = min(vBarycentric.x, min(vBarycentric.y, vBarycentric.z));
    float edgeWidth = max(fwidth(edgeDistance) * 1.5, 0.001);
    float wire = 1.0 - smoothstep(0.0, edgeWidth, edgeDistance);

    vec3 faceNormal = normalize(cross(dFdy(vViewPos), dFdx(vViewPos)));
    vec3 lightDir = normalize(-vViewPos);
    float diffuse = clamp(abs(dot(faceNormal, lightDir)), 0.0, 1.0);

    vec3 fillColor = vec3(0.72, 0.74, 0.78) * (0.25 + 0.75 * diffuse);
    vec3 lineColor = vec3(0.06, 0.06, 0.08);
    vec3 color = mix(fillColor, lineColor, wire);

    fragColor = vec4(color, 1.0);
}