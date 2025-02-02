#pragma once

struct alignas(16) Matrix4f
{
    // Column-major
    float values[16];
};
