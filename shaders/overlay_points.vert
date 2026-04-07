#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inMeshColor;
layout(location = 2) in vec4 inNormalAndFlag;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
    mat3 normalMatrix;
    vec4 bboxColor;
    vec4 pointColor;
    vec4 pointParams;
} ub;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
};
layout(location = 0) out vec4 vMeshColor;
layout(location = 1) out vec4 vNormalAndFlag;
layout(location = 2) out vec3 vViewPos;

void main()
{
    vec4 viewPos = ub.modelView * vec4(inPos, 1.0);
    gl_Position = ub.mvp * vec4(inPos, 1.0);
    gl_PointSize = ub.pointParams.x;
    vMeshColor = inMeshColor;
    // Keep raw transformed normal; normalize safely in fragment stage.
    vNormalAndFlag = vec4(ub.normalMatrix * inNormalAndFlag.xyz, inNormalAndFlag.w);
    vViewPos = viewPos.xyz;
}
