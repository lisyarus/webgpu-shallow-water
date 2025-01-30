#pragma once

#include <imgui.h>

#include <SDL2/SDL_video.h>

#include <webgpu/webgpu.h>

struct Application
{
    Application();

    bool running();
    void pollEvents();
    WGPUTextureView newFrame();
    void drawUI(WGPUTextureView target);
    void present();

    WGPUDevice device();
    WGPUQueue queue();
    WGPUTextureFormat surfaceFormat();

private:
    SDL_Window * window_ = nullptr;
    int width_ = 1024;
    int height_ = 576;

    WGPUSurface surface_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_BGRA8UnormSrgb;

    ImGuiContext * imguiContext_ = nullptr;

    bool running_ = true;

    void onResize();
};
