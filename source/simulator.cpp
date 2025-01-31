#include <webgpu-shallow-water/simulator.hpp>

#include <iostream>

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

@group(0) @binding(0) var bedWaterTexture : texture_storage_2d<rg32float, read_write>;

@group(1) @binding(0) var<uniform> interactionSettings : InteractionSettings;

@compute @workgroup_size(16, 16)
fn clearBedWaterTexture(@builtin(global_invocation_id) id: vec3<u32>)
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
fn interact(@builtin(global_invocation_id) id: vec3<u32>)
{
    let position = vec2f(id.xy) + vec2f(0.5);
    let distance = pointToSegmentDistance(position, interactionSettings.oldPosition, interactionSettings.position);

    let delta = pow(128.0, interactionSettings.force) * interactionSettings.force * interactionSettings.dt * smoothstep(interactionSettings.radius, interactionSettings.radius * interactionSettings.force - 1.0, distance);

    var value = textureLoad(bedWaterTexture, id.xy);

    if (interactionSettings.mode == 1u) {
        value.x += delta;
    } else if (interactionSettings.mode == 2u) {
        value.x -= delta;
    } else if (interactionSettings.mode == 3u) {
        value.y += delta;
    } else if (interactionSettings.mode == 4u) {
        value.y -= delta;
    }

    value.x = max(0.0, min(1.0, value.x));
    value.y = max(0.0, value.y);

    textureStore(bedWaterTexture, id.xy, value);
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

}

struct Simulator::Impl
{
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    WGPUShaderModule shaderModule = nullptr;

    WGPUBuffer interactionSettingsUniformBuffer = nullptr;

    WGPUBindGroupLayout buffersBindGroupLayout = nullptr;
    WGPUBindGroup buffersBindGroup = nullptr;

    WGPUBindGroupLayout settingsBindGroupLayout = nullptr;
    WGPUBindGroup settingsBindGroup = nullptr;

    WGPUComputePipeline clearPipeline = nullptr;
    WGPUComputePipeline interactPipeline = nullptr;

    unsigned int cellsX = -1;
    unsigned int cellsY = -1;
    WGPUTexture bedWaterTexture = nullptr;
    WGPUTextureView bedWaterTextureView = nullptr;

    Impl(WGPUDevice device);

    void interact(float dt, InteractionSettings const & settings, Vector2f const & oldPosition, Vector2f const & position);
    void step(SimulationSettings const & settings);

    void createShaderModule();

    void createInteractionSettingsUniformBuffer();

    void createBuffersBindGroupLayout();
    void createSettingsBindGroupLayout();
    void createSettingsBindGroup();

    void createClearPipeline();
    void createInteractPipeline();

    void recreateBuffers();
    void recreateBuffersBindGroup();
};

Simulator::Impl::Impl(WGPUDevice device)
    : device(device)
    , queue(wgpuDeviceGetQueue(device))
{
    createShaderModule();

    createBuffersBindGroupLayout();
    createSettingsBindGroupLayout();

    createInteractionSettingsUniformBuffer();

    createSettingsBindGroup();

    createClearPipeline();
    createInteractPipeline();
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

        recreateBuffers();
        recreateBuffersBindGroup();
    }
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

void Simulator::Impl::createInteractionSettingsUniformBuffer()
{
    WGPUBufferDescriptor bufferDescriptor = {};
    bufferDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    bufferDescriptor.size = sizeof(InteractionSettingsUniform);
    bufferDescriptor.mappedAtCreation = 0;

    interactionSettingsUniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDescriptor);
}

void Simulator::Impl::createBuffersBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    entries[0].storageTexture.format = WGPUTextureFormat_RG32Float;
    entries[0].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    buffersBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Simulator::Impl::createSettingsBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = 0;
    entries[0].buffer.minBindingSize = sizeof(InteractionSettingsUniform);

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    settingsBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Simulator::Impl::createSettingsBindGroup()
{
    WGPUBindGroupEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].buffer = interactionSettingsUniformBuffer;
    entries[0].offset = 0;
    entries[0].size = sizeof(InteractionSettingsUniform);

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

void Simulator::Impl::recreateBuffers()
{
    if (bedWaterTexture) wgpuTextureRelease(bedWaterTexture);
    if (bedWaterTextureView) wgpuTextureViewRelease(bedWaterTextureView);

    WGPUTextureDescriptor bedWaterTextureDescriptor = {};
    bedWaterTextureDescriptor.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding;
    bedWaterTextureDescriptor.dimension = WGPUTextureDimension_2D;
    bedWaterTextureDescriptor.size = {cellsX, cellsY, 1};
    bedWaterTextureDescriptor.format = WGPUTextureFormat_RG32Float;
    bedWaterTextureDescriptor.mipLevelCount = 1;
    bedWaterTextureDescriptor.sampleCount = 1;

    bedWaterTexture = wgpuDeviceCreateTexture(device, &bedWaterTextureDescriptor);

    WGPUTextureViewDescriptor bedWaterTextureViewDescriptor = {};
    bedWaterTextureViewDescriptor.format = WGPUTextureFormat_RG32Float;
    bedWaterTextureViewDescriptor.dimension = WGPUTextureViewDimension_2D;
    bedWaterTextureViewDescriptor.baseMipLevel = 0;
    bedWaterTextureViewDescriptor.mipLevelCount = 1;
    bedWaterTextureViewDescriptor.baseArrayLayer = 0;
    bedWaterTextureViewDescriptor.arrayLayerCount = 1;
    bedWaterTextureViewDescriptor.aspect = WGPUTextureAspect_All;

    bedWaterTextureView = wgpuTextureCreateView(bedWaterTexture, &bedWaterTextureViewDescriptor);

    std::cout << "Created simulation buffers with size " << cellsX << "x" << cellsY << std::endl;
}

void Simulator::Impl::recreateBuffersBindGroup()
{
    if (buffersBindGroup) wgpuBindGroupRelease(buffersBindGroup);

    WGPUBindGroupEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].textureView = bedWaterTextureView;

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
