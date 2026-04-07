#version 440

layout(location = 0) in vec3 position;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
    vec4 bboxColor;
    vec4 pointColor;
    vec4 pointParams;
} ub;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
};

void main()
{
    gl_Position = ub.mvp * vec4(position, 1.0);
    gl_PointSize = max(1.0, ub.pointParams.x);
}
