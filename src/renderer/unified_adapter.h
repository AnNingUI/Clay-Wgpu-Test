#ifndef CLAY_UNIFIED_ADAPTER_H
#define CLAY_UNIFIED_ADAPTER_H

#include "clay.h"
#include "unified_renderer.h"
#include <webgpu/wgpu.h>
#include <stdint.h>
#include <stdbool.h>

// 统一渲染器适配器 - 提供与原有Clay_WebGPU_Context兼容的接口
typedef struct {
    // 核心统一渲染器
    UnifiedRenderer *unifiedRenderer;
    
    // WebGPU资源（用于兼容性）
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureView targetView;
    
    // 屏幕信息
    uint32_t screenWidth;
    uint32_t screenHeight;
    
    // 字体管理
    int defaultFontId;
    
    // 兼容性标志
    bool initialized;
    
    // 调试和统计
    int frameCount;
    bool debugMode;
    
    // 兼容字段（保持与原有代码的兼容性）
    void *textRenderer; // 指向UnifiedRenderer以保持兼容
} UnifiedAdapter;

// ===== 主要API - 替代原有的Clay_WebGPU函数 =====

// 初始化和清理
UnifiedAdapter* unified_adapter_create(WGPUDevice device, WGPUQueue queue,
                                     WGPUTextureView targetView,
                                     uint32_t screenWidth, uint32_t screenHeight);
void unified_adapter_destroy(UnifiedAdapter *adapter);

// 屏幕尺寸更新
void unified_adapter_update_screen_size(UnifiedAdapter *adapter,
                                       uint32_t screenWidth, uint32_t screenHeight);

// 主渲染函数
void unified_adapter_render(UnifiedAdapter *adapter, 
                          Clay_RenderCommandArray renderCommands);

// 字体管理
int unified_adapter_load_font(UnifiedAdapter *adapter, const char *fontPath, int fontSize);
bool unified_adapter_set_default_font(UnifiedAdapter *adapter, int fontId);

float unified_adapter_get_line_height(UnifiedAdapter *adapter, int fontId);

// 字体查找
int unified_adapter_get_font_id_by_path(UnifiedAdapter *adapter, const char *fontPath);

// ===== 兼容性API - 与原有Clay_WebGPU_Context接口兼容 =====

// 这些函数保持原有命名，但内部使用UnifiedRenderer
#define Clay_WebGPU_Context UnifiedAdapter
#define Clay_WebGPU_Initialize unified_adapter_create
#define Clay_WebGPU_Cleanup unified_adapter_destroy
#define Clay_WebGPU_UpdateScreenSize unified_adapter_update_screen_size
#define Clay_WebGPU_Render unified_adapter_render

// 兼容性包装函数
bool Clay_WebGPU_LoadFont(UnifiedAdapter *adapter, const char *fontPath, int fontSize);
bool Clay_WebGPU_SetDefaultFont(UnifiedAdapter *adapter, int fontId);
int Clay_WebGPU_GetFontId(UnifiedAdapter *adapter, const char *fontPath);
void Clay_WebGPU_PrintTextStats(UnifiedAdapter *adapter);

// 文本渲染兼容函数
void Clay_WebGPU_RenderText(UnifiedAdapter *adapter,
                           WGPURenderPassEncoder renderPass,
                           Clay_TextRenderData *textData,
                           Clay_BoundingBox bbox, float z_index);

// Clay文本测量回调
Clay_Dimensions unified_adapter_measure_clay_text(Clay_StringSlice text,
                                                 Clay_TextElementConfig *config,
                                                 void *userData);

// ===== 内部辅助函数 =====

// 渲染命令处理
void unified_adapter_process_render_commands(UnifiedAdapter *adapter,
                                           Clay_RenderCommandArray renderCommands);

// 完成渲染
void unified_adapter_finish_render(UnifiedAdapter *adapter, 
                                 WGPURenderPassEncoder renderPass);

// 调试和统计
void unified_adapter_print_stats(UnifiedAdapter *adapter);
void unified_adapter_reset_stats(UnifiedAdapter *adapter);
void unified_adapter_enable_debug(UnifiedAdapter *adapter, bool enable);

// ===== 高级功能 =====

// 纹理管理（用于图像支持）
int unified_adapter_load_texture(UnifiedAdapter *adapter, const char *imagePath);
void unified_adapter_release_texture(UnifiedAdapter *adapter, int textureIndex);
bool unified_adapter_get_texture_dimensions(UnifiedAdapter *adapter, int textureIndex, int *width, int *height);

// 自定义渲染支持
typedef void (*UnifiedCustomRenderCallback)(UnifiedAdapter *adapter, 
                                           Clay_BoundingBox boundingBox,
                                           void *userData);

void unified_adapter_add_custom_render(UnifiedAdapter *adapter,
                                     float x, float y, float width, float height,
                                     float zIndex, UnifiedCustomRenderCallback callback,
                                     void *userData);

// ===== 渲染状态管理 =====

// 裁剪区域
void unified_adapter_push_scissor(UnifiedAdapter *adapter, Clay_BoundingBox scissorRect);
void unified_adapter_pop_scissor(UnifiedAdapter *adapter);

// Z-index层管理
void unified_adapter_set_z_layer(UnifiedAdapter *adapter, float baseZ);
float unified_adapter_get_z_layer(UnifiedAdapter *adapter);

// ===== 配置选项 =====

typedef struct {
    bool enableDebugOutput;
    bool enableWireframe;
    bool enableDepthTesting;
    float globalAlpha;
    Clay_Color clearColor;
} UnifiedAdapterConfig;

void unified_adapter_configure(UnifiedAdapter *adapter, const UnifiedAdapterConfig *config);
UnifiedAdapterConfig unified_adapter_get_default_config(void);

// ===== 性能分析 =====

typedef struct {
    int frameCount;
    int totalVertices;
    int totalIndices;
    int drawCalls;
    int batchCount;
    float frameTime;
    int glyphCacheHits;
    int glyphCacheMisses;
    float gpuMemoryUsage;
} UnifiedAdapterStats;

UnifiedAdapterStats unified_adapter_get_stats(UnifiedAdapter *adapter);

// ===== 错误处理 =====

typedef enum {
    UNIFIED_ADAPTER_SUCCESS = 0,
    UNIFIED_ADAPTER_ERROR_DEVICE_LOST,
    UNIFIED_ADAPTER_ERROR_OUT_OF_MEMORY,
    UNIFIED_ADAPTER_ERROR_INVALID_PARAMETER,
    UNIFIED_ADAPTER_ERROR_SHADER_COMPILATION,
    UNIFIED_ADAPTER_ERROR_TEXTURE_LOAD_FAILED,
    UNIFIED_ADAPTER_ERROR_FONT_LOAD_FAILED
} UnifiedAdapterError;

typedef void (*UnifiedAdapterErrorCallback)(UnifiedAdapterError error, const char *message, void *userData);

void unified_adapter_set_error_callback(UnifiedAdapter *adapter, 
                                       UnifiedAdapterErrorCallback callback, 
                                       void *userData);

// ===== 实用宏 =====

// 颜色转换
#define UNIFIED_COLOR_FROM_RGB(r, g, b) ((Clay_Color){r, g, b, 255})
#define UNIFIED_COLOR_FROM_RGBA(r, g, b, a) ((Clay_Color){r, g, b, a})
#define UNIFIED_COLOR_FROM_HEX(hex) ((Clay_Color){ \
    .r = (uint8_t)(((hex) >> 16) & 0xFF), \
    .g = (uint8_t)(((hex) >> 8) & 0xFF), \
    .b = (uint8_t)((hex) & 0xFF), \
    .a = 255 \
})

// Z-index助手
#define UNIFIED_Z_BACKGROUND    0.0f
#define UNIFIED_Z_CONTENT       0.5f  
#define UNIFIED_Z_UI            0.8f
#define UNIFIED_Z_OVERLAY       0.9f
#define UNIFIED_Z_MODAL         0.95f
#define UNIFIED_Z_TOOLTIP       1.0f

// 常用圆角
#define UNIFIED_CORNER_NONE     ((Clay_CornerRadius){0, 0, 0, 0})
#define UNIFIED_CORNER_SMALL    ((Clay_CornerRadius){4, 4, 4, 4})
#define UNIFIED_CORNER_MEDIUM   ((Clay_CornerRadius){8, 8, 8, 8})
#define UNIFIED_CORNER_LARGE    ((Clay_CornerRadius){12, 12, 12, 12})
#define UNIFIED_CORNER_ROUND    ((Clay_CornerRadius){999, 999, 999, 999})

// ===== 向前兼容性保证 =====

// 为了确保现有代码能够无缝迁移，我们提供这些别名
#ifndef CLAY_UNIFIED_RENDERER_DISABLE_LEGACY_NAMES

// 类型别名
typedef UnifiedAdapter Clay_WebGPU_Context_New;
typedef UnifiedAdapterConfig Clay_WebGPU_Config;
typedef UnifiedAdapterStats Clay_WebGPU_Stats;

// 函数别名
#define clay_webgpu_initialize unified_adapter_create
#define clay_webgpu_cleanup unified_adapter_destroy
#define clay_webgpu_render unified_adapter_render
#define clay_webgpu_update_screen_size unified_adapter_update_screen_size

#endif // CLAY_UNIFIED_RENDERER_DISABLE_LEGACY_NAMES

#endif // CLAY_UNIFIED_ADAPTER_H