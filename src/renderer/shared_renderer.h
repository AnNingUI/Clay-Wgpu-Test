#ifndef CLAY_SHARED_RENDERER_H
#define CLAY_SHARED_RENDERER_H

#include <webgpu/wgpu.h>

// 共享渲染资源管理器
typedef struct {
    // 共享的顶点缓冲区
    WGPUBuffer shared_vertex_buffer;
    
    // 共享的渲染管线
    WGPURenderPipeline shared_pipeline;
    
    // 着色器模块
    WGPUShaderModule shader_module;
    
    // 管线布局
    WGPUPipelineLayout pipeline_layout;
    
    // 设备引用
    WGPUDevice device;
    
    // 缓冲区大小管理
    size_t buffer_size;
} SharedRendererResources;

// 初始化共享渲染资源
SharedRendererResources* shared_renderer_init(WGPUDevice device, uint32_t buffer_size);

// 销毁共享渲染资源
void shared_renderer_destroy(SharedRendererResources* resources);

// 获取共享顶点缓冲区
WGPUBuffer shared_renderer_get_vertex_buffer(SharedRendererResources* resources);

// 获取共享渲染管线
WGPURenderPipeline shared_renderer_get_pipeline(SharedRendererResources* resources);

// 更新缓冲区数据
void shared_renderer_update_buffer(SharedRendererResources* resources, WGPUQueue queue, 
                                  const void* data, size_t data_size, size_t offset);

#endif // CLAY_SHARED_RENDERER_H