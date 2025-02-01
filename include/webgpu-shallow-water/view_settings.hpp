#pragma once

#include <webgpu-shallow-water/matrix.hpp>
#include <webgpu-shallow-water/vector.hpp>

#include <optional>

struct ViewSettings
{
    Matrix4f viewMatrix;
    unsigned int cellsX;
    unsigned int cellsY;

    struct ActionInfo
    {
        Vector2f position;
        float radius;
    };

    std::optional<ActionInfo> action = std::nullopt;

    bool showVelocity = false;

    unsigned int particleCount;

    bool showParticles = false;

    void createUI();
};
