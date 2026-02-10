#version 330

in vec4 position;
out vec4 FragColor;

void main() {
	//vec2 uv = gl_FragCoord.xy/vec2(750,750); // (or whatever screen size)
	//FragColor.xyz = vec3(position.w/100); // or similar
	//return; // which disables all further code in the shader.

	FragColor = position;
}