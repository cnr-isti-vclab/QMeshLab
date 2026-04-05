#version 440

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
    vec4 bboxColor;
} ub;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = ub.bboxColor;
}
