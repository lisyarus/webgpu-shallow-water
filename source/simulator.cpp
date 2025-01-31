#include <webgpu-shallow-water/simulator.hpp>

#include <iostream>
#include <cmath>

static char const shaderSource[] =
R"(

struct InteractionSettings
{
    mode : u32,
    radius : f32,
    force : f32,
    dt : f32,
    oldPosition : vec2f,
    position : vec2f,
}

struct SimulationSettings
{
    size : vec2u,
    dt : f32,
    dx : f32,
    gravity : f32,
    frictionFactor : f32,
}

@group(0) @binding(0) var bedWaterTexture : texture_storage_2d<rg32float, read_write>;
@group(0) @binding(1) var flowXTexture : texture_storage_2d<r32float, read_write>;
@group(0) @binding(2) var flowYTexture : texture_storage_2d<r32float, read_write>;

@group(1) @binding(0) var<uniform> interactionSettings : InteractionSettings;
@group(1) @binding(1) var<uniform> simulationSettings : SimulationSettings;

@compute @workgroup_size(16, 16)
fn clearBedWaterTexture(@builtin(global_invocation_id) id: vec3u)
{
    textureStore(bedWaterTexture, id.xy, vec4f(0.0));
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
}

fn waterSurfaceAt(p : vec2u) -> f32
{
    let bedWaterSample = textureLoad(bedWaterTexture, p);
    return bedWaterSample.x + bedWaterSample.y;
}

@compute @workgroup_size(16, 16)
fn stepAccelerate(@builtin(global_invocation_id) id: vec3u)
{
    let waterSurfaceBase = waterSurfaceAt(id.xy);

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
    let totalFlow = 0.0
        + textureLoad(flowXTexture, id.xy).x
        + textureLoad(flowYTexture, id.xy).x
        - textureLoad(flowXTexture, id.xy + vec2u(1, 0)).x
        - textureLoad(flowYTexture, id.xy + vec2u(0, 1)).x
        ;

    var bedWaterSample = textureLoad(bedWaterTexture, id.xy);
    bedWaterSample.y += totalFlow * simulationSettings.dt / simulationSettings.dx / simulationSettings.dx;
    textureStore(bedWaterTexture, id.xy, bedWaterSample);
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
    };

    struct SimulationSettingsUniform
    {
        unsigned int cellsX;
        unsigned int cellsY;
        float dt;
        float dx;
        float gravity;
        float frictionFactor;
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
    WGPUComputePipeline interactPipeline = nullptr;

    WGPUComputePipeline stepAcceleratePipeline = nullptr;
    WGPUComputePipeline stepScalePipeline = nullptr;
    WGPUComputePipeline stepMovePipeline = nullptr;

    unsigned int cellsX = -1;
    unsigned int cellsY = -1;
    WGPUTexture bedWaterTexture = nullptr;
    WGPUTexture flowXTexture = nullptr;
    WGPUTexture flowYTexture = nullptr;
    WGPUTextureView bedWaterTextureView = nullptr;
    WGPUTextureView flowXTextureView = nullptr;
    WGPUTextureView flowYTextureView = nullptr;

    Impl(WGPUDevice device);

    void interact(float dt, InteractionSettings const & settings, Vector2f const & oldPosition, Vector2f const & position);
    void step(SimulationSettings const & settings);

    void createShaderModule();

    void createUniformBuffers();

    void createBuffersBindGroupLayout();
    void createSettingsBindGroupLayout();
    void createSettingsBindGroup();

    void createClearPipeline();
    void createInteractPipeline();
    void createStepPipelines();

    void recreateGridBuffers();
    void recreateBuffersBindGroup();
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
    createInteractPipeline();
    createStepPipelines();
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
}

void Simulator::Impl::step(SimulationSettings const & settings)
{
    if (settings.cellsX != cellsX || settings.cellsY != cellsY)
    {
        cellsX = settings.cellsX;
        cellsY = settings.cellsY;

        recreateGridBuffers();
        recreateBuffersBindGroup();
    }

    SimulationSettingsUniform settingsUniform
    {
        .cellsX = settings.cellsX,
        .cellsY = settings.cellsY,
        .dt = settings.dt,
        .dx = 1.f,
        .gravity = settings.gravity,
        .frictionFactor = std::pow(1.f - settings.friction, settings.dt),
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
    wgpuComputePassEncoderEnd(computePassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);

    wgpuQueueSubmit(queue, 1, &commandBuffer);

    wgpuCommandBufferRelease(commandBuffer);
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
    WGPUBindGroupLayoutEntry entries[3] = {};

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
    WGPUBindGroupLayout bindGroupLayouts[1] =
    {
        buffersBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUComputePipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor.layout = pipelineLayout;
    pipelineDescriptor.compute.module = shaderModule;
    pipelineDescriptor.compute.entryPoint = "clearBedWaterTexture";

    clearPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDescriptor);
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

void Simulator::Impl::recreateGridBuffers()
{
    if (bedWaterTexture) wgpuTextureRelease(bedWaterTexture);
    if (bedWaterTextureView) wgpuTextureViewRelease(bedWaterTextureView);
    if (flowXTexture) wgpuTextureRelease(flowXTexture);
    if (flowXTextureView) wgpuTextureViewRelease(flowXTextureView);
    if (flowYTexture) wgpuTextureRelease(flowYTexture);
    if (flowYTextureView) wgpuTextureViewRelease(flowYTextureView);

    WGPUTextureDescriptor bedWaterTextureDescriptor = {};
    bedWaterTextureDescriptor.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding;
    bedWaterTextureDescriptor.dimension = WGPUTextureDimension_2D;
    bedWaterTextureDescriptor.size = {cellsX, cellsY, 1};
    bedWaterTextureDescriptor.format = WGPUTextureFormat_RG32Float;
    bedWaterTextureDescriptor.mipLevelCount = 1;
    bedWaterTextureDescriptor.sampleCount = 1;

    bedWaterTexture = wgpuDeviceCreateTexture(device, &bedWaterTextureDescriptor);

    WGPUTextureDescriptor flowTextureDescriptor = {};
    flowTextureDescriptor.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding;
    flowTextureDescriptor.dimension = WGPUTextureDimension_2D;
    flowTextureDescriptor.size = {cellsX + 1, cellsY + 1, 1};
    flowTextureDescriptor.format = WGPUTextureFormat_R32Float;
    flowTextureDescriptor.mipLevelCount = 1;
    flowTextureDescriptor.sampleCount = 1;

    flowXTexture = wgpuDeviceCreateTexture(device, &flowTextureDescriptor);
    flowYTexture = wgpuDeviceCreateTexture(device, &flowTextureDescriptor);

    WGPUTextureViewDescriptor bedWaterTextureViewDescriptor = {};
    bedWaterTextureViewDescriptor.format = WGPUTextureFormat_RG32Float;
    bedWaterTextureViewDescriptor.dimension = WGPUTextureViewDimension_2D;
    bedWaterTextureViewDescriptor.baseMipLevel = 0;
    bedWaterTextureViewDescriptor.mipLevelCount = 1;
    bedWaterTextureViewDescriptor.baseArrayLayer = 0;
    bedWaterTextureViewDescriptor.arrayLayerCount = 1;
    bedWaterTextureViewDescriptor.aspect = WGPUTextureAspect_All;

    bedWaterTextureView = wgpuTextureCreateView(bedWaterTexture, &bedWaterTextureViewDescriptor);

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

void Simulator::Impl::recreateBuffersBindGroup()
{
    if (buffersBindGroup) wgpuBindGroupRelease(buffersBindGroup);

    WGPUBindGroupEntry entries[3] = {};

    entries[0].binding = 0;
    entries[0].textureView = bedWaterTextureView;

    entries[1].binding = 1;
    entries[1].textureView = flowXTextureView;

    entries[2].binding = 2;
    entries[2].textureView = flowYTextureView;

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = buffersBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    buffersBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

Simulator::Simulator(WGPUDevice device)
    : pimpl_(std::make_unique<Impl>(device))
{}

Simulator::~Simulator() = default;

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
    };
}
