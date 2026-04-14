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
} ub;

layout(location = 0) in vec3 vViewPos;
layout(location = 1) in vec4 v_meshColor;
layout(location = 2) in vec3 v_texInfo;

layout(binding = 1) uniform sampler2D albedoTex;
layout(binding = 2) uniform sampler2D qualityLutTex;
layout(location = 0) out vec4 fragColor;

void main()
{
    // Derivative-based face normal in view space.
    vec3 N = normalize(cross(dFdy(vViewPos), dFdx(vViewPos)));
    vec3 lightDir = normalize(vec3(0.0, 0.0, 1.0));
    float diff = max(dot(N, lightDir), 0.0);
    float ambient = 0.15;
    vec3 baseColor = ub.fillColor.rgb;
    if (v_meshColor.a < -0.5)
        baseColor = texture(qualityLutTex, vec2(clamp(v_meshColor.r, 0.0, 1.0), 0.5)).rgb;
    else
        baseColor = mix(ub.fillColor.rgb, v_meshColor.rgb, clamp(v_meshColor.a, 0.0, 1.0));
    if (v_texInfo.z > 0.5)
        baseColor = texture(albedoTex, vec2(v_texInfo.x, 1.0 - v_texInfo.y)).rgb;
    vec3 color = baseColor;
    if (ub.lightingParams.w > 0.5)
        color *= ambient + (1.0 - ambient) * diff;
    fragColor = vec4(color, 1.0);
}
