#include <webgpu-shallow-water/interaction_settings.hpp>

#include <imgui.h>

#include <iterator>
#include <format>

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
    {
        auto forceStr = std::format("{}%%", int(force * 100.f));

        ImGui::SliderFloat("Radius", &radius, 1.f, 256.f, "%.0f");
        ImGui::SliderFloat("Force", &force, 0.f, 1.f, forceStr.data());
    }
}
