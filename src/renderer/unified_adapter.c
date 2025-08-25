#include "unified_adapter.h"
#include "unified_renderer.h"
#include "../DEV.h"
#include <stdlib.h>
#include <string.h>
// 创建统一适配器
UnifiedAdapter* unified_adapter_create(WGPUDevice device, WGPUQueue queue,
                                     WGPUTextureView targetView,
                                     uint32_t screenWidth, uint32_t screenHeight) {
    UnifiedAdapter *adapter = malloc(sizeof(UnifiedAdapter));
    if (!adapter) {
        Log("统一适配器内存分配失败\n");
        return NULL;
    }
    
    memset(adapter, 0, sizeof(UnifiedAdapter));
    
    // 初始化基本信息
    adapter->device = device;
    adapter->queue = queue;
    adapter->targetView = targetView;
    adapter->screenWidth = screenWidth;
    adapter->screenHeight = screenHeight;
    adapter->defaultFontId = -1;
    adapter->initialized = false;
    adapter->frameCount = 0;
    adapter->debugMode = false;
    
    // 创建统一渲染器
    adapter->unifiedRenderer = unified_renderer_create(device, queue, screenWidth, screenHeight);
    if (!adapter->unifiedRenderer) {
        Log("统一渲染器创建失败\n");
        free(adapter);
        return NULL;
    }
    
    // 设置兼容指针
    adapter->textRenderer = adapter->unifiedRenderer;
    adapter->initialized = true;
    
    Log("统一适配器创建成功 (屏幕尺寸: %dx%d)\n", screenWidth, screenHeight);
    return adapter;
}

// 销毁统一适配器
void unified_adapter_destroy(UnifiedAdapter *adapter) {
    if (!adapter) return;
    
    if (adapter->unifiedRenderer) {
        unified_renderer_destroy(adapter->unifiedRenderer);
    }
    
    free(adapter);
    Log("统一适配器已销毁\n");
}

// 更新屏幕尺寸
void unified_adapter_update_screen_size(UnifiedAdapter *adapter,
                                       uint32_t screenWidth, uint32_t screenHeight) {
    if (!adapter || !adapter->unifiedRenderer) return;
    
    adapter->screenWidth = screenWidth;
    adapter->screenHeight = screenHeight;
    
    unified_renderer_resize(adapter->unifiedRenderer, screenWidth, screenHeight);
    
    Log("屏幕尺寸已更新: %dx%d\n", screenWidth, screenHeight);
}

// 主渲染函数
void unified_adapter_render(UnifiedAdapter *adapter, 
                          Clay_RenderCommandArray renderCommands) {
    if (!adapter || !adapter->unifiedRenderer || !adapter->initialized) {
        Log("适配器未初始化或无效\n");
        return;
    }
    
    adapter->frameCount++;
    
    if (adapter->debugMode) {
        Log("=== 开始渲染帧 %d，命令数量: %d ===\n", 
            adapter->frameCount, renderCommands.length);
    }
    
    // 创建渲染通道
    WGPUCommandEncoderDescriptor encoderDesc = {
        .label = {.data = "Unified Adapter Encoder", .length = WGPU_STRLEN}
    };
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(adapter->device, &encoderDesc);
    
    // 创建颜色附件
    WGPURenderPassColorAttachment colorAttachment = {
        .view = adapter->targetView,
        .resolveTarget = NULL,
        .clearValue = {0.1f, 0.1f, 0.1f, 1.0f},
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    
    // 深度附件已禁用以简化渲染
    
    // 创建渲染通道
    WGPURenderPassDescriptor renderPassDesc = {
        .label = {.data = "Unified Render Pass", .length = WGPU_STRLEN},
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
        .depthStencilAttachment = NULL
    };
    
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    
    // 处理渲染命令
    unified_adapter_process_render_commands(adapter, renderCommands);
    
    // 完成渲染
    unified_adapter_finish_render(adapter, renderPass);
    
    // 结束渲染通道
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);
    
    // 提交命令
    WGPUCommandBufferDescriptor commandBufferDesc = {
        .label = {.data = "Unified Command Buffer", .length = WGPU_STRLEN}
    };
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDesc);
    wgpuQueueSubmit(adapter->queue, 1, &commandBuffer);
    
    // 清理资源
    wgpuCommandBufferRelease(commandBuffer);
    wgpuCommandEncoderRelease(encoder);
    
    if (adapter->debugMode) {
        Log("=== 帧 %d 渲染完成 ===\n", adapter->frameCount);
    }
}

// 字体管理
int unified_adapter_load_font(UnifiedAdapter *adapter, const char *fontPath, int fontSize) {
    if (!adapter || !adapter->unifiedRenderer) return -1;
    
    int fontId = unified_renderer_load_font(adapter->unifiedRenderer, fontPath, fontSize);
    
    if (fontId >= 0) {
        // 如果这是第一个字体，设置为默认字体
        if (adapter->defaultFontId < 0) {
            adapter->defaultFontId = fontId;
            unified_renderer_set_default_font(adapter->unifiedRenderer, fontId);
        }
        
        Log("字体加载成功: %s (ID: %d, 大小: %d)\n", fontPath, fontId, fontSize);
    } else {
        Log("字体加载失败: %s\n", fontPath);
    }
    
    return fontId;
}

bool unified_adapter_set_default_font(UnifiedAdapter *adapter, int fontId) {
    if (!adapter || !adapter->unifiedRenderer) return false;
    
    if (unified_renderer_set_default_font(adapter->unifiedRenderer, fontId)) {
        adapter->defaultFontId = fontId;
        Log("默认字体已设置为 ID: %d\n", fontId);
        return true;
    }
    
    Log("设置默认字体失败 ID: %d\n", fontId);
    return false;
}

// 文本测量
float unified_adapter_measure_text_width(UnifiedAdapter *adapter, 
                                        const char *text, int textLength, int fontId) {
    if (!adapter || !adapter->unifiedRenderer) return 0.0f;
    
    if (fontId < 0) {
        fontId = adapter->defaultFontId;
    }
    
    return unified_renderer_measure_text(adapter->unifiedRenderer, text, fontId, textLength);
}

// 添加与Clay文本测量回调兼容的函数
Clay_Dimensions unified_adapter_measure_clay_text(Clay_StringSlice text,
                                                 Clay_TextElementConfig *config,
                                                 void *userData) {
    UnifiedAdapter *adapter = (UnifiedAdapter *)userData;
    if (!adapter || !adapter->unifiedRenderer) {
        return (Clay_Dimensions){.width = text.length * config->fontSize * 0.6f,
                               .height = config->fontSize};
    }
    
    float width = unified_renderer_measure_text(adapter->unifiedRenderer, text.chars, 
                                              config->fontId, text.length);
    float height = unified_adapter_get_line_height(adapter, config->fontId);
    
    return (Clay_Dimensions){.width = width, .height = height};
}

float unified_adapter_get_line_height(UnifiedAdapter *adapter, int fontId) {
    if (!adapter || !adapter->unifiedRenderer) return 16.0f;
    
    if (fontId < 0) {
        fontId = adapter->defaultFontId;
    }
    
    if (fontId >= 0 && fontId < adapter->unifiedRenderer->fontCount) {
        return adapter->unifiedRenderer->fonts[fontId].lineHeight;
    }
    
    return 16.0f; // 默认行高
}

// ===== 兼容性函数实现 =====

bool Clay_WebGPU_LoadFont(UnifiedAdapter *adapter, const char *fontPath, int fontSize) {
    return unified_adapter_load_font(adapter, fontPath, fontSize) >= 0;
}

bool Clay_WebGPU_SetDefaultFont(UnifiedAdapter *adapter, int fontId) {
    return unified_adapter_set_default_font(adapter, fontId);
}

// 添加兼容性函数
void Clay_WebGPU_RenderText(UnifiedAdapter *adapter,
                           WGPURenderPassEncoder renderPass,
                           Clay_TextRenderData *textData,
                           Clay_BoundingBox bbox, float z_index) {
    // 这个函数现在由统一渲染器内部处理，这里提供空实现以保持兼容
    if (adapter && adapter->debugMode) {
        Log("Clay_WebGPU_RenderText调用（由统一渲染器处理）\n");
    }
}

void Clay_WebGPU_PrintTextStats(UnifiedAdapter *adapter) {
    if (!adapter || !adapter->unifiedRenderer) return;
    
    unified_renderer_print_stats(adapter->unifiedRenderer);
}

// ===== 调试和统计 =====

void unified_adapter_print_stats(UnifiedAdapter *adapter) {
    if (!adapter) return;
    
    Log("=== 统一适配器统计 ===\n");
    Log("帧数: %d\n", adapter->frameCount);
    Log("屏幕尺寸: %dx%d\n", adapter->screenWidth, adapter->screenHeight);
    Log("默认字体ID: %d\n", adapter->defaultFontId);
    Log("调试模式: %s\n", adapter->debugMode ? "开启" : "关闭");
    
    if (adapter->unifiedRenderer) {
        unified_renderer_print_stats(adapter->unifiedRenderer);
    }
}

void unified_adapter_reset_stats(UnifiedAdapter *adapter) {
    if (!adapter || !adapter->unifiedRenderer) return;
    
    unified_renderer_reset_stats(adapter->unifiedRenderer);
    adapter->frameCount = 0;
    
    Log("统计数据已重置\n");
}

void unified_adapter_enable_debug(UnifiedAdapter *adapter, bool enable) {
    if (!adapter) return;
    
    adapter->debugMode = enable;
    Log("调试模式: %s\n", enable ? "开启" : "关闭");
}

// ===== 高级功能 =====

int unified_adapter_load_texture(UnifiedAdapter *adapter, const char *imagePath) {
    if (!adapter || !adapter->unifiedRenderer) return -1;
    
    return unified_renderer_load_texture(adapter->unifiedRenderer, imagePath);
}

// 获取纹理尺寸
bool unified_adapter_get_texture_dimensions(UnifiedAdapter *adapter, int textureIndex, int *width, int *height) {
    if (!adapter || !adapter->unifiedRenderer || !width || !height) return false;
    
    return unified_renderer_get_texture_dimensions(adapter->unifiedRenderer, textureIndex, width, height);
}

void unified_adapter_release_texture(UnifiedAdapter *adapter, int textureIndex) {
    if (!adapter || !adapter->unifiedRenderer) return;
    
    unified_renderer_release_texture(adapter->unifiedRenderer, textureIndex);
}

void unified_adapter_add_custom_render(UnifiedAdapter *adapter,
                                     float x, float y, float width, float height,
                                     float zIndex, UnifiedCustomRenderCallback callback,
                                     void *userData) {
    if (!adapter || !callback) return;
    
    // 简化实现：直接调用回调
    Clay_BoundingBox bbox = {x, y, width, height};
    callback(adapter, bbox, userData);
}

// ===== 渲染状态管理 =====

static float current_z_layer = 0.0f;

void unified_adapter_push_scissor(UnifiedAdapter *adapter, Clay_BoundingBox scissorRect) {
    // 简化实现：记录裁剪区域但不立即应用
    // 实际应用需要在渲染时处理
    if (adapter->debugMode) {
        Log("推送裁剪区域: (%.1f, %.1f, %.1f, %.1f)\n", 
            scissorRect.x, scissorRect.y, scissorRect.width, scissorRect.height);
    }
}

void unified_adapter_pop_scissor(UnifiedAdapter *adapter) {
    // 简化实现：恢复之前的裁剪区域
    if (adapter->debugMode) {
        Log("弹出裁剪区域\n");
    }
}

void unified_adapter_set_z_layer(UnifiedAdapter *adapter, float baseZ) {
    current_z_layer = baseZ;
    
    if (adapter && adapter->debugMode) {
        Log("设置Z层级: %.3f\n", baseZ);
    }
}

float unified_adapter_get_z_layer(UnifiedAdapter *adapter) {
    return current_z_layer;
}

// ===== 配置管理 =====

UnifiedAdapterConfig unified_adapter_get_default_config(void) {
    return (UnifiedAdapterConfig){
        .enableDebugOutput = false,
        .enableWireframe = false,
        .enableDepthTesting = true,
        .globalAlpha = 1.0f,
        .clearColor = {26, 26, 26, 255} // 深灰色背景
    };
}

void unified_adapter_configure(UnifiedAdapter *adapter, const UnifiedAdapterConfig *config) {
    if (!adapter || !config) return;
    
    adapter->debugMode = config->enableDebugOutput;
    
    Log("适配器配置更新:\n");
    Log("  调试输出: %s\n", config->enableDebugOutput ? "开启" : "关闭");
    Log("  线框模式: %s\n", config->enableWireframe ? "开启" : "关闭");
    Log("  深度测试: %s\n", config->enableDepthTesting ? "开启" : "关闭");
    Log("  全局透明度: %.2f\n", config->globalAlpha);
    Log("  清屏颜色: (%d, %d, %d, %d)\n", 
        config->clearColor.r, config->clearColor.g, config->clearColor.b, config->clearColor.a);
}

// ===== 性能分析 =====

UnifiedAdapterStats unified_adapter_get_stats(UnifiedAdapter *adapter) {
    UnifiedAdapterStats stats = {0};
    
    if (!adapter || !adapter->unifiedRenderer) return stats;
    
    UnifiedRenderer *renderer = adapter->unifiedRenderer;
    
    stats.frameCount = adapter->frameCount;
    stats.totalVertices = renderer->vertexCount;
    stats.totalIndices = renderer->indexCount;
    stats.drawCalls = renderer->drawCalls;
    stats.batchCount = renderer->batchCount;
    stats.frameTime = 16.67f; // 假设60fps
    stats.glyphCacheHits = renderer->glyphCacheHits;
    stats.glyphCacheMisses = renderer->glyphCacheMisses;
    stats.gpuMemoryUsage = (float)(renderer->vertexCount * sizeof(UnifiedVertex) + 
                                  renderer->indexCount * sizeof(uint16_t)) / (1024.0f * 1024.0f);
    
    return stats;
}

// ===== 错误处理 =====

static UnifiedAdapterErrorCallback global_error_callback = NULL;
static void *global_error_userdata = NULL;

void unified_adapter_set_error_callback(UnifiedAdapter *adapter, 
                                       UnifiedAdapterErrorCallback callback, 
                                       void *userData) {
    global_error_callback = callback;
    global_error_userdata = userData;
    
    if (adapter && adapter->debugMode) {
        Log("错误回调已设置\n");
    }
}

static void unified_adapter_report_error(UnifiedAdapterError error, const char *message) {
    if (global_error_callback) {
        global_error_callback(error, message, global_error_userdata);
    } else {
        Log("错误: %s\n", message);
    }
}

// ===== 内部辅助函数的具体实现 =====

// 实现process_render_commands的具体版本（替代inline版本）
void unified_adapter_process_render_commands(UnifiedAdapter *adapter,
                                           Clay_RenderCommandArray renderCommands) {
    if (!adapter || !adapter->unifiedRenderer) return;
    
    // 开始帧渲染
    unified_renderer_begin_frame(adapter->unifiedRenderer);
    
    // 处理每个渲染命令
    for (int i = 0; i < renderCommands.length; i++) {
        Clay_RenderCommand *command = &renderCommands.internalArray[i];
        
        // 简化的渲染命令日志
        if (command->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            Log("发现图片渲染命令 %d: 边界框=(%.1f,%.1f,%.1f,%.1f)\n", 
                i, command->boundingBox.x, command->boundingBox.y, 
                command->boundingBox.width, command->boundingBox.height);
        }
        
        switch (command->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                unified_renderer_process_clay_rectangle(adapter->unifiedRenderer, command);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                unified_renderer_process_clay_text(adapter->unifiedRenderer, command);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                Log(">>> 开始处理图片渲染命令\n");
                unified_renderer_process_clay_image(adapter->unifiedRenderer, command);
                Log(">>> 图片渲染命令处理完成\n");
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                unified_renderer_process_clay_border(adapter->unifiedRenderer, command);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                unified_adapter_push_scissor(adapter, command->boundingBox);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                unified_adapter_pop_scissor(adapter);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                // 处理自定义渲染命令
                if (adapter->debugMode) {
                    Log("处理自定义渲染命令 (Z: %.3f)\n", command->zIndex);
                }
                break;
            }
            default:
                Log("警告: 未处理的渲染命令类型: %d\n", command->commandType);
                break;
        }
    }
    
    if (adapter->debugMode) {
        Log("处理了 %d 个渲染命令\n", renderCommands.length);
    }
}

// 实现finish_render的具体版本（替代inline版本）
void unified_adapter_finish_render(UnifiedAdapter *adapter, 
                                 WGPURenderPassEncoder renderPass) {
    if (!adapter || !adapter->unifiedRenderer) return;
    
    // 完成帧渲染
    unified_renderer_end_frame(adapter->unifiedRenderer, renderPass);
    
    if (adapter->debugMode) {
        UnifiedAdapterStats stats = unified_adapter_get_stats(adapter);
        Log("渲染完成 - 顶点: %d, 索引: %d, 绘制调用: %d, 批次: %d\n",
            stats.totalVertices, stats.totalIndices, stats.drawCalls, stats.batchCount);
    }
}

// ===== 实用工具函数 =====

// 颜色混合
Clay_Color unified_adapter_blend_colors(Clay_Color base, Clay_Color overlay, float alpha) {
    float invAlpha = 1.0f - alpha;
    return (Clay_Color){
        .r = (uint8_t)(base.r * invAlpha + overlay.r * alpha),
        .g = (uint8_t)(base.g * invAlpha + overlay.g * alpha),
        .b = (uint8_t)(base.b * invAlpha + overlay.b * alpha),
        .a = (uint8_t)(base.a * invAlpha + overlay.a * alpha)
    };
}

// 矩形相交测试
bool unified_adapter_rect_intersects(Clay_BoundingBox a, Clay_BoundingBox b) {
    return !(a.x + a.width < b.x || b.x + b.width < a.x ||
             a.y + a.height < b.y || b.y + b.height < a.y);
}

// 点在矩形内测试
bool unified_adapter_point_in_rect(float x, float y, Clay_BoundingBox rect) {
    return x >= rect.x && x < rect.x + rect.width &&
           y >= rect.y && y < rect.y + rect.height;
}

// Z-index排序比较函数
int unified_adapter_compare_z_index(const void *a, const void *b) {
    Clay_RenderCommand *cmdA = (Clay_RenderCommand*)a;
    Clay_RenderCommand *cmdB = (Clay_RenderCommand*)b;
    
    if (cmdA->zIndex < cmdB->zIndex) return -1;
    if (cmdA->zIndex > cmdB->zIndex) return 1;
    return 0;
}