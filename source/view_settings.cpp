#include <webgpu-shallow-water/view_settings.hpp>

#include <imgui.h>

void ViewSettings::createUI()
{
    ImGui::SeparatorText("View");

    ImGui::Checkbox("Show velocity", &showVelocity);
    ImGui::Checkbox("Show particles", &showParticles);
}
