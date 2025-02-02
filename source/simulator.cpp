#include <webgpu-shallow-water/simulator.hpp>

#include <iostream>
#include <cmath>

static char const shaderSource[] =
R"(

const PI = 3.1415926535;

struct InteractionSettings
{
    mode : u32,
    radius : f32,
    force : f32,
    dt : f32,
    oldPosition : vec2f,
    position : vec2f,
    preset : u32,
}

struct SimulationSettings
{
    size : vec2u,
    dt : f32,
    dx : f32,
    gravity : f32,
    frictionFactor : f32,
    timestamp : u32,
    borderMask : u32,
}

struct Particle
{
    position : vec2f,
    lifetime : u32,
    alive : u32,
}

struct RNGState
{
    state : u32,
}

fn rngHash(state : u32) -> u32
{
    var x = state;
    x = x * 747796405u + 2891336453u;
    let y = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (y >> 22u) ^ y;
    return x;
}

fn randomUint(state : ptr<function, RNGState>) -> u32
{
    (*state).state = rngHash((*state).state);
    return (*state).state;
}

fn rngInit(state : ptr<function, RNGState>, seed : u32)
{
    (*state).state += seed;
    randomUint(state);
}

fn randomFloat(state : ptr<function, RNGState>) -> f32
{
    return f32(randomUint(state)) / 4294967295.0;
}

fn perlinNoiseGridVector(gridPoint : vec2u, seed : u32) -> vec2f
{
    var state = RNGState(0u);
    rngInit(&state, seed);
    rngInit(&state, gridPoint.x);
    rngInit(&state, gridPoint.y);

    let angle = randomFloat(&state) * (2.0 * PI);

    return vec2f(cos(angle), sin(angle));
}

fn perlinNoise(point : vec2f, gridSize : f32, seed : u32) -> f32
{
    let gridPosition = point / gridSize;

    let ix = u32(floor(gridPosition.x));
    let iy = u32(floor(gridPosition.y));

    let tx = gridPosition.x - f32(ix);
    let ty = gridPosition.y - f32(iy);

    let sx = smoothstep(0.0, 1.0, tx);
    let sy = smoothstep(0.0, 1.0, ty);

    let v00 = perlinNoiseGridVector(vec2u(ix + 0u, iy + 0u), seed);
    let v01 = perlinNoiseGridVector(vec2u(ix + 1u, iy + 0u), seed);
    let v10 = perlinNoiseGridVector(vec2u(ix + 0u, iy + 1u), seed);
    let v11 = perlinNoiseGridVector(vec2u(ix + 1u, iy + 1u), seed);

    let d00 = dot(v00, vec2f(tx, ty));
    let d01 = dot(v01, vec2f(tx - 1.0, ty));
    let d10 = dot(v10, vec2f(tx, ty - 1.0));
    let d11 = dot(v11, vec2f(tx - 1.0, ty - 1.0));

    let d = mix(
        mix(d00, d01, sx),
        mix(d10, d11, sx),
        sy
    );

    return 0.5 + d / sqrt(2.0);
}

@group(0) @binding(0) var bedWaterTexture : texture_storage_2d<rg32float, read_write>;
@group(0) @binding(1) var flowXTexture : texture_storage_2d<r32float, read_write>;
@group(0) @binding(2) var flowYTexture : texture_storage_2d<r32float, read_write>;
@group(0) @binding(3) var velocityTexture : texture_storage_2d<rg32float, read_write>;
@group(0) @binding(4) var<storage, read_write> particles : array<Particle>;

@group(1) @binding(0) var<uniform> interactionSettings : InteractionSettings;
@group(1) @binding(1) var<uniform> simulationSettings : SimulationSettings;

@compute @workgroup_size(16, 16)
fn clearBuffers(@builtin(global_invocation_id) id: vec3u)
{
    textureStore(bedWaterTexture, id.xy, vec4f(0.0));
    textureStore(flowXTexture, id.xy, vec4f(0.0));
    textureStore(flowYTexture, id.xy, vec4f(0.0));

    if (id.x + 1u == simulationSettings.size.x) {
        textureStore(flowXTexture, id.xy + vec2u(1u, 0u), vec4f(0.0));
    }

    if (id.y + 1u == simulationSettings.size.y) {
        textureStore(flowXTexture, id.xy + vec2u(0u, 1u), vec4f(0.0));
    }
}

@compute @workgroup_size(16, 16)
fn loadPreset(@builtin(global_invocation_id) id: vec3u)
{
    let position = vec2f(id.xy) + vec2f(0.5);

    let simulationMinSize = f32(min(simulationSettings.size.x, simulationSettings.size.y));
    let baseNoiseGridSize = simulationMinSize / 16.0;

    var bed = 0.0;

    if (interactionSettings.preset == 0u) {
        let noise = 0.75 * perlinNoise(position, baseNoiseGridSize, simulationSettings.timestamp)
            + 0.25 * perlinNoise(position, baseNoiseGridSize / 2.0, simulationSettings.timestamp);

        let center = vec2f(simulationSettings.size) / 2.0;
        let t = noise - 0.5 * length(position - center) / simulationMinSize;
        bed = 10.0 * smoothstep(0.45, 0.55, t);
    } else if (interactionSettings.preset == 1u) {
        let noise = perlinNoise(vec2f(position.x, 0.0), baseNoiseGridSize * 2.0, simulationSettings.timestamp);
        let riverY = mix(0.4, 0.6, noise) * f32(simulationSettings.size.y);

        bed = 10.0 * clamp(0.0, 1.0, 8.0 * abs(position.y - riverY) / simulationMinSize - 0.25);
    } else if (interactionSettings.preset == 2u) {
        let noise = perlinNoise(position, baseNoiseGridSize * 2.0, simulationSettings.timestamp);

        bed = clamp(20.0 * abs(2.0 * noise - 1.0) - 4.0, 0.0, 10.0);
    } else if (interactionSettings.preset == 3u) {
        let noise = perlinNoise(vec2f(position.x, 0.0), baseNoiseGridSize * 2.0, simulationSettings.timestamp);

        bed = clamp(20.0 * (position.y / f32(simulationSettings.size.y) - mix(0.4, 0.6, noise)), 0.0, 10.0);
    }

    textureStore(bedWaterTexture, id.xy, vec4f(bed, 0.0, 0.0, 0.0));
}

fn pointToSegmentDistance(p : vec2f, s0 : vec2f, s1 : vec2f) -> f32
{
    if (all(s0 == s1)) {
        return length(p - s0);
    }

    let s = s1 - s0;
    let d = p - s0;

    let t = dot(d, s) / dot(s, s);

    if (t < 0.0) {
        return length(d);
    } else if (t > 1.0) {
        return length(p - s1);
    } else {
        return length(d - s * t);
    }
}

@compute @workgroup_size(16, 16)
fn interact(@builtin(global_invocation_id) id: vec3u)
{
    let position = vec2f(id.xy) + vec2f(0.5);

    if (interactionSettings.mode >= 1u && interactionSettings.mode <= 4u) {

        let distance = pointToSegmentDistance(position, interactionSettings.oldPosition, interactionSettings.position);

        let delta = pow(128.0, interactionSettings.force) * interactionSettings.force * interactionSettings.dt * smoothstep(interactionSettings.radius, interactionSettings.radius * interactionSettings.force - 1.0, distance);

        var value = textureLoad(bedWaterTexture, id.xy);

        if (interactionSettings.mode == 1u) {
            value.x += 10.0 * delta;
        } else if (interactionSettings.mode == 2u) {
            value.x -= 10.0 * delta;
        } else if (interactionSettings.mode == 3u) {
            value.y += delta;
        } else if (interactionSettings.mode == 4u) {
            value.y -= delta;
        }

        value.x = max(0.0, min(10.0, value.x));
        value.y = max(0.0, value.y);

        textureStore(bedWaterTexture, id.xy, value);
    } else if (interactionSettings.mode == 5u) {
        if (id.x >= 1u && id.y >= 1u) {
            let distance = length(interactionSettings.position - position);
            let distanceFactor = smoothstep(interactionSettings.radius * 1.1, interactionSettings.radius * 0.9, distance);

            let impulse = distanceFactor * (interactionSettings.position - interactionSettings.oldPosition) / interactionSettings.dt * interactionSettings.force;

            var flowX = textureLoad(flowXTexture, id.xy).r;
            var flowY = textureLoad(flowYTexture, id.xy).r;

            flowX += impulse.x;
            flowY += impulse.y;

            textureStore(flowXTexture, id.xy, vec4f(flowX, 0.0, 0.0, 0.0));
            textureStore(flowYTexture, id.xy, vec4f(flowY, 0.0, 0.0, 0.0));
        }
    }
}

fn waterSurfaceAt(p : vec2u) -> f32
{
    let bedWaterSample = textureLoad(bedWaterTexture, p);
    return bedWaterSample.x + bedWaterSample.y;
}

fn borderFlow(borderType : u32, bed : f32) -> f32
{
    if (bed > 1.0) {
        return 0.0;
    }

    if (borderType == 0u) {
        return 0.0;
    } else if (borderType == 1u) {
        return 10.0;
    } else {
        return -10.0;
    }
}

@compute @workgroup_size(16, 16)
fn stepAccelerate(@builtin(global_invocation_id) id: vec3u)
{
    let bedWaterSample = textureLoad(bedWaterTexture, id.xy);
    let waterSurfaceBase = bedWaterSample.x + bedWaterSample.y;

    let leftBorder = simulationSettings.borderMask & 3u;
    let rightBorder = (simulationSettings.borderMask >> 2) & 3u;
    let bottomBorder = (simulationSettings.borderMask >> 4) & 3u;
    let topBorder = (simulationSettings.borderMask >> 6) & 3u;

    if (id.x == 0u) {
        textureStore(flowXTexture, id.xy, vec4f(borderFlow(leftBorder, bedWaterSample.x), 0.0, 0.0, 0.0));
    }

    if (id.x + 1u == simulationSettings.size[0]) {
        textureStore(flowXTexture, vec2u(id.x + 1u, id.y), vec4f(-borderFlow(rightBorder, bedWaterSample.x), 0.0, 0.0, 0.0));
    }

    if (id.y == 0u) {
        textureStore(flowYTexture, id.xy, vec4f(borderFlow(bottomBorder, bedWaterSample.x), 0.0, 0.0, 0.0));
    }

    if (id.y + 1u == simulationSettings.size[1]) {
        textureStore(flowYTexture, vec2u(id.x, id.y + 1u), vec4f(-borderFlow(topBorder, bedWaterSample.x), 0.0, 0.0, 0.0));
    }

    if (id.x >= 1u) {
        var flowX = textureLoad(flowXTexture, id.xy).x;
        flowX *= simulationSettings.frictionFactor;
        flowX += simulationSettings.gravity * simulationSettings.dt * (waterSurfaceAt(id.xy - vec2u(1, 0)) - waterSurfaceBase);
        textureStore(flowXTexture, id.xy, vec4f(flowX, 0.0, 0.0, 0.0));
    }

    if (id.y >= 1u) {
        var flowY = textureLoad(flowYTexture, id.xy).x;
        flowY *= simulationSettings.frictionFactor;
        flowY += simulationSettings.gravity * simulationSettings.dt * (waterSurfaceAt(id.xy - vec2u(0, 1)) - waterSurfaceBase);
        textureStore(flowYTexture, id.xy, vec4f(flowY, 0.0, 0.0, 0.0));
    }
}

@compute @workgroup_size(16, 16)
fn stepScale(@builtin(global_invocation_id) id: vec3u)
{
    let inFlowX = textureLoad(flowXTexture, id.xy).x;
    let inFlowY = textureLoad(flowYTexture, id.xy).x;
    let outFlowX = textureLoad(flowXTexture, id.xy + vec2u(1, 0)).x;
    let outFlowY = textureLoad(flowYTexture, id.xy + vec2u(0, 1)).x;

    let totalOutflow = 0.0
        + max(0.0, -inFlowX)
        + max(0.0, outFlowX)
        + max(0.0, -inFlowY)
        + max(0.0, outFlowY)
        ;

    let water = textureLoad(bedWaterTexture, id.xy).y;

    let maxOutflow = water * simulationSettings.dx * simulationSettings.dx / simulationSettings.dt;

    if (totalOutflow > maxOutflow && totalOutflow > 0.0) {
        let scale = maxOutflow / totalOutflow;

        if (inFlowX < 0.0) { textureStore(flowXTexture, id.xy, vec4f(inFlowX * scale, 0.0, 0.0, 0.0)); }
        if (inFlowY < 0.0) { textureStore(flowYTexture, id.xy, vec4f(inFlowY * scale, 0.0, 0.0, 0.0)); }
        if (outFlowX > 0.0) { textureStore(flowXTexture, id.xy + vec2u(1, 0), vec4f(outFlowX * scale, 0.0, 0.0, 0.0)); }
        if (outFlowY > 0.0) { textureStore(flowYTexture, id.xy + vec2u(0, 1), vec4f(outFlowY * scale, 0.0, 0.0, 0.0)); }
    }
}

@compute @workgroup_size(16, 16)
fn stepMove(@builtin(global_invocation_id) id: vec3u)
{
    let inFlowX = textureLoad(flowXTexture, id.xy).x;
    let inFlowY = textureLoad(flowYTexture, id.xy).x;
    let outFlowX = textureLoad(flowXTexture, id.xy + vec2u(1, 0)).x;
    let outFlowY = textureLoad(flowYTexture, id.xy + vec2u(0, 1)).x;

    let totalFlow = inFlowX + inFlowY - outFlowX - outFlowY;

    var bedWaterSample = textureLoad(bedWaterTexture, id.xy);
    let waterOld = bedWaterSample.y;
    bedWaterSample.y += totalFlow * simulationSettings.dt / simulationSettings.dx / simulationSettings.dx;
    textureStore(bedWaterTexture, id.xy, bedWaterSample);

    let waterAverage = (bedWaterSample.y + waterOld) / 2.0;

    var velocity = vec2f(0.0);

    if (waterAverage > 0.0) {
        velocity = vec2f(inFlowX + outFlowX, inFlowY + outFlowY) / (2.0 * simulationSettings.dx * waterAverage);
    }

    textureStore(velocityTexture, id.xy, vec4f(velocity, 0.0, 0.0));
}

@compute @workgroup_size(64)
fn updateParticles(@builtin(global_invocation_id) id: vec3u)
{
    var particle = particles[id.x];

    if (particle.lifetime > 0u) {
        particle.lifetime -= 1u;
    }

    if (false
        || particle.alive == 0u
        || particle.lifetime == 0u
        || particle.position[0] < 0.0
        || particle.position[1] < 0.0
        || particle.position[0] >= f32(simulationSettings.size[0])
        || particle.position[1] >= f32(simulationSettings.size[1])
        ) {
        var rngState = RNGState(0u);
        rngInit(&rngState, id.x);
        rngInit(&rngState, simulationSettings.timestamp);

        particle.position.x = randomFloat(&rngState) * f32(simulationSettings.size[0]);
        particle.position.y = randomFloat(&rngState) * f32(simulationSettings.size[1]);

        particle.alive = 1u;
        particle.lifetime = (randomUint(&rngState) % 300u);
    }

    let px = max(0.0, min(f32(simulationSettings.size[0]) - 1.0, particle.position[0] - 0.5));
    let py = max(0.0, min(f32(simulationSettings.size[1]) - 1.0, particle.position[1] - 0.5));

    let ix = u32(floor(px));
    let iy = u32(floor(py));

    let tx = px - f32(ix);
    let ty = py - f32(iy);

    if (textureLoad(bedWaterTexture, vec2u(ix, iy)).y < 1e-3) {
        particle.alive = 0u;
    }

    let v00 = textureLoad(velocityTexture, vec2u(ix, iy)).xy;
    let v01 = textureLoad(velocityTexture, vec2u(ix + 1u, iy)).xy;
    let v10 = textureLoad(velocityTexture, vec2u(ix, iy + 1u)).xy;
    let v11 = textureLoad(velocityTexture, vec2u(ix + 1u, iy + 1u)).xy;

    let velocity = mix(
        mix(v00, v01, tx),
        mix(v10, v11, tx),
        ty
    );

    particle.position += velocity * simulationSettings.dt * 0.5;

    particles[id.x] = particle;
}

)";

namespace
{

    struct InteractionSettingsUniform
    {
        InteractionSettings settings;
        float dt;
        Vector2f oldPosition;
        Vector2f position;
        unsigned int preset = -1;
    };

    struct alignas(8) SimulationSettingsUniform
    {
        unsigned int cellsX;
        unsigned int cellsY;
        float dt;
        float dx;
        float gravity;
        float frictionFactor;
        unsigned int timestamp;
        unsigned int borderMask;
    };

    struct Particle
    {
        Vector2f position;
        unsigned int lifetime;
        unsigned int alive;
    };

}

struct Simulator::Impl
{
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    WGPUShaderModule shaderModule = nullptr;

    WGPUBuffer interactionSettingsUniformBuffer = nullptr;
    WGPUBuffer simulationSettingsUniformBuffer = nullptr;

    WGPUBindGroupLayout buffersBindGroupLayout = nullptr;
    WGPUBindGroup buffersBindGroup = nullptr;

    WGPUBindGroupLayout settingsBindGroupLayout = nullptr;
    WGPUBindGroup settingsBindGroup = nullptr;

    WGPUComputePipeline clearPipeline = nullptr;
    WGPUComputePipeline presetPipeline = nullptr;
    WGPUComputePipeline interactPipeline = nullptr;

    WGPUComputePipeline stepAcceleratePipeline = nullptr;
    WGPUComputePipeline stepScalePipeline = nullptr;
    WGPUComputePipeline stepMovePipeline = nullptr;

    WGPUComputePipeline updateParticlesPipeline = nullptr;

    unsigned int cellsX = -1;
    unsigned int cellsY = -1;
    WGPUTexture bedWaterTexture = nullptr;
    WGPUTexture flowXTexture = nullptr;
    WGPUTexture flowYTexture = nullptr;
    WGPUTexture velocityTexture = nullptr;
    WGPUTextureView bedWaterTextureView = nullptr;
    WGPUTextureView flowXTextureView = nullptr;
    WGPUTextureView flowYTextureView = nullptr;
    WGPUTextureView velocityTextureView = nullptr;

    unsigned int particleCount = -1;
    WGPUBuffer particlesBuffer = nullptr;

    unsigned int timestamp = 0;

    Impl(WGPUDevice device);

    void loadPreset(Preset preset);
    void interact(float dt, InteractionSettings const & settings, Vector2f const & oldPosition, Vector2f const & position);
    void step(SimulationSettings const & settings);

    void createShaderModule();

    void createUniformBuffers();

    void createBuffersBindGroupLayout();
    void createSettingsBindGroupLayout();
    void createSettingsBindGroup();

    void createClearPipeline();
    void createPresetPipeline();
    void createInteractPipeline();
    void createStepPipelines();
    void createParticlesPipeline();

    void recreateGridBuffers();
    void recreateParticleBuffers();
    void recreateBuffersBindGroup();

    void clearGridBuffers();
};

Simulator::Impl::Impl(WGPUDevice device)
    : device(device)
    , queue(wgpuDeviceGetQueue(device))
{
    createShaderModule();

    createBuffersBindGroupLayout();
    createSettingsBindGroupLayout();

    createUniformBuffers();

    createSettingsBindGroup();

    createClearPipeline();
    createPresetPipeline();
    createInteractPipeline();
    createStepPipelines();

    createParticlesPipeline();
}

void Simulator::Impl::loadPreset(Preset preset)
{
    InteractionSettingsUniform interactionSettingsUniform = {};
    interactionSettingsUniform.preset = (unsigned int)preset;

    wgpuQueueWriteBuffer(queue, interactionSettingsUniformBuffer, 0, &interactionSettingsUniform, sizeof(interactionSettingsUniform));

    WGPUCommandEncoderDescriptor commandEncoderDescriptor = {};

    WGPUCommandEncoder commandEncoder = wgpuDeviceCreateCommandEncoder(device, &commandEncoderDescriptor);

    WGPUComputePassDescriptor computePassDescriptor = {};

    WGPUComputePassEncoder computePassEncoder = wgpuCommandEncoderBeginComputePass(commandEncoder, &computePassDescriptor);

    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, buffersBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 1, settingsBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetPipeline(computePassEncoder, presetPipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, cellsX / 16, cellsY / 16, 1);
    wgpuComputePassEncoderEnd(computePassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);

    wgpuQueueSubmit(queue, 1, &commandBuffer);

    wgpuCommandBufferRelease(commandBuffer);
    wgpuComputePassEncoderRelease(computePassEncoder);
    wgpuCommandEncoderRelease(commandEncoder);
}

void Simulator::Impl::interact(float dt, InteractionSettings const & settings, Vector2f const & oldPosition, Vector2f const & position)
{
    if (settings.mode == InteractionMode::None)
        return;

    InteractionSettingsUniform interactionSettingsUniform
    {
        .settings = settings,
        .dt = dt,
        .oldPosition = oldPosition,
        .position = position,
    };

    wgpuQueueWriteBuffer(queue, interactionSettingsUniformBuffer, 0, &interactionSettingsUniform, sizeof(interactionSettingsUniform));

    WGPUCommandEncoderDescriptor commandEncoderDescriptor = {};

    WGPUCommandEncoder commandEncoder = wgpuDeviceCreateCommandEncoder(device, &commandEncoderDescriptor);

    WGPUComputePassDescriptor computePassDescriptor = {};

    WGPUComputePassEncoder computePassEncoder = wgpuCommandEncoderBeginComputePass(commandEncoder, &computePassDescriptor);

    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, buffersBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 1, settingsBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetPipeline(computePassEncoder, interactPipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, cellsX / 16, cellsY / 16, 1);
    wgpuComputePassEncoderEnd(computePassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);

    wgpuQueueSubmit(queue, 1, &commandBuffer);

    wgpuCommandBufferRelease(commandBuffer);
    wgpuComputePassEncoderRelease(computePassEncoder);
    wgpuCommandEncoderRelease(commandEncoder);
}

void Simulator::Impl::step(SimulationSettings const & settings)
{
    bool needUpdateBuffersBindGroup = false;
    bool needClearBuffers = false;

    if (settings.cellsX != cellsX || settings.cellsY != cellsY)
    {
        cellsX = settings.cellsX;
        cellsY = settings.cellsY;

        recreateGridBuffers();
        needUpdateBuffersBindGroup = true;
        needClearBuffers = true;
    }

    if (settings.particleCount != particleCount)
    {
        particleCount = settings.particleCount;

        recreateParticleBuffers();
        needUpdateBuffersBindGroup = true;
    }

    if (needUpdateBuffersBindGroup)
        recreateBuffersBindGroup();

    if (needClearBuffers)
        clearGridBuffers();

    if (settings.paused)
        return;

    SimulationSettingsUniform settingsUniform
    {
        .cellsX = settings.cellsX,
        .cellsY = settings.cellsY,
        .dt = settings.dt,
        .dx = 1.f,
        .gravity = settings.gravity,
        .frictionFactor = std::pow(1.f - settings.friction, settings.dt),
        .timestamp = timestamp,
        .borderMask = (unsigned int)(settings.leftBorder) | ((unsigned int)(settings.rightBorder) << 2) | ((unsigned int)(settings.bottomBorder) << 4) | ((unsigned int)(settings.topBorder) << 6),
    };

    wgpuQueueWriteBuffer(queue, simulationSettingsUniformBuffer, 0, &settingsUniform, sizeof(settingsUniform));

    WGPUCommandEncoderDescriptor commandEncoderDescriptor = {};

    WGPUCommandEncoder commandEncoder = wgpuDeviceCreateCommandEncoder(device, &commandEncoderDescriptor);

    WGPUComputePassDescriptor computePassDescriptor = {};

    WGPUComputePassEncoder computePassEncoder = wgpuCommandEncoderBeginComputePass(commandEncoder, &computePassDescriptor);

    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, buffersBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 1, settingsBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetPipeline(computePassEncoder, stepAcceleratePipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, cellsX / 16, cellsY / 16, 1);
    wgpuComputePassEncoderSetPipeline(computePassEncoder, stepScalePipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, cellsX / 16, cellsY / 16, 1);
    wgpuComputePassEncoderSetPipeline(computePassEncoder, stepMovePipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, cellsX / 16, cellsY / 16, 1);
    wgpuComputePassEncoderSetPipeline(computePassEncoder, updateParticlesPipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, particleCount / 64, 1, 1);
    wgpuComputePassEncoderEnd(computePassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);

    wgpuQueueSubmit(queue, 1, &commandBuffer);

    wgpuCommandBufferRelease(commandBuffer);
    wgpuComputePassEncoderRelease(computePassEncoder);
    wgpuCommandEncoderRelease(commandEncoder);

    ++timestamp;
}

void Simulator::Impl::createShaderModule()
{
    WGPUShaderModuleWGSLDescriptor shaderModuleWGSLDescriptor = {};
    shaderModuleWGSLDescriptor.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    shaderModuleWGSLDescriptor.chain.next = nullptr;
    shaderModuleWGSLDescriptor.code = shaderSource;

    WGPUShaderModuleDescriptor shaderModuleDescriptor = {};
    shaderModuleDescriptor.nextInChain = &shaderModuleWGSLDescriptor.chain;

    shaderModule = wgpuDeviceCreateShaderModule(device, &shaderModuleDescriptor);
}

void Simulator::Impl::createUniformBuffers()
{
    WGPUBufferDescriptor interactionSettingsBufferDescriptor = {};
    interactionSettingsBufferDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    interactionSettingsBufferDescriptor.size = sizeof(InteractionSettingsUniform);
    interactionSettingsBufferDescriptor.mappedAtCreation = 0;

    interactionSettingsUniformBuffer = wgpuDeviceCreateBuffer(device, &interactionSettingsBufferDescriptor);

    WGPUBufferDescriptor simulationSettingsBufferDescriptor = {};
    simulationSettingsBufferDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    simulationSettingsBufferDescriptor.size = sizeof(SimulationSettingsUniform);
    simulationSettingsBufferDescriptor.mappedAtCreation = 0;

    simulationSettingsUniformBuffer = wgpuDeviceCreateBuffer(device, &simulationSettingsBufferDescriptor);
}

void Simulator::Impl::createBuffersBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[5] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    entries[0].storageTexture.format = WGPUTextureFormat_RG32Float;
    entries[0].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    entries[1].storageTexture.format = WGPUTextureFormat_R32Float;
    entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    entries[2].storageTexture.format = WGPUTextureFormat_R32Float;
    entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Compute;
    entries[3].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    entries[3].storageTexture.format = WGPUTextureFormat_RG32Float;
    entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Compute;
    entries[4].buffer.type = WGPUBufferBindingType_Storage;
    entries[4].buffer.hasDynamicOffset = 0;
    entries[4].buffer.minBindingSize = 0;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    buffersBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Simulator::Impl::createSettingsBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[2] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = 0;
    entries[0].buffer.minBindingSize = sizeof(InteractionSettingsUniform);

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer.type = WGPUBufferBindingType_Uniform;
    entries[1].buffer.hasDynamicOffset = 0;
    entries[1].buffer.minBindingSize = sizeof(SimulationSettingsUniform);

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    settingsBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Simulator::Impl::createSettingsBindGroup()
{
    WGPUBindGroupEntry entries[2] = {};

    entries[0].binding = 0;
    entries[0].buffer = interactionSettingsUniformBuffer;
    entries[0].offset = 0;
    entries[0].size = sizeof(InteractionSettingsUniform);

    entries[1].binding = 1;
    entries[1].buffer = simulationSettingsUniformBuffer;
    entries[1].offset = 0;
    entries[1].size = sizeof(SimulationSettingsUniform);

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = settingsBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    settingsBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

void Simulator::Impl::createClearPipeline()
{
    WGPUBindGroupLayout bindGroupLayouts[2] =
    {
        buffersBindGroupLayout,
        settingsBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUComputePipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.compute.module = shaderModule;
    pipelineDescriptor.compute.entryPoint = "clearBuffers";

    clearPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDescriptor);
}

void Simulator::Impl::createPresetPipeline()
{
    WGPUBindGroupLayout bindGroupLayouts[2] =
    {
        buffersBindGroupLayout,
        settingsBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUComputePipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.compute.module = shaderModule;
    pipelineDescriptor.compute.entryPoint = "loadPreset";

    presetPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDescriptor);
}

void Simulator::Impl::createInteractPipeline()
{
    WGPUBindGroupLayout bindGroupLayouts[2] =
    {
        buffersBindGroupLayout,
        settingsBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUComputePipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.compute.module = shaderModule;
    pipelineDescriptor.compute.entryPoint = "interact";

    interactPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDescriptor);
}

void Simulator::Impl::createStepPipelines()
{
    WGPUBindGroupLayout bindGroupLayouts[2] =
    {
        buffersBindGroupLayout,
        settingsBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUComputePipelineDescriptor stepAcceleratePipelineDescriptor = {};
    stepAcceleratePipelineDescriptor.layout = pipelineLayout;
    stepAcceleratePipelineDescriptor.compute.module = shaderModule;
    stepAcceleratePipelineDescriptor.compute.entryPoint = "stepAccelerate";

    stepAcceleratePipeline = wgpuDeviceCreateComputePipeline(device, &stepAcceleratePipelineDescriptor);

    WGPUComputePipelineDescriptor stepScalePipelineDescriptor = {};
    stepScalePipelineDescriptor.layout = pipelineLayout;
    stepScalePipelineDescriptor.compute.module = shaderModule;
    stepScalePipelineDescriptor.compute.entryPoint = "stepScale";

    stepScalePipeline = wgpuDeviceCreateComputePipeline(device, &stepScalePipelineDescriptor);

    WGPUComputePipelineDescriptor stepMovePipelineDescriptor = {};
    stepMovePipelineDescriptor.layout = pipelineLayout;
    stepMovePipelineDescriptor.compute.module = shaderModule;
    stepMovePipelineDescriptor.compute.entryPoint = "stepMove";

    stepMovePipeline = wgpuDeviceCreateComputePipeline(device, &stepMovePipelineDescriptor);
}

void Simulator::Impl::createParticlesPipeline()
{
    WGPUBindGroupLayout bindGroupLayouts[2] =
    {
        buffersBindGroupLayout,
        settingsBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUComputePipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.compute.module = shaderModule;
    pipelineDescriptor.compute.entryPoint = "updateParticles";

    updateParticlesPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDescriptor);
}

void Simulator::Impl::recreateGridBuffers()
{
    if (bedWaterTexture) wgpuTextureRelease(bedWaterTexture);
    if (bedWaterTextureView) wgpuTextureViewRelease(bedWaterTextureView);
    if (flowXTexture) wgpuTextureRelease(flowXTexture);
    if (flowXTextureView) wgpuTextureViewRelease(flowXTextureView);
    if (flowYTexture) wgpuTextureRelease(flowYTexture);
    if (flowYTextureView) wgpuTextureViewRelease(flowYTextureView);
    if (velocityTexture) wgpuTextureRelease(velocityTexture);
    if (velocityTextureView) wgpuTextureViewRelease(velocityTextureView);

    WGPUTextureDescriptor gridTextureDescriptor = {};
    gridTextureDescriptor.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding;
    gridTextureDescriptor.dimension = WGPUTextureDimension_2D;
    gridTextureDescriptor.size = {cellsX, cellsY, 1};
    gridTextureDescriptor.format = WGPUTextureFormat_RG32Float;
    gridTextureDescriptor.mipLevelCount = 1;
    gridTextureDescriptor.sampleCount = 1;

    bedWaterTexture = wgpuDeviceCreateTexture(device, &gridTextureDescriptor);
    velocityTexture = wgpuDeviceCreateTexture(device, &gridTextureDescriptor);

    WGPUTextureDescriptor flowTextureDescriptor = {};
    flowTextureDescriptor.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding;
    flowTextureDescriptor.dimension = WGPUTextureDimension_2D;
    flowTextureDescriptor.size = {cellsX + 1, cellsY + 1, 1};
    flowTextureDescriptor.format = WGPUTextureFormat_R32Float;
    flowTextureDescriptor.mipLevelCount = 1;
    flowTextureDescriptor.sampleCount = 1;

    flowXTexture = wgpuDeviceCreateTexture(device, &flowTextureDescriptor);
    flowYTexture = wgpuDeviceCreateTexture(device, &flowTextureDescriptor);

    WGPUTextureViewDescriptor gridTextureViewDescriptor = {};
    gridTextureViewDescriptor.format = WGPUTextureFormat_RG32Float;
    gridTextureViewDescriptor.dimension = WGPUTextureViewDimension_2D;
    gridTextureViewDescriptor.baseMipLevel = 0;
    gridTextureViewDescriptor.mipLevelCount = 1;
    gridTextureViewDescriptor.baseArrayLayer = 0;
    gridTextureViewDescriptor.arrayLayerCount = 1;
    gridTextureViewDescriptor.aspect = WGPUTextureAspect_All;

    bedWaterTextureView = wgpuTextureCreateView(bedWaterTexture, &gridTextureViewDescriptor);
    velocityTextureView = wgpuTextureCreateView(velocityTexture, &gridTextureViewDescriptor);

    WGPUTextureViewDescriptor flowTextureViewDescriptor = {};
    flowTextureViewDescriptor.format = WGPUTextureFormat_R32Float;
    flowTextureViewDescriptor.dimension = WGPUTextureViewDimension_2D;
    flowTextureViewDescriptor.baseMipLevel = 0;
    flowTextureViewDescriptor.mipLevelCount = 1;
    flowTextureViewDescriptor.baseArrayLayer = 0;
    flowTextureViewDescriptor.arrayLayerCount = 1;
    flowTextureViewDescriptor.aspect = WGPUTextureAspect_All;

    flowXTextureView = wgpuTextureCreateView(flowXTexture, &flowTextureViewDescriptor);
    flowYTextureView = wgpuTextureCreateView(flowYTexture, &flowTextureViewDescriptor);

    std::cout << "Created simulation buffers with size " << cellsX << "x" << cellsY << std::endl;
}

void Simulator::Impl::recreateParticleBuffers()
{
    WGPUBufferDescriptor particlesBufferDescriptor = {};
    particlesBufferDescriptor.usage = WGPUBufferUsage_Storage;
    particlesBufferDescriptor.size = sizeof(Particle) * particleCount;
    particlesBufferDescriptor.mappedAtCreation = 0;

    particlesBuffer = wgpuDeviceCreateBuffer(device, &particlesBufferDescriptor);
}

void Simulator::Impl::recreateBuffersBindGroup()
{
    if (buffersBindGroup) wgpuBindGroupRelease(buffersBindGroup);

    WGPUBindGroupEntry entries[5] = {};

    entries[0].binding = 0;
    entries[0].textureView = bedWaterTextureView;

    entries[1].binding = 1;
    entries[1].textureView = flowXTextureView;

    entries[2].binding = 2;
    entries[2].textureView = flowYTextureView;

    entries[3].binding = 3;
    entries[3].textureView = velocityTextureView;

    entries[4].binding = 4;
    entries[4].buffer = particlesBuffer;
    entries[4].offset = 0;
    entries[4].size = wgpuBufferGetSize(particlesBuffer);

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = buffersBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    buffersBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

void Simulator::Impl::clearGridBuffers()
{
    WGPUCommandEncoderDescriptor commandEncoderDescriptor = {};

    WGPUCommandEncoder commandEncoder = wgpuDeviceCreateCommandEncoder(device, &commandEncoderDescriptor);

    WGPUComputePassDescriptor computePassDescriptor = {};

    WGPUComputePassEncoder computePassEncoder = wgpuCommandEncoderBeginComputePass(commandEncoder, &computePassDescriptor);

    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, buffersBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(computePassEncoder, 1, settingsBindGroup, 0, nullptr);
    wgpuComputePassEncoderSetPipeline(computePassEncoder, clearPipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, cellsX / 16, cellsY / 16, 1);
    wgpuComputePassEncoderEnd(computePassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);

    wgpuQueueSubmit(queue, 1, &commandBuffer);

    wgpuCommandBufferRelease(commandBuffer);
    wgpuComputePassEncoderRelease(computePassEncoder);
    wgpuCommandEncoderRelease(commandEncoder);
}

Simulator::Simulator(WGPUDevice device)
    : pimpl_(std::make_unique<Impl>(device))
{}

Simulator::~Simulator() = default;

void Simulator::loadPreset(Preset preset)
{
    pimpl_->loadPreset(preset);
}

void Simulator::interact(float dt, InteractionSettings const & settings, Vector2f const & oldPosition, Vector2f const & position)
{
    pimpl_->interact(dt, settings, oldPosition, position);
}

void Simulator::step(SimulationSettings const & settings)
{
    pimpl_->step(settings);
}

SimulationBuffers Simulator::buffers()
{
    return {
        .bedWaterTextureView = pimpl_->bedWaterTextureView,
        .velocityTextureView = pimpl_->velocityTextureView,
        .particlesBuffer = pimpl_->particlesBuffer,
    };
}
