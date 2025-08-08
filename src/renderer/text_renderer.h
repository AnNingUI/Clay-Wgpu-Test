// text_renderer.h - 独立的文本渲染系统
#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include "clay.h"
#include "stb_truetype.h"
#include <webgpu/wgpu.h>
#include <stdint.h>
#include <stdbool.h>

// 配置常量
#define TEXT_GLYPH_CACHE_SIZE 16384  // 进一步增加缓存大小以支持更多中文字符
#define TEXT_ATLAS_WIDTH 4096
#define TEXT_ATLAS_HEIGHT 4096
#define TEXT_MAX_CHARS_PER_BATCH 2048
#define TEXT_MAX_FONTS 16

// UTF-8相关结构
typedef struct {
    uint32_t codepoint;
    int byte_length;
    bool valid;
} UTF8Result;

// 字形信息
typedef struct {
    uint32_t codepoint;
    float width, height;        // 字形像素尺寸
    float advance;              // 字符前进距离
    float bearing_x, bearing_y; // 字符基准点偏移
    float u0, v0, u1, v1;      // 纹理坐标
    bool loaded;
} TextGlyph;

// 字体信息
typedef struct {
    int font_id;
    char font_path[256];
    int font_size;
    float scale;
    
    // STB TrueType相关
    unsigned char *font_buffer;
    size_t font_buffer_size;
    stbtt_fontinfo font_info;
    
    // 字体度量信息
    int ascent, descent, line_gap;
    float line_height;
    
    bool loaded;
} TextFont;

// 字体纹理图集
typedef struct {
    WGPUTexture texture;
    WGPUTextureView texture_view;
    WGPUSampler sampler;
    WGPUBindGroup bind_group;
    
    unsigned char *pixels;
    int current_x, current_y;
    int line_height;
    
    bool dirty;  // 标记纹理是否需要更新
} TextAtlas;

// 字形缓存条目
typedef struct {
    uint32_t codepoint;
    int font_id;
    TextGlyph glyph;
    bool occupied;
} TextGlyphCacheEntry;

// 文本渲染批次
typedef struct {
    float *vertex_data;     // 顶点数据缓冲区
    uint16_t *index_data;   // 索引数据缓冲区
    int vertex_count;       // 当前顶点数量
    int index_count;        // 当前索引数量
    int char_count;         // 当前字符数量
    
    Clay_Color color;       // 当前批次颜色
    int font_id;            // 当前批次字体ID
} TextRenderBatch;

// 主文本渲染器上下文
typedef struct {
    // WebGPU相关
    WGPUDevice device;
    WGPUQueue queue;
    WGPURenderPipeline text_pipeline;
    
    // 缓冲区
    WGPUBuffer vertex_buffer;
    WGPUBuffer index_buffer;
    WGPUBuffer uniform_buffer;
    
    // 屏幕信息
    uint32_t screen_width;
    uint32_t screen_height;
    
    // 字体管理
    TextFont fonts[TEXT_MAX_FONTS];
    int font_count;
    int default_font_id;
    
    // 纹理图集
    TextAtlas atlas;
    
    // 字形缓存
    TextGlyphCacheEntry glyph_cache[TEXT_GLYPH_CACHE_SIZE];
    
    // 渲染批次
    TextRenderBatch current_batch;
    
    // 统计信息
    int cache_hits;
    int cache_misses;
    int dynamic_generations;
} TextRenderer;

// API函数声明

// 初始化和清理
TextRenderer* text_renderer_create(WGPUDevice device, WGPUQueue queue, 
                                  uint32_t screen_width, uint32_t screen_height);
void text_renderer_destroy(TextRenderer *renderer);
void text_renderer_update_screen_size(TextRenderer *renderer, 
                                     uint32_t screen_width, uint32_t screen_height);

// 字体管理
int text_renderer_load_font(TextRenderer *renderer, const char *font_path, int font_size);
bool text_renderer_set_default_font(TextRenderer *renderer, int font_id);
TextFont* text_renderer_get_font(TextRenderer *renderer, int font_id);

// UTF-8处理
UTF8Result text_decode_utf8(const char **utf8_str);
int text_utf8_string_length(const char *utf8_str, int byte_length);

// 字形管理
TextGlyph* text_renderer_get_glyph(TextRenderer *renderer, uint32_t codepoint, int font_id);
bool text_renderer_generate_glyph(TextRenderer *renderer, uint32_t codepoint, int font_id);
void text_renderer_flush_atlas(TextRenderer *renderer);

// 文本测量
float text_renderer_measure_string_width(TextRenderer *renderer, const char *text, 
                                        int font_id, int max_chars);
float text_renderer_get_line_height(TextRenderer *renderer, int font_id);

// 文本渲染
void text_renderer_begin_frame(TextRenderer *renderer);
void text_renderer_render_string(TextRenderer *renderer, WGPURenderPassEncoder render_pass,
                                const char *text, int text_length,
                                float x, float y, Clay_Color color, int font_id);
void text_renderer_render_clay_text(TextRenderer *renderer, WGPURenderPassEncoder render_pass,
                                   Clay_TextRenderData *text_data, Clay_BoundingBox bbox);
void text_renderer_end_frame(TextRenderer *renderer);

// 批量渲染内部函数
void text_renderer_flush_batch(TextRenderer *renderer, WGPURenderPassEncoder render_pass);
void text_renderer_add_char_to_batch(TextRenderer *renderer, uint32_t codepoint, 
                                    float x, float y, int font_id);

// 调试和统计
void text_renderer_print_stats(TextRenderer *renderer);
void text_renderer_reset_stats(TextRenderer *renderer);

#endif // TEXT_RENDERER_H