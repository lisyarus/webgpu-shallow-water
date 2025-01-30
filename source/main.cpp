#include <webgpu-shallow-water/application.hpp>
#include <webgpu-shallow-water/simulation_settings.hpp>
#include <webgpu-shallow-water/interaction_settings.hpp>
#include <webgpu-shallow-water/simulator.hpp>
#include <webgpu-shallow-water/renderer.hpp>
#include <webgpu-shallow-water/camera.hpp>

#include <chrono>
#include <optional>

int main()
{
    Application application;

    SimulationSettings simulationSettings;
    InteractionSettings interactionSettings;

    Simulator simulator(application.device());

    Renderer renderer(application.device(), application.surfaceFormat());

    Camera camera(simulationSettings.cellsX, simulationSettings.cellsY, application.aspectRatio());

    std::optional<Vector2f> oldMouse;

    auto lastFrameStart = std::chrono::high_resolution_clock::now();

    while (application.running())
    {
        auto now = std::chrono::high_resolution_clock::now();
        float frameDt = std::chrono::duration_cast<std::chrono::duration<float>>(now - lastFrameStart).count();
        lastFrameStart = now;

        application.pollEvents();

        auto surfaceTextureView = application.newFrame();

        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SeparatorText("Simulation");
        simulationSettings.createUI();
        ImGui::SeparatorText("Interaction");
        interactionSettings.createUI();
        ImGui::End();

        camera.update(frameDt, simulationSettings.cellsX, simulationSettings.cellsY, application.aspectRatio());

        if (application.mouseDown())
        {
            Vector2f mouse = application.mousePosition();
            mouse.x = 2.f * mouse.x / application.width() - 1.f;
            mouse.y = 1.f - 2.f * mouse.y / application.height();
            mouse = camera.ndcToWorld(mouse);

            if (!oldMouse)
                oldMouse = mouse;

            simulator.interact(frameDt, interactionSettings, *oldMouse, mouse);

            oldMouse = mouse;
        }
        else
        {
            oldMouse = std::nullopt;
        }

        simulator.step(simulationSettings);

        ViewSettings viewSettings
        {
            .viewMatrix = camera.viewMatrix(),
            .simulationSizeX = (float)simulationSettings.cellsX,
            .simulationSizeY = (float)simulationSettings.cellsY,
        };

        renderer.update(simulator.buffers());
        renderer.render(surfaceTextureView, viewSettings);

        application.drawUI(surfaceTextureView);

        application.present();
    }
}
