#version 440

layout(std140, binding = 0) uniform extractBuf {
    vec4 params;
} ub;

layout(binding = 1) uniform sampler2D srcTex;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 invSize = vec2(ub.params.x, ub.params.y);
    vec2 uv = gl_FragCoord.xy * invSize;
    vec2 uvClamped = clamp(uv, vec2(0.0), vec2(1.0));

    float center = texture(srcTex, uvClamped).a;
    if (center < 0.5) {
        fragColor = vec4(0.0);
        return;
    }

    vec2 d = invSize;
    float n0 = texture(srcTex, clamp(uv + vec2( d.x, 0.0), vec2(0.0), vec2(1.0))).a;
    float n1 = texture(srcTex, clamp(uv + vec2(-d.x, 0.0), vec2(0.0), vec2(1.0))).a;
    float n2 = texture(srcTex, clamp(uv + vec2(0.0,  d.y), vec2(0.0), vec2(1.0))).a;
    float n3 = texture(srcTex, clamp(uv + vec2(0.0, -d.y), vec2(0.0), vec2(1.0))).a;
    float n4 = texture(srcTex, clamp(uv + vec2( d.x,  d.y), vec2(0.0), vec2(1.0))).a;
    float n5 = texture(srcTex, clamp(uv + vec2(-d.x,  d.y), vec2(0.0), vec2(1.0))).a;
    float n6 = texture(srcTex, clamp(uv + vec2( d.x, -d.y), vec2(0.0), vec2(1.0))).a;
    float n7 = texture(srcTex, clamp(uv + vec2(-d.x, -d.y), vec2(0.0), vec2(1.0))).a;

    bool surrounded = (n0 >= 0.5 && n1 >= 0.5 && n2 >= 0.5 && n3 >= 0.5
        && n4 >= 0.5 && n5 >= 0.5 && n6 >= 0.5 && n7 >= 0.5);
    if (uv.x <= invSize.x || uv.x >= (1.0 - invSize.x)
        || uv.y <= invSize.y || uv.y >= (1.0 - invSize.y)) {
        surrounded = false;
    }
    fragColor = surrounded ? vec4(0.0) : vec4(1.0);
}
