#version 440

layout(location = 0) in vec3 position;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
};

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
};

void main()
{
    gl_Position = mvp * vec4(position, 1.0);
    // Keep mask point rendering cheap and stable for huge clouds; expand in post-process.
    gl_PointSize = 1.0;
}
