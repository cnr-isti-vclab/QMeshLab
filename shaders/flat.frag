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

layout(location = 0) in vec3 vViewPos;
layout(location = 0) out vec4 fragColor;

void main()
{
    // Derivative-based face normal. The dFdy x dFdx order matches the view-space handedness here.
    vec3 N = normalize(cross(dFdy(vViewPos), dFdx(vViewPos)));
    // Camera headlight in view space: light comes from the eye toward the fragment.
    vec3 lightDir = normalize(-vViewPos);
    // Use absolute term to avoid dark output from winding/normal orientation mismatches.
    float diff = clamp(abs(dot(N, lightDir)), 0.0, 1.0);
    float ambient = 0.25;
    vec3 color = ub.fillColor.rgb * (ambient + (1.0 - ambient) * diff);
    fragColor = vec4(color, 1.0);
}
