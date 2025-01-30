#include <webgpu-shallow-water/application.hpp>
#include <webgpu-shallow-water/sdl_wgpu.h>

#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_wgpu.h>

#include <SDL2/SDL.h>

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <iostream>
#include <algorithm>
#include <format>

Application::Application()
{
    SDL_Init(SDL_INIT_VIDEO);

    window_ = SDL_CreateWindow("WebGPU Shallow Water Demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width_, height_, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP);

    SDL_GetWindowSize(window_, &width_, &height_);

    // Create WebGPU instance

    WGPUInstanceExtras instanceExtras = {};
    instanceExtras.chain.sType = (WGPUSType)WGPUSType_InstanceExtras;

#if defined(__APPLE__)
    instanceExtras.backends = WGPUInstanceBackend_Metal;
#elif defined(_WIN32)
    instanceExtras.backends = WGPUInstanceBackend_DX12;
#else
    instanceExtras.backends = WGPUInstanceBackend_Vulkan;
#endif

    WGPUInstanceDescriptor instanceDescriptor = {};
    instanceDescriptor.nextInChain = (WGPUChainedStruct *)&instanceExtras;

    WGPUInstance instance = wgpuCreateInstance(&instanceDescriptor);

    std::cout << "Instance: " << instance << std::endl;

    // Create WebGPU surface

    surface_ = SDL_WGPU_CreateSurface(instance, window_);

    std::cout << "Surface: " << surface_ << std::endl;

    // Request WebGPU adapter

    WGPURequestAdapterOptions requestAdapterOptions = {};
    requestAdapterOptions.compatibleSurface = surface_;
    requestAdapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPUAdapter adapter = nullptr;

    auto requestAdapterCallback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * userdata){
        if (message && *message)
            std::cout << "Adapter callback message: " << message << std::endl;

        if (status == WGPURequestAdapterStatus_Success)
            *((WGPUAdapter *)(userdata)) = adapter;
        else
            throw std::runtime_error(message);
    };

    wgpuInstanceRequestAdapter(instance, &requestAdapterOptions, requestAdapterCallback, &adapter);

    std::cout << "Adapter: " << adapter << std::endl;

    // Request WebGPU device

    WGPUSupportedLimits supportedLimits = {};
    wgpuAdapterGetLimits(adapter, &supportedLimits);

    WGPURequiredLimits requiredLimits = {};
    requiredLimits.limits = supportedLimits.limits;

    WGPUFeatureName requiredFeatures[2]
    {
        WGPUFeatureName_Float32Filterable,
        (WGPUFeatureName)WGPUNativeFeature_TextureAdapterSpecificFormatFeatures,
    };

    WGPUDeviceDescriptor deviceDescriptor = {};
    deviceDescriptor.requiredFeatureCount = std::size(requiredFeatures);
    deviceDescriptor.requiredFeatures = requiredFeatures;
    deviceDescriptor.requiredLimits = &requiredLimits;

    auto requestDeviceCallback = [](WGPURequestDeviceStatus status, WGPUDevice device, char const * message, void * userdata)
    {
        if (message && *message)
            std::cout << "Device callback message: " << message << std::endl;

        if (status == WGPURequestDeviceStatus_Success)
            ((Application *)(userdata))->device_ = device;
        else
            throw std::runtime_error(message);
    };

    wgpuAdapterRequestDevice(adapter, &deviceDescriptor, requestDeviceCallback, this);

    std::cout << "Device: " << device_ << std::endl;

    // Check if surface supports the surface format

    WGPUSurfaceCapabilities surfaceCapabilities;
    wgpuSurfaceGetCapabilities(surface_, adapter, &surfaceCapabilities);

    auto formatsBegin = surfaceCapabilities.formats;
    auto formatsEnd = surfaceCapabilities.formats + surfaceCapabilities.formatCount;
    if (std::find(formatsBegin, formatsEnd, surfaceFormat_) == formatsEnd)
        throw std::runtime_error("Surface doesn't support BRGA8 sRGB format");

    std::cout << "Surface format: " << surfaceFormat_ << std::endl;
    std::cout << "Supported formats:\n";

    for (auto it = formatsBegin; it != formatsEnd; ++it)
        std::cout << "    " << *it << "\n";
    std::cout << std::flush;

    onResize();

    // Get device queue

    queue_ = wgpuDeviceGetQueue(device_);

    std::cout << "Queue: " << queue_ << std::endl;

    // Adapter & instance are only needed during initialization - release them

    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

    // Init ImGui

    imguiContext_ = ImGui::CreateContext();

    ImGui_ImplSDL2_InitForOther(window_);

    ImGui_ImplWGPU_InitInfo imguiWgpuInitInfo;
    imguiWgpuInitInfo.Device = device_;
    imguiWgpuInitInfo.RenderTargetFormat = surfaceFormat_;

    ImGui_ImplWGPU_Init(&imguiWgpuInitInfo);
}

bool Application::running()
{
    return running_;
}

void Application::pollEvents()
{
    for (SDL_Event event; SDL_PollEvent(&event);)
    {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT)
            running_ = false;

        if (event.type == SDL_WINDOWEVENT)
            if (event.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                width_ = event.window.data1;
                height_ = event.window.data2;
                onResize();
            }

        bool mouseCaptured = ImGui::GetIO().WantCaptureMouse;

        if (event.type == SDL_MOUSEMOTION)
            mousePosition_ = {(float)event.motion.x, (float)event.motion.y};

        if (event.type == SDL_MOUSEBUTTONDOWN && !mouseCaptured)
            if (event.button.button == SDL_BUTTON_LEFT)
                mouseDown_ = true;

        if (event.type == SDL_MOUSEBUTTONUP)
            if (event.button.button == SDL_BUTTON_LEFT)
                mouseDown_ = false;
    }
}

WGPUTextureView Application::newFrame()
{
    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);

    if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Timeout)
    {
        std::cout << "Surface texture timeout" << std::endl;
        return nullptr;
    }

    if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated)
    {
        std::cout << "Surface texture outdated" << std::endl;
        return nullptr;
    }

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
        throw std::runtime_error(std::format("Can't get surface texture: {}", (int)surfaceTexture.status));

    WGPUTextureViewDescriptor surfaceTextureViewDescriptor = {};
    surfaceTextureViewDescriptor.format = surfaceFormat_;
    surfaceTextureViewDescriptor.dimension = WGPUTextureViewDimension_2D;
    surfaceTextureViewDescriptor.baseMipLevel = 0;
    surfaceTextureViewDescriptor.mipLevelCount = 1;
    surfaceTextureViewDescriptor.baseArrayLayer = 0;
    surfaceTextureViewDescriptor.arrayLayerCount = 1;
    surfaceTextureViewDescriptor.aspect = WGPUTextureAspect_All;

    return wgpuTextureCreateView(surfaceTexture.texture, &surfaceTextureViewDescriptor);
}

void Application::drawUI(WGPUTextureView target)
{
    ImGui::Render();

    WGPUCommandEncoderDescriptor commandEncoderDescriptor = {};
    WGPUCommandEncoder commandEncoder = wgpuDeviceCreateCommandEncoder(device_, &commandEncoderDescriptor);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = target;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor renderPassDescriptor = {};
    renderPassDescriptor.colorAttachmentCount = 1;
    renderPassDescriptor.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder renderPassEncoder = wgpuCommandEncoderBeginRenderPass(commandEncoder, &renderPassDescriptor);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPassEncoder);
    wgpuRenderPassEncoderEnd(renderPassEncoder);

    WGPUCommandBufferDescriptor commandBufferDescriptor = {};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);
    wgpuQueueSubmit(queue_, 1, &commandBuffer);
    wgpuCommandBufferRelease(commandBuffer);
}

void Application::present()
{
    wgpuSurfacePresent(surface_);
}

WGPUDevice Application::device() const
{
    return device_;
}

WGPUQueue Application::queue() const
{
    return queue_;
}

WGPUTextureFormat Application::surfaceFormat() const
{
    return surfaceFormat_;
}

int Application::width() const
{
    return width_;
}

int Application::height() const
{
    return height_;
}

float Application::aspectRatio() const
{
    return width_ * 1.f / height_;
}

Vector2f Application::mousePosition() const
{
    return mousePosition_;
}

bool Application::mouseDown() const
{
    return mouseDown_;
}

void Application::onResize()
{
    // Reconfigure the surface for the new size

    WGPUSurfaceConfiguration surfaceConfiguration = {};
    surfaceConfiguration.device = device_;
    surfaceConfiguration.format = surfaceFormat_;
    surfaceConfiguration.usage = WGPUTextureUsage_RenderAttachment;
    surfaceConfiguration.viewFormatCount = 1;
    surfaceConfiguration.viewFormats = &surfaceFormat_;
    surfaceConfiguration.alphaMode = WGPUCompositeAlphaMode_Auto;
    surfaceConfiguration.width = width_;
    surfaceConfiguration.height = height_;
    surfaceConfiguration.presentMode = WGPUPresentMode_Fifo;

    wgpuSurfaceConfigure(surface_, &surfaceConfiguration);

    std::cout << "Resized to " << width_ << "x" << height_ << std::endl;
}
