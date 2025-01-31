#include <webgpu-shallow-water/simulation_settings.hpp>

#include <imgui.h>

#include <format>

void SimulationSettings::createUI()
{
    const int cellsStep = 32;
    const int cellsMax = 1024;

    int cellsX = this->cellsX / cellsStep;
    int cellsY = this->cellsY / cellsStep;

    auto cellsXStr = std::format("{}", this->cellsX);
    auto cellsYStr = std::format("{}", this->cellsY);

    ImGui::SliderInt("Cells X", &cellsX, 1, cellsMax / cellsStep, cellsXStr.data());
    ImGui::SliderInt("Cells Y", &cellsY, 1, cellsMax / cellsStep, cellsYStr.data());

    this->cellsX = cellsX * cellsStep;
    this->cellsY = cellsY * cellsStep;

    ImGui::SliderFloat("dt", &dt, 0.01f, 1.f, "%.2f");
    ImGui::SliderFloat("Gravity", &gravity, 0.f, 100.f, "%.1f");
    ImGui::SliderFloat("Friction", &friction, 0.f, 1.f, "%.2f");
}
