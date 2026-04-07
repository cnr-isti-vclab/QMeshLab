#version 440

layout(std140, binding = 0) uniform debugBuf {
    vec4 params; // x: invWidth, y: invHeight, z: flipY
} ub;

layout(binding = 1) uniform sampler2D srcTex;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy * vec2(ub.params.x, ub.params.y);
    if (ub.params.z > 0.5)
        uv.y = 1.0 - uv.y;
    float v = texture(srcTex, clamp(uv, vec2(0.0), vec2(1.0))).r;
    fragColor = vec4(v, v, v, 1.0);
}
