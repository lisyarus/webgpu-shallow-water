#pragma once

#include <webgpu-shallow-water/simulation_settings.hpp>
#include <webgpu-shallow-water/simulation_buffers.hpp>

#include <webgpu/webgpu.h>

#include <memory>

struct Simulator
{
    Simulator(WGPUDevice device);
    ~Simulator();

    void step(SimulationSettings const & settings);

    SimulationBuffers buffers();

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
