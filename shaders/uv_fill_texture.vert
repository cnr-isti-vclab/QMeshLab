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
    vec3 uvPos = vec3(texInfo.xy, 0.0);
    v_normal = vec3(0.0, 0.0, 1.0);
    v_meshColor = vec4(1.0, 1.0, 1.0, 0.0);
    v_texInfo = vec3(texInfo.xy, 1.0);
    gl_Position = mvp * vec4(uvPos, 1.0);
}
