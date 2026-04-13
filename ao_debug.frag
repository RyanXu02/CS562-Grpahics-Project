#version 430

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D aoMap;

void main() {
    float ao = texture(aoMap, vUV).r;
    fragColor = vec4(vec3(ao), 1.0);
}