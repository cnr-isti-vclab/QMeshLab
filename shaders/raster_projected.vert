#version 440

layout(location = 0) in vec3 position;

layout(std140, binding = 0) uniform rasterProjected {
    mat4 mvp;
    vec4 color;
} ub;

void main()
{
    gl_Position = ub.mvp * vec4(position, 1.0);
}
