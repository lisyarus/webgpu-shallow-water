#include <webgpu-shallow-water/renderer.hpp>

static char const shaderSource[] =
R"(

struct ViewSettings
{
    viewMatrix : mat4x4f,
    simulationSize : vec2u,
    actionPosition : vec2f,
    actionRadius : f32,
}

struct Particle
{
    position : vec2f,
    lifetime : u32,
    alive : u32,
}

@group(0) @binding(0) var bedWaterTexture : texture_2d<f32>;
@group(0) @binding(1) var velocityTexture : texture_2d<f32>;
@group(0) @binding(2) var<storage, read> particles : array<Particle>;

@group(1) @binding(0) var linearSampler : sampler;

@group(2) @binding(0) var<uniform> viewSettings : ViewSettings;

struct BedWaterVertexOut
{
    @builtin(position) position : vec4f,
    @location(0) texcoord : vec2f,
}

@vertex
fn drawBedWaterVertexMain(@builtin(vertex_index) index : u32) -> BedWaterVertexOut
{
    var texcoord = vec2f(0.0);
    if (index == 0u) {
        texcoord = vec2f(0.0);
    } else if (index == 1u) {
        texcoord = vec2f(1.0, 0.0);
    } else if (index == 2u){
        texcoord = vec2f(0.0, 1.0);
    } else {
        texcoord = vec2f(1.0, 1.0);
    }

    return BedWaterVertexOut(
        viewSettings.viewMatrix * vec4f(vec2f(viewSettings.simulationSize) * texcoord, 0.0, 1.0),
        texcoord
    );
}

@fragment
fn drawBedWaterFragmentMain(in : BedWaterVertexOut) -> @location(0) vec4f
{
    let bedWaterSample = textureSampleLevel(bedWaterTexture, linearSampler, in.texcoord, 0.0).xy;

    let bedFactor = 1.0 - exp(- 0.5 * bedWaterSample.x);
    let waterFactor = 1.0 - exp(- bedWaterSample.y);

    let color = mix(vec3f(bedFactor), vec3f(0.0, 0.125, 0.5), waterFactor);

    return vec4f(color, 1.0);
}

struct VelocityVertexOut
{
    @builtin(position) position : vec4f,
}

@vertex
fn drawVelocityVertexMain(@builtin(vertex_index) index : u32) -> VelocityVertexOut
{
    let id = vec2u((index / 3u) % viewSettings.simulationSize.x, (index / 3u) / viewSettings.simulationSize.x);
    let center = vec2f(id) + vec2f(0.5);
    let texcoord = center / vec2f(viewSettings.simulationSize);

    let bedWaterSample = textureSampleLevel(bedWaterTexture, linearSampler, texcoord, 0.0).xy;
    var flow = textureSampleLevel(velocityTexture, linearSampler, texcoord, 0.0).xy * bedWaterSample.y;

    let flowLength = length(flow);

    flow *= (1.0 - exp(- flowLength)) / flowLength;

    let normal = vec2f(flow.y, -flow.x) * 0.25;

    let idInTriangle = index % 3u;

    var extent = vec2f(0.0);

    if (idInTriangle == 0u) {
        extent = flow / 2.0;
    } else if (idInTriangle == 1u) {
        extent = - flow / 2.0 + normal / 2.0;
    } else {
        extent = - flow / 2.0 - normal / 2.0;
    }

    return VelocityVertexOut(
        viewSettings.viewMatrix * vec4f(center + extent, 0.0, 1.0),
    );
}

@fragment
fn drawVelocityFragmentMain(in : VelocityVertexOut) -> @location(0) vec4f
{
    return vec4f(1.0);
}

struct ParticleVertexOut
{
    @builtin(position) position : vec4f,
    @location(0) extent : vec2f,
}

@vertex
fn drawParticlesVertexMain(@builtin(vertex_index) index : u32) -> ParticleVertexOut
{
    let particle = particles[index / 6u];

    if (particle.alive == 0u) {
        return ParticleVertexOut(vec4f(100.0, 100.0, 0.0, 1.0), vec2f(0.0));
    }

    var extent = vec2f(0.0);

    let vertexID = index % 6u;

    if (vertexID == 0u) {
        extent = vec2f(-1.0, -1.0);
    } else if (vertexID == 1u || vertexID == 4u) {
        extent = vec2f( 1.0, -1.0);
    } else if (vertexID == 2u || vertexID == 3u) {
        extent = vec2f(-1.0,  1.0);
    } else {
        extent = vec2f( 1.0,  1.0);
    }

    let radius = f32(max(viewSettings.simulationSize[0], viewSettings.simulationSize[1])) / 512.0;

    return ParticleVertexOut(
        viewSettings.viewMatrix * vec4f(particle.position + extent * radius, 0.0, 1.0),
        extent
    );
}

@fragment
fn drawParticlesFragmentMain(in : ParticleVertexOut) -> @location(0) vec4f
{
    let alpha = 0.5 * exp(- 3.0 * dot(in.extent, in.extent));
    return vec4f(vec3f(1.0), alpha);
}

struct ActionVertexOut
{
    @builtin(position) position : vec4f,
    @location(0) worldPosition : vec2f,
}

@vertex
fn drawActionVertexMain(@builtin(vertex_index) index : u32) -> ActionVertexOut
{
    var texcoord = vec2f(0.0);
    if (index == 0u) {
        texcoord = vec2f(0.0);
    } else if (index == 1u) {
        texcoord = vec2f(1.0, 0.0);
    } else if (index == 2u){
        texcoord = vec2f(0.0, 1.0);
    } else {
        texcoord = vec2f(1.0, 1.0);
    }

    let worldPosition = vec2f(viewSettings.simulationSize) * texcoord;

    return ActionVertexOut(
        viewSettings.viewMatrix * vec4f(worldPosition, 0.0, 1.0),
        worldPosition
    );
}

@fragment
fn drawActionFragmentMain(in : ActionVertexOut) -> @location(0) vec4f
{
    let l = length(in.worldPosition - viewSettings.actionPosition) - viewSettings.actionRadius;

    let eps = length(vec2f(dpdx(l), dpdy(l)));
    let alpha = smoothstep(2.0 * eps, eps, l) * smoothstep(- 2.0 * eps, - eps, l);
    let shadowAlpha = smoothstep(8.0 * eps, 0.0, l) * smoothstep(- 8.0 * eps, 0.0, l);

    var color = vec4f(vec3f(1.0) * alpha, shadowAlpha + alpha - shadowAlpha * alpha);

    return color;
}
)";

namespace
{

    struct alignas(16) ViewSettingsUniform
    {
        Matrix4f viewMatrix;
        unsigned int cellsX;
        unsigned int cellsY;
        Vector2f actionPosition;
        float actionRadius;
    };

}

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
    WGPURenderPipeline drawVelocityPipeline = nullptr;
    WGPURenderPipeline drawParticlesPipeline = nullptr;
    WGPURenderPipeline drawActionPipeline = nullptr;

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
    void createDrawBedWaterPipeline();
    void createDrawVelocityPipeline();
    void createDrawParticlesPipeline();
    void createDrawActionPipeline();

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

    createDrawBedWaterPipeline();
    createDrawVelocityPipeline();
    createDrawParticlesPipeline();
    createDrawActionPipeline();
}

void Renderer::Impl::update(SimulationBuffers const & simulationBuffers)
{
    if (this->simulationBuffers != simulationBuffers || !drawBedWaterPipeline)
    {
        this->simulationBuffers = simulationBuffers;

        recreateBuffersBindGroup();
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

    if (viewSettings.showVelocity)
    {
        wgpuRenderPassEncoderSetPipeline(renderPassEncoder, drawVelocityPipeline);
        wgpuRenderPassEncoderDraw(renderPassEncoder, viewSettings.cellsX * viewSettings.cellsY * 3, 1, 0, 0);
    }

    if (viewSettings.showParticles)
    {
        wgpuRenderPassEncoderSetPipeline(renderPassEncoder, drawParticlesPipeline);
        wgpuRenderPassEncoderDraw(renderPassEncoder, viewSettings.particleCount * 6, 1, 0, 0);
    }

    if (viewSettings.action)
    {
        wgpuRenderPassEncoderSetPipeline(renderPassEncoder, drawActionPipeline);
        wgpuRenderPassEncoderDraw(renderPassEncoder, 4, 1, 0, 0);
    }

    wgpuRenderPassEncoderEnd(renderPassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);
    wgpuQueueSubmit(queue, 1, &commandBuffer);

    wgpuCommandBufferRelease(commandBuffer);
    wgpuRenderPassEncoderRelease(renderPassEncoder);
    wgpuCommandEncoderRelease(commandEncoder);
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
    WGPUBindGroupLayoutEntry entries[3] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[0].texture.multisampled = 0;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Vertex;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = 0;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Vertex;
    entries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[2].buffer.hasDynamicOffset = 0;
    entries[2].buffer.minBindingSize = 0;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
    bindGroupLayoutDescriptor.entries = entries;
    bindGroupLayoutDescriptor.entryCount = std::size(entries);

    buffersBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

void Renderer::Impl::createSamplersBindGroupLayout()
{
    WGPUBindGroupLayoutEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
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
    entries[0].buffer.minBindingSize = sizeof(ViewSettingsUniform);

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
    bufferDescriptor.size = sizeof(ViewSettingsUniform);
    bufferDescriptor.mappedAtCreation = 0;

    settingsUniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDescriptor);
}

void Renderer::Impl::createSettingsBindGroup()
{
    WGPUBindGroupEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].buffer = settingsUniformBuffer;
    entries[0].offset = 0;
    entries[0].size = sizeof(ViewSettingsUniform);

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = settingsBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    settingsBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

void Renderer::Impl::recreateBuffersBindGroup()
{
    if (buffersBindGroup) wgpuBindGroupRelease(buffersBindGroup);

    WGPUBindGroupEntry entries[3] = {};

    entries[0].binding = 0;
    entries[0].textureView = simulationBuffers.bedWaterTextureView;

    entries[1].binding = 1;
    entries[1].textureView = simulationBuffers.velocityTextureView;

    entries[2].binding = 2;
    entries[2].buffer = simulationBuffers.particlesBuffer;
    entries[2].offset = 0;
    entries[2].size = wgpuBufferGetSize(simulationBuffers.particlesBuffer);

    WGPUBindGroupDescriptor bindGroupDescriptor = {};
    bindGroupDescriptor.layout = buffersBindGroupLayout;
    bindGroupDescriptor.entryCount = std::size(entries);
    bindGroupDescriptor.entries = entries;

    buffersBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

void Renderer::Impl::createDrawBedWaterPipeline()
{
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

void Renderer::Impl::createDrawVelocityPipeline()
{
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
    fragmentState.entryPoint = "drawVelocityFragmentMain";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    WGPURenderPipelineDescriptor renderPipelineDescriptor = {};
    renderPipelineDescriptor.layout = pipelineLayout;
    renderPipelineDescriptor.vertex.module = shaderModule;
    renderPipelineDescriptor.vertex.entryPoint = "drawVelocityVertexMain";
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    renderPipelineDescriptor.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    renderPipelineDescriptor.primitive.frontFace = WGPUFrontFace_CCW;
    renderPipelineDescriptor.primitive.cullMode = WGPUCullMode_None;
    renderPipelineDescriptor.multisample.count = 1;
    renderPipelineDescriptor.multisample.mask = (unsigned int)(-1);
    renderPipelineDescriptor.fragment = &fragmentState;

    drawVelocityPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
}

void Renderer::Impl::createDrawParticlesPipeline()
{
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

    WGPUBlendState blendState = {};
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState colorTargetState = {};
    colorTargetState.format = surfaceFormat;
    colorTargetState.blend = &blendState;
    colorTargetState.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "drawParticlesFragmentMain";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    WGPURenderPipelineDescriptor renderPipelineDescriptor = {};
    renderPipelineDescriptor.layout = pipelineLayout;
    renderPipelineDescriptor.vertex.module = shaderModule;
    renderPipelineDescriptor.vertex.entryPoint = "drawParticlesVertexMain";
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    renderPipelineDescriptor.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    renderPipelineDescriptor.primitive.frontFace = WGPUFrontFace_CCW;
    renderPipelineDescriptor.primitive.cullMode = WGPUCullMode_None;
    renderPipelineDescriptor.multisample.count = 1;
    renderPipelineDescriptor.multisample.mask = (unsigned int)(-1);
    renderPipelineDescriptor.fragment = &fragmentState;

    drawParticlesPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
}

void Renderer::Impl::createDrawActionPipeline()
{
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

    WGPUBlendState blendState = {};
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState colorTargetState = {};
    colorTargetState.format = surfaceFormat;
    colorTargetState.blend = &blendState;
    colorTargetState.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "drawActionFragmentMain";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    WGPURenderPipelineDescriptor renderPipelineDescriptor = {};
    renderPipelineDescriptor.layout = pipelineLayout;
    renderPipelineDescriptor.vertex.module = shaderModule;
    renderPipelineDescriptor.vertex.entryPoint = "drawActionVertexMain";
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    renderPipelineDescriptor.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    renderPipelineDescriptor.primitive.frontFace = WGPUFrontFace_CCW;
    renderPipelineDescriptor.primitive.cullMode = WGPUCullMode_None;
    renderPipelineDescriptor.multisample.count = 1;
    renderPipelineDescriptor.multisample.mask = (unsigned int)(-1);
    renderPipelineDescriptor.fragment = &fragmentState;

    drawActionPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
}

void Renderer::Impl::updateSettingsBuffer(ViewSettings const & viewSettings)
{
    ViewSettingsUniform viewSettingsUniform;
    viewSettingsUniform.viewMatrix = viewSettings.viewMatrix;
    viewSettingsUniform.cellsX = viewSettings.cellsX;
    viewSettingsUniform.cellsY = viewSettings.cellsY;
    viewSettingsUniform.actionPosition = viewSettings.action ? viewSettings.action->position : Vector2f{0.f, 0.f};
    viewSettingsUniform.actionRadius = viewSettings.action ? viewSettings.action->radius: 0.f;
    wgpuQueueWriteBuffer(queue, settingsUniformBuffer, 0, &viewSettingsUniform, sizeof(viewSettingsUniform));
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
