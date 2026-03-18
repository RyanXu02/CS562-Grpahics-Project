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

uniform float z0; // near depth
uniform float z1; // far depth

vec3 cholesky(float m11, float m12, float m13,
              float m22, float m23, float m33,
              float z1,  float z2,  float z3)
{
    float a = sqrt(max(m11, 1e-8));
    float b = m12 / a;
    float c = m13 / a;
    float d = sqrt(max(m22 - b*b, 1e-8));
    float e = (m23 - b*c) / d;
    float f = sqrt(max(m33 - c*c - e*e, 1e-8));

    float c1h = z1 / a;
    float c2h = (z2 - b*c1h) / d;
    float c3h = (z3 - c*c1h - e*c2h) / f;

    float c3 = c3h / f;
    float c2 = (c2h - e*c3) / d;
    float c1 = (c1h - b*c2 - c*c3) / a;
    return vec3(c1, c2, c3);
}

float MSM(vec4 b_raw, float zf) {
    float alpha = 1e-3;
    // 1. bias
    vec4 b = mix(b_raw, vec4(0.5), alpha);

    // 2. Cholesky solve for c
    vec3 c = cholesky(
        1.0,   b.x,   b.y,
               b.y,   b.z,
                      b.w,
        1.0, zf, zf*zf
    );

    // 3. solve c3*z^2 + c2*z + c1 = 0
    float disc = c.y*c.y - 4.0*c.z*c.x;
    disc = max(disc, 0.0);
    float sq = sqrt(disc);
    float z2 = (-c.y - sq) / (2.0 * c.z);
    float z3 = (-c.y + sq) / (2.0 * c.z);
    if (z2 > z3) { float tmp = z2; z2 = z3; z3 = tmp; }

    // 4.
    if (zf <= z2) return 0.0;
    // 5.
    else if (zf <= z3)
        return (zf*z3 - b.x*(zf + z3) + b.y) / ((z3 - z2)*(zf - z2));
    // 6.
    else
        return 1.0 - (z2*z3 - b.x*(z2 + z3) + b.y) / ((zf - z2)*(zf - z3));
}

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
    vec2 shadowUV = shadowCoord.xy / shadowCoord.w;
    bool inRange = shadowCoord.w > 0.0 &&
                   all(greaterThanEqual(shadowUV, vec2(0.0))) &&
                   all(lessThanEqual(shadowUV, vec2(1.0)));
   
    float rawDist = length(P - lightPos);
    float zf = clamp((rawDist - z0) / (z1 - z0), 0.0, 1.0);
    float shadow = 0.0;
    if (inRange)
    {
        vec4 b = texture(shadowMap, shadowUV);
        shadow = MSM(b, zf);
    }
    vec3 color = Ambient * Kd + lit * (1.0 - shadow);
    FragColor = vec4(color, 1.0);
}