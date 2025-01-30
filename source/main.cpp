#include <webgpu-shallow-water/application.hpp>

int main()
{
    Application application;

    char temp[128] = {};
    float test = 0.5f;

    while (application.running())
    {
        application.pollEvents();

        auto surfaceTexture = application.newFrame();

        WGPUTextureViewDescriptor surfaceTextureViewDescriptor = {};
        surfaceTextureViewDescriptor.format = application.surfaceFormat();
        surfaceTextureViewDescriptor.dimension = WGPUTextureViewDimension_2D;
        surfaceTextureViewDescriptor.baseMipLevel = 0;
        surfaceTextureViewDescriptor.mipLevelCount = 1;
        surfaceTextureViewDescriptor.baseArrayLayer = 0;
        surfaceTextureViewDescriptor.arrayLayerCount = 1;
        surfaceTextureViewDescriptor.aspect = WGPUTextureAspect_All;

        auto surfaceTextureView = wgpuTextureCreateView(surfaceTexture, &surfaceTextureViewDescriptor);

        {
            // Just clear the screen for now

            WGPUCommandEncoderDescriptor commandEncoderDescriptor = {};
            WGPUCommandEncoder commandEncoder = wgpuDeviceCreateCommandEncoder(application.device(), &commandEncoderDescriptor);

            WGPURenderPassColorAttachment colorAttachment = {};
            colorAttachment.view = surfaceTextureView;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.6, 0.8, 1.0, 0.0};

            WGPURenderPassDescriptor renderPassDescriptor = {};
            renderPassDescriptor.colorAttachmentCount = 1;
            renderPassDescriptor.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder renderPassEncoder = wgpuCommandEncoderBeginRenderPass(commandEncoder, &renderPassDescriptor);
            wgpuRenderPassEncoderEnd(renderPassEncoder);

            WGPUCommandBufferDescriptor commandBufferDescriptor = {};
            WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(commandEncoder, &commandBufferDescriptor);
            wgpuQueueSubmit(application.queue(), 1, &commandBuffer);
            wgpuCommandBufferRelease(commandBuffer);
        }

        ImGui::Text("Hello, world %f", test);
        ImGui::Button("Button");
        ImGui::InputText("string", temp, sizeof(temp));
        ImGui::SliderFloat("float", &test, 0.f, 1.f);

        application.drawUI(surfaceTextureView);

        application.present();
    }
}
