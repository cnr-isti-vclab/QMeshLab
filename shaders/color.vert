#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 meshColor;
layout(location = 3) in vec3 texInfo;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
};

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_meshColor;
layout(location = 2) out vec3 v_texInfo;

void main()
{
    v_normal = normalize(normalMatrix * normal);
    v_meshColor = meshColor;
    v_texInfo = texInfo;
    gl_Position = mvp * vec4(position, 1.0);
}
