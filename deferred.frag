#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D gWorldPos;  // gbuffer 0
uniform sampler2D gNormal;    // gbuffer 1
uniform sampler2D gKd;        // gbuffer 2
uniform sampler2D gKsAlpha;   // gbuffer 3

uniform sampler2D shadowMap; 

uniform vec3 Light;           // Ii
uniform vec3 Ambient;         // Ia
uniform vec3 lightPos;        // world space light pos
uniform vec3 eyePos;          // world space camera pos
uniform mat4 ShadowMatrix;    // this is precomputed!

const float PI = 3.14159265358979323846264338327950;

void main()
{
    // Read G-buffer
    vec3 P = texture(gWorldPos, vUV).xyz;
    vec3 N = texture(gNormal,   vUV).xyz;
    vec3 Kd = texture(gKd,      vUV).rgb;
    vec4 ksA = texture(gKsAlpha, vUV);
    vec3 Ks = ksA.rgb;
    float alpha = ksA.a;

    N = normalize(N);

    // Reconstruct lighting vectors in world space
    vec3 L = normalize(lightPos - P);
    vec3 V = normalize(eyePos - P);
    vec3 H = normalize(L + V);
    // Compute dot products
    float LN = max(dot(L, N), 0.0);
    float HN = max(dot(H, N), 0.0);
    float LH = max(dot(L, H), 0.0);

    // Microfacet BRDF
    vec3 F = Ks + (vec3(1.0) - Ks) * pow((1.0 - LH), 5.0);
    float G = 1.0 / max(pow(LH, 2.0), 1e-6); // avoid divide-by-zero
    float D = ((alpha + 2.0) / (2.0 * PI)) * pow(HN, alpha);

    vec3 lit = Light * LN * ((Kd / PI) + (F * G * D / 4.0));

    // Shadows
    vec4 shadowCoord = ShadowMatrix * vec4(P, 1.0);
    vec2 shadowIndex = shadowCoord.xy / shadowCoord.w;
    bool inRange = shadowCoord.w > 0.0 &&
                   all(greaterThanEqual(shadowIndex, vec2(0.0))) &&
                   all(lessThanEqual(shadowIndex, vec2(1.0)));

    float shadowed = 0.0;
    if (inRange) {
        float lightDepth = texture(shadowMap, shadowIndex).w;
        float pixelDepth = shadowCoord.w;
        shadowed = ((pixelDepth - 0.005) > lightDepth) ? 1.0 : 0.0;
    }

    vec3 color = Ambient * Kd + lit * (1.0 - shadowed);
    FragColor = vec4(color, 1.0);
}