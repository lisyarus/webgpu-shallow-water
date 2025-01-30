#include <webgpu-shallow-water/application.hpp>
#include <webgpu-shallow-water/simulation_settings.hpp>
#include <webgpu-shallow-water/simulator.hpp>
#include <webgpu-shallow-water/renderer.hpp>

int main()
{
    Application application;

    SimulationSettings simulationSettings;
    Simulator simulator(application.device());
    Renderer renderer(application.device(), application.surfaceFormat());

    while (application.running())
    {
        application.pollEvents();

        auto surfaceTextureView = application.newFrame();

        simulationSettings.createUI();

        simulator.step(simulationSettings);
        renderer.update(simulator.buffers());
        renderer.render(surfaceTextureView);

        application.drawUI(surfaceTextureView);

        application.present();
    }
}
