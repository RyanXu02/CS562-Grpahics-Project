#version 330
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D gWorldPos;
uniform sampler2D gNormal;
uniform sampler2D gKd;
uniform sampler2D gKsAlpha;

uniform int uMode;        // 0 - 3

void main()
{
    if (uMode == 0) {
        vec3 P = texture(gWorldPos, vUV).xyz;
        FragColor = vec4(P, 1.0);
        return;
    }
    if (uMode == 1) {
        vec3 N = normalize(texture(gNormal, vUV).xyz);
        FragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    if (uMode == 2) {
        FragColor = texture(gKd, vUV);
        return;
    }
    vec4 ksA = texture(gKsAlpha, vUV);
    FragColor = vec4(ksA.rgb, 1.0);
}