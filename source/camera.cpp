#include <webgpu-shallow-water/camera.hpp>

#include <cmath>

static const float PADDING = 0.1f;
static const float SMOOTHNESS = 10.f;

Camera::Camera(int cellsX, int cellsY, float aspectRatio)
{
    viewCenterX_ = cellsX / 2.f;
    viewCenterY_ = cellsY / 2.f;

    updateViewExtent(cellsX, cellsY, aspectRatio);

    viewExtentX_ = viewExtentXTarget_;
    viewExtentY_ = viewExtentYTarget_;
}

void Camera::update(float dt, int cellsX, int cellsY, float aspectRatio)
{
    viewCenterX_ = cellsX / 2.f;
    viewCenterY_ = cellsY / 2.f;

    updateViewExtent(cellsX, cellsY, aspectRatio);

    float const smoothnessFactor = - std::expm1(- dt * SMOOTHNESS);

    viewExtentX_ += (viewExtentXTarget_ - viewExtentX_) * smoothnessFactor;
    viewExtentY_ += (viewExtentYTarget_ - viewExtentY_) * smoothnessFactor;
}

Matrix4f Camera::viewMatrix()
{
    // Column-major
    return {
        1.f / viewExtentX_, 0.f, 0.f, 0.f,
        0.f, 1.f / viewExtentY_, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        - viewCenterX_ / viewExtentX_, - viewCenterY_ / viewExtentY_, 0.f, 1.f,
    };
}

void Camera::updateViewExtent(int cellsX, int cellsY, float aspectRatio)
{
    float simulationAspectRatio = (cellsX * 1.f) / cellsY;

    if (simulationAspectRatio < aspectRatio)
    {
        viewExtentYTarget_ = cellsY / 2.f * (1.f + PADDING);
        viewExtentXTarget_ = viewExtentYTarget_ * aspectRatio;
    }
    else
    {
        viewExtentXTarget_ = cellsX / 2.f * (1.f + PADDING);
        viewExtentYTarget_ = viewExtentXTarget_ / aspectRatio;
    }
}
