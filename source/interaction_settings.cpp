#include <webgpu-shallow-water/interaction_settings.hpp>

#include <imgui.h>

#include <iterator>

void InteractionSettings::createUI()
{
    static char const * modeNames[] =
    {
        "None",
        "Add bed",
        "Remove bed",
    };

    ImGui::Combo("Action", (int *)&mode, modeNames, std::size(modeNames));

    if (mode != InteractionMode::None)
        ImGui::SliderFloat("Radius", &radius, 0.f, 256.f, "%.0f");
}
