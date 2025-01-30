#include <webgpu-shallow-water/application.hpp>
#include <webgpu-shallow-water/simulation_settings.hpp>
#include <webgpu-shallow-water/simulator.hpp>
#include <webgpu-shallow-water/renderer.hpp>
#include <webgpu-shallow-water/camera.hpp>

#include <chrono>

int main()
{
    Application application;

    SimulationSettings simulationSettings;
    Simulator simulator(application.device());
    Renderer renderer(application.device(), application.surfaceFormat());
    Camera camera(simulationSettings.cellsX, simulationSettings.cellsY, application.aspectRatio());

    auto lastFrameStart = std::chrono::high_resolution_clock::now();

    while (application.running())
    {
        auto now = std::chrono::high_resolution_clock::now();
        float frameDt = std::chrono::duration_cast<std::chrono::duration<float>>(now - lastFrameStart).count();
        lastFrameStart = now;

        application.pollEvents();

        auto surfaceTextureView = application.newFrame();

        simulationSettings.createUI();

        camera.update(frameDt, simulationSettings.cellsX, simulationSettings.cellsY, application.aspectRatio());

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
