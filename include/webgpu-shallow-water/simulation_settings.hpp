#pragma once

enum class BorderType : unsigned int
{
    Wall,
    Source,
    Drain,
    Waves,
};

struct SimulationSettings
{
    bool paused = false;
    unsigned int cellsX = 256;
    unsigned int cellsY = 256;
    float dt = 0.2f;
    float gravity = 10.f;
    float friction = 0.f;
    unsigned int particleCount = 16 * 1024;

    BorderType leftBorder = BorderType::Wall;
    BorderType rightBorder = BorderType::Wall;
    BorderType bottomBorder = BorderType::Wall;
    BorderType topBorder = BorderType::Wall;

    void createUI();
};
