// text_renderer.c - 独立的文本渲染系统实现
#include "text_renderer.h"
#include "../DEV.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// WebGPU着色器代码
static const char *text_vertex_shader_wgsl =
    "struct VertexInput {\n"
    "    @location(0) position: vec2<f32>,\n"
    "    @location(1) texCoords: vec2<f32>,\n"
    "}\n"
    "\n"
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    @location(0) texCoords: vec2<f32>,\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_main(input: VertexInput) -> VertexOutput {\n"
    "    var output: VertexOutput;\n"
    "    output.position = vec4<f32>(input.position, 0.0, 1.0);\n"
    "    output.texCoords = input.texCoords;\n"
    "    return output;\n"
    "}\n";

static const char *text_fragment_shader_wgsl =
    "struct FragmentInput {\n"
    "    @location(0) texCoords: vec2<f32>,\n"
    "}\n"
    "\n"
    "@group(0) @binding(0) var textTexture: texture_2d<f32>;\n"
    "@group(0) @binding(1) var textSampler: sampler;\n"
    "@group(0) @binding(2) var<uniform> textColor: vec4<f32>;\n"
    "\n"
    "@fragment\n"
    "fn fs_main(input: FragmentInput) -> @location(0) vec4<f32> {\n"
    "    let alpha = textureSample(textTexture, textSampler, "
    "input.texCoords).r;\n"
    "    return vec4<f32>(textColor.rgb, textColor.a * alpha);\n"
    "}\n";

// UTF-8解码实现（改进版）
UTF8Result text_decode_utf8(const char **utf8_str) {
  UTF8Result result = {0, 0, false};
  const unsigned char *s = (const unsigned char *)*utf8_str;

  if (*s == 0) {
    return result; // 字符串结束
  }

  // 单字节字符（ASCII）
  if ((*s & 0x80) == 0) {
    result.codepoint = *s;
    result.byte_length = 1;
    result.valid = true;
  }
  // 双字节字符
  else if ((*s & 0xE0) == 0xC0) {
    if ((s[1] & 0xC0) == 0x80) {
      result.codepoint = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
      result.byte_length = 2;
      result.valid = (result.codepoint >= 0x80); // 检查最小值
    }
  }
  // 三字节字符
  else if ((*s & 0xF0) == 0xE0) {
    if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
      result.codepoint =
          ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
      result.byte_length = 3;
      result.valid = (result.codepoint >= 0x800); // 检查最小值
    }
  }
  // 四字节字符
  else if ((*s & 0xF8) == 0xF0) {
    if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
      result.codepoint = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                         ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
      result.byte_length = 4;
      result.valid =
          (result.codepoint >= 0x10000 && result.codepoint <= 0x10FFFF);
    }
  }

  // 如果解码失败，跳过一个字节并返回替代字符
  if (!result.valid) {
    result.codepoint = 0xFFFD; // Unicode替代字符
    result.byte_length = 1;
    result.valid = true;
  }

  *utf8_str += result.byte_length;
  return result;
}

int text_utf8_string_length(const char *utf8_str, int byte_length) {
  int char_count = 0;
  const char *ptr = utf8_str;
  const char *end = utf8_str + byte_length;

  while (ptr < end && *ptr != 0) {
    UTF8Result result = text_decode_utf8(&ptr);
    if (!result.valid)
      break;
    char_count++;
  }

  return char_count;
}

// 哈希函数，用于字形缓存
static uint32_t hash_glyph_key(uint32_t codepoint, int font_id) {
  uint32_t key = (codepoint << 8) | (font_id & 0xFF);
  key = ((key >> 16) ^ key) * 0x45d9f3b;
  key = ((key >> 16) ^ key) * 0x45d9f3b;
  key = (key >> 16) ^ key;
  return key % TEXT_GLYPH_CACHE_SIZE;
}

// 在缓存中查找字形
static TextGlyphCacheEntry *find_glyph_cache_entry(TextRenderer *renderer,
                                                   uint32_t codepoint,
                                                   int font_id) {
  uint32_t index = hash_glyph_key(codepoint, font_id);
  uint32_t original_index = index;

  do {
    TextGlyphCacheEntry *entry = &renderer->glyph_cache[index];

    if (!entry->occupied) {
      return NULL; // 空槽位，字形不在缓存中
    }

    if (entry->codepoint == codepoint && entry->font_id == font_id) {
      renderer->cache_hits++;
      return entry; // 找到匹配的字形
    }

    index = (index + 1) % TEXT_GLYPH_CACHE_SIZE;
  } while (index != original_index);

  return NULL; // 缓存已满且未找到
}

// 向缓存添加字形
static void add_glyph_to_cache(TextRenderer *renderer, uint32_t codepoint,
                               int font_id, const TextGlyph *glyph) {
  uint32_t index = hash_glyph_key(codepoint, font_id);
  uint32_t original_index = index;

  do {
    TextGlyphCacheEntry *entry = &renderer->glyph_cache[index];

    if (!entry->occupied) {
      // 找到空槽位
      entry->codepoint = codepoint;
      entry->font_id = font_id;
      entry->glyph = *glyph;
      entry->occupied = true;
      return;
    }

    if (entry->codepoint == codepoint && entry->font_id == font_id) {
      // 更新现有条目
      entry->glyph = *glyph;
      return;
    }

    index = (index + 1) % TEXT_GLYPH_CACHE_SIZE;
  } while (index != original_index);

  // 缓存已满，覆盖原始位置（LRU策略可以在这里实现）
  TextGlyphCacheEntry *entry = &renderer->glyph_cache[original_index];
  entry->codepoint = codepoint;
  entry->font_id = font_id;
  entry->glyph = *glyph;
  entry->occupied = true;
}

// 创建WebGPU渲染管线
static WGPUBindGroupLayout text_bind_group_layout = NULL;

static bool create_text_pipeline(TextRenderer *renderer) {
  // 创建着色器模块
  WGPUShaderSourceWGSL vertex_shader_source = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = text_vertex_shader_wgsl, .length = WGPU_STRLEN}};

  WGPUShaderModuleDescriptor vertex_shader_desc = {
      .nextInChain = (const WGPUChainedStruct *)&vertex_shader_source,
      .label = {.data = "Text Vertex Shader", .length = WGPU_STRLEN}};

  WGPUShaderModule vertex_shader =
      wgpuDeviceCreateShaderModule(renderer->device, &vertex_shader_desc);

  WGPUShaderSourceWGSL fragment_shader_source = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = text_fragment_shader_wgsl, .length = WGPU_STRLEN}};

  WGPUShaderModuleDescriptor fragment_shader_desc = {
      .nextInChain = (const WGPUChainedStruct *)&fragment_shader_source,
      .label = {.data = "Text Fragment Shader", .length = WGPU_STRLEN}};

  WGPUShaderModule fragment_shader =
      wgpuDeviceCreateShaderModule(renderer->device, &fragment_shader_desc);

  // 创建绑定组布局
  WGPUBindGroupLayoutEntry bind_group_entries[] = {
      {.binding = 0,
       .visibility = WGPUShaderStage_Fragment,
       .texture = {.sampleType = WGPUTextureSampleType_Float,
                   .viewDimension = WGPUTextureViewDimension_2D,
                   .multisampled = false}},
      {.binding = 1,
       .visibility = WGPUShaderStage_Fragment,
       .sampler = {.type = WGPUSamplerBindingType_Filtering}},
      {.binding = 2,
       .visibility = WGPUShaderStage_Fragment,
       .buffer = {.type = WGPUBufferBindingType_Uniform,
                  .hasDynamicOffset = false,
                  .minBindingSize = sizeof(float) * 4}}};

  WGPUBindGroupLayoutDescriptor bind_group_layout_desc = {
      .entryCount = 3, .entries = bind_group_entries};

  text_bind_group_layout = wgpuDeviceCreateBindGroupLayout(
      renderer->device, &bind_group_layout_desc);

  WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
      .bindGroupLayoutCount = 1, .bindGroupLayouts = &text_bind_group_layout};

  WGPUPipelineLayout pipeline_layout =
      wgpuDeviceCreatePipelineLayout(renderer->device, &pipeline_layout_desc);

  // 顶点属性
  WGPUVertexAttribute vertex_attributes[] = {
      {.format = WGPUVertexFormat_Float32x2, .offset = 0, .shaderLocation = 0},
      {.format = WGPUVertexFormat_Float32x2,
       .offset = sizeof(float) * 2,
       .shaderLocation = 1}};

  WGPUVertexBufferLayout vertex_buffer_layout = {
      .arrayStride = sizeof(float) * 4,
      .stepMode = WGPUVertexStepMode_Vertex,
      .attributeCount = 2,
      .attributes = vertex_attributes};

  // 混合状态
  WGPUBlendState blend_state = {
      .color = {.operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha},
      .alpha = {.operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha}};

  WGPUColorTargetState color_target_state = {
      .format = WGPUTextureFormat_BGRA8Unorm,
      .blend = &blend_state,
      .writeMask = WGPUColorWriteMask_All};

  // 创建渲染管线
  WGPURenderPipelineDescriptor pipeline_desc = {
      .label = {.data = "Text Render Pipeline", .length = WGPU_STRLEN},
      .layout = pipeline_layout,
      .vertex = {.module = vertex_shader,
                 .entryPoint = {.data = "vs_main", .length = WGPU_STRLEN},
                 .bufferCount = 1,
                 .buffers = &vertex_buffer_layout},
      .fragment = &(WGPUFragmentState){.module = fragment_shader,
                                       .entryPoint = {.data = "fs_main",
                                                      .length = WGPU_STRLEN},
                                       .targetCount = 1,
                                       .targets = &color_target_state},
      .primitive = {.topology = WGPUPrimitiveTopology_TriangleList,
                    .stripIndexFormat = WGPUIndexFormat_Undefined,
                    .frontFace = WGPUFrontFace_CCW,
                    .cullMode = WGPUCullMode_None},
      .multisample = {
          .count = 1, .mask = ~0u, .alphaToCoverageEnabled = false}};

  renderer->text_pipeline =
      wgpuDeviceCreateRenderPipeline(renderer->device, &pipeline_desc);

  // 清理资源
  wgpuShaderModuleRelease(vertex_shader);
  wgpuShaderModuleRelease(fragment_shader);
  wgpuPipelineLayoutRelease(pipeline_layout);

  return renderer->text_pipeline != NULL;
}

// 创建缓冲区
static bool create_buffers(TextRenderer *renderer) {
  // 顶点缓冲区
  renderer->vertex_buffer = wgpuDeviceCreateBuffer(
      renderer->device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Text Vertex Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
          .size = TEXT_MAX_CHARS_PER_BATCH * 4 * 4 * sizeof(float),
          .mappedAtCreation = false});

  // 索引缓冲区
  renderer->index_buffer = wgpuDeviceCreateBuffer(
      renderer->device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Text Index Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
          .size = TEXT_MAX_CHARS_PER_BATCH * 6 * sizeof(uint16_t),
          .mappedAtCreation = false});

  // Uniform缓冲区
  renderer->uniform_buffer = wgpuDeviceCreateBuffer(
      renderer->device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Text Uniform Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
          .size = sizeof(float) * 4,
          .mappedAtCreation = false});

  return renderer->vertex_buffer && renderer->index_buffer &&
         renderer->uniform_buffer;
}

// 创建纹理图集
static bool create_atlas(TextRenderer *renderer) {
  // 创建纹理
  WGPUTextureDescriptor texture_desc = {
      .label = {.data = "Text Atlas Texture", .length = WGPU_STRLEN},
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
      .dimension = WGPUTextureDimension_2D,
      .size = {TEXT_ATLAS_WIDTH, TEXT_ATLAS_HEIGHT, 1},
      .format = WGPUTextureFormat_R8Unorm,
      .mipLevelCount = 1,
      .sampleCount = 1};

  renderer->atlas.texture =
      wgpuDeviceCreateTexture(renderer->device, &texture_desc);
  if (!renderer->atlas.texture)
    return false;

  // 创建纹理视图
  renderer->atlas.texture_view =
      wgpuTextureCreateView(renderer->atlas.texture, NULL);
  if (!renderer->atlas.texture_view)
    return false;

  // 创建采样器
  WGPUSamplerDescriptor sampler_desc = {
      .label = {.data = "Text Atlas Sampler", .length = WGPU_STRLEN},
      .addressModeU = WGPUAddressMode_ClampToEdge,
      .addressModeV = WGPUAddressMode_ClampToEdge,
      .addressModeW = WGPUAddressMode_ClampToEdge,
      .magFilter = WGPUFilterMode_Linear,
      .minFilter = WGPUFilterMode_Linear,
      .mipmapFilter = WGPUMipmapFilterMode_Nearest,
      .lodMinClamp = 0.0f,
      .lodMaxClamp = 1.0f,
      .maxAnisotropy = 1};

  renderer->atlas.sampler =
      wgpuDeviceCreateSampler(renderer->device, &sampler_desc);
  if (!renderer->atlas.sampler)
    return false;

  // 分配像素缓冲区
  renderer->atlas.pixels = calloc(TEXT_ATLAS_WIDTH * TEXT_ATLAS_HEIGHT, 1);
  if (!renderer->atlas.pixels)
    return false;

  // 初始化图集状态
  renderer->atlas.current_x = 0;
  renderer->atlas.current_y = 0;
  renderer->atlas.line_height = 0;
  renderer->atlas.dirty = false;

  return true;
}

static bool create_bind_group(TextRenderer *renderer) {
  if (!text_bind_group_layout || !renderer->atlas.texture_view ||
      !renderer->atlas.sampler || !renderer->uniform_buffer) {
    Log("创建绑定组失败：缺少必要资源\n");
    return false;
  }

  WGPUBindGroupEntry bind_group_entries[] = {
      {.binding = 0, .textureView = renderer->atlas.texture_view},
      {.binding = 1, .sampler = renderer->atlas.sampler},
      {.binding = 2,
       .buffer = renderer->uniform_buffer,
       .offset = 0,
       .size = sizeof(float) * 4}};

  WGPUBindGroupDescriptor bind_group_desc = {
      .label = {.data = "Text Bind Group", .length = WGPU_STRLEN},
      .layout = text_bind_group_layout,
      .entryCount = 3,
      .entries = bind_group_entries};

  renderer->atlas.bind_group =
      wgpuDeviceCreateBindGroup(renderer->device, &bind_group_desc);

  if (!renderer->atlas.bind_group) {
    Log("创建文本绑定组失败\n");
    return false;
  }

  Log("文本绑定组创建成功\n");
  return true;
}

// API实现

TextRenderer *text_renderer_create(WGPUDevice device, WGPUQueue queue,
                                   uint32_t screen_width,
                                   uint32_t screen_height) {
  TextRenderer *renderer = calloc(1, sizeof(TextRenderer));
  if (!renderer)
    return NULL;

  renderer->device = device;
  renderer->queue = queue;
  renderer->screen_width = screen_width;
  renderer->screen_height = screen_height;
  renderer->default_font_id = -1;

  // 分配批次缓冲区
  renderer->current_batch.vertex_data =
      malloc(TEXT_MAX_CHARS_PER_BATCH * 4 * 4 * sizeof(float));
  renderer->current_batch.index_data =
      malloc(TEXT_MAX_CHARS_PER_BATCH * 6 * sizeof(uint16_t));

  if (!renderer->current_batch.vertex_data ||
      !renderer->current_batch.index_data) {
    text_renderer_destroy(renderer);
    return NULL;
  }

  // 创建WebGPU资源
  if (!create_text_pipeline(renderer) || !create_buffers(renderer) ||
      !create_atlas(renderer) || !create_bind_group(renderer)) {
    text_renderer_destroy(renderer);
    return NULL;
  }

  Log("文本渲染器创建成功\n");
  return renderer;
}

void text_renderer_destroy(TextRenderer *renderer) {
  if (!renderer)
    return;

  // 释放批次缓冲区
  free(renderer->current_batch.vertex_data);
  free(renderer->current_batch.index_data);

  // 释放字体资源
  for (int i = 0; i < renderer->font_count; i++) {
    free(renderer->fonts[i].font_buffer);
  }

  // 释放图集资源
  free(renderer->atlas.pixels);
  if (renderer->atlas.bind_group)
    wgpuBindGroupRelease(renderer->atlas.bind_group);
  if (renderer->atlas.sampler)
    wgpuSamplerRelease(renderer->atlas.sampler);
  if (renderer->atlas.texture_view)
    wgpuTextureViewRelease(renderer->atlas.texture_view);
  if (renderer->atlas.texture)
    wgpuTextureRelease(renderer->atlas.texture);

  // 释放缓冲区
  if (renderer->uniform_buffer)
    wgpuBufferRelease(renderer->uniform_buffer);
  if (renderer->index_buffer)
    wgpuBufferRelease(renderer->index_buffer);
  if (renderer->vertex_buffer)
    wgpuBufferRelease(renderer->vertex_buffer);

  // 释放管线
  if (renderer->text_pipeline)
    wgpuRenderPipelineRelease(renderer->text_pipeline);

  // 释放全局绑定组布局（只在最后一个实例销毁时释放）
  if (text_bind_group_layout) {
    wgpuBindGroupLayoutRelease(text_bind_group_layout);
    text_bind_group_layout = NULL;
  }

  free(renderer);
  Log("文本渲染器已清理\n");
}

void text_renderer_update_screen_size(TextRenderer *renderer,
                                      uint32_t screen_width,
                                      uint32_t screen_height) {
  if (!renderer)
    return;

  renderer->screen_width = screen_width;
  renderer->screen_height = screen_height;
}

int text_renderer_load_font(TextRenderer *renderer, const char *font_path,
                            int font_size) {
  if (!renderer || renderer->font_count >= TEXT_MAX_FONTS)
    return -1;

  // 读取字体文件
  FILE *file = fopen(font_path, "rb");
  if (!file) {
    Log("无法打开字体文件: %s\n", font_path);
    return -1;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0) {
    fclose(file);
    Log("字体文件大小无效: %s\n", font_path);
    return -1;
  }

  unsigned char *font_buffer = malloc(file_size);
  if (!font_buffer) {
    fclose(file);
    return -1;
  }

  size_t read_size = fread(font_buffer, 1, file_size, file);
  fclose(file);

  if (read_size != file_size) {
    free(font_buffer);
    Log("读取字体文件失败: %s\n", font_path);
    return -1;
  }

  // 初始化字体
  TextFont *font = &renderer->fonts[renderer->font_count];
  memset(font, 0, sizeof(TextFont));

  if (!stbtt_InitFont(&font->font_info, font_buffer, 0)) {
    free(font_buffer);
    Log("初始化字体失败: %s\n", font_path);
    return -1;
  }

  // 设置字体信息
  font->font_id = renderer->font_count;
  strncpy(font->font_path, font_path, sizeof(font->font_path) - 1);
  font->font_size = font_size;
  font->font_buffer = font_buffer;
  font->font_buffer_size = file_size;
  font->scale = stbtt_ScaleForPixelHeight(&font->font_info, font_size);

  // 获取字体度量信息
  stbtt_GetFontVMetrics(&font->font_info, &font->ascent, &font->descent,
                        &font->line_gap);
  font->line_height =
      (font->ascent - font->descent + font->line_gap) * font->scale;
  font->loaded = true;

  int font_id = renderer->font_count++;

  // 如果这是第一个字体，设为默认字体
  if (renderer->default_font_id == -1) {
    renderer->default_font_id = font_id;
  }

  Log("字体加载成功: %s (ID: %d, 大小: %d)\n", font_path, font_id,
         font_size);
  return font_id;
}

bool text_renderer_set_default_font(TextRenderer *renderer, int font_id) {
  if (!renderer || font_id < 0 || font_id >= renderer->font_count)
    return false;

  renderer->default_font_id = font_id;
  return true;
}

TextFont *text_renderer_get_font(TextRenderer *renderer, int font_id) {
  if (!renderer || font_id < 0 || font_id >= renderer->font_count)
    return NULL;
  return &renderer->fonts[font_id];
}

TextGlyph *text_renderer_get_glyph(TextRenderer *renderer, uint32_t codepoint,
                                   int font_id) {
  if (!renderer)
    return NULL;

  // 使用默认字体如果没有指定字体
  if (font_id < 0)
    font_id = renderer->default_font_id;
  if (font_id < 0 || font_id >= renderer->font_count)
    return NULL;

  // 在缓存中查找
  TextGlyphCacheEntry *entry =
      find_glyph_cache_entry(renderer, codepoint, font_id);
  if (entry) {
    return &entry->glyph;
  }

  // 尝试动态生成字形
  renderer->cache_misses++;
  if (text_renderer_generate_glyph(renderer, codepoint, font_id)) {
    entry = find_glyph_cache_entry(renderer, codepoint, font_id);
    if (entry)
      return &entry->glyph;
  }

  return NULL;
}

bool text_renderer_generate_glyph(TextRenderer *renderer, uint32_t codepoint,
                                  int font_id) {
  if (!renderer || font_id < 0 || font_id >= renderer->font_count)
    return false;

  TextFont *font = &renderer->fonts[font_id];
  if (!font->loaded)
    return false;

  // 获取字符边界框
  int x0, y0, x1, y1;
  stbtt_GetCodepointBitmapBox(&font->font_info, codepoint, font->scale,
                              font->scale, &x0, &y0, &x1, &y1);

  int width = x1 - x0;
  int height = y1 - y0;

  // 处理空白字符或无效字符
  if (width <= 0 || height <= 0) {
    TextGlyph glyph = {0};
    glyph.codepoint = codepoint;
    glyph.width = 0;
    glyph.height = 0;
    glyph.bearing_x = 0;
    glyph.bearing_y = 0;
    glyph.loaded = true;

    // 获取前进距离
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font->font_info, codepoint, &advance, &lsb);
    glyph.advance = advance * font->scale;

    add_glyph_to_cache(renderer, codepoint, font_id, &glyph);
    return true;
  }

  // 检查图集空间
  if (renderer->atlas.current_x + width + 2 >= TEXT_ATLAS_WIDTH) {
    renderer->atlas.current_x = 0;
    renderer->atlas.current_y += renderer->atlas.line_height + 1;
    renderer->atlas.line_height = 0;
  }

  if (renderer->atlas.current_y + height + 1 >= TEXT_ATLAS_HEIGHT) {
    Log("字体图集空间不足，无法生成字形 U+%04X\n", codepoint);
    return false;
  }

  // 生成字形位图
  int atlas_x = renderer->atlas.current_x;
  int atlas_y = renderer->atlas.current_y;

  // 清空目标区域
  for (int y = atlas_y; y < atlas_y + height; y++) {
    for (int x = atlas_x; x < atlas_x + width; x++) {
      renderer->atlas.pixels[y * TEXT_ATLAS_WIDTH + x] = 0;
    }
  }

  // 生成位图到图集中
  stbtt_MakeCodepointBitmap(
      &font->font_info,
      renderer->atlas.pixels + atlas_y * TEXT_ATLAS_WIDTH + atlas_x, width,
      height, TEXT_ATLAS_WIDTH, font->scale, font->scale, codepoint);

  // 创建字形信息 - 使用准确的基线信息
  TextGlyph glyph = {0};
  glyph.codepoint = codepoint;
  glyph.width = width;
  glyph.height = height;
  glyph.bearing_x = x0;
  // bearing_y是从基线到字形顶部的距离（正值向上）
  // stb_truetype返回的y1是从基线到字形底部的距离（负值）
  glyph.bearing_y = -y1;
  glyph.u0 = (float)atlas_x / TEXT_ATLAS_WIDTH;
  glyph.v0 = (float)atlas_y / TEXT_ATLAS_HEIGHT;
  glyph.u1 = (float)(atlas_x + width) / TEXT_ATLAS_WIDTH;
  glyph.v1 = (float)(atlas_y + height) / TEXT_ATLAS_HEIGHT;
  glyph.loaded = true;

  // 获取前进距离
  int advance, lsb;
  stbtt_GetCodepointHMetrics(&font->font_info, codepoint, &advance, &lsb);
  glyph.advance = advance * font->scale;

  // 添加到缓存
  add_glyph_to_cache(renderer, codepoint, font_id, &glyph);

  // 更新图集位置
  renderer->atlas.current_x += width + 1; // 留1像素间距
  if (height > renderer->atlas.line_height) {
    renderer->atlas.line_height = height;
  }

  // 标记图集需要更新
  renderer->atlas.dirty = true;
  renderer->dynamic_generations++;

  Log("动态生成字形 U+%04X 到图集位置 (%d, %d), 尺寸 %dx%d, bearing(%.0f, "
      "%.0f), advance %.2f\n",
      codepoint, atlas_x, atlas_y, width, height, glyph.bearing_x,
      glyph.bearing_y, glyph.advance);

  return true;
}

void text_renderer_flush_atlas(TextRenderer *renderer) {
  if (!renderer || !renderer->atlas.dirty)
    return;

  WGPUTexelCopyTextureInfo dest = {.texture = renderer->atlas.texture,
                                   .mipLevel = 0,
                                   .origin = {0, 0, 0},
                                   .aspect = WGPUTextureAspect_All};

  WGPUTexelCopyBufferLayout layout = {.offset = 0,
                                      .bytesPerRow = TEXT_ATLAS_WIDTH,
                                      .rowsPerImage = TEXT_ATLAS_HEIGHT};

  WGPUExtent3D writeSize = {.width = TEXT_ATLAS_WIDTH,
                            .height = TEXT_ATLAS_HEIGHT,
                            .depthOrArrayLayers = 1};

  wgpuQueueWriteTexture(renderer->queue, &dest, renderer->atlas.pixels,
                        TEXT_ATLAS_WIDTH * TEXT_ATLAS_HEIGHT, &layout,
                        &writeSize);

  Log("更新字体图集纹理 (当前位置: %d, %d)\n", renderer->atlas.current_x,
         renderer->atlas.current_y);

  renderer->atlas.dirty = false;
}

float text_renderer_measure_string_width(TextRenderer *renderer,
                                         const char *text, int font_id,
                                         int max_chars) {
  if (!renderer || !text)
    return 0.0f;

  if (font_id < 0)
    font_id = renderer->default_font_id;
  if (font_id < 0 || font_id >= renderer->font_count)
    return 0.0f;

  float width = 0.0f;
  const char *ptr = text;
  int char_count = 0;

  while (*ptr && (max_chars <= 0 || char_count < max_chars)) {
    UTF8Result result = text_decode_utf8(&ptr);
    if (!result.valid)
      break;

    TextGlyph *glyph =
        text_renderer_get_glyph(renderer, result.codepoint, font_id);
    if (glyph) {
      width += glyph->advance;
    }

    char_count++;
  }

  return width;
}

float text_renderer_get_line_height(TextRenderer *renderer, int font_id) {
  if (!renderer)
    return 0.0f;

  if (font_id < 0)
    font_id = renderer->default_font_id;
  if (font_id < 0 || font_id >= renderer->font_count)
    return 0.0f;

  return renderer->fonts[font_id].line_height;
}

void text_renderer_begin_frame(TextRenderer *renderer) {
  if (!renderer)
    return;

  // 重置批次
  renderer->current_batch.vertex_count = 0;
  renderer->current_batch.index_count = 0;
  renderer->current_batch.char_count = 0;
  renderer->current_batch.font_id = -1;

  // 如果图集需要更新，现在更新
  if (renderer->atlas.dirty) {
    text_renderer_flush_atlas(renderer);
  }
}

void text_renderer_flush_batch(TextRenderer *renderer,
                               WGPURenderPassEncoder render_pass) {
  if (!renderer || !render_pass || renderer->current_batch.char_count == 0)
    return;

  Log("刷新文本批次：%d 个字符，%d 个顶点，%d 个索引\n",
         renderer->current_batch.char_count,
         renderer->current_batch.vertex_count,
         renderer->current_batch.index_count);

  // 刷新图集纹理（如果有更新）
  text_renderer_flush_atlas(renderer);

  // 更新uniform缓冲区颜色
  float color_data[4] = {renderer->current_batch.color.r / 255.0f,
                         renderer->current_batch.color.g / 255.0f,
                         renderer->current_batch.color.b / 255.0f,
                         renderer->current_batch.color.a / 255.0f};
  wgpuQueueWriteBuffer(renderer->queue, renderer->uniform_buffer, 0, color_data,
                       sizeof(color_data));

  // 上传顶点和索引数据
  size_t vertex_data_size =
      renderer->current_batch.vertex_count * 4 * sizeof(float);
  size_t index_data_size =
      renderer->current_batch.index_count * sizeof(uint16_t);

  wgpuQueueWriteBuffer(renderer->queue, renderer->vertex_buffer, 0,
                       renderer->current_batch.vertex_data, vertex_data_size);
  wgpuQueueWriteBuffer(renderer->queue, renderer->index_buffer, 0,
                       renderer->current_batch.index_data, index_data_size);

  // 设置渲染状态
  wgpuRenderPassEncoderSetPipeline(render_pass, renderer->text_pipeline);
  wgpuRenderPassEncoderSetBindGroup(render_pass, 0, renderer->atlas.bind_group,
                                    0, NULL);

  // 设置缓冲区
  wgpuRenderPassEncoderSetVertexBuffer(render_pass, 0, renderer->vertex_buffer,
                                       0, vertex_data_size);
  wgpuRenderPassEncoderSetIndexBuffer(render_pass, renderer->index_buffer,
                                      WGPUIndexFormat_Uint16, 0,
                                      index_data_size);

  // 绘制
  wgpuRenderPassEncoderDrawIndexed(
      render_pass, renderer->current_batch.index_count, 1, 0, 0, 0);

  // 重置批次
  renderer->current_batch.vertex_count = 0;
  renderer->current_batch.index_count = 0;
  renderer->current_batch.char_count = 0;
}

void text_renderer_add_char_to_batch(TextRenderer *renderer, uint32_t codepoint,
                                     float x, float y, int font_id) {
  if (!renderer)
    return;

  // 检查是否需要刷新批次
  if (renderer->current_batch.char_count >= TEXT_MAX_CHARS_PER_BATCH) {
    Log("警告：批次已满，无法添加更多字符\n");
    return;
  }

  TextGlyph *glyph = text_renderer_get_glyph(renderer, codepoint, font_id);
  if (!glyph || !glyph->loaded)
    return;

  // 跳过空白字符的渲染
  if (glyph->width <= 0 || glyph->height <= 0)
    return;

  // 计算屏幕坐标 - 使用字形基线对齐
  float x1 = x + glyph->bearing_x;
  float y1 = y - glyph->bearing_y - glyph->height; // 从基线开始计算字形顶部
  float x2 = x1 + glyph->width;
  float y2 = y1 + glyph->height;

  // 调试：输出异常字符信息
  if (fabsf(glyph->bearing_y) > glyph->height * 0.5f) {
    Log("警告：字符 U+%04X 的bearing_y值异常: bearing_y=%.1f, height=%.1f\n",
           codepoint, glyph->bearing_y, glyph->height);
  }

  // 转换为NDC坐标
  float ndc_x1 = (x1 / (float)renderer->screen_width) * 2.0f - 1.0f;
  float ndc_y1 = 1.0f - (y1 / (float)renderer->screen_height) * 2.0f;
  float ndc_x2 = (x2 / (float)renderer->screen_width) * 2.0f - 1.0f;
  float ndc_y2 = 1.0f - (y2 / (float)renderer->screen_height) * 2.0f;

  // 确保坐标在合理范围内
  if (ndc_x1 < -2.0f || ndc_x1 > 2.0f || ndc_y1 < -2.0f || ndc_y1 > 2.0f) {
    Log("警告：字符坐标超出范围 U+%04X at (%.2f, %.2f) -> NDC(%.2f, %.2f)\n",
           codepoint, x1, y1, ndc_x1, ndc_y1);
    return;
  }

  // 添加顶点数据
  int base_vertex = renderer->current_batch.vertex_count;
  float *vertices = renderer->current_batch.vertex_data + base_vertex * 4;

  // 左上
  vertices[0] = ndc_x1;
  vertices[1] = ndc_y1;
  vertices[2] = glyph->u0;
  vertices[3] = glyph->v0;
  vertices += 4;
  // 右上
  vertices[0] = ndc_x2;
  vertices[1] = ndc_y1;
  vertices[2] = glyph->u1;
  vertices[3] = glyph->v0;
  vertices += 4;
  // 左下
  vertices[0] = ndc_x1;
  vertices[1] = ndc_y2;
  vertices[2] = glyph->u0;
  vertices[3] = glyph->v1;
  vertices += 4;
  // 右下
  vertices[0] = ndc_x2;
  vertices[1] = ndc_y2;
  vertices[2] = glyph->u1;
  vertices[3] = glyph->v1;

  // 添加索引数据
  int base_index = renderer->current_batch.index_count;
  uint16_t *indices = renderer->current_batch.index_data + base_index;

  uint16_t vertex_offset = renderer->current_batch.vertex_count;
  indices[0] = vertex_offset + 0; // 左上
  indices[1] = vertex_offset + 1; // 右上
  indices[2] = vertex_offset + 2; // 左下
  indices[3] = vertex_offset + 1; // 右上
  indices[4] = vertex_offset + 3; // 右下
  indices[5] = vertex_offset + 2; // 左下

  renderer->current_batch.vertex_count += 4;
  renderer->current_batch.index_count += 6;
  renderer->current_batch.char_count++;

  // 调试输出
  Log("添加字符 U+%04X 到批次: 屏幕(%.1f,%.1f-%.1f,%.1f) "
         "NDC(%.3f,%.3f-%.3f,%.3f) UV(%.3f,%.3f-%.3f,%.3f)\n",
         codepoint, x1, y1, x2, y2, ndc_x1, ndc_y1, ndc_x2, ndc_y2, glyph->u0,
         glyph->v0, glyph->u1, glyph->v1);
}

void text_renderer_render_string(TextRenderer *renderer,
                                 WGPURenderPassEncoder render_pass,
                                 const char *text, int text_length, float x,
                                 float y, Clay_Color color, int font_id) {
  if (!renderer || !text)
    return;

  if (font_id < 0)
    font_id = renderer->default_font_id;
  if (font_id < 0 || font_id >= renderer->font_count)
    return;

  // 只有在有render_pass时才检查是否需要刷新批次
  if (render_pass) {
    // 检查是否需要切换字体或颜色
    bool need_flush = (renderer->current_batch.font_id != font_id &&
                       renderer->current_batch.char_count > 0) ||
                      ((renderer->current_batch.color.r != color.r ||
                        renderer->current_batch.color.g != color.g ||
                        renderer->current_batch.color.b != color.b ||
                        renderer->current_batch.color.a != color.a) &&
                       renderer->current_batch.char_count > 0);

    if (need_flush) {
      text_renderer_flush_batch(renderer, render_pass);
    }
  }

  // 设置当前批次参数
  renderer->current_batch.font_id = font_id;
  renderer->current_batch.color = color;

  // 渲染字符串
  const char *ptr = text;
  const char *end = text + (text_length > 0 ? text_length : strlen(text));
  float cursor_x = x;
  float cursor_y = y;

  // 计算字体基线信息，确保换行时保持一致的基线间距
  TextFont *font = text_renderer_get_font(renderer, font_id);
  float baseline_spacing =
      (font && font->loaded) ? font->line_height
                             : text_renderer_get_line_height(renderer, font_id);

  while (ptr < end && *ptr) {
    UTF8Result result = text_decode_utf8(&ptr);
    if (!result.valid)
      break;

    if (result.codepoint == '\n') {
      cursor_x = x;
      cursor_y += baseline_spacing; // 使用一致的基线间距
      continue;
    }

    TextGlyph *glyph =
        text_renderer_get_glyph(renderer, result.codepoint, font_id);
    if (glyph) {
      text_renderer_add_char_to_batch(renderer, result.codepoint, cursor_x,
                                      cursor_y, font_id);
      cursor_x += glyph->advance;
    }
  }
}

void text_renderer_render_clay_text(TextRenderer *renderer,
                                    WGPURenderPassEncoder render_pass,
                                    Clay_TextRenderData *text_data,
                                    Clay_BoundingBox bbox) {
  if (!renderer || !text_data)
    return;

  // 使用准确的字体度量信息计算基线
  TextFont *font = text_renderer_get_font(renderer, renderer->default_font_id);
  if (!font || !font->loaded)
    return;

  // 计算垂直居中的基线位置
  // 使用字体基线作为参考，让字形自然对齐
  float ascent = font->ascent * font->scale;
  float descent = font->descent * font->scale;
  float font_height = ascent - descent;

  // 使用字体基线作为参考，字形会自然对齐到基线
  float baseline_y = bbox.y + bbox.height - (bbox.height - font_height) / 2.0f;

  //   Log("渲染Clay文本: \"%.*s\" 在位置 (%.1f, %.1f), 字体大小 %d, 基线
  //   %.1f\n",
  //          (int)text_data->stringContents.length,
  //          text_data->stringContents.chars, bbox.x, baseline_y,
  //          text_data->fontSize);

  // 累积文本到批次，不立即渲染 - 传递NULL作为render_pass
  text_renderer_render_string(renderer, NULL, text_data->stringContents.chars,
                              text_data->stringContents.length, bbox.x,
                              baseline_y, text_data->textColor,
                              renderer->default_font_id);
}

void text_renderer_end_frame(TextRenderer *renderer) {
  if (!renderer)
    return;

  // 这里可以添加帧结束时的清理工作
  // 当前实现中，批次在每次flush时就被清理了
}

void text_renderer_print_stats(TextRenderer *renderer) {
  if (!renderer)
    return;

  Log("=== 文本渲染器统计信息 ===\n");
  Log("已加载字体数量: %d\n", renderer->font_count);
  Log("默认字体ID: %d\n", renderer->default_font_id);
  Log("缓存命中: %d\n", renderer->cache_hits);
  Log("缓存未命中: %d\n", renderer->cache_misses);
  Log("动态生成字形数: %d\n", renderer->dynamic_generations);
  Log("图集当前位置: (%d, %d)\n", renderer->atlas.current_x,
         renderer->atlas.current_y);
  Log("当前批次字符数: %d\n", renderer->current_batch.char_count);

  // 计算缓存使用率
  int occupied_slots = 0;
  for (int i = 0; i < TEXT_GLYPH_CACHE_SIZE; i++) {
    if (renderer->glyph_cache[i].occupied) {
      occupied_slots++;
    }
  }
  Log("缓存使用率: %d/%d (%.1f%%)\n", occupied_slots, TEXT_GLYPH_CACHE_SIZE,
         (float)occupied_slots / TEXT_GLYPH_CACHE_SIZE * 100.0f);
  Log("=========================\n");
}

void text_renderer_reset_stats(TextRenderer *renderer) {
  if (!renderer)
    return;

  renderer->cache_hits = 0;
  renderer->cache_misses = 0;
  renderer->dynamic_generations = 0;
}
