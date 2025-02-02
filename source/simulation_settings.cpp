#include <webgpu-shallow-water/simulation_settings.hpp>

#include <imgui.h>

#include <format>
#include <cmath>

void SimulationSettings::createUI()
{
    ImGui::SeparatorText("Simulation");

    if (ImGui::Button(paused ? "Paused" : "Running"))
        paused ^= true;

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

    float particlesLog2 = particleCount == 0 ? 0.f : std::log2(1.f * particleCount);
    auto particlesStr = std::format("{}", this->particleCount);

    ImGui::SliderFloat("Particles", &particlesLog2, 6.f, 20.f, particlesStr.data());

    particleCount = (particlesLog2 == 0.f) ? 0 : std::exp2(particlesLog2);
    particleCount = std::max(64u, (particleCount / 64u) * 64u);

    ImGui::SeparatorText("Borders");

    char const * borderTypeStr[]
    {
        "Wall",
        "Source",
        "Drain",
        "Waves",
    };

    ImGui::Combo("Left border", (int *)&leftBorder, borderTypeStr, std::size(borderTypeStr));
    ImGui::Combo("Right border", (int *)&rightBorder, borderTypeStr, std::size(borderTypeStr));
    ImGui::Combo("Bottom border", (int *)&bottomBorder, borderTypeStr, std::size(borderTypeStr));
    ImGui::Combo("Top border", (int *)&topBorder, borderTypeStr, std::size(borderTypeStr));
}
