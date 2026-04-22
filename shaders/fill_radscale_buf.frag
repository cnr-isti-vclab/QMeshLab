/****************************************************************************
* Radiance Scaling — gradient buffer pass (pass 1 of 2)                    *
*                                                                           *
* Faithful port of 01_buffer.fs (Vergne & Dumas, INRIA 2010).              *
* Renders (gx, gy, logZ, 1.0) into an RGBA32F intermediate texture.        *
* Fragments not covered by mesh geometry remain cleared to vec4(0).        *
*                                                                           *
* Pass 2 (fill_radscale.frag) reads this texture with a 3x3 neighbourhood  *
* to compute the Hessian and depth-discontinuity weight exactly as          *
* in the original two-pass implementation.                                  *
****************************************************************************/
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
    vec4 materialFlags;
    vec4 materialParams;
} ub;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_meshColor;  // unused; present for vertex layout compat
layout(location = 2) in vec3 v_texInfo;    // unused
layout(location = 3) in vec3 v_viewPos;

layout(location = 0) out vec4 fragColor;   // (gx, gy, logZ, 1.0)

void main()
{
    // Exact port of 01_buffer.fs:
    //   gs  = pow(1 / max(n.z, eps), foreshortening)
    //   gx  = -n.x * gs
    //   gy  = -n.y * gs
    //   logZ = log(-view.z)      (matches 01_buffer.vs: depth = log(-z))
    const float eps           = 0.01;
    const float foreshortening = 0.4;

    vec3 n = normalize(v_normal);
    float gs = pow(max(n.z, eps), -foreshortening);

    float gx   = -n.x * gs;
    float gy   = -n.y * gs;
    float logZ = log(max(-v_viewPos.z, 0.001));

    fragColor = vec4(gx, gy, logZ, 1.0);
}
