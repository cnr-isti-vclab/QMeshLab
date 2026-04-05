#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 meshColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
} ub;

layout(location = 0) out vec3 vViewPos;
layout(location = 1) out vec4 v_meshColor;

void main()
{
    vec4 vp = ub.modelView * vec4(inPos, 1.0);
    vViewPos = vp.xyz;
    v_meshColor = meshColor;
    gl_Position = ub.mvp * vec4(inPos, 1.0);
}
