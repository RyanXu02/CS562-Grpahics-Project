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
uniform int  noDirectLight;   // 1 to disable direct lights

const float PI = 3.14159265358979323846264338327950;

uniform float z0; // near depth
uniform float z1; // far depth

uniform sampler2D environmentMap;
uniform sampler2D irradianceMap;

uniform float exposure;

uniform sampler2D aoMap;

uniform HammersleyBlock {
    float N;
    float hammersley[2 * 100];
};

vec2 uvOf(vec3 w)
{
    return vec2(0.5 - atan(w.y, w.x) / (2.0 * PI), 1.0 - acos(clamp(w.z, -1.0, 1.0)) / PI);
}

vec3 microfacetBRDF(vec3 L, vec3 V, vec3 N, vec3 Kd, vec3 Ks, float alpha)
{
    vec3  H    = normalize(L + V);
    float LdotH = max(dot(L, H), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    float D = ((alpha + 2.0) / (2.0 * PI)) * pow(NdotH, alpha);
    vec3 F = Ks + (1.0 - Ks) * pow(1.0 - LdotH, 5.0);
    float G = 1.0 / max(LdotH * LdotH, 0.01);
    vec3  diffuse  = Kd / PI;
    vec3  specular = (dot(Ks, Ks) > 0.0) ? (D * F * G) / max(4.0 * NdotL * NdotV, 0.01) : vec3(0.0);

    return diffuse + specular;
}

vec3 specularIBL(vec3 N_vec, vec3 V, vec3 Ks, float alpha) {
    vec3 R = 2.0 * dot(N_vec, V) * N_vec - V;
    vec3 A = normalize(vec3(-R.y, R.x, 0.0));
    vec3 B = normalize(cross(R, A));

    vec3 sum = vec3(0.0);
    int n = int(N);
    for (int k = 0; k < n; k++) {
        float xi1 = hammersley[2*k + 0];
        float xi2 = hammersley[2*k + 1];

        // Phong distribution: skew xi2 into theta
        float theta = acos(pow(xi2, 1.0 / (alpha + 1.0)));

        // Build direction D in a frame centered on Z
        float sinT = sin(theta);
        float cosT = cos(theta);
        float phi  = 2.0 * PI * (0.5 - xi1);
        vec3 D = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);

        // Rotate D into the frame centered on R
        vec3 wk = normalize(D.x * A + D.y * B + D.z * R);

        // Evaluate the BRDF terms (G, F) for this sample
        vec3  H     = normalize(wk + V);
        float NdotL = max(dot(N_vec, wk), 0.0);
        float NdotV = max(dot(N_vec, V),  0.0);
        float LdotH = max(dot(wk, H),     0.0);

        // Sample environment in direction wk
        float NdotH = max(dot(N_vec, H), 0.0);
        float D_term = ((alpha + 2.0) / (2.0 * PI)) * pow(NdotH, alpha);

        vec2 envSize = vec2(textureSize(environmentMap, 0));
        float level = 0.5 * log2((envSize.x * envSize.y) / N)
                    - 0.5 * log2(D_term / 4.0)
                    - 1.0;

        vec3 Li = textureLod(environmentMap, uvOf(wk), level).rgb;

        vec3  F = Ks + (1.0 - Ks) * pow(1.0 - LdotH, 5.0);
        float G = 1.0 / max(LdotH * LdotH, 0.01);

        sum += Li * G * F / max(4.0 * NdotV, 0.01);
    }
    return sum / N;
}

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

    float isGeometry = texture(gWorldPos, vUV).a;
    if (isGeometry < 0.5) {
        vec3 viewDir = normalize(P - eyePos);
        vec3 skyColor = texture(environmentMap, uvOf(viewDir)).rgb;
        FragColor = vec4(skyColor, 1.0);
        return;
    }


    N = normalize(N);

    // Reconstruct lighting vectors in world space
    vec3 L = normalize(lightPos - P);
    vec3 V = normalize(eyePos - P);
    // Compute dot products
    float LN = max(dot(L, N), 0.0);

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
    float visibility = 1.0 - shadow;
    
    vec3 brdf        = microfacetBRDF(L, V, N, Kd, Ks, alpha);
    vec3 directLight = (noDirectLight == 0) ? Light * LN * brdf * visibility : vec3(0.0);
    vec3 irradiance  = texture(irradianceMap, uvOf(N)).rgb;
    vec3 diffuseIBL  = (Kd / PI) * irradiance;
    vec3 specIBL = (dot(Ks, Ks) > 0.0) ? specularIBL(N, V, Ks, alpha) : vec3(0.0);
    float ao = texture(aoMap, vUV).r;
    vec3 C = ao*(directLight + diffuseIBL + specIBL);

    vec3 mapped = (exposure * C) / (exposure * C + vec3(1.0));
    vec3 display = pow(mapped, vec3(1.0 / 2.2));
    FragColor = vec4(display, 1.0);
    //FragColor = vec4(irradiance, 1.0);
    //FragColor = vec4(N * 0.5 + 0.5, 1.0);
    //FragColor = vec4(textureLod(irradianceMap, vec2(0.5, 0.5), 0.0).rgb, 1.0);

}