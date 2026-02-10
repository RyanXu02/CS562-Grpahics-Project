#version 330

uniform mat4 WorldView, WorldInverse, WorldProj, ModelTr, NormalTr;

in vec4 vertex;
in vec3 vertexNormal;
in vec2 vertexTexture;
in vec3 vertexTangent;

out vec3 vWorldPos;
out vec3 vN;
out vec2 vUV;
out vec3 vT;

void main()
{
    gl_Position = WorldProj * WorldView * ModelTr * vertex;

    vec3 worldPos = (ModelTr * vertex).xyz;
    vWorldPos = worldPos;

    vN = vertexNormal * mat3(NormalTr);
    vT = mat3(ModelTr) * vertexTangent;
    vUV = vertexTexture;
}