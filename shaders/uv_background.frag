#version 440

layout(std140, binding = 0) uniform bg {
    vec4 params0;    // pan.x, pan.y, zoom, aspect
    vec4 checkerA;   // rgb + unused
    vec4 checkerB;   // rgb + unused
    vec4 gridParams; // checkerScale, gridScale, unused, unused
} ub;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

void main()
{
    const float aspect = max(ub.params0.w, 1e-6);
    const float xLim = (aspect >= 1.0) ? aspect : 1.0;
    const float yLim = (aspect >= 1.0) ? 1.0 : (1.0 / aspect);
    const float zoom = max(ub.params0.z, 1e-6);
    const vec2 pan = ub.params0.xy;

    const vec2 uv = pan + vec2(vNdc.x * xLim / zoom, vNdc.y * yLim / zoom);

    const float checkerScale = max(1.0, ub.gridParams.x);
    const vec2 checkerCell = floor(uv * checkerScale);
    const float checker = mod(checkerCell.x + checkerCell.y, 2.0);
    vec3 color = mix(ub.checkerA.rgb, ub.checkerB.rgb, checker);

    const float gridScale = max(1.0, ub.gridParams.y);
    vec2 g = abs(fract(uv * gridScale) - 0.5) / max(fwidth(uv * gridScale), vec2(1e-4));
    float fineGrid = 1.0 - clamp(min(g.x, g.y), 0.0, 1.0);

    vec2 gm = abs(fract(uv) - 0.5) / max(fwidth(uv), vec2(1e-4));
    float majorGrid = 1.0 - clamp(min(gm.x, gm.y), 0.0, 1.0);

    vec3 gridColor = mix(vec3(0.55), vec3(0.35), majorGrid);
    float gridAlpha = clamp(fineGrid * 0.22 + majorGrid * 0.35, 0.0, 0.60);
    color = mix(color, gridColor, gridAlpha);

    fragColor = vec4(color, 1.0);
}
