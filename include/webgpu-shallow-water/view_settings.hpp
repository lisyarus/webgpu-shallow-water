#pragma once

#include <webgpu-shallow-water/matrix.hpp>

struct ViewSettings
{
    Matrix4f viewMatrix;
    unsigned int cellsX;
    unsigned int cellsY;

    bool showVelocity = false;

    void createUI();
};
