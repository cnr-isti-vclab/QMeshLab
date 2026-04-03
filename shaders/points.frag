#version 440

layout(location = 0) out vec4 fragColor;

void main()
{
    // Discard corners to produce round dots
    vec2 coord = gl_PointCoord - vec2(0.5);
    if (dot(coord, coord) > 0.25)
        discard;

    fragColor = vec4(1.0, 0.75, 0.2, 1.0);
}
