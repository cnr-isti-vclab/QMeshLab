#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
};

layout(location = 0) out vec3 v_normal;

void main()
{
    v_normal = normalize(normalMatrix * normal);
    gl_Position = mvp * vec4(position, 1.0);
}
