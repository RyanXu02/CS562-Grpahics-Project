#version 330

uniform mat4 WorldView, WorldInverse, WorldProj, ModelTr, NormalTr;

in vec4 vertex;
in vec3 vertexNormal;
in vec2 vertexTexture;
in vec3 vertexTangent;
in vec3 vertexCurvDir;

out vec3 vWorldPos;
out vec3 vN;
out vec2 vUV;
out vec3 vT;
out vec2 vCurvDA;

void main()
{
    gl_Position = WorldProj * WorldView * ModelTr * vertex;

    vec3 worldPos = (ModelTr * vertex).xyz;
    vWorldPos = worldPos;

    vN = vertexNormal * mat3(NormalTr);
    vT = mat3(ModelTr) * vertexTangent;
    vUV = vertexTexture;

    vec3 curvWS = (ModelTr * vec4(vertexCurvDir, 0.0)).xyz;

    vec4 clipBase = gl_Position;
    vec4 clipEnd  = WorldProj * WorldView * vec4(worldPos + curvWS, 1.0);

    vec2 ndcBase  = clipBase.xy / clipBase.w;
    vec2 ndcEnd   = clipEnd.xy  / clipEnd.w;
    vec2 screenDir = ndcEnd - ndcBase;

    float lenSD = length(screenDir);
    if (lenSD < 1e-6) {
        vCurvDA = vec2(1.0, 0.0);
    } else {
        screenDir /= lenSD;
        // (cos 2theta, sin 2theta)
        vCurvDA = vec2(screenDir.x * screenDir.x - screenDir.y * screenDir.y, 2.0 * screenDir.x * screenDir.y);
    }
}