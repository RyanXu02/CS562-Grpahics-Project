#version 330

layout(location=0) out vec4 G0_WorldPos;
layout(location=1) out vec4 G1_WorldN;
layout(location=2) out vec4 G2_Kd;
layout(location=3) out vec4 G3_KsAlpha;

const int skyId = 1;
const int seaId = 2;
const int groundId = 3;
const int roomId = 4;
const int boxId = 5;
const int frameId = 6;
const int lPicId = 7;
const int rPicId = 8;
const int teapotId = 9;
const int spheresId = 10;
const int floorId = 11;

const float PI = 3.14159265358979323846;


in vec3 vWorldPos;
in vec3 vN;
in vec2 vUV;
in vec3 vT;
in vec2 vCurvDA;

uniform sampler2D tex;
uniform int hasTexture;

uniform sampler2D normalMap;
uniform int hasNormalMap;

uniform int objectId;

uniform vec3 diffuse;    // Kd fallback for the spheres
uniform vec3 specular;   // Ks
uniform float shininess; // alpha exponent

void main()
{
    vec3 N = normalize(vN);
    vec3 T = normalize(vT);
    vec3 B = normalize(cross(T, N));

    float tile = 1.0;
    switch (objectId) {
        case boxId:     tile = 1.0; break;
        case groundId:  tile = 16.0; break;
        case floorId:   tile = 4.0; break;
        case roomId:    tile = 32.0; break;
        case seaId:     tile = 256.0; break;
    }

    vec4 texColor = vec4(1.0);
    if (hasTexture == 1) {
        texColor = texture(tex, vUV * tile);
        texColor.rgb = pow(texColor.rgb, vec3(2.2));  // sRGB -> linear
    }

    vec3 Kd = (hasTexture == 1) ? texColor.rgb : diffuse;
    vec3 Ks = specular;
    float alpha = shininess;

    if (hasNormalMap == 1) {
        vec2 uv = vUV * tile;
        vec3 d = texture(normalMap, uv).xyz * 2.0 - vec3(1);
        N = normalize(d.x*T + d.y*B + d.z*N);
    }

    float theta2 = atan(vCurvDA.y, vCurvDA.x);
    float theta  = 0.5 * theta2;
    if (theta < 0.0) theta += PI;
    float thetaNorm = theta / PI;

    float isGeometry = (objectId == skyId) ? 0.0 : 1.0;
    G0_WorldPos = vec4(vWorldPos, isGeometry);
    G1_WorldN   = vec4(N, thetaNorm);
    G2_Kd       = vec4(Kd, 1.0);
    G3_KsAlpha  = vec4(Ks, alpha);
}
