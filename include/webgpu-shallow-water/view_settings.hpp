#pragma once

#include <webgpu-shallow-water/matrix.hpp>

struct alignas(16) ViewSettings
{
    Matrix4f viewMatrix;
    unsigned int cellsX;
    unsigned int cellsY;
};
