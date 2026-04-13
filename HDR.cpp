#include "HDR.h"

#include "math.h"
#include <fstream>
#include <stdlib.h>

#include <glbinding/gl/gl.h>
#include <glbinding/Binding.h>
using namespace gl;

#define GLM_FORCE_CTOR_INIT
#define GLM_FORCE_RADIANS
#define GLM_SWIZZLE
#include <glm/glm.hpp>

#include "texture.h"

//#define STB_IMAGE_IMPLEMENTATION
//#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include <glu.h>                // For gluErrorString
#define CHECKERROR {GLenum err = glGetError(); if (err != GL_NO_ERROR) { fprintf(stderr, "OpenGL error (at line texture.cpp:%d): %s\n", __LINE__, gluErrorString(err)); exit(-1);} }


HDR::HDR(const std::string& path)
{
    stbi_set_flip_vertically_on_load(true);
    image = stbi_loadf(path.c_str(), &width, &height, &depth, 4);
    printf("%d %d %d %s\n", depth, width, height, path.c_str());
    if (!image) {
        printf("\nRead error on file %s:\n  %s\n\n", path.c_str(), stbi_failure_reason());
        exit(-1);
    }

    glGenTextures(1, &textureId);   // Get an integer id for this texture from OpenGL
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 10);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (int)GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (int)GL_LINEAR_MIPMAP_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(image);
}