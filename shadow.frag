#version 330

in vec4 position;
out vec4 FragColor;

uniform float z0; // near depth
uniform float z1; // far depth

void main() {
    float z = clamp((position.w - z0) / (z1 - z0), 0.0, 1.0);
    FragColor = vec4(z, z*z, z*z*z, z*z*z*z);
}