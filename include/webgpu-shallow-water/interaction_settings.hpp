#pragma once

enum class InteractionMode : int
{
    None,
    AddBed,
    RemoveBed,
    AddWater,
    RemoveWater,
    MoveWater,
};

struct InteractionSettings
{
    InteractionMode mode = InteractionMode::AddWater;
    float radius = 16.f;
    float force = 0.75f;

    void createUI();
};
