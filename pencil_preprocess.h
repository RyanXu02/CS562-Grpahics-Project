// helpers for pencil rendering
// don't use during runtime this is pretty slow

#pragma once

#include <vector>
#include <glm/glm.hpp>

// Returns GL_TEXTURE_3D id
// Dimensions are texSize x texSize x numTones
// Slice 0 is pure white, successive slices get progressively darker
// Sample with (uv, brightness) where brightness in [0,1] selects the tone slice and gets the pre rendered strokes for free
unsigned int CreatePencilTonesTexture(int texSize = 256, int numTones = 32);

// Returns GL_TEXTURE_2D id in RGB8
unsigned int CreatePaperNormalMap(int size = 512);

// Returns a GL_TEXTURE_2D id of a single tone of loose pencil strokes used to tint the final contour lines.
unsigned int CreateContourPencilTex(int width = 256, int height = 256);

// For each vertex v return a tangent vector along the direction of minimum normal curvature.  
std::vector<glm::vec3> ComputeMinCurvatureDirs(
    const std::vector<glm::vec3>& positions,
    const std::vector<glm::vec3>& normals,
    const std::vector<unsigned int>& indices);


class Shape;

// compute the min curvature directions and appends a new VBO to the Shape's existing VAO bound to vertex attribute location 4 (vec3)
void AttachCurvatureAttribute(Shape* shape);