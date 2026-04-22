#version 440

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
    vec4 bboxColor;
    vec4 pointColor;
    vec4 pointParams;
    vec4 wireColor;
    vec4 wireParams;
    vec4 fillColor;
    vec4 lightingParams;
    vec4 edgeColor;
    vec4 pbrMapUsage;
    vec4 pbrParams;
} ub;

layout(location = 0) in vec3 vViewPos;
layout(location = 1) in vec4 v_meshColor;
layout(location = 2) in vec3 v_texInfo;

layout(binding = 1) uniform sampler2D albedoTex;
layout(binding = 2) uniform sampler2D qualityLutTex;
layout(binding = 3) uniform sampler2D normalTex;
layout(binding = 4) uniform sampler2D occlusionTex;
layout(binding = 5) uniform sampler2D roughnessTex;
layout(location = 0) out vec4 fragColor;

vec3 applyNormalMap(vec3 baseNormal, vec2 uv)
{
    vec3 N = normalize(baseNormal);
    vec3 dp1 = dFdx(vViewPos);
    vec3 dp2 = dFdy(vViewPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float t2 = dot(T, T);
    float b2 = dot(B, B);
    float scale2 = max(t2, b2);
    if (scale2 <= 1e-20)
        return N;
    float invScale = inversesqrt(scale2);
    T *= invScale;
    B *= invScale;
    vec3 mapN = texture(normalTex, uv).xyz * 2.0 - 1.0;
    mapN.xy *= ub.pbrParams.x;
    mapN = normalize(mapN);
    return normalize(mat3(T, B, N) * mapN);
}

void main()
{
    vec3 baseColor = ub.fillColor.rgb;
    if (v_meshColor.a < -0.5)
        baseColor = texture(qualityLutTex, vec2(clamp(v_meshColor.r, 0.0, 1.0), 0.5)).rgb;
    else
        baseColor = mix(ub.fillColor.rgb, v_meshColor.rgb, clamp(v_meshColor.a, 0.0, 1.0));
    const bool hasUv = v_texInfo.z > 0.5;
    vec2 uv = vec2(v_texInfo.x, 1.0 - v_texInfo.y);
    int albedoMode = int(ub.pbrMapUsage.w + 0.5);
    int normalMode = int(ub.pbrMapUsage.x + 0.5);
    int aoMode = int(ub.pbrMapUsage.y + 0.5);
    int roughnessMode = int(ub.pbrMapUsage.z + 0.5);
    if (albedoMode == 0)
        baseColor = vec3(1.0);
    else if (hasUv && albedoMode == 2)
        baseColor = texture(albedoTex, uv).rgb;

    vec3 color = baseColor;
    if (ub.lightingParams.w > 0.5) {
        vec3 N = normalize(cross(dFdy(vViewPos), dFdx(vViewPos)));
        if (hasUv && normalMode == 2)
            N = applyNormalMap(N, uv);

        vec3 L = normalize(vec3(0.0, 0.0, 1.0));
        vec3 V = normalize(-vViewPos);
        vec3 H = normalize(L + V);
        float diff = max(dot(N, L), 0.0);

        float roughness = (roughnessMode == 0) ? 1.0 : max(0.02, ub.pbrParams.z);
        if (hasUv && roughnessMode == 2) {
            vec3 roughSample = texture(roughnessTex, uv).rgb;
            roughness *= clamp(dot(roughSample, vec3(0.3333333)), 0.0, 1.0);
            roughness = clamp(roughness, 0.02, 1.0);
        }

        float ao = 1.0;
        if (aoMode == 1) {
            ao = clamp(ub.pbrParams.y, 0.0, 1.0);
        } else if (hasUv && aoMode == 2) {
            float aoSample = clamp(texture(occlusionTex, uv).r, 0.0, 1.0);
            ao = mix(1.0, aoSample, clamp(ub.pbrParams.y, 0.0, 1.0));
        }

        float ambient = 0.18 * ao;
        float specPower = mix(96.0, 8.0, roughness);
        float spec = pow(max(dot(N, H), 0.0), specPower);
        float specStrength = mix(0.18, 0.02, roughness);
        color = baseColor * (ambient + (1.0 - ambient) * diff) + vec3(spec * specStrength);
    }
    fragColor = vec4(color, 1.0);
}
