#define STB_TRUETYPE_IMPLEMENTATION

#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// UTF-8解码函数
uint32_t decode_utf8(const char **utf8, int *len) {
  uint32_t codepoint = 0;
  const char *s = *utf8;

  if ((*s & 0x80) == 0) {
    // 1字节字符 (ASCII)
    *len = 1;
    codepoint = *s;
  } else if ((*s & 0xE0) == 0xC0) {
    // 2字节字符
    *len = 2;
    codepoint = ((*s & 0x1F) << 6) | (*(s + 1) & 0x3F);
  } else if ((*s & 0xF0) == 0xE0) {
    // 3字节字符
    *len = 3;
    codepoint =
        ((*s & 0x0F) << 12) | ((*(s + 1) & 0x3F) << 6) | (*(s + 2) & 0x3F);
  } else if ((*s & 0xF8) == 0xF0) {
    // 4字节字符
    *len = 4;
    codepoint = ((*s & 0x07) << 18) | ((*(s + 1) & 0x3F) << 12) |
                ((*(s + 2) & 0x3F) << 6) | (*(s + 3) & 0x3F);
  } else {
    // 无效UTF-8序列
    *len = 1;
    codepoint = *s;
  }

  *utf8 += *len;
  return codepoint;
}

// 简单的哈希函数，用于字形缓存
static uint32_t hash_codepoint(uint32_t codepoint) {
  codepoint = ((codepoint >> 16) ^ codepoint) * 0x45d9f3b;
  codepoint = ((codepoint >> 16) ^ codepoint) * 0x45d9f3b;
  codepoint = (codepoint >> 16) ^ codepoint;
  return codepoint % CLAY_GLYPH_CACHE_SIZE;
}

// 在缓存中查找字形信息（使用线性探测法解决哈希冲突）
static GlyphInfo *find_glyph_in_cache(Clay_WebGPU_Context *context,
                                      uint32_t codepoint) {
  uint32_t index = hash_codepoint(codepoint);
  uint32_t original_index = index;

  do {
    if (context->glyphCache[index].loaded &&
        context->glyphCache[index].codepoint == codepoint) {
      return &context->glyphCache[index].glyph;
    }
    if (!context->glyphCache[index].loaded) {
      // 找到空位置，说明字符不在缓存中
      return NULL;
    }
    // 线性探测：移动到下一个位置
    index = (index + 1) % CLAY_GLYPH_CACHE_SIZE;
  } while (index != original_index);

  return NULL;
}

// 在缓存中添加字形信息（使用线性探测法解决哈希冲突）
static void add_glyph_to_cache(Clay_WebGPU_Context *context, uint32_t codepoint,
                               GlyphInfo *glyph) {
  uint32_t index = hash_codepoint(codepoint);
  uint32_t original_index = index;

  do {
    if (!context->glyphCache[index].loaded) {
      // 找到空位置，添加字形信息
      context->glyphCache[index].codepoint = codepoint;
      context->glyphCache[index].glyph = *glyph;
      context->glyphCache[index].loaded = true;
      return;
    }
    if (context->glyphCache[index].codepoint == codepoint) {
      // 字符已存在，更新字形信息
      context->glyphCache[index].glyph = *glyph;
      return;
    }
    // 线性探测：移动到下一个位置
    index = (index + 1) % CLAY_GLYPH_CACHE_SIZE;
  } while (index != original_index);

  // 缓存已满，这种情况下可以考虑替换策略，这里简单覆盖原始位置
  context->glyphCache[original_index].codepoint = codepoint;
  context->glyphCache[original_index].glyph = *glyph;
  context->glyphCache[original_index].loaded = true;
}
static const char *vertexShaderWGSL =
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    @location(0) color: vec4<f32>\n"
    "};\n"
    "@vertex\n"
    "fn vs_main(@location(0) position: vec2<f32>, @location(1) color: "
    "vec4<f32>) -> VertexOutput {\n"
    "    var output: VertexOutput;\n"
    "    output.position = vec4<f32>(position, 0.0, 1.0);\n"
    "    output.color = color;\n"
    "    return output;\n"
    "}\n";

static const char *fragmentShaderWGSL =
    "@fragment\n"
    "fn fs_main(@location(0) color: vec4<f32>) -> @location(0) vec4<f32> {\n"
    "    return color;\n"
    "}\n";

// 文本渲染使用的顶点着色器
static const char *textVertexShaderWGSL =
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    @location(0) texCoords: vec2<f32>\n"
    "};\n"
    "\n"
    "struct VertexInput {\n"
    "    @location(0) position: vec2<f32>,\n"
    "    @location(1) texCoords: vec2<f32>\n"
    "};\n"
    "\n"
    "@vertex\n"
    "fn vs_main(input: VertexInput) -> VertexOutput {\n"
    "    var output: VertexOutput;\n"
    "    output.position = vec4<f32>(input.position, 0.0, 1.0);\n"
    "    output.texCoords = input.texCoords;\n"
    "    return output;\n"
    "}\n";

// 文本渲染使用的片段着色器
static const char *textFragmentShaderWGSL =
    "struct FragmentInput {\n"
    "    @location(0) texCoords: vec2<f32>\n"
    "};\n"
    "\n"
    "@group(0) @binding(0) var textureData: texture_2d<f32>;\n"
    "@group(0) @binding(1) var textureSampler: sampler;\n"
    "@group(0) @binding(2) var<uniform> textColor: vec4<f32>;\n"
    "\n"
    "@fragment\n"
    "fn fs_main(input: FragmentInput) -> @location(0) vec4<f32> {\n"
    "    let alpha = textureSample(textureData, textureSampler, "
    "input.texCoords).r;\n"
    "    return vec4<f32>(textColor.rgb, textColor.a * alpha);\n"
    "}\n";

Clay_WebGPU_Context *Clay_WebGPU_Initialize(WGPUDevice device, WGPUQueue queue,
                                            WGPUTextureView targetView,
                                            uint32_t screenWidth,
                                            uint32_t screenHeight) {
  Clay_WebGPU_Context *context = malloc(sizeof(Clay_WebGPU_Context));
  memset(context, 0, sizeof(Clay_WebGPU_Context));

  context->device = device;
  context->queue = queue;
  context->targetView = targetView; // 可能为NULL，会在渲染时更新
  context->screenWidth = screenWidth;
  context->screenHeight = screenHeight;

  // 创建顶点着色器模块
  WGPUShaderSourceWGSL vertexShaderSource = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = vertexShaderWGSL, .length = WGPU_STRLEN}};

  WGPUShaderModuleDescriptor vertexShaderDesc = {
      .nextInChain = (const WGPUChainedStruct *)&vertexShaderSource,
      .label = {.data = "Vertex Shader", .length = WGPU_STRLEN}};

  WGPUShaderModule vertexShader =
      wgpuDeviceCreateShaderModule(device, &vertexShaderDesc);

  // 创建片段着色器模块
  WGPUShaderSourceWGSL fragmentShaderSource = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = fragmentShaderWGSL, .length = WGPU_STRLEN}};

  WGPUShaderModuleDescriptor fragmentShaderDesc = {
      .nextInChain = (const WGPUChainedStruct *)&fragmentShaderSource,
      .label = {.data = "Fragment Shader", .length = WGPU_STRLEN}};

  WGPUShaderModule fragmentShader =
      wgpuDeviceCreateShaderModule(device, &fragmentShaderDesc);

  // 顶点属性配置
  WGPUVertexAttribute vertexAttributes[2] = {
      {.format = WGPUVertexFormat_Float32x2, .offset = 0, .shaderLocation = 0},
      {.format = WGPUVertexFormat_Float32x4, .offset = 8, .shaderLocation = 1}};

  WGPUVertexBufferLayout vertexBufferLayout = {
      .arrayStride = sizeof(float) * 6, // 2 for position, 4 for color
      .stepMode = WGPUVertexStepMode_Vertex,
      .attributeCount = 2,
      .attributes = vertexAttributes};

  // 创建渲染管线布局
  WGPUPipelineLayoutDescriptor layoutDesc = {.bindGroupLayoutCount = 0,
                                             .bindGroupLayouts = NULL};
  WGPUPipelineLayout pipelineLayout =
      wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

  // 创建片段状态
  WGPUBlendState blendState = {
      .color =
          (WGPUBlendComponent){.operation = WGPUBlendOperation_Add,
                               .srcFactor = WGPUBlendFactor_SrcAlpha,
                               .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha},
      .alpha =
          (WGPUBlendComponent){.operation = WGPUBlendOperation_Add,
                               .srcFactor = WGPUBlendFactor_One,
                               .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha}};

  WGPUColorTargetState colorTargetState = {.format =
                                               WGPUTextureFormat_BGRA8Unorm,
                                           .blend = &blendState,
                                           .writeMask = WGPUColorWriteMask_All};

  // 创建渲染管线
  WGPURenderPipelineDescriptor pipelineDesc = {
      .label = {.data = "Rectangle Pipeline", .length = WGPU_STRLEN},
      .layout = pipelineLayout,
      .vertex = {.module = vertexShader,
                 .entryPoint = {.data = "vs_main", .length = WGPU_STRLEN},
                 .bufferCount = 1,
                 .buffers = &vertexBufferLayout}};

  // 设置片段状态
  pipelineDesc.fragment = &(WGPUFragmentState){
      .module = fragmentShader,
      .entryPoint = {.data = "fs_main", .length = WGPU_STRLEN},
      .targetCount = 1,
      .targets = &colorTargetState};

  pipelineDesc.primitive =
      (WGPUPrimitiveState){.topology = WGPUPrimitiveTopology_TriangleList,
                           .stripIndexFormat = WGPUIndexFormat_Undefined,
                           .frontFace = WGPUFrontFace_CCW,
                           .cullMode = WGPUCullMode_None};

  pipelineDesc.multisample = (WGPUMultisampleState){
      .count = 1, .mask = ~0u, .alphaToCoverageEnabled = false};

  pipelineDesc.depthStencil = NULL;

  context->rectanglePipeline =
      wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

  // 创建文本渲染管线
  WGPUShaderSourceWGSL textVertexShaderSource = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = textVertexShaderWGSL, .length = WGPU_STRLEN}};

  WGPUShaderModule textVertexShader = wgpuDeviceCreateShaderModule(
      device,
      &(WGPUShaderModuleDescriptor){
          .nextInChain = (const WGPUChainedStruct *)&textVertexShaderSource,
          .label = {.data = "Text Vertex Shader", .length = WGPU_STRLEN}});

  WGPUShaderSourceWGSL textFragmentShaderSource = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = textFragmentShaderWGSL, .length = WGPU_STRLEN}};

  WGPUShaderModule textFragmentShader = wgpuDeviceCreateShaderModule(
      device,
      &(WGPUShaderModuleDescriptor){
          .nextInChain = (const WGPUChainedStruct *)&textFragmentShaderSource,
          .label = {.data = "Text Fragment Shader", .length = WGPU_STRLEN}});

  // 文本渲染顶点属性
  WGPUVertexAttribute textVertexAttributes[2] = {
      {.format = WGPUVertexFormat_Float32x2, .offset = 0, .shaderLocation = 0},
      {.format = WGPUVertexFormat_Float32x2, .offset = 8, .shaderLocation = 1}};

  WGPUVertexBufferLayout textVertexBufferLayout = {
      .arrayStride =
          sizeof(float) * 4, // 2 for position, 2 for texture coordinates
      .stepMode = WGPUVertexStepMode_Vertex,
      .attributeCount = 2,
      .attributes = textVertexAttributes};

  // 创建纹理采样器绑定布局
  WGPUBindGroupLayoutEntry bindGroupLayoutEntries[3] = {
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

  WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {
      .entryCount = 3, .entries = bindGroupLayoutEntries};
  WGPUBindGroupLayout bindGroupLayout =
      wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

  WGPUPipelineLayoutDescriptor textPipelineLayoutDesc = {
      .bindGroupLayoutCount = 1, .bindGroupLayouts = &bindGroupLayout};
  WGPUPipelineLayout textPipelineLayout =
      wgpuDeviceCreatePipelineLayout(device, &textPipelineLayoutDesc);

  // 创建文本片段状态
  WGPUBlendState textBlendState = {
      .color =
          (WGPUBlendComponent){.operation = WGPUBlendOperation_Add,
                               .srcFactor = WGPUBlendFactor_SrcAlpha,
                               .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha},
      .alpha =
          (WGPUBlendComponent){.operation = WGPUBlendOperation_Add,
                               .srcFactor = WGPUBlendFactor_One,
                               .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha}};

  WGPUColorTargetState textColorTargetState = {
      .format = WGPUTextureFormat_BGRA8Unorm,
      .blend = &textBlendState,
      .writeMask = WGPUColorWriteMask_All};

  WGPURenderPipelineDescriptor textPipelineDesc = {
      .label = {.data = "Text Pipeline", .length = WGPU_STRLEN},
      .layout = textPipelineLayout,
      .vertex = {.module = textVertexShader,
                 .entryPoint = {.data = "vs_main", .length = WGPU_STRLEN},
                 .bufferCount = 1,
                 .buffers = &textVertexBufferLayout}};

  // 设置文本片段状态
  textPipelineDesc.fragment = &(WGPUFragmentState){
      .module = textFragmentShader,
      .entryPoint = {.data = "fs_main", .length = WGPU_STRLEN},
      .targetCount = 1,
      .targets = &textColorTargetState};

  textPipelineDesc.primitive =
      (WGPUPrimitiveState){.topology = WGPUPrimitiveTopology_TriangleList,
                           .stripIndexFormat = WGPUIndexFormat_Undefined,
                           .frontFace = WGPUFrontFace_CCW,
                           .cullMode = WGPUCullMode_None};

  textPipelineDesc.multisample = (WGPUMultisampleState){
      .count = 1, .mask = ~0u, .alphaToCoverageEnabled = false};

  textPipelineDesc.depthStencil = NULL;

  context->textPipeline =
      wgpuDeviceCreateRenderPipeline(device, &textPipelineDesc);

  // 创建缓冲区
  context->vertexBuffer = wgpuDeviceCreateBuffer(
      device, &(WGPUBufferDescriptor){
                  .label = {.data = "Vertex Buffer", .length = WGPU_STRLEN},
                  .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                  .size = 6 * 6 * sizeof(float), // 6 vertices, 4 floats each
                  .mappedAtCreation = false});

  context->indexBuffer = wgpuDeviceCreateBuffer(
      device, &(WGPUBufferDescriptor){
                  .label = {.data = "Index Buffer", .length = WGPU_STRLEN},
                  .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                  .size = 6 * sizeof(uint32_t), // 6 indices
                  .mappedAtCreation = false});

  // 创建文本专用缓冲区 - 增加大小以支持批量渲染
  context->textVertexBuffer = wgpuDeviceCreateBuffer(
      device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Text Vertex Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
          .size = 1024 * 4 * 4 *
                  sizeof(float), // 1024个字符，每个4个顶点，每个顶点4个float
          .mappedAtCreation = false});

  context->textIndexBuffer = wgpuDeviceCreateBuffer(
      device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Text Index Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
          .size = 1024 * 6 * sizeof(uint16_t), // 1024个字符，每个6个索引
          .mappedAtCreation = false});

  context->textUniformBuffer = wgpuDeviceCreateBuffer(
      device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Text Uniform Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
          .size = sizeof(float) * 4, // Color vector
          .mappedAtCreation = false});

  // 释放着色器模块
  wgpuShaderModuleRelease(vertexShader);
  wgpuShaderModuleRelease(fragmentShader);
  wgpuShaderModuleRelease(textVertexShader);
  wgpuShaderModuleRelease(textFragmentShader);
  wgpuPipelineLayoutRelease(pipelineLayout);
  wgpuBindGroupLayoutRelease(bindGroupLayout);
  wgpuPipelineLayoutRelease(textPipelineLayout);

  return context;
}

// 加载字体纹理
bool LoadFontTexture(Clay_WebGPU_Context *context, const char *fontPath,
                     int fontSize) {
  // 读取字体文件
  FILE *fontFile = fopen(fontPath, "rb");
  if (!fontFile) {
    printf("无法打开字体文件: %s\n", fontPath);
    return false;
  }

  fseek(fontFile, 0, SEEK_END);
  long fontSizeBytes = ftell(fontFile);
  fseek(fontFile, 0, SEEK_SET);

  if (fontSizeBytes <= 0) {
    printf("字体文件大小无效: %s\n", fontPath);
    fclose(fontFile);
    return false;
  }

  unsigned char *fontBuffer = (unsigned char *)malloc(fontSizeBytes);
  if (!fontBuffer) {
    printf("无法分配字体缓冲区内存\n");
    fclose(fontFile);
    return false;
  }

  size_t readBytes = fread(fontBuffer, 1, fontSizeBytes, fontFile);
  fclose(fontFile);

  if (readBytes != fontSizeBytes) {
    printf("无法读取完整的字体文件: %s\n", fontPath);
    free(fontBuffer);
    return false;
  }

  // 初始化stb_truetype
  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, fontBuffer, 0)) {
    printf("无法初始化字体: %s\n", fontPath);
    free(fontBuffer);
    return false;
  }

  // 创建字体纹理
  context->fontTexture = (FontTexture *)malloc(sizeof(FontTexture));
  if (!context->fontTexture) {
    printf("无法分配字体纹理结构内存\n");
    free(fontBuffer);
    return false;
  }

  // 初始化绑定组为NULL
  context->fontTexture->bindGroup = NULL;

  context->fontTexture->width = CLAY_FONT_ATLAS_WIDTH;
  context->fontTexture->height = CLAY_FONT_ATLAS_HEIGHT;
  context->fontTexture->font_size = fontSize;
  context->fontTexture->pixels = (unsigned char *)calloc(
      1, CLAY_FONT_ATLAS_WIDTH * CLAY_FONT_ATLAS_HEIGHT);

  if (!context->fontTexture->pixels) {
    printf("无法分配字体纹理像素内存\n");
    free(fontBuffer);
    free(context->fontTexture);
    context->fontTexture = NULL;
    return false;
  }

  // 分配字符数据数组
  stbtt_bakedchar *chardata =
      (stbtt_bakedchar *)malloc(sizeof(stbtt_bakedchar) * 96);
  if (!chardata) {
    printf("无法分配字符数据内存\n");
    free(fontBuffer);
    free(context->fontTexture->pixels);
    free(context->fontTexture);
    context->fontTexture = NULL;
    return false;
  }

  // 检查字体缩放
  float scale = stbtt_ScaleForPixelHeight(&font, fontSize);
  printf("字体缩放因子: %.6f (字体大小: %d)\n", scale, fontSize);

  // 生成字体位图 - 支持更大的字符范围包括中文
  // 首先尝试ASCII字符 (32-127)
  int result = stbtt_BakeFontBitmap(
      fontBuffer, 0, fontSize, context->fontTexture->pixels,
      CLAY_FONT_ATLAS_WIDTH, CLAY_FONT_ATLAS_HEIGHT, 32, 96, chardata);

  printf("stbtt_BakeFontBitmap返回值: %d\n", result);

  if (result == 0) {
    printf("警告: 字体烘焙结果为0字符\n");
  } else if (result < 0) {
    printf("错误: 字体烘焙失败，错误码: %d\n", result);
    free(chardata);
    free(fontBuffer);
    free(context->fontTexture->pixels);
    free(context->fontTexture);
    context->fontTexture = NULL;
    return false;
  }

  // 将ASCII字符数据存储到字形缓存中
  printf("开始填充ASCII字形缓存...\n");
  for (int i = 0; i < 96; i++) {
    uint32_t codepoint = 32 + i; // ASCII字符从32开始
    stbtt_bakedchar *bc = &chardata[i];
    // 创建字形信息
    GlyphInfo glyph;
    glyph.width = bc->x1 - bc->x0;
    glyph.height = bc->y1 - bc->y0;
    glyph.advance = bc->xadvance;
    glyph.bearingX = bc->xoff;
    glyph.bearingY = bc->yoff;
    glyph.u0 = bc->x0 / (float)CLAY_FONT_ATLAS_WIDTH;
    glyph.v0 = bc->y0 / (float)CLAY_FONT_ATLAS_HEIGHT;
    glyph.u1 = bc->x1 / (float)CLAY_FONT_ATLAS_WIDTH;
    glyph.v1 = bc->y1 / (float)CLAY_FONT_ATLAS_HEIGHT;
    // 添加到字形缓存
    add_glyph_to_cache(context, codepoint, &glyph);
  }
  printf("ASCII字形缓存填充完成\n");

  // 保存字体信息用于动态字符生成
  context->fontTexture->fontBuffer = fontBuffer;
  context->fontTexture->fontBufferSize = fontSizeBytes;
  if (!stbtt_InitFont(&context->fontTexture->fontInfo, fontBuffer, 0)) {
    printf("无法初始化字体信息用于动态生成\n");
  }

  // 释放字符数据数组
  free(chardata);

  // 初始化动态字符生成的位置追踪
  context->fontTexture->currentAtlasX = 0;
  context->fontTexture->currentAtlasY = result; // result是ASCII字符占用的高度
  context->fontTexture->lineHeight = fontSize + 2; // 留一些间距

  printf("成功加载字体并缓存了96个ASCII字符，支持动态中文字符生成\n");

  // 创建WGPU纹理
  WGPUTextureDescriptor textureDesc = {
      .label = {.data = "Font Texture", .length = WGPU_STRLEN},
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
      .dimension = WGPUTextureDimension_2D,
      .size = {.width = CLAY_FONT_ATLAS_WIDTH,
               .height = CLAY_FONT_ATLAS_HEIGHT,
               .depthOrArrayLayers = 1},
      .format = WGPUTextureFormat_R8Unorm,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  context->fontTexture->texture =
      wgpuDeviceCreateTexture(context->device, &textureDesc);

  if (!context->fontTexture->texture) {
    printf("无法创建字体纹理\n");
    free(fontBuffer);
    free(context->fontTexture->pixels);
    free(context->fontTexture);
    context->fontTexture = NULL;
    return false;
  }

  // 上传纹理数据
  WGPUTexelCopyTextureInfo destination = {
      .texture = context->fontTexture->texture,
      .mipLevel = 0,
      .origin = {0, 0, 0},
      .aspect = WGPUTextureAspect_All,
  };

  WGPUTexelCopyBufferLayout dataLayout = {
      .offset = 0,
      .bytesPerRow = CLAY_FONT_ATLAS_WIDTH,
      .rowsPerImage = CLAY_FONT_ATLAS_HEIGHT,
  };

  WGPUExtent3D writeSize = {
      .width = CLAY_FONT_ATLAS_WIDTH,
      .height = CLAY_FONT_ATLAS_HEIGHT,
      .depthOrArrayLayers = 1,
  };

  wgpuQueueWriteTexture(
      context->queue, &destination, context->fontTexture->pixels,
      CLAY_FONT_ATLAS_WIDTH * CLAY_FONT_ATLAS_HEIGHT, &dataLayout, &writeSize);

  // 创建纹理视图
  WGPUTextureViewDescriptor textureViewDesc = {
      .label = {.data = "Font Texture View", .length = WGPU_STRLEN},
      .format = WGPUTextureFormat_R8Unorm,
      .dimension = WGPUTextureViewDimension_2D,
      .baseMipLevel = 0,
      .mipLevelCount = 1,
      .baseArrayLayer = 0,
      .arrayLayerCount = 1,
      .aspect = WGPUTextureAspect_All,
  };
  context->fontTexture->textureView =
      wgpuTextureCreateView(context->fontTexture->texture, &textureViewDesc);

  // 创建采样器
  WGPUSamplerDescriptor samplerDesc = {
      .label = {.data = "Font Sampler", .length = WGPU_STRLEN},
      .addressModeU = WGPUAddressMode_ClampToEdge,
      .addressModeV = WGPUAddressMode_ClampToEdge,
      .addressModeW = WGPUAddressMode_ClampToEdge,
      .magFilter = WGPUFilterMode_Linear,
      .minFilter = WGPUFilterMode_Linear,
      .mipmapFilter = WGPUMipmapFilterMode_Linear,
      .lodMinClamp = 0,
      .lodMaxClamp = 1,
      .compare = WGPUCompareFunction_Undefined,
      .maxAnisotropy = 1,
  };
  context->fontTexture->sampler =
      wgpuDeviceCreateSampler(context->device, &samplerDesc);

  if (!context->fontTexture->sampler) {
    printf("无法创建字体采样器\n");
    // 清理已创建的资源
    wgpuTextureViewRelease(context->fontTexture->textureView);
    wgpuTextureRelease(context->fontTexture->texture);
    free(fontBuffer);
    free(context->fontTexture->pixels);
    free(context->fontTexture);
    context->fontTexture = NULL;
    return false;
  }

  // 不要释放fontBuffer，因为我们需要保留它用于动态字符生成
  // free(fontBuffer); // 注释掉这行
  return true;
}

// 动态生成字符到字体纹理
static bool generate_glyph_to_atlas(Clay_WebGPU_Context *context,
                                    uint32_t codepoint) {
  if (!context->fontTexture || !context->fontTexture->fontBuffer) {
    return false;
  }

  stbtt_fontinfo *font = &context->fontTexture->fontInfo;
  float scale =
      stbtt_ScaleForPixelHeight(font, context->fontTexture->font_size);

  // 获取字符的边界框
  int x0, y0, x1, y1;
  stbtt_GetCodepointBitmapBox(font, codepoint, scale, scale, &x0, &y0, &x1,
                              &y1);

  int glyph_width = x1 - x0;
  int glyph_height = y1 - y0;

  if (glyph_width <= 0 || glyph_height <= 0) {
    return false;
  }

  // 检查是否有足够空间
  if (context->fontTexture->currentAtlasX + glyph_width >=
      CLAY_FONT_ATLAS_WIDTH) {
    // 换行
    context->fontTexture->currentAtlasX = 0;
    context->fontTexture->currentAtlasY += context->fontTexture->lineHeight;
  }

  if (context->fontTexture->currentAtlasY + glyph_height >=
      CLAY_FONT_ATLAS_HEIGHT) {
    printf("字体纹理空间不足，无法添加更多字符\n");
    return false;
  }

  // 生成字符位图
  int atlas_x = context->fontTexture->currentAtlasX;
  int atlas_y = context->fontTexture->currentAtlasY;

  unsigned char *bitmap =
      context->fontTexture->pixels + atlas_y * CLAY_FONT_ATLAS_WIDTH + atlas_x;

  stbtt_MakeCodepointBitmap(font, bitmap, glyph_width, glyph_height,
                            CLAY_FONT_ATLAS_WIDTH, scale, scale, codepoint);

  // 获取字符度量信息
  int advance, lsb;
  stbtt_GetCodepointHMetrics(font, codepoint, &advance, &lsb);

  // 创建字形信息
  GlyphInfo glyph;
  glyph.width = glyph_width;
  glyph.height = glyph_height;
  glyph.advance = advance * scale;
  glyph.bearingX = x0;
  glyph.bearingY =
      -y0; // y0是负值，bearingY应该是正值（从基线到字符顶部的距离）
  glyph.u0 = atlas_x / (float)CLAY_FONT_ATLAS_WIDTH;
  glyph.v0 = atlas_y / (float)CLAY_FONT_ATLAS_HEIGHT;
  glyph.u1 = (atlas_x + glyph_width) / (float)CLAY_FONT_ATLAS_WIDTH;
  glyph.v1 = (atlas_y + glyph_height) / (float)CLAY_FONT_ATLAS_HEIGHT;

  // 添加到缓存
  add_glyph_to_cache(context, codepoint, &glyph);

  // 更新纹理
  WGPUTexelCopyTextureInfo destination = {
      .texture = context->fontTexture->texture,
      .mipLevel = 0,
      .origin = {atlas_x, atlas_y, 0},
      .aspect = WGPUTextureAspect_All,
  };

  WGPUTexelCopyBufferLayout dataLayout = {
      .offset = 0,
      .bytesPerRow = glyph_width,
      .rowsPerImage = glyph_height,
  };

  WGPUExtent3D writeSize = {
      .width = glyph_width,
      .height = glyph_height,
      .depthOrArrayLayers = 1,
  };

  wgpuQueueWriteTexture(context->queue, &destination, bitmap,
                        glyph_width * glyph_height, &dataLayout, &writeSize);

  // 更新位置
  context->fontTexture->currentAtlasX += glyph_width + 1; // 留1像素间距

  printf("动态生成字符 U+%04X 到纹理位置 (%d, %d)\n", codepoint, atlas_x,
         atlas_y);
  return true;
}

// 渲染UTF-8文本字符串
void RenderText(Clay_WebGPU_Context *context, WGPURenderPassEncoder renderPass,
                Clay_TextRenderData *textData, Clay_BoundingBox bbox) {
  // 获取文本内容
  const char *text = textData->stringContents.chars;
  int length = textData->stringContents.length;

  if (length <= 0 || !text)
    return;

  // 检查字体纹理是否可用
  if (!context->fontTexture || !context->fontTexture->texture ||
      !context->fontTexture->textureView || !context->fontTexture->sampler) {
    printf("字体纹理未加载，跳过文本渲染: '%.*s'\n", length, text);
    return;
  }

  float fontSize = (float)textData->fontSize;
  Clay_Color color = textData->textColor;
  printf("Rendering text '%.*s' with color (%.0f,%.0f,%.0f,%.0f)\n", length,
         text, color.r, color.g, color.b, color.a);

  // 设置文本渲染管线
  wgpuRenderPassEncoderSetPipeline(renderPass, context->textPipeline);

  // 使用预创建的 uniform Buffer，避免每次都创建新的
  float colorData[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                        color.a / 255.0f};
  wgpuQueueWriteBuffer(context->queue, context->textUniformBuffer, 0, colorData,
                       sizeof(colorData));

  // 创建绑定组（如果尚未创建或需要更新）
  if (!context->fontTexture->bindGroup) {
    WGPUBindGroupEntry bindGroupEntries[] = {
        {.binding = 0, .textureView = context->fontTexture->textureView},
        {.binding = 1, .sampler = context->fontTexture->sampler},
        {.binding = 2,
         .buffer = context->textUniformBuffer,
         .offset = 0,
         .size = sizeof(float) * 4}};

    WGPUBindGroupDescriptor bindGroupDesc = {
        .label = {.data = "Text Bind Group", .length = WGPU_STRLEN},
        .layout =
            wgpuRenderPipelineGetBindGroupLayout(context->textPipeline, 0),
        .entryCount = 3,
        .entries = bindGroupEntries};

    context->fontTexture->bindGroup =
        wgpuDeviceCreateBindGroup(context->device, &bindGroupDesc);
  }

  wgpuRenderPassEncoderSetBindGroup(renderPass, 0,
                                    context->fontTexture->bindGroup, 0, NULL);

  float cursorX = bbox.x;
  float cursorY = bbox.y;

  // 批量渲染模式：收集所有字符的顶点和索引数据
  const char *ptr = text;
  const char *end = text + length;

  // 预分配足够大的缓冲区来存储所有字符的数据
  const int MAX_CHARS = 1024;
  float *all_vertices = malloc(
      MAX_CHARS * 4 * 4 * sizeof(float)); // 每个字符4个顶点，每个顶点4个float
  uint16_t *all_indices =
      malloc(MAX_CHARS * 6 * sizeof(uint16_t)); // 每个字符6个索引
  int vertex_count = 0;
  int index_count = 0;
  int rendered_chars = 0;

  while (ptr < end && rendered_chars < MAX_CHARS) {
    int charLen = 0;
    uint32_t codepoint = decode_utf8(&ptr, &charLen);

    if (codepoint == '\n') {
      cursorX = bbox.x;
      cursorY += fontSize * 1.2f; // 换行
      continue;
    }

    // 获取字形信息
    GlyphInfo *glyph = find_glyph_in_cache(context, codepoint);
    if (!glyph) {
      // 如果字符不在缓存中，尝试动态生成
      if (generate_glyph_to_atlas(context, codepoint)) {
        glyph = find_glyph_in_cache(context, codepoint);
      }

      if (!glyph) {
        // 如果动态生成失败，尝试使用空格字符作为默认
        glyph = find_glyph_in_cache(context, 32); // 空格字符
        if (!glyph) {
          // 如果连空格都没有，跳过这个字符
          continue;
        }
      }
    }

    // 只跳过空格字符，其他字符即使尺寸很小也尝试渲染
    if (codepoint == 32) { // 空格字符
      cursorX += glyph->advance;
      continue;
    }

    // 计算字符位置 - 修正Y坐标计算
    float x = cursorX + glyph->bearingX;
    float y = cursorY +
              glyph->bearingY; // bearingY现在是正值，表示从基线到字符顶部的距离

    // 转换为NDC坐标
    float x1 = (x / (float)context->screenWidth) * 2.0f - 1.0f;
    float y1 = 1.0f - (y / (float)context->screenHeight) * 2.0f;
    float x2 = ((x + glyph->width) / (float)context->screenWidth) * 2.0f - 1.0f;
    float y2 =
        1.0f - ((y + glyph->height) / (float)context->screenHeight) * 2.0f;

    printf("  字符 U+%04X: 屏幕坐标(%.1f,%.1f) -> NDC(%.4f,%.4f)-(%.4f,%.4f)\n",
           codepoint, x, y, x1, y1, x2, y2);

    // 使用真实的纹理坐标
    float u1 = glyph->u0;
    float v1 = glyph->v0;
    float u2 = glyph->u1;
    float v2 = glyph->v1;

    // 将顶点数据添加到批量缓冲区
    int base_vertex = vertex_count;
    all_vertices[vertex_count * 4 + 0] = x1;
    all_vertices[vertex_count * 4 + 1] = y1;
    all_vertices[vertex_count * 4 + 2] = u1;
    all_vertices[vertex_count * 4 + 3] = v1;
    vertex_count++; // 左上
    all_vertices[vertex_count * 4 + 0] = x2;
    all_vertices[vertex_count * 4 + 1] = y1;
    all_vertices[vertex_count * 4 + 2] = u2;
    all_vertices[vertex_count * 4 + 3] = v1;
    vertex_count++; // 右上
    all_vertices[vertex_count * 4 + 0] = x1;
    all_vertices[vertex_count * 4 + 1] = y2;
    all_vertices[vertex_count * 4 + 2] = u1;
    all_vertices[vertex_count * 4 + 3] = v2;
    vertex_count++; // 左下
    all_vertices[vertex_count * 4 + 0] = x2;
    all_vertices[vertex_count * 4 + 1] = y2;
    all_vertices[vertex_count * 4 + 2] = u2;
    all_vertices[vertex_count * 4 + 3] = v2;
    vertex_count++; // 右下

    // 将索引数据添加到批量缓冲区
    all_indices[index_count++] = base_vertex + 0;
    all_indices[index_count++] = base_vertex + 1;
    all_indices[index_count++] = base_vertex + 2;
    all_indices[index_count++] = base_vertex + 1;
    all_indices[index_count++] = base_vertex + 3;
    all_indices[index_count++] = base_vertex + 2;

    rendered_chars++;

    // 移动光标
    cursorX += glyph->advance;

    // 检查是否需要换行
    if (cursorX > bbox.x + bbox.width) {
      cursorX = bbox.x;
      cursorY += fontSize * 1.2f;
    }
  }

  // 批量渲染所有字符
  if (rendered_chars > 0) {

    // 一次性写入所有顶点和索引数据
    size_t vertex_data_size = vertex_count * 4 * sizeof(float);
    size_t index_data_size = index_count * sizeof(uint16_t);

    wgpuQueueWriteBuffer(context->queue, context->textVertexBuffer, 0,
                         all_vertices, vertex_data_size);
    wgpuQueueWriteBuffer(context->queue, context->textIndexBuffer, 0,
                         all_indices, index_data_size);

    wgpuRenderPassEncoderSetVertexBuffer(
        renderPass, 0, context->textVertexBuffer, 0, vertex_data_size);
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, context->textIndexBuffer,
                                        WGPUIndexFormat_Uint16, 0,
                                        index_data_size);
    wgpuRenderPassEncoderDrawIndexed(renderPass, index_count, 1, 0, 0, 0);
  }

  // 释放临时缓冲区
  free(all_vertices);
  free(all_indices);
}

void Clay_WebGPU_Render(Clay_WebGPU_Context *context,
                        Clay_RenderCommandArray renderCommands) {

  WGPUCommandEncoderDescriptor encoderDesc = {
      .label = {.data = "Clay Command Encoder", .length = WGPU_STRLEN}};
  WGPUCommandEncoder encoder =
      wgpuDeviceCreateCommandEncoder(context->device, &encoderDesc);

  WGPURenderPassColorAttachment colorAttachment = {
      .view = context->targetView,
      .resolveTarget = NULL,
      .loadOp = WGPULoadOp_Clear,
      .storeOp = WGPUStoreOp_Store,
      .clearValue = {.r = 0.0, .g = 0.0, .b = 0.0, .a = 1.0}};

  WGPURenderPassDescriptor renderPassDesc = {
      .label = {.data = "Clay Render Pass", .length = WGPU_STRLEN},
      .colorAttachmentCount = 1,
      .colorAttachments = &colorAttachment,
      .depthStencilAttachment = NULL};

  WGPURenderPassEncoder renderPass =
      wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

  // 处理Clay渲染命令
  for (int32_t i = 0; i < renderCommands.length; i++) {
    Clay_RenderCommand *cmd = &renderCommands.internalArray[i];
    Clay_BoundingBox bbox = cmd->boundingBox;

    switch (cmd->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_NONE:
      // 跳过空命令
      break;

    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
      Clay_RectangleRenderData *config = &cmd->renderData.rectangle;

      // 转换为NDC坐标
      float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
      float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
      float x2 =
          ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
      float y2 =
          1.0f - ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

      float r = config->backgroundColor.r / 255.0f;
      float g = config->backgroundColor.g / 255.0f;
      float b = config->backgroundColor.b / 255.0f;
      float a = config->backgroundColor.a / 255.0f;

      // 矩形顶点数据 (两个三角形组成)
      float vertices[] = {
          x1, y1, r, g, b, a, // 左上
          x2, y1, r, g, b, a, // 右上
          x1, y2, r, g, b, a, // 左下
          x2, y1, r, g, b, a, // 右上
          x2, y2, r, g, b, a, // 右下
          x1, y2, r, g, b, a, // 左下
      };

      // 为每个矩形创建临时缓冲区，避免数据覆盖
      WGPUBufferDescriptor tempBufferDesc = {
          .label = {.data = "Temp Rectangle Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
          .size = sizeof(vertices),
          .mappedAtCreation = false};
      WGPUBuffer tempBuffer =
          wgpuDeviceCreateBuffer(context->device, &tempBufferDesc);

      wgpuQueueWriteBuffer(context->queue, tempBuffer, 0, vertices,
                           sizeof(vertices));
      wgpuRenderPassEncoderSetPipeline(renderPass, context->rectanglePipeline);
      wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, tempBuffer, 0,
                                           sizeof(vertices));
      wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

      // 立即释放临时缓冲区
      wgpuBufferRelease(tempBuffer);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
      Clay_TextRenderData *config = &cmd->renderData.text;
      RenderText(context, renderPass, config, cmd->boundingBox);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
      Clay_ImageRenderData *config = &cmd->renderData.image;

      // 转换为NDC坐标
      float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
      float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
      float x2 =
          ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
      float y2 =
          1.0f - ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

      // TODO: 实现真正的图像渲染，目前使用背景色作为占位符
      float r = config->backgroundColor.r / 255.0f;
      float g = config->backgroundColor.g / 255.0f;
      float b = config->backgroundColor.b / 255.0f;
      float a = config->backgroundColor.a / 255.0f;

      float vertices[] = {
          x1, y1, r, g, b, a, x2, y1, r, g, b, a, x1, y2, r, g, b, a,
          x2, y1, r, g, b, a, x2, y2, r, g, b, a, x1, y2, r, g, b, a,
      };

      // 为图像渲染创建临时缓冲区
      WGPUBufferDescriptor tempBufferDesc = {
          .label = {.data = "Temp Image Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
          .size = sizeof(vertices),
          .mappedAtCreation = false};
      WGPUBuffer tempBuffer =
          wgpuDeviceCreateBuffer(context->device, &tempBufferDesc);

      wgpuQueueWriteBuffer(context->queue, tempBuffer, 0, vertices,
                           sizeof(vertices));
      wgpuRenderPassEncoderSetPipeline(renderPass, context->rectanglePipeline);
      wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, tempBuffer, 0,
                                           sizeof(vertices));
      wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

      wgpuBufferRelease(tempBuffer);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_BORDER: {
      Clay_BorderRenderData *config = &cmd->renderData.border;

      float borderColor[4] = {
          config->color.r / 255.0f, config->color.g / 255.0f,
          config->color.b / 255.0f, config->color.a / 255.0f};

      // 渲染边框的四条边
      Clay_BorderWidth width = config->width;

      // 左边框
      if (width.left > 0) {
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float x2 =
            ((bbox.x + width.left) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        float vertices[] = {
            x1,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
        };

        // 为左边框创建临时缓冲区
        WGPUBufferDescriptor tempBufferDesc = {
            .label = {.data = "Temp Border Buffer", .length = WGPU_STRLEN},
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = sizeof(vertices),
            .mappedAtCreation = false};
        WGPUBuffer tempBuffer =
            wgpuDeviceCreateBuffer(context->device, &tempBufferDesc);

        wgpuQueueWriteBuffer(context->queue, tempBuffer, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, tempBuffer, 0,
                                             sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

        wgpuBufferRelease(tempBuffer);
      }

      // 右边框
      if (width.right > 0) {
        float x1 = ((bbox.x + bbox.width - width.right) /
                    (float)context->screenWidth) *
                       2.0f -
                   1.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        float vertices[] = {
            x1,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
        };

        // 为右边框创建临时缓冲区
        WGPUBufferDescriptor tempBufferDesc2 = {
            .label = {.data = "Temp Border Buffer 2", .length = WGPU_STRLEN},
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = sizeof(vertices),
            .mappedAtCreation = false};
        WGPUBuffer tempBuffer2 =
            wgpuDeviceCreateBuffer(context->device, &tempBufferDesc2);

        wgpuQueueWriteBuffer(context->queue, tempBuffer2, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, tempBuffer2, 0,
                                             sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

        wgpuBufferRelease(tempBuffer2);
      }

      // 顶边框
      if (width.top > 0) {
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float y2 =
            1.0f - ((bbox.y + width.top) / (float)context->screenHeight) * 2.0f;

        float vertices[] = {
            x1,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
        };

        // 为顶边框创建临时缓冲区
        WGPUBufferDescriptor tempBufferDesc3 = {
            .label = {.data = "Temp Border Buffer 3", .length = WGPU_STRLEN},
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = sizeof(vertices),
            .mappedAtCreation = false};
        WGPUBuffer tempBuffer3 =
            wgpuDeviceCreateBuffer(context->device, &tempBufferDesc3);

        wgpuQueueWriteBuffer(context->queue, tempBuffer3, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, tempBuffer3, 0,
                                             sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

        wgpuBufferRelease(tempBuffer3);
      }

      // 底边框
      if (width.bottom > 0) {
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - ((bbox.y + bbox.height - width.bottom) /
                           (float)context->screenHeight) *
                              2.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        float vertices[] = {
            x1,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y1,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x2,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
            x1,
            y2,
            borderColor[0],
            borderColor[1],
            borderColor[2],
            borderColor[3],
        };

        // 为底边框创建临时缓冲区
        WGPUBufferDescriptor tempBufferDesc4 = {
            .label = {.data = "Temp Border Buffer 4", .length = WGPU_STRLEN},
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = sizeof(vertices),
            .mappedAtCreation = false};
        WGPUBuffer tempBuffer4 =
            wgpuDeviceCreateBuffer(context->device, &tempBufferDesc4);

        wgpuQueueWriteBuffer(context->queue, tempBuffer4, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, tempBuffer4, 0,
                                             sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

        wgpuBufferRelease(tempBuffer4);
      }
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
      // 设置裁剪矩形
      uint32_t scissorX = (uint32_t)bbox.x;
      uint32_t scissorY = (uint32_t)bbox.y;
      uint32_t scissorWidth = (uint32_t)bbox.width;
      uint32_t scissorHeight = (uint32_t)bbox.height;

      // 调试输出裁剪信息
      printf("SCISSOR_START: x=%u, y=%u, w=%u, h=%u, screen=%ux%u\n", scissorX,
             scissorY, scissorWidth, scissorHeight, context->screenWidth,
             context->screenHeight);

      // 确保裁剪区域在屏幕范围内
      if (scissorX < context->screenWidth && scissorY < context->screenHeight) {
        if (scissorX + scissorWidth > context->screenWidth) {
          scissorWidth = context->screenWidth - scissorX;
        }
        if (scissorY + scissorHeight > context->screenHeight) {
          scissorHeight = context->screenHeight - scissorY;
        }

        // 防止零尺寸裁剪
        if (scissorWidth > 0 && scissorHeight > 0) {
          wgpuRenderPassEncoderSetScissorRect(renderPass, scissorX, scissorY,
                                              scissorWidth, scissorHeight);
        } else {
          printf("WARNING: Zero size scissor rect detected!\n");
        }
      }
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
      // 重置裁剪区域到全屏
      wgpuRenderPassEncoderSetScissorRect(
          renderPass, 0, 0, context->screenWidth, context->screenHeight);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
      Clay_CustomRenderData *config = &cmd->renderData.custom;

      // 转换为NDC坐标
      float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
      float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
      float x2 =
          ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
      float y2 =
          1.0f - ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

      // 使用背景色渲染自定义元素（可以根据customData实现特殊效果）
      float r = config->backgroundColor.r / 255.0f;
      float g = config->backgroundColor.g / 255.0f;
      float b = config->backgroundColor.b / 255.0f;
      float a = config->backgroundColor.a / 255.0f;

      float vertices[] = {
          x1, y1, r, g, b, a, x2, y1, r, g, b, a, x1, y2, r, g, b, a,
          x2, y1, r, g, b, a, x2, y2, r, g, b, a, x1, y2, r, g, b, a,
      };

      // 为自定义元素创建临时缓冲区
      WGPUBufferDescriptor tempBufferDesc5 = {
          .label = {.data = "Temp Custom Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
          .size = sizeof(vertices),
          .mappedAtCreation = false};
      WGPUBuffer tempBuffer5 =
          wgpuDeviceCreateBuffer(context->device, &tempBufferDesc5);

      wgpuQueueWriteBuffer(context->queue, tempBuffer5, 0, vertices,
                           sizeof(vertices));
      wgpuRenderPassEncoderSetPipeline(renderPass, context->rectanglePipeline);
      wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, tempBuffer5, 0,
                                           sizeof(vertices));
      wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

      wgpuBufferRelease(tempBuffer5);
      break;
    }
    }
  }

  wgpuRenderPassEncoderEnd(renderPass);
  wgpuRenderPassEncoderRelease(renderPass);

  WGPUCommandBufferDescriptor cmdBufferDesc = {
      .label = {.data = "Clay Command Buffer", .length = WGPU_STRLEN}};
  WGPUCommandBuffer cmdBuffer =
      wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
  wgpuQueueSubmit(context->queue, 1, &cmdBuffer);

  wgpuCommandBufferRelease(cmdBuffer);
  wgpuCommandEncoderRelease(encoder);
}

void Clay_WebGPU_Cleanup(Clay_WebGPU_Context *context) {
  if (!context)
    return;

  // 等待设备完成所有操作
  if (context->device) {
    wgpuDevicePoll(context->device, true, NULL);
  }

  // 按正确顺序释放资源
  // 1. 首先释放缓冲区
  if (context->vertexBuffer) {
    wgpuBufferRelease(context->vertexBuffer);
    context->vertexBuffer = NULL;
  }
  if (context->indexBuffer) {
    wgpuBufferRelease(context->indexBuffer);
    context->indexBuffer = NULL;
  }
  if (context->uniformBuffer) {
    wgpuBufferRelease(context->uniformBuffer);
    context->uniformBuffer = NULL;
  }
  if (context->textVertexBuffer) {
    wgpuBufferRelease(context->textVertexBuffer);
    context->textVertexBuffer = NULL;
  }
  if (context->textIndexBuffer) {
    wgpuBufferRelease(context->textIndexBuffer);
    context->textIndexBuffer = NULL;
  }
  if (context->textUniformBuffer) {
    wgpuBufferRelease(context->textUniformBuffer);
    context->textUniformBuffer = NULL;
  }

  // 2. 释放字体纹理资源
  if (context->fontTexture) {
    // 释放绑定组
    if (context->fontTexture->bindGroup) {
      wgpuBindGroupRelease(context->fontTexture->bindGroup);
      context->fontTexture->bindGroup = NULL;
    }
    if (context->fontTexture->sampler) {
      wgpuSamplerRelease(context->fontTexture->sampler);
      context->fontTexture->sampler = NULL;
    }
    if (context->fontTexture->textureView) {
      wgpuTextureViewRelease(context->fontTexture->textureView);
      context->fontTexture->textureView = NULL;
    }
    if (context->fontTexture->texture) {
      wgpuTextureRelease(context->fontTexture->texture);
      context->fontTexture->texture = NULL;
    }
    if (context->fontTexture->pixels) {
      free(context->fontTexture->pixels);
      context->fontTexture->pixels = NULL;
    }
    if (context->fontTexture->fontBuffer) {
      free(context->fontTexture->fontBuffer);
      context->fontTexture->fontBuffer = NULL;
    }
    free(context->fontTexture);
    context->fontTexture = NULL;
  }

  // 3. 最后释放渲染管线
  if (context->rectanglePipeline) {
    wgpuRenderPipelineRelease(context->rectanglePipeline);
    context->rectanglePipeline = NULL;
  }
  if (context->textPipeline) {
    wgpuRenderPipelineRelease(context->textPipeline);
    context->textPipeline = NULL;
  }

  // 4. 释放上下文本身
  free(context);
}

// 更新屏幕尺寸
void Clay_WebGPU_UpdateScreenSize(Clay_WebGPU_Context *context,
                                  uint32_t screenWidth, uint32_t screenHeight) {
  if (!context) {
    return;
  }

  context->screenWidth = screenWidth;
  context->screenHeight = screenHeight;
}
