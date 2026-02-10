#version 330 core

uniform mat4 WorldView;
uniform mat4 WorldProj;
uniform mat4 ModelTr;

in vec4 vertex;

void main()
{
    gl_Position = WorldProj * WorldView * ModelTr * vertex;
}
