// clay_renderer_webgpu.h
#ifndef CLAY_RENDERER_WEBGPU_H
#define CLAY_RENDERER_WEBGPU_H

#include "clay.h"
#include <webgpu/wgpu.h>

#define CLAY_GLYPH_CACHE_SIZE 4096
#define CLAY_FONT_ATLAS_WIDTH 1024
#define CLAY_FONT_ATLAS_HEIGHT 1024

// 字形信息结构
typedef struct {
  float x, y;               // 纹理坐标
  float width, height;      // 字形尺寸
  float advance;            // 字符前进距离
  float bearingX, bearingY; // 字符基准点偏移
} GlyphInfo;

// 字体纹理结构
typedef struct {
  WGPUTexture texture;
  WGPUTextureView textureView;
  WGPUSampler sampler;
  unsigned char *pixels;
  int width, height;
  int font_size;
} FontTexture;

// 字形缓存条目
typedef struct {
  uint32_t codepoint;
  GlyphInfo glyph;
  bool loaded;
} GlyphCacheEntry;

typedef struct {
  WGPUDevice device;
  WGPUQueue queue;
  WGPURenderPipeline rectanglePipeline;
  WGPURenderPipeline textPipeline;
  WGPUBuffer vertexBuffer;
  WGPUBuffer indexBuffer;
  WGPUBuffer uniformBuffer; // 添加缺失的字段
  WGPUTextureView targetView;
  uint32_t screenWidth;
  uint32_t screenHeight;

  // 字体和字形相关
  FontTexture *fontTexture;
  GlyphCacheEntry glyphCache[CLAY_GLYPH_CACHE_SIZE];

  // 文本渲染专用缓冲区
  WGPUBuffer textVertexBuffer;
  WGPUBuffer textIndexBuffer;
  WGPUBuffer textUniformBuffer;
} Clay_WebGPU_Context;

Clay_WebGPU_Context *Clay_WebGPU_Initialize(WGPUDevice device, WGPUQueue queue,
                                            WGPUTextureView targetView,
                                            uint32_t screenWidth,
                                            uint32_t screenHeight);
void Clay_WebGPU_Render(Clay_WebGPU_Context *context,
                        Clay_RenderCommandArray renderCommands);
void Clay_WebGPU_Cleanup(Clay_WebGPU_Context *context);

// UTF-8解析和文本渲染函数
uint32_t decode_utf8(const char **utf8, int *len);
bool LoadFontTexture(Clay_WebGPU_Context *context, const char *fontPath,
                     int fontSize);
void RenderText(Clay_WebGPU_Context *context, WGPURenderPassEncoder renderPass,
                Clay_TextRenderData *textData, Clay_BoundingBox bbox);

#endif