#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inBarycentric;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
} ub;

layout(location = 0) out vec3 vBarycentric;
layout(location = 1) out vec3 vViewPos;

void main()
{
    vBarycentric = inBarycentric;
    vViewPos = (ub.modelView * vec4(inPos, 1.0)).xyz;
    gl_Position = ub.mvp * vec4(inPos, 1.0);
}
