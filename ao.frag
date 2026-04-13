#version 330 core

in vec2 vUV;
out float fragAO;

uniform sampler2D gPosition; // gbuffer0
uniform sampler2D gNormal; // gbuffer1

uniform mat4  WorldView; // to get camera-space depth
uniform vec2  screenSize;

uniform float R; // range of influence
uniform int n; // sample count
uniform float s; // intensity scale
uniform float k; // contrast exponent
uniform float delta; // depth bias

uniform float projScale;   // height / (2 * tan(fovy/2))

const float PI = 3.14159265358979323846264338327950;

float camDepth(vec3 Pworld) {
    return -(WorldView * vec4(Pworld, 1.0)).z;
}

void main() {
    vec3 P = texture(gPosition, vUV).xyz;
    vec3 N = normalize(texture(gNormal, vUV).xyz);

    // Skip background
    float isGeometry = texture(gPosition, vUV).a;
    if (isGeometry < 0.5) { fragAO = 1.0; return; }

    float d = camDepth(P);

    // Per-pixel rotation hash
    vec2 xyInt = gl_FragCoord.xy;
    float phi = float((int(30.0 * xyInt.x)) ^ int(xyInt.y))
              + 10.0 * xyInt.x * xyInt.y;

    float c = 0.1 * R;
    float sum = 0.0;

    for (int i = 0; i < n; ++i) {
        float alpha = (float(i) + 0.5) / float(n);
        float h = alpha * R / d; // screen-space spiral radius
        float radiusPixels = h * projScale;
        float theta = 2.0 * PI * alpha * (7.0 * float(n) / 9.0) + phi;
        vec2  offsPixels = radiusPixels * vec2(cos(theta), sin(theta));
        vec2  sampleUV = vUV + offsPixels / screenSize;

        vec3  Pi = texture(gPosition, sampleUV).xyz;

        vec3  w = Pi - P;

        float di = camDepth(Pi);

        float heaviside = (length(w) < R) ? 1.0 : 0.0;
        float num = max(0.0, dot(N, w) - delta * di) * heaviside;
        float den = max(c * c, dot(w, w));
        sum += (num / den) * heaviside;

    }

    float S = (2.0 * PI * c / float(n)) * sum;
    float A = pow(max(0.0, 1.0 - s * S), k);

    fragAO = A;
}