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
layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 color = ub.bboxColor.rgb;
    if (ub.lightingParams.x > 0.5) {
        vec3 lightDir = normalize(-vViewPos);
        float diff = clamp(lightDir.z, 0.0, 1.0);
        float ambient = 0.25;
        color *= ambient + (1.0 - ambient) * diff;
    }
    fragColor = vec4(color, ub.bboxColor.a);
}
