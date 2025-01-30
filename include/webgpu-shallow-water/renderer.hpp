#pragma once

#include <webgpu-shallow-water/simulation_buffers.hpp>

#include <webgpu/webgpu.h>

#include <memory>

struct Renderer
{
    Renderer(WGPUDevice device, WGPUTextureFormat surfaceFormat);
    ~Renderer();

    void update(SimulationBuffers const & simulationBuffers);
    void render(WGPUTextureView target);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
