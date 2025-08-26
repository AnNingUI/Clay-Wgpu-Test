#ifndef CLAY_UNIFIED_RENDERER_H
#define CLAY_UNIFIED_RENDERER_H

#include "clay.h"
#include <webgpu/wgpu.h>
#include <stdbool.h>
#include <stdint.h>

// 统一渲染器配置
#define UNIFIED_MAX_VERTICES 65536
#define UNIFIED_MAX_INDICES 98304
#define UNIFIED_MAX_TEXTURES 64
#define UNIFIED_MAX_FONTS 16
#define UNIFIED_GLYPH_CACHE_SIZE 2048  // 从8192减少到2048
#define UNIFIED_ATLAS_SIZE 1024        // 从2048减少到1024

// 渲染对象类型
typedef enum {
    UNIFIED_RENDER_TYPE_RECTANGLE = 0,
    UNIFIED_RENDER_TYPE_TEXT = 1,
    UNIFIED_RENDER_TYPE_IMAGE = 2,
    UNIFIED_RENDER_TYPE_BORDER = 3
} UnifiedRenderType;

// 混合模式
typedef enum {
    UNIFIED_BLEND_NONE = 0,
    UNIFIED_BLEND_ALPHA = 1,
    UNIFIED_BLEND_PREMULTIPLIED = 2
} UnifiedBlendMode;

// 统一顶点数据结构 - 所有渲染对象都使用这个结构
typedef struct {
    float position[2];      // 屏幕位置 (NDC)
    float texCoords[2];     // 纹理坐标 (0-1)
    float color[4];         // RGBA颜色
    float params[4];        // 参数向量：[type, textureIndex, blendMode, reserved]
    float geometry[4];      // 几何参数：矩形=[cornerRadius.xyzw], 文本=[fontSize, 0, 0, 0]
    float bounds[4];        // 包围盒：[x, y, width, height]
    float zIndex;           // Z深度值
} UnifiedVertex;

// 纹理槽信息
typedef struct {
    WGPUTexture texture;
    WGPUTextureView textureView;
    uint32_t width, height;
    bool inUse;
    char debugName[64];
} UnifiedTextureSlot;

// 字体信息
typedef struct {
    int fontId;
    char fontPath[256];
    int fontSize;
    float scale;
    float lineHeight;
    
    // STB字体数据
    unsigned char *fontBuffer;
    size_t fontBufferSize;
    void *stbFont; // stbtt_fontinfo*
    
    // 字体度量
    int ascent, descent, lineGap;
    bool loaded;
} UnifiedFont;

// 字形缓存条目
typedef struct {
    uint32_t codepoint;
    int fontId;
    float uvX, uvY, uvWidth, uvHeight;  // 在atlas中的UV坐标
    float width, height;                // 字形尺寸
    float offsetX, offsetY;             // 字形偏移
    float advance;                      // 字符前进距离
    bool valid;
} UnifiedGlyph;

// 渲染批次 - 按纹理和渲染状态分组
typedef struct {
    int textureIndex;           // 纹理索引
    UnifiedBlendMode blendMode; // 混合模式
    int startVertex;            // 起始顶点索引
    int vertexCount;            // 顶点数量
    int startIndex;             // 起始索引位置
    int indexCount;             // 索引数量
    float minZ, maxZ;           // Z范围用于排序
} UnifiedRenderBatch;

// 主渲染上下文
typedef struct {
    // WebGPU资源
    WGPUDevice device;
    WGPUQueue queue;
    WGPURenderPipeline pipeline;
    WGPUBindGroupLayout textureBindGroupLayout;
    WGPUBindGroupLayout uniformBindGroupLayout;
    
    // 缓冲区
    WGPUBuffer vertexBuffer;
    WGPUBuffer indexBuffer;
    WGPUBuffer uniformBuffer;
    
    // 深度缓冲
    WGPUTexture depthTexture;
    WGPUTextureView depthTextureView;
    
    // 纹理管理
    UnifiedTextureSlot textures[UNIFIED_MAX_TEXTURES];
    int textureCount;
    WGPUSampler defaultSampler;
    WGPUBindGroup textureBindGroup;
    
    // 字体atlas
    WGPUTexture fontAtlasTexture;
    WGPUTextureView fontAtlasView;
    unsigned char *fontAtlasPixels;
    int fontAtlasX, fontAtlasY, fontAtlasRowHeight;
    bool fontAtlasDirty;
    int fontAtlasTextureIndex;
    
    // 字体和字形管理
    UnifiedFont fonts[UNIFIED_MAX_FONTS];
    int fontCount;
    int defaultFontId;
    UnifiedGlyph glyphCache[UNIFIED_GLYPH_CACHE_SIZE];
    
    // 渲染数据
    UnifiedVertex *vertices;
    uint16_t *indices;
    int vertexCount;
    int indexCount;
    int vertexCapacity;  // 动态容量
    int indexCapacity;   // 动态容量
    
    // 批次管理
    UnifiedRenderBatch *batches;
    int batchCount;
    int batchCapacity;
    
    // 屏幕信息
    uint32_t screenWidth, screenHeight;
    
    // uniform数据
    struct {
        float screenSize[2];
        float time;
        float _padding;
    } uniforms;
    
    // 统计信息
    int frameCount;
    int drawCalls;
    int verticesRendered;
    int glyphCacheHits;
    int glyphCacheMisses;
} UnifiedRenderer;

// ===== 核心API =====

// 初始化和清理
UnifiedRenderer* unified_renderer_create(WGPUDevice device, WGPUQueue queue,
                                        uint32_t screenWidth, uint32_t screenHeight);
void unified_renderer_destroy(UnifiedRenderer *renderer);
void unified_renderer_resize(UnifiedRenderer *renderer, 
                           uint32_t screenWidth, uint32_t screenHeight);

// 帧管理
void unified_renderer_begin_frame(UnifiedRenderer *renderer);
void unified_renderer_end_frame(UnifiedRenderer *renderer, WGPURenderPassEncoder renderPass);

// 纹理管理
int unified_renderer_load_texture(UnifiedRenderer *renderer, const char *imagePath);
int unified_renderer_create_texture_from_data(UnifiedRenderer *renderer, 
                                             const unsigned char *data,
                                             uint32_t width, uint32_t height,
                                             const char *debugName);
void unified_renderer_release_texture(UnifiedRenderer *renderer, int textureIndex);
bool unified_renderer_get_texture_dimensions(UnifiedRenderer *renderer, int textureIndex, int *width, int *height);

// 字体管理
int unified_renderer_load_font(UnifiedRenderer *renderer, const char *fontPath, int fontSize);
bool unified_renderer_set_default_font(UnifiedRenderer *renderer, int fontId);
float unified_renderer_measure_text(UnifiedRenderer *renderer, const char *text, 
                                   int fontId, int maxLength);

// ===== 渲染API - 统一接口 =====

// 渲染矩形
void unified_renderer_add_rectangle(UnifiedRenderer *renderer,
                                  float x, float y, float width, float height,
                                  Clay_Color color, Clay_CornerRadius cornerRadius,
                                  float zIndex);

// 渲染文本
void unified_renderer_add_text(UnifiedRenderer *renderer,
                             const char *text, int textLength,
                             float x, float y, Clay_Color color,
                             int fontId, float zIndex);

// 渲染图片
void unified_renderer_add_image(UnifiedRenderer *renderer,
                              int textureIndex,
                              float x, float y, float width, float height,
                              Clay_Color tintColor, Clay_CornerRadius cornerRadius,
                              float zIndex);

// 渲染边框
void unified_renderer_add_border(UnifiedRenderer *renderer,
                               float x, float y, float width, float height,
                               Clay_Color color, float borderWidth,
                               Clay_CornerRadius cornerRadius, float zIndex);

// ===== Clay集成API =====

// 处理Clay渲染命令 - 主要入口
void unified_renderer_process_clay_commands(UnifiedRenderer *renderer,
                                          Clay_RenderCommandArray renderCommands);

// 处理单个Clay渲染命令
void unified_renderer_process_clay_rectangle(UnifiedRenderer *renderer,
                                           Clay_RenderCommand *command);
void unified_renderer_process_clay_text(UnifiedRenderer *renderer,
                                       Clay_RenderCommand *command);
void unified_renderer_process_clay_image(UnifiedRenderer *renderer,
                                        Clay_RenderCommand *command);
void unified_renderer_process_clay_border(UnifiedRenderer *renderer,
                                         Clay_RenderCommand *command);

// ===== 内部函数 =====

// 字形管理
UnifiedGlyph* unified_renderer_get_glyph(UnifiedRenderer *renderer,
                                        uint32_t codepoint, int fontId);
bool unified_renderer_add_glyph_to_atlas(UnifiedRenderer *renderer,
                                        uint32_t codepoint, int fontId);
void unified_renderer_flush_font_atlas(UnifiedRenderer *renderer);

// 批次管理
void unified_renderer_optimize_batches(UnifiedRenderer *renderer);
void unified_renderer_sort_batches_by_z(UnifiedRenderer *renderer);

// UTF-8处理
uint32_t unified_renderer_decode_utf8(const char **str);

// 调试和统计
void unified_renderer_print_stats(UnifiedRenderer *renderer);
void unified_renderer_reset_stats(UnifiedRenderer *renderer);

// 工具函数
static inline Clay_Color unified_color_premultiply(Clay_Color color) {
    float a = color.a / 255.0f;
    return (Clay_Color){
        .r = (uint8_t)(color.r * a),
        .g = (uint8_t)(color.g * a), 
        .b = (uint8_t)(color.b * a),
        .a = color.a
    };
}

static inline float unified_color_to_float(uint8_t c) {
    return c / 255.0f;
}

#endif // CLAY_UNIFIED_RENDERER_H