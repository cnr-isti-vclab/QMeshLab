#version 440

// Light gizmo overlay: rim circle + arrow pointing toward projected light position.
// Vertex format: inPos.xyz
//   z == 0: rim circle point, inPos.xy on unit circle
//   z != 0: arrow geometry, inPos.x = perp-to-light offset, inPos.y = along-light scale (0..1)
//           The "along" direction is L2 = lightDir.xy (NOT normalised), so the
//           tip ends up at the projected light position inside the circle.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(std140, binding = 0) uniform lightGizmoBuf {
    mat4 unused;      // padding — keeps struct at kLightGizmoUbufSize = 96 bytes
    vec4 lightDir;    // xyz = view-space light direction (unit), w unused
    vec4 params;      // x = radius (NDC height units), y = anchor NDC X, z = anchor NDC Y, w = aspect (w/h)
} ub;

layout(location = 0) out vec3 vColor;

void main()
{
    float r      = ub.params.x;
    float ax     = ub.params.y;
    float ay     = ub.params.z;
    float aspect = ub.params.w;   // viewport width / height

    // L2: screen-space projection of the light direction.
    // Negate Y because view-space Y+ is up but our drag convention produces
    // the opposite sign (delta.y from Qt is positive downward).
    vec2 L2   = vec2(ub.lightDir.x, -ub.lightDir.y);
    float ll  = length(L2);
    vec2 Lhat = (ll > 0.001) ? (L2 / ll) : vec2(0.0, 1.0); // normalised direction
    vec2 Lperp = vec2(-Lhat.y, Lhat.x);           // perpendicular (screen right of L)

    vec2 off;
    if (inPos.z < 0.5) {
        // Rim circle: unit circle on screen, corrected for aspect ratio
        off = vec2(inPos.x / aspect, inPos.y);
    } else {
        // Arrow: inPos.x = perp offset, inPos.y ∈ [0,1] scales along L2
        // Using L2 (not normalised) means the tip lands exactly at the
        // projected light position, which is at distance ll*r from the centre.
        off = inPos.x * Lperp / aspect + inPos.y * vec2(L2.x / aspect, L2.y);
    }

    gl_Position = vec4(ax + off.x * r, ay + off.y * r, 0.5, 1.0);
    vColor = inColor;
}
