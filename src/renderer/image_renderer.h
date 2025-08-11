#ifndef CLAY_IMAGE_RENDERER_WEBGPU_H
#define CLAY_IMAGE_RENDERER_WEBGPU_H

#include "clay.h"
#include <webgpu/wgpu.h>

// 图像数据结构体
typedef struct {
    WGPUTexture texture;
    int width;
    int height;
} WebGPUImage;

// 图像渲染器结构体
typedef struct {
    WGPUDevice device;
    WGPUQueue queue;
    WGPURenderPipeline imagePipeline;
    WGPUBuffer vertexBuffer;
    WGPUBindGroupLayout textureBindGroupLayout;
    WGPUPipelineLayout imagePipelineLayout;
    WGPUSampler defaultSampler;
    WGPUTexture defaultTexture;
    WGPUTextureView defaultTextureView;
    WGPUBindGroup defaultBindGroup;
} ImageRenderer;

// 创建图像渲染器
ImageRenderer* image_renderer_create(WGPUDevice device, WGPUQueue queue);

// 销毁图像渲染器
void image_renderer_destroy(ImageRenderer* renderer);

// 创建WebGPU纹理
WebGPUImage* image_renderer_create_texture(ImageRenderer* renderer, const char* imagePath);

// 销毁WebGPU纹理
void image_renderer_destroy_texture(WebGPUImage* image);

// 渲染纹理
void image_renderer_render_texture(ImageRenderer* renderer, 
                                 WGPURenderPassEncoder renderPass,
                                 WebGPUImage* image,
                                 Clay_BoundingBox boundingBox,
                                 Clay_Color tintColor,
                                 Clay_CornerRadius cornerRadius,
                                 uint32_t windowWidth,
                                 uint32_t windowHeight);

// 处理Clay渲染命令
void image_renderer_process_clay_commands(ImageRenderer* renderer,
                                        WGPURenderPassEncoder renderPass,
                                        Clay_RenderCommandArray renderCommands,
                                        uint32_t windowWidth,
                                        uint32_t windowHeight);

#endif // CLAY_IMAGE_RENDERER_WEBGPU_H