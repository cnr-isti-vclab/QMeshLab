#version 440

layout(location = 0) in vec3 inPos;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
} ub;

layout(location = 0) out vec3 vViewPos;

void main()
{
    vec4 vp = ub.modelView * vec4(inPos, 1.0);
    vViewPos = vp.xyz;
    gl_Position = ub.mvp * vec4(inPos, 1.0);
}
