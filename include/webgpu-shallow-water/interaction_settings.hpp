#pragma once

enum class InteractionMode : int
{
    None,
    AddBed,
    RemoveBed,
};

struct InteractionSettings
{
    InteractionMode mode = InteractionMode::None;
    float radius = 16.f;

    void createUI();
};
