#include <webgpu-shallow-water/interaction_settings.hpp>

#include <imgui.h>

#include <iterator>
#include <format>

std::optional<Preset> InteractionSettings::createUI()
{
    ImGui::SeparatorText("Interaction");

    static char const * modeNames[] =
    {
        "None",
        "Add bed",
        "Remove bed",
        "Add water",
        "Remove water",
        "Move water",
    };

    ImGui::Combo("Action", (int *)&mode, modeNames, std::size(modeNames));

    if (mode != InteractionMode::None)
    {
        auto forceStr = std::format("{}%%", int(force * 100.f));

        ImGui::SliderFloat("Radius", &radius, 1.f, 256.f, "%.0f");
        ImGui::SliderFloat("Force", &force, 0.f, 1.f, forceStr.data());
    }

    static char const * presetNames[] =
    {
        "Islands",
        "River",
        "Canyon",
        "Shore",
    };

    int chosenPreset = -1;
    ImGui::Combo("Load preset", &chosenPreset, presetNames, std::size(presetNames));

    if (chosenPreset != -1)
        return (Preset)(chosenPreset);

    return std::nullopt;
}
