#version 440

layout(std140, binding = 0) uniform outlineBuf {
    vec4 outlineColor;
    vec4 outlineParams; // x: widthPx, y: invWidth, z: invHeight, w: flipY
} ub;

layout(binding = 1) uniform sampler2D maskTex;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 invSize = vec2(ub.outlineParams.y, ub.outlineParams.z);
    vec2 uv = gl_FragCoord.xy * invSize;
    if (ub.outlineParams.w > 0.5)
        uv.y = 1.0 - uv.y;

    vec2 d = invSize * ub.outlineParams.x;
    vec2 uv0 = clamp(uv + vec2( d.x, 0.0), vec2(0.0), vec2(1.0));
    vec2 uv1 = clamp(uv + vec2(-d.x, 0.0), vec2(0.0), vec2(1.0));
    vec2 uv2 = clamp(uv + vec2(0.0,  d.y), vec2(0.0), vec2(1.0));
    vec2 uv3 = clamp(uv + vec2(0.0, -d.y), vec2(0.0), vec2(1.0));
    vec2 uv4 = clamp(uv + vec2( d.x,  d.y), vec2(0.0), vec2(1.0));
    vec2 uv5 = clamp(uv + vec2(-d.x,  d.y), vec2(0.0), vec2(1.0));
    vec2 uv6 = clamp(uv + vec2( d.x, -d.y), vec2(0.0), vec2(1.0));
    vec2 uv7 = clamp(uv + vec2(-d.x, -d.y), vec2(0.0), vec2(1.0));

    float center = texture(maskTex, uv).r;
    float n0 = texture(maskTex, uv0).r;
    float n1 = texture(maskTex, uv1).r;
    float n2 = texture(maskTex, uv2).r;
    float n3 = texture(maskTex, uv3).r;
    float n4 = texture(maskTex, uv4).r;
    float n5 = texture(maskTex, uv5).r;
    float n6 = texture(maskTex, uv6).r;
    float n7 = texture(maskTex, uv7).r;

    float dilated = max(max(max(n0, n1), max(n2, n3)), max(max(n4, n5), max(n6, n7)));
    float edge = clamp(dilated - center, 0.0, 1.0);
    if (edge <= 0.001)
        discard;

    fragColor = vec4(ub.outlineColor.rgb, ub.outlineColor.a * edge);
}
