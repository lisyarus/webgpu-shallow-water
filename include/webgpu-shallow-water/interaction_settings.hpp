#pragma once

#include <optional>

enum class InteractionMode : int
{
    None,
    AddBed,
    RemoveBed,
    AddWater,
    RemoveWater,
    MoveWater,
};

enum class Preset : int
{
    Islands,
    River,
    Canyon,
    Shore,
};

struct InteractionSettings
{
    InteractionMode mode = InteractionMode::AddWater;
    float radius = 16.f;
    float force = 0.75f;

    std::optional<Preset> createUI();
};
