#include <webgpu-shallow-water/simulator.hpp>

#include <iostream>

static char const shaderSource[] =
R"(

@group(0) @binding(0) var bedWaterTexture : texture_storage_2d<rg32float, read_write>;

@compute @workgroup_size(16, 16)
fn clearBedWaterTexture(@builtin(global_invocation_id) id: vec3<u32>)
{
    textureStore(bedWaterTexture, id.xy, vec4f(0.0));
}

)";

struct Simulator::Impl
{
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    WGPUShaderModule shaderModule = nullptr;

    WGPUBindGroupLayout buffersBindGroupLayout = nullptr;
    WGPUBindGroup buffersBindGroup = nullptr;

    WGPUComputePipeline clearPipeline = nullptr;

    unsigned int cellsX = -1;
    unsigned int cellsY = -1;
    WGPUTexture bedWaterTexture = nullptr;
    WGPUTextureView bedWaterTextureView = nullptr;

    Impl(WGPUDevice device);

    void step(SimulationSettings const & settings);

    void createShaderModule();

    void createBuffersBindGroupLayout();

    void createClearPipeline();

    void recreateBuffers();
    void recreateBuffersBindGroup();
};

Simulator::Impl::Impl(WGPUDevice device)
    : device(device)
    , queue(wgpuDeviceGetQueue(device))
{
    createShaderModule();

    createBuffersBindGroupLayout();

    createClearPipeline();
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
