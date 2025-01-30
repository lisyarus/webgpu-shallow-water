#pragma once

#include <webgpu-shallow-water/matrix.hpp>

struct Camera
{
    Camera(int cellsX, int cellsY, float aspectRatio);

    void update(float dt, int cellsX, int cellsY, float aspectRatio);

    Matrix4f viewMatrix();

private:
    float viewCenterX_;
    float viewCenterY_;

    float viewExtentXTarget_;
    float viewExtentYTarget_;

    float viewExtentX_;
    float viewExtentY_;

    void updateViewExtent(int cellsX, int cellsY, float aspectRatio);
};
