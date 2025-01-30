#pragma once

#include <webgpu-shallow-water/simulation_buffers.hpp>
#include <webgpu-shallow-water/view_settings.hpp>

#include <webgpu/webgpu.h>

#include <memory>

struct Renderer
{
    Renderer(WGPUDevice device, WGPUTextureFormat surfaceFormat);
    ~Renderer();

    void update(SimulationBuffers const & simulationBuffers);
    void render(WGPUTextureView target, ViewSettings const & viewSettings);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
