#include <webgpu-shallow-water/renderer.hpp>

static char const shaderSource[] =
R"(

struct ViewSettings
{
    viewMatrix : mat4x4f,
    simulationSize : vec2f,
}

@group(0) @binding(0) var bedWaterTexture : texture_2d<f32>;

@group(1) @binding(0) var linearSampler : sampler;

@group(2) @binding(0) var<uniform> viewSettings : ViewSettings;

struct VertexOut
{
    @builtin(position) position : vec4f,
    @location(0) texcoord : vec2f,
}

@vertex
fn drawBedWaterVertexMain(@builtin(vertex_index) index : u32) -> VertexOut
{
    var texcoord = vec2f(0.0);
    if (index == 0u) {
        texcoord = vec2f(0.0);
    } else if (index == 1u) {
        texcoord = vec2f(1.0, 0.0);
    } else if (index == 2u ){
        texcoord = vec2f(0.0, 1.0);
    } else {
        texcoord = vec2f(1.0, 1.0);
    }

    return VertexOut(
        viewSettings.viewMatrix * vec4f(viewSettings.simulationSize * texcoord, 0.0, 1.0),
        texcoord
    );
}

@fragment
fn drawBedWaterFragmentMain(in : VertexOut) -> @location(0) vec4f
{
    let bedWaterSample = textureSampleLevel(bedWaterTexture, linearSampler, in.texcoord, 0.0).xy;

    return vec4f(vec3f(bedWaterSample.x), 1.0);
}

)";

struct Renderer::Impl
{
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;

    WGPUShaderModule shaderModule = nullptr;

    SimulationBuffers simulationBuffers = {};

    WGPUSampler linearSampler = nullptr;

    WGPUBuffer settingsUniformBuffer = nullptr;

    WGPUBindGroupLayout buffersBindGroupLayout = nullptr;
    WGPUBindGroup buffersBindGroup = nullptr;

    WGPUBindGroupLayout samplersBindGroupLayout = nullptr;
    WGPUBindGroup samplersBindGroup = nullptr;

    WGPUBindGroupLayout settingsBindGroupLayout = nullptr;
    WGPUBindGroup settingsBindGroup = nullptr;

    WGPURenderPipeline drawBedWaterPipeline = nullptr;

    Impl(WGPUDevice device, WGPUTextureFormat surfaceFormat);

    void update(SimulationBuffers const & simulationBuffers);
    void render(WGPUTextureView target, ViewSettings const & viewSettings);

    void createShaderModule();
    void createBuffersBindGroupLayout();
    void createSamplersBindGroupLayout();
    void createSettingsBindGroupLayout();

    void createSamplers();
    void createSamplersBindGroup();

    void createSettingsUniformBuffer();
    void createSettingsBindGroup();

    void recreateBuffersBindGroup();
    void recreateDrawBedWaterPipeline();

    void updateSettingsBuffer(ViewSettings const & viewSettings);
};

Renderer::Impl::Impl(WGPUDevice device, WGPUTextureFormat surfaceFormat)
    : device(device)
    , queue(wgpuDeviceGetQueue(device))
    , surfaceFormat(surfaceFormat)
{
    createShaderModule();
    createBuffersBindGroupLayout();

    createSamplersBindGroupLayout();
    createSamplers();
    createSamplersBindGroup();

    createSettingsBindGroupLayout();
    createSettingsUniformBuffer();
    createSettingsBindGroup();
}

void Renderer::Impl::update(SimulationBuffers const & simulationBuffers)
{
    if (this->simulationBuffers != simulationBuffers || !drawBedWaterPipeline)
    {
        this->simulationBuffers = simulationBuffers;

        recreateBuffersBindGroup();
        recreateDrawBedWaterPipeline();
    }
}

void Renderer::Impl::render(WGPUTextureView target, ViewSettings const & viewSettings)
{
    updateSettingsBuffer(viewSettings);

    WGPUCommandEncoderDescriptor commandEncoderDescriptor = {};
    WGPUCommandEncoder commandEncoder = wgpuDeviceCreateCommandEncoder(device, &commandEncoderDescriptor);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = target;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.02, 0.02, 0.02, 0.0};

    WGPURenderPassDescriptor renderPassDescriptor = {};
    renderPassDescriptor.colorAttachmentCount = 1;
    renderPassDescriptor.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder renderPassEncoder = wgpuCommandEncoderBeginRenderPass(commandEncoder, &renderPassDescriptor);

    wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0, buffersBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 1, samplersBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 2, settingsBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetPipeline(renderPassEncoder, drawBedWaterPipeline);
    wgpuRenderPassEncoderDraw(renderPassEncoder, 4, 1, 0, 0);

    wgpuRenderPassEncoderEnd(renderPassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);
    wgpuQueueSubmit(queue, 1, &commandBuffer);
    wgpuCommandBufferRelease(commandBuffer);
}

void Renderer::Impl::createShaderModule()
{
    WGPUShaderModuleWGSLDescriptor shaderModuleWGSLDescriptor = {};
    shaderModuleWGSLDescriptor.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    shaderModuleWGSLDescriptor.chain.next = nullptr;
    shaderModuleWGSLDescriptor.code = shaderSource;

    WGPUShaderModuleDescriptor shaderModuleDescriptor = {};
    shaderModuleDescriptor.nextInChain = &shaderModuleWGSLDescriptor.chain;

    shaderModule = wgpuDeviceCreateShaderModule(device, &shaderModuleDescriptor);
}

void Renderer::Impl::createBuffersBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[0].texture.multisampled = 0;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    buffersBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Renderer::Impl::createSamplersBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    samplersBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Renderer::Impl::createSettingsBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = 0;
    entries[0].buffer.minBindingSize = sizeof(ViewSettings);

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    settingsBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Renderer::Impl::createSamplers()
{
    WGPUSamplerDescriptor samplerDescriptor = {};
    samplerDescriptor.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDescriptor.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDescriptor.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDescriptor.magFilter = WGPUFilterMode_Linear;
    samplerDescriptor.minFilter = WGPUFilterMode_Linear;
    samplerDescriptor.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDescriptor.lodMinClamp = 0.f;
    samplerDescriptor.lodMaxClamp = 0.f;
    samplerDescriptor.maxAnisotropy = 1;

    linearSampler = wgpuDeviceCreateSampler(device, &samplerDescriptor);
}

void Renderer::Impl::createSamplersBindGroup()
{
    WGPUBindGroupEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].sampler = linearSampler;

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = samplersBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    samplersBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

void Renderer::Impl::createSettingsUniformBuffer()
{
    WGPUBufferDescriptor bufferDescriptor = {};
    bufferDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    bufferDescriptor.size = sizeof(ViewSettings);
    bufferDescriptor.mappedAtCreation = 0;

    settingsUniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDescriptor);
}

void Renderer::Impl::createSettingsBindGroup()
{
    WGPUBindGroupEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].buffer = settingsUniformBuffer;
    entries[0].offset = 0;
    entries[0].size = sizeof(ViewSettings);

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = settingsBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    settingsBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

void Renderer::Impl::recreateBuffersBindGroup()
{
    if (buffersBindGroup) wgpuBindGroupRelease(buffersBindGroup);

    WGPUBindGroupEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].textureView = simulationBuffers.bedWaterTextureView;

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = buffersBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    buffersBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

void Renderer::Impl::recreateDrawBedWaterPipeline()
{
    if (drawBedWaterPipeline) wgpuRenderPipelineRelease(drawBedWaterPipeline);

    WGPUBindGroupLayout bindGroupLayouts[] =
    {
        buffersBindGroupLayout,
        samplersBindGroupLayout,
        settingsBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUColorTargetState colorTargetState = {};
    colorTargetState.format = surfaceFormat;
    colorTargetState.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "drawBedWaterFragmentMain";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    WGPURenderPipelineDescriptor renderPipelineDescriptor = {};
    renderPipelineDescriptor.layout = pipelineLayout;
    renderPipelineDescriptor.vertex.module = shaderModule;
    renderPipelineDescriptor.vertex.entryPoint = "drawBedWaterVertexMain";
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    renderPipelineDescriptor.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    renderPipelineDescriptor.primitive.frontFace = WGPUFrontFace_CCW;
    renderPipelineDescriptor.primitive.cullMode = WGPUCullMode_None;
    renderPipelineDescriptor.multisample.count = 1;
    renderPipelineDescriptor.multisample.mask = (unsigned int)(-1);
    renderPipelineDescriptor.fragment = &fragmentState;

    drawBedWaterPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
}

void Renderer::Impl::updateSettingsBuffer(ViewSettings const & viewSettings)
{
    wgpuQueueWriteBuffer(queue, settingsUniformBuffer, 0, &viewSettings, sizeof(viewSettings));
}

Renderer::Renderer(WGPUDevice device, WGPUTextureFormat surfaceFormat)
    : pimpl_(std::make_unique<Impl>(device, surfaceFormat))
{}

Renderer::~Renderer() = default;

void Renderer::update(SimulationBuffers const & simulationBuffers)
{
    pimpl_->update(simulationBuffers);
}

void Renderer::render(WGPUTextureView target, ViewSettings const & viewSettings)
{
    pimpl_->render(target, viewSettings);
}
