#pragma once

enum class InteractionMode : int
{
    None,
    AddBed,
    RemoveBed,
    AddWater,
    RemoveWater,
};

struct InteractionSettings
{
    InteractionMode mode = InteractionMode::None;
    float radius = 16.f;
    float force = 0.75f;

    void createUI();
};
