#version 440

layout(location = 0) in vec3 position;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
};

void main()
{
    gl_Position = mvp * vec4(position, 1.0);
}
