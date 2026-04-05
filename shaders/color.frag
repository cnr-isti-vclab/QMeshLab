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
} ub;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_meshColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.0, 0.0, 1.0));
    float diff = max(dot(normalize(v_normal), lightDir), 0.0);
    float ambient = 0.15;
    vec3 baseColor = mix(ub.fillColor.rgb, v_meshColor.rgb, clamp(v_meshColor.a, 0.0, 1.0));
    vec3 color = baseColor * (ambient + (1.0 - ambient) * diff);
    fragColor = vec4(color, 1.0);
}
