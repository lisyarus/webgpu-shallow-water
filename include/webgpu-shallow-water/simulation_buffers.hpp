#pragma once

#include <webgpu/webgpu.h>

struct SimulationBuffers
{
    WGPUTextureView bedWaterTextureView = nullptr;
    WGPUTextureView velocityTextureView = nullptr;
    WGPUBuffer particlesBuffer = nullptr;

    friend bool operator == (SimulationBuffers const &, SimulationBuffers const &) = default;
};
