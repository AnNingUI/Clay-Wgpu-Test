#ifndef CLAY_RENDERER_WEBGPU_H
#define CLAY_RENDERER_WEBGPU_H

#include "clay.h"
#include "text_renderer.h"
#include <webgpu/wgpu.h>

// 向后兼容的定义 - 已废弃，请使用新的TextRenderer系统
#define CLAY_GLYPH_CACHE_SIZE 4096
#define CLAY_FONT_ATLAS_WIDTH 1024
#define CLAY_FONT_ATLAS_HEIGHT 1024



typedef struct {
  WGPUDevice device;
  WGPUQueue queue;
  WGPURenderPipeline rectanglePipeline;
  WGPUBuffer vertexBuffer;
  WGPUBuffer indexBuffer;
  WGPUBuffer uniformBuffer;
  WGPUTextureView targetView;
  uint32_t screenWidth;
  uint32_t screenHeight;

  // 新的独立文本渲染器
  TextRenderer *textRenderer;
  
  // 默认字体ID
  int defaultFontId;
} Clay_WebGPU_Context;

Clay_WebGPU_Context *Clay_WebGPU_Initialize(WGPUDevice device, WGPUQueue queue,
                                            WGPUTextureView targetView,
                                            uint32_t screenWidth,
                                            uint32_t screenHeight);
void Clay_WebGPU_UpdateScreenSize(Clay_WebGPU_Context *context,
                                  uint32_t screenWidth, uint32_t screenHeight);
void Clay_WebGPU_Render(Clay_WebGPU_Context *context,
                        Clay_RenderCommandArray renderCommands);
void Clay_WebGPU_Cleanup(Clay_WebGPU_Context *context);

// 字体管理函数
bool Clay_WebGPU_LoadFont(Clay_WebGPU_Context *context, const char *fontPath,
                          int fontSize);
bool Clay_WebGPU_SetDefaultFont(Clay_WebGPU_Context *context, int fontId);

// 文本渲染函数 (使用新的文本渲染器)
void Clay_WebGPU_RenderText(Clay_WebGPU_Context *context, WGPURenderPassEncoder renderPass,
                           Clay_TextRenderData *textData, Clay_BoundingBox bbox);

// 调试函数
void Clay_WebGPU_PrintTextStats(Clay_WebGPU_Context *context);

#endif
