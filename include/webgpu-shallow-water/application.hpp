#pragma once

#include <webgpu-shallow-water/vector.hpp>

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

    void createUI();

    WGPUDevice device() const;
    WGPUQueue queue() const;
    WGPUTextureFormat surfaceFormat() const;

    int width() const;
    int height() const;
    float aspectRatio() const;

    Vector2f mousePosition() const;
    bool mouseDown() const;

private:
    SDL_Window * window_ = nullptr;
    int width_ = 1024;
    int height_ = 576;

    bool vsync_ = true;
    bool needUpdateSurface_ = false;

    WGPUSurface surface_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_BGRA8UnormSrgb;

    ImGuiContext * imguiContext_ = nullptr;

    bool running_ = true;
    Vector2f mousePosition_ = {0.f, 0.f};
    bool mouseDown_ = false;

    void onResize();
};
