#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(std140, binding = 0) uniform gizmoBuf {
    mat4 mvp;
    vec4 centerRadius; // xyz = center, w = radius
} ub;

layout(location = 0) out vec3 vColor;

void main()
{
    vec3 worldPos = ub.centerRadius.xyz + inPos * ub.centerRadius.w;
    gl_Position = ub.mvp * vec4(worldPos, 1.0);
    vColor = inColor;
}

