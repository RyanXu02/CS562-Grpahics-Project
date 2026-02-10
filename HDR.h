#ifndef _HDR_
#define _HDR_

#include <string>
#include "texture.h"

class HDR : public Texture
{
public:
    float* image;
    HDR(const std::string& filename);
};

#endif