#version 330
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D shadowMap;

void main()
{
    vec4 moments = texture(shadowMap, vUV);
    float depth = moments.r;
    FragColor = vec4(vec3(depth), 1.0);
}