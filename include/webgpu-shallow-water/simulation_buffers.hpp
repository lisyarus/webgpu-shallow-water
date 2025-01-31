#pragma once

#include <webgpu/webgpu.h>

struct SimulationBuffers
{
    WGPUTextureView bedWaterTextureView = nullptr;
    WGPUTextureView velocityTextureView = nullptr;

    friend bool operator == (SimulationBuffers const &, SimulationBuffers const &) = default;
};
