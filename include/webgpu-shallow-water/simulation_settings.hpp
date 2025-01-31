#pragma once

struct SimulationSettings
{
    unsigned int cellsX = 256;
    unsigned int cellsY = 256;
    float dt = 0.1f;
    float gravity = 10.f;
    float friction = 0.f;

    void createUI();
};
