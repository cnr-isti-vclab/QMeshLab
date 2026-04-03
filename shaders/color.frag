#version 440

layout(location = 0) in vec3 v_normal;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.0, 0.0, 1.0));
    float diff = max(dot(normalize(v_normal), lightDir), 0.0);
    float ambient = 0.15;
    vec3 color = vec3(0.6, 0.6, 0.7) * (ambient + (1.0 - ambient) * diff);
    fragColor = vec4(color, 1.0);
}
