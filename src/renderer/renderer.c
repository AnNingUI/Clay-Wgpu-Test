#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

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

// 在缓存中查找字形信息
static GlyphInfo *find_glyph_in_cache(Clay_WebGPU_Context *context,
                                      uint32_t codepoint) {
  uint32_t index = hash_codepoint(codepoint);
  if (context->glyphCache[index].loaded &&
      context->glyphCache[index].codepoint == codepoint) {
    return &context->glyphCache[index].glyph;
  }
  return NULL;
}

// 在缓存中添加字形信息
static void add_glyph_to_cache(Clay_WebGPU_Context *context, uint32_t codepoint,
                               GlyphInfo *glyph) {
  uint32_t index = hash_codepoint(codepoint);
  context->glyphCache[index].codepoint = codepoint;
  context->glyphCache[index].glyph = *glyph;
  context->glyphCache[index].loaded = true;
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
                  .size = 6 * 4 * sizeof(float), // 6 vertices, 4 floats each
                  .mappedAtCreation = false});

  context->indexBuffer = wgpuDeviceCreateBuffer(
      device, &(WGPUBufferDescriptor){
                  .label = {.data = "Index Buffer", .length = WGPU_STRLEN},
                  .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                  .size = 6 * sizeof(uint32_t), // 6 indices
                  .mappedAtCreation = false});

  // 创建文本专用缓冲区
  context->textVertexBuffer = wgpuDeviceCreateBuffer(
      device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Text Vertex Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
          .size = 6 * 4 * sizeof(float), // 6 vertices, 4 floats each
          .mappedAtCreation = false});

  context->textIndexBuffer = wgpuDeviceCreateBuffer(
      device, &(WGPUBufferDescriptor){
                  .label = {.data = "Text Index Buffer", .length = WGPU_STRLEN},
                  .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                  .size = 6 * sizeof(uint32_t), // 6 indices
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

  unsigned char *fontBuffer = (unsigned char *)malloc(fontSizeBytes);
  fread(fontBuffer, 1, fontSizeBytes, fontFile);
  fclose(fontFile);

  // 初始化stb_truetype
  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, fontBuffer, 0)) {
    printf("无法初始化字体\n");
    free(fontBuffer);
    return false;
  }

  // 创建字体纹理
  context->fontTexture = (FontTexture *)malloc(sizeof(FontTexture));
  context->fontTexture->width = CLAY_FONT_ATLAS_WIDTH;
  context->fontTexture->height = CLAY_FONT_ATLAS_HEIGHT;
  context->fontTexture->font_size = fontSize;
  context->fontTexture->pixels = (unsigned char *)calloc(
      1, CLAY_FONT_ATLAS_WIDTH * CLAY_FONT_ATLAS_HEIGHT);

  // 生成字体位图
  stbtt_BakeFontBitmap(fontBuffer, 0, fontSize, context->fontTexture->pixels,
                       CLAY_FONT_ATLAS_WIDTH, CLAY_FONT_ATLAS_HEIGHT, 32, 96,
                       NULL);

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

  free(fontBuffer);
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

  float fontSize = (float)textData->fontSize;
  Clay_Color color = textData->textColor;

  // 设置文本渲染管线
  wgpuRenderPassEncoderSetPipeline(renderPass, context->textPipeline);

  // 绑定字体纹理和采样器
  if (context->fontTexture) {
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

    WGPUBindGroup bindGroup =
        wgpuDeviceCreateBindGroup(context->device, &bindGroupDesc);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, NULL);
    wgpuBindGroupRelease(bindGroup);

    // 更新文本颜色uniform
    float colorData[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                          color.a / 255.0f};
    wgpuQueueWriteBuffer(context->queue, context->textUniformBuffer, 0,
                         colorData, sizeof(colorData));
  }

  float cursorX = bbox.x;
  float cursorY = bbox.y;

  // 解析并渲染UTF-8字符
  const char *ptr = text;
  const char *end = text + length;

  while (ptr < end) {
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
      // 对于简单实现，我们使用默认字形
      static GlyphInfo defaultGlyph = {0};
      defaultGlyph.width = fontSize * 0.6f;
      defaultGlyph.height = fontSize;
      defaultGlyph.advance = fontSize * 0.6f;
      defaultGlyph.bearingX = 0.0f;
      defaultGlyph.bearingY = 0.0f;
      glyph = &defaultGlyph;
    }

    // 计算字符位置
    float x = cursorX + glyph->bearingX;
    float y = cursorY + glyph->bearingY;

    // 转换为NDC坐标
    float x1 = (x / (float)context->screenWidth) * 2.0f - 1.0f;
    float y1 = 1.0f - (y / (float)context->screenHeight) * 2.0f;
    float x2 = ((x + glyph->width) / (float)context->screenWidth) * 2.0f - 1.0f;
    float y2 =
        1.0f - ((y + glyph->height) / (float)context->screenHeight) * 2.0f;

    // 纹理坐标（简化处理）
    float u1 = 0.0f;
    float v1 = 0.0f;
    float u2 = 1.0f;
    float v2 = 1.0f;

    // 顶点数据
    float vertices[] = {
        // 位置          // 纹理坐标
        x1, y1, u1, v1, // 左上
        x2, y1, u2, v1, // 右上
        x1, y2, u1, v2, // 左下
        x2, y2, u2, v2  // 右下
    };

    // 索引数据
    uint16_t indices[] = {0, 1, 2, 1, 3, 2};

    wgpuQueueWriteBuffer(context->queue, context->textVertexBuffer, 0, vertices,
                         sizeof(vertices));
    wgpuQueueWriteBuffer(context->queue, context->textIndexBuffer, 0, indices,
                         sizeof(indices));

    wgpuRenderPassEncoderSetVertexBuffer(
        renderPass, 0, context->textVertexBuffer, 0, sizeof(vertices));
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, context->textIndexBuffer,
                                        WGPUIndexFormat_Uint16, 0,
                                        sizeof(indices));
    wgpuRenderPassEncoderDrawIndexed(renderPass, 6, 1, 0, 0, 0);

    // 移动光标
    cursorX += glyph->advance;

    // 检查是否需要换行
    if (cursorX > bbox.x + bbox.width) {
      cursorX = bbox.x;
      cursorY += fontSize * 1.2f;
    }
  }
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

  // 处理Clay渲染命令 - 使用正确的结构
  for (int32_t i = 0; i < renderCommands.length; i++) {
    Clay_RenderCommand *cmd = &renderCommands.internalArray[i];
    Clay_BoundingBox bbox = cmd->boundingBox;

    switch (cmd->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_NONE:
      // 跳过空命令
      break;

    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
      // 使用renderData而不是config
      Clay_RectangleRenderData *config = &cmd->renderData.rectangle;

      // 转换为NDC坐标
      float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
      float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
      float x2 =
          ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
      float y2 =
          1.0f - ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

      // 使用backgroundColor而不是color
      float r = config->backgroundColor.r / 255.0f;
      float g = config->backgroundColor.g / 255.0f;
      float b = config->backgroundColor.b / 255.0f;
      float a = config->backgroundColor.a / 255.0f;

      // 矩形顶点数据
      float vertices[] = {
          x1, y1, r, g, b, a, x2, y1, r, g, b, a, x1, y2, r, g, b, a,
          x2, y1, r, g, b, a, x2, y2, r, g, b, a, x1, y2, r, g, b, a,
      };

      wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                           sizeof(vertices));
      wgpuRenderPassEncoderSetPipeline(renderPass, context->rectanglePipeline);
      wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, context->vertexBuffer,
                                           0, sizeof(vertices));
      wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_TEXT:
      // 文本渲染命令
      {
        Clay_TextRenderData *config = &cmd->renderData.text;
        RenderText(context, renderPass, config, bbox);
        break;
      }

    case CLAY_RENDER_COMMAND_TYPE_IMAGE:
      // 图像渲染命令 - 简单处理，实际项目中需要实现完整的图像渲染
      {
        Clay_ImageRenderData *config = &cmd->renderData.image;

        // 转换为NDC坐标
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        // 使用backgroundColor作为图像色调
        float r = config->backgroundColor.r / 255.0f;
        float g = config->backgroundColor.g / 255.0f;
        float b = config->backgroundColor.b / 255.0f;
        float a = config->backgroundColor.a / 255.0f;

        // 矩形顶点数据 (将图像区域渲染为彩色矩形)
        float vertices[] = {
            x1, y1, r, g, b, a, x2, y1, r, g, b, a, x1, y2, r, g, b, a,
            x2, y1, r, g, b, a, x2, y2, r, g, b, a, x1, y2, r, g, b, a,
        };

        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
        break;
      }

    case CLAY_RENDER_COMMAND_TYPE_BORDER:
      // 边框渲染命令 - 简化处理为一个矩形框
      {
        Clay_BorderRenderData *config = &cmd->renderData.border;

        // 转换为NDC坐标
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        // 使用color
        float r = config->color.r / 255.0f;
        float g = config->color.g / 255.0f;
        float b = config->color.b / 255.0f;
        float a = config->color.a / 255.0f;

        // 矩形顶点数据 (将边框区域渲染为彩色矩形)
        float vertices[] = {
            x1, y1, r, g, b, a, x2, y1, r, g, b, a, x1, y2, r, g, b, a,
            x2, y1, r, g, b, a, x2, y2, r, g, b, a, x1, y2, r, g, b, a,
        };

        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
        break;
      }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
      // 裁剪开始命令 - 简单处理，实际项目中需要实现完整的裁剪功能
      {
        // 在这个简化实现中，我们只是忽略裁剪功能
        // 实际项目中需要设置渲染器的裁剪区域
        break;
      }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
      // 裁剪结束命令 - 简单处理，实际项目中需要实现完整的裁剪功能
      {
        // 在这个简化实现中，我们只是忽略裁剪功能
        // 实际项目中需要重置渲染器的裁剪区域
        break;
      }

    case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
      // 自定义渲染命令 - 简单处理，实际项目中需要根据customData实现特定渲染
      {
        Clay_CustomRenderData *config = &cmd->renderData.custom;

        // 转换为NDC坐标
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        // 使用backgroundColor
        float r = config->backgroundColor.r / 255.0f;
        float g = config->backgroundColor.g / 255.0f;
        float b = config->backgroundColor.b / 255.0f;
        float a = config->backgroundColor.a / 255.0f;

        // 矩形顶点数据 (将自定义区域渲染为彩色矩形)
        float vertices[] = {
            x1, y1, r, g, b, a, x2, y1, r, g, b, a, x1, y2, r, g, b, a,
            x2, y1, r, g, b, a, x2, y2, r, g, b, a, x1, y2, r, g, b, a,
        };

        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
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
  if (context) {
    if (context->vertexBuffer)
      wgpuBufferRelease(context->vertexBuffer);
    if (context->indexBuffer)
      wgpuBufferRelease(context->indexBuffer);
    if (context->textVertexBuffer)
      wgpuBufferRelease(context->textVertexBuffer);
    if (context->textIndexBuffer)
      wgpuBufferRelease(context->textIndexBuffer);
    if (context->textUniformBuffer)
      wgpuBufferRelease(context->textUniformBuffer);
    if (context->rectanglePipeline)
      wgpuRenderPipelineRelease(context->rectanglePipeline);
    if (context->textPipeline)
      wgpuRenderPipelineRelease(context->textPipeline);

    if (context->fontTexture) {
      if (context->fontTexture->texture)
        wgpuTextureRelease(context->fontTexture->texture);
      if (context->fontTexture->textureView)
        wgpuTextureViewRelease(context->fontTexture->textureView);
      if (context->fontTexture->sampler)
        wgpuSamplerRelease(context->fontTexture->sampler);
      if (context->fontTexture->pixels)
        free(context->fontTexture->pixels);
      free(context->fontTexture);
    }

    free(context);
  }
}