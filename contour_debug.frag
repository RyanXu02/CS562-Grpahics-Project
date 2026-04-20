#version 330

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D contourMap;

void main()
{
    float v = texture(contourMap, vUV).r;
    FragColor = vec4(v, v, v, 1.0);
}
