#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(std140, binding = 0) uniform gizmoBuf {
    mat4 mvp;
    vec4 centerRadius; // xyz = center, w = radius
    vec4 cameraBackShade; // xyz = camera world pos, w = back hemisphere shade floor [0..1]
} ub;

layout(location = 0) out vec3 vColor;

void main()
{
    vec3 worldPos = ub.centerRadius.xyz + inPos * ub.centerRadius.w;
    gl_Position = ub.mvp * vec4(worldPos, 1.0);

    vec3 camDir = ub.cameraBackShade.xyz - ub.centerRadius.xyz;
    float camLen = length(camDir);
    camDir = (camLen > 1e-6) ? (camDir / camLen) : vec3(0.0, 0.0, 1.0);

    float facing = dot(normalize(inPos), camDir);
    float shade = mix(
        clamp(ub.cameraBackShade.w, 0.0, 1.0),
        1.0,
        smoothstep(-0.2, 0.3, facing));
    vColor = inColor * shade;
}
