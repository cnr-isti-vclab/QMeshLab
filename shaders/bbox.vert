#version 440

layout(location = 0) in vec3 inPos;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
} ub;

void main()
{
    gl_Position = ub.mvp * vec4(inPos, 1.0);
}
