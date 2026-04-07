#version 440

layout(location = 0) out vec4 fragColor;

vec3 packDepth24(float depth)
{
    vec3 enc = fract(depth * vec3(1.0, 255.0, 65025.0));
    enc -= enc.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
    return enc;
}

void main()
{
    fragColor = vec4(packDepth24(gl_FragCoord.z), 1.0);
}
