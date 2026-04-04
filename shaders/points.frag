#version 440

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
    vec4 bboxColor;
    vec4 pointColor;
    vec4 pointParams;
} ub;

layout(location = 0) out vec4 fragColor;

void main()
{
    // Discard corners to produce round dots
    vec2 coord = gl_PointCoord - vec2(0.5);
    if (dot(coord, coord) > 0.25)
        discard;

    fragColor = ub.pointColor;
}
