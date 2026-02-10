#version 330 core

out vec4 FragColor;

uniform sampler2D gWorldPos;
uniform sampler2D gNormal;
uniform sampler2D gKd;
uniform sampler2D gKsAlpha;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform float lightRadius;
uniform vec3 eyePos;

uniform int debugDrawSpheres;

uniform vec2 screenSize;

const float PI = 3.14159265358979323846;

void main()
{
    // just draw the spheres
    if (debugDrawSpheres == 1)
    {
        vec3 nlightColor = normalize(lightColor);
        FragColor = vec4(nlightColor*0.1, 1.0);
        return;
    }

    vec2 uv = gl_FragCoord.xy / screenSize;

    vec3 P = texture(gWorldPos, uv).xyz;
    vec3 N = normalize(texture(gNormal, uv).xyz);
    vec3 Kd = texture(gKd, uv).rgb;
    vec4 ksA = texture(gKsAlpha, uv);
    vec3 Ks = ksA.rgb;
    float alpha = ksA.a;

    vec3 Lvec = lightPos - P;
    float d = length(Lvec);

    // Outside light volume
    if (d >= lightRadius)
        discard;

    vec3 L = Lvec / d;
    vec3 V = normalize(eyePos - P);
    vec3 H = normalize(L + V);

    float LN = max(dot(L, N), 0.0);
    float HN = max(dot(H, N), 0.0);
    float LH = max(dot(L, H), 0.0);

    // attenuation
    float att = (1.0 / pow(d,2)) - (1.0 / pow(lightRadius, 2));
    att = max(att, 0.0); // just in case

    vec3 F = Ks + (vec3(1,1,1)-Ks)*pow((1-LH),5); // Schlick approximation to the Fresnel term F
    float G = 1 / pow(LH,2); // masking term G and part of the denominator lumped together
    float D = ((alpha+2) / (2*PI))*pow(HN,alpha); // normal distribution term D

    vec3 lit = lightColor*att*LN*( (Kd/PI)+(F*G*D/4) );

    FragColor = vec4(lit, 1.0);
}
