#version 440

layout(std140, binding = 0) uniform morphBuf {
    vec4 params; // x: invWidth, y: invHeight, z: radiusPx, w: mode (0=dilate, 1=erode)
} ub;

layout(binding = 1) uniform sampler2D srcTex;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 invSize = vec2(ub.params.x, ub.params.y);
    vec2 uv = gl_FragCoord.xy * invSize;
    const int kMaxRadiusPx = 16;
    int radiusPx = int(floor(ub.params.z + 0.5));
    radiusPx = clamp(radiusPx, 0, kMaxRadiusPx);

    float v = (ub.params.w < 0.5) ? 0.0 : 1.0;
    if (radiusPx == 0) {
        v = texture(srcTex, uv).r;
    } else {
        int radius2 = radiusPx * radiusPx;
        for (int oy = -kMaxRadiusPx; oy <= kMaxRadiusPx; ++oy) {
            if (abs(oy) > radiusPx)
                continue;
            for (int ox = -kMaxRadiusPx; ox <= kMaxRadiusPx; ++ox) {
                if (abs(ox) > radiusPx)
                    continue;
                if (ox * ox + oy * oy > radius2)
                    continue;
                vec2 duv = vec2(float(ox) * invSize.x, float(oy) * invSize.y);
                float s = texture(srcTex, clamp(uv + duv, vec2(0.0), vec2(1.0))).r;
                if (ub.params.w < 0.5)
                    v = max(v, s);
                else
                    v = min(v, s);
            }
        }
    }

    fragColor = vec4(v, v, v, 1.0);
}
