#version 440

layout(location = 0) out vec2 vNdc;

void main()
{
    const vec2 verts[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0));
    const vec2 p = verts[gl_VertexIndex];
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
