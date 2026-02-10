////////////////////////////////////////////////////////////////////////
// A small library of 4x4 matrix operations needed for graphics
// transformations.  glm::mat4 is a 4x4 float matrix class with indexing
// and printing methods.  A small list or procedures are supplied to
// create Rotate, Scale, Translate, and Perspective matrices and to
// return the product of any two such.

#include <glm/glm.hpp>

#include "math.h"
#include "transform.h"

float* Pntr(glm::mat4& M)
{
    return &(M[0][0]);
}

//@@ The following procedures should calculate and return 4x4
//transformation matrices instead of the identity.

// Return a rotation matrix around an axis (0:X, 1:Y, 2:Z) by an angle
// measured in degrees.  NOTE: Make sure to convert degrees to radians
// before using sin and cos.  HINT: radians = degrees*PI/180
const float pi = 3.14159f;
glm::mat4 Rotate(const int i, const float theta)
{
	float const rad = theta * pi / 180.0f;
    glm::mat4 R(1.0);
	if (i == 0) { // X axis
		R[1].y = cos(rad); R[1].z = sin(rad);
		R[2].y = -sin(rad); R[2].z = cos(rad);
	}
    else if (i == 1) { // Y axis
        R[0].x = cos(rad); R[0].z = -sin(rad);
        R[2].x = sin(rad); R[2].z = cos(rad);
    }
    else if (i == 2) { // Z axis
        R[0].x = cos(rad); R[0].y = sin(rad);
        R[1].x = -sin(rad); R[1].y = cos(rad);
	}
    return R;
}

// Return a scale matrix
glm::mat4 Scale(const float x, const float y, const float z)
{
    glm::mat4 S(1.0);
    S[0].x = x;
    S[1].y = y;
    S[2].z = z;
    return S;
}

// Return a translation matrix
glm::mat4 Translate(const float x, const float y, const float z)
{
    glm::mat4 T(1.0);
	T[3].x = x;
	T[3].y = y;
	T[3].z = z;
    return T;
}

// Returns a perspective projection matrix
glm::mat4 Perspective(const float rx, const float ry,
             const float front, const float back)
{
    glm::mat4 P(1.0);
    P[0].x = 1 / rx;
	P[1].y = 1 / ry;
	P[2].z = -(back + front) / (back - front);
	P[2].w = -1;
	P[3].z = -(2 * back * front) / (back - front);
    return P;
}

glm::mat4 LookAt(const glm::vec3 E, const glm::vec3 C, const glm::vec3 U)
{
    const glm::vec3 V = glm::normalize(C - E);
    const glm::vec3 A = glm::normalize(glm::cross(V, U));
    const glm::vec3 B = glm::cross(A, V);
    glm::mat4 M(1.0);
    M[0].x = A.x;   M[1].x = A.y;   M[2].x = A.z;   M[3].x = -glm::dot(A, E);
    M[0].y = B.x;   M[1].y = B.y;   M[2].y = B.z;   M[3].y = -glm::dot(B, E);
    M[0].z = -V.x;  M[1].z = -V.y;  M[2].z = -V.z;  M[3].z = glm::dot(V, E);    
    // 0, 0, 0, 1
    return M;
}

