#include "renderer.h"
#include "../DEV.h"
#include <stdlib.h>
#include <string.h>

// 矩形渲染着色器
static const char *vertexShaderWGSL =
    "struct VertexInput {\n"
    "    @location(0) position: vec2<f32>,\n"
    "    @location(1) color: vec4<f32>,\n"
    "    @location(2) local_pos: vec2<f32>,\n"
    "    @location(3) rect_size: vec2<f32>,\n"
    "    @location(4) corner_radius: vec4<f32>,\n"
    "    @location(5) z_index: f32,\n"
    "}\n"
    "\n"
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    @location(0) color: vec4<f32>,\n"
    "    @location(1) local_pos: vec2<f32>,\n"
    "    @location(2) rect_size: vec2<f32>,\n"
    "    @location(3) corner_radius: vec4<f32>,\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_main(input: VertexInput) -> VertexOutput {\n"
    "    var output: VertexOutput;\n"
    "    output.position = vec4<f32>(input.position, input.z_index, 1.0);\n"
    "    output.color = input.color;\n"
    "    output.local_pos = input.local_pos;\n"
    "    output.rect_size = input.rect_size;\n"
    "    output.corner_radius = input.corner_radius;\n"
    "    return output;\n"
    "}\n";

static const char *fragmentShaderWGSL =
    "struct FragmentInput {\n"
    "    @location(0) color: vec4<f32>,\n"
    "    @location(1) local_pos: vec2<f32>,\n"
    "    @location(2) rect_size: vec2<f32>,\n"
    "    @location(3) corner_radius: vec4<f32>,\n" // tl, tr, br, bl
    "}\n"
    "\n"
    // p: coordinates from center, b: half-size, r: corner radii (tl, tr, br,
    // bl)
    "fn sd_rounded_box(p: vec2<f32>, b: vec2<f32>, r: vec4<f32>) -> f32 {\n"
    "    var r_for_quadrant: f32;\n"
    "    if (p.x > 0.0) { \n"                             // right
    "        if (p.y > 0.0) { r_for_quadrant = r.y; } \n" // top-right
    "        else { r_for_quadrant = r.z; } \n"           // bottom-right
    "    } else { \n"                                     // left
    "        if (p.y > 0.0) { r_for_quadrant = r.x; } \n" // top-left
    "        else { r_for_quadrant = r.w; } \n"           // bottom-left
    "    }\n"
    "    let q = abs(p) - b + r_for_quadrant;\n"
    "    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0))) - "
    "r_for_quadrant;\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(input: FragmentInput) -> @location(0) vec4<f32> {\n"
    "    let p = (input.local_pos - 0.5) * input.rect_size;\n"
    "    let half_size = input.rect_size * 0.5;\n"
    "    let p_flipped_y = vec2<f32>(p.x, -p.y);\n"
    "    let d = sd_rounded_box(p_flipped_y, half_size, input.corner_radius);\n"
    "    let alpha = 1.0 - smoothstep(-1.0, 1.0, d);\n"
    "    if (alpha < 0.001) { discard; }\n"
    "    return vec4<f32>(input.color.rgb, input.color.a * alpha);\n"
    "}\n";

Clay_WebGPU_Context *Clay_WebGPU_Initialize(WGPUDevice device, WGPUQueue queue,
                                            WGPUTextureView targetView,
                                            uint32_t screenWidth,
                                            uint32_t screenHeight) {
  Clay_WebGPU_Context *context = malloc(sizeof(Clay_WebGPU_Context));
  memset(context, 0, sizeof(Clay_WebGPU_Context));

  context->device = device;
  context->queue = queue;
  context->targetView = targetView;
  context->screenWidth = screenWidth;
  context->screenHeight = screenHeight;
  context->defaultFontId = -1;

  // 创建深度纹理
  WGPUTextureDescriptor depthTextureDesc = {
      .usage = WGPUTextureUsage_RenderAttachment,
      .dimension = WGPUTextureDimension_2D,
      .size = {screenWidth, screenHeight, 1},
      .format = WGPUTextureFormat_Depth24Plus,
      .mipLevelCount = 1,
      .sampleCount = 1};
  context->depthTexture = wgpuDeviceCreateTexture(device, &depthTextureDesc);
  context->depthTextureView =
      wgpuTextureCreateView(context->depthTexture, NULL);

  // 创建独立的文本渲染器
  context->textRenderer =
      text_renderer_create(device, queue, screenWidth, screenHeight);
  if (!context->textRenderer) {
    Log("文本渲染器创建失败\n");
    free(context);
    return NULL;
  }

  // 创建矩形渲染的着色器模块
  WGPUShaderSourceWGSL vertexShaderSource = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = vertexShaderWGSL, .length = WGPU_STRLEN}};

  WGPUShaderModuleDescriptor vertexShaderDesc = {
      .nextInChain = (const WGPUChainedStruct *)&vertexShaderSource,
      .label = {.data = "Rectangle Vertex Shader", .length = WGPU_STRLEN}};

  WGPUShaderModule vertexShader =
      wgpuDeviceCreateShaderModule(device, &vertexShaderDesc);

  WGPUShaderSourceWGSL fragmentShaderSource = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = {.data = fragmentShaderWGSL, .length = WGPU_STRLEN}};

  WGPUShaderModuleDescriptor fragmentShaderDesc = {
      .nextInChain = (const WGPUChainedStruct *)&fragmentShaderSource,
      .label = {.data = "Rectangle Fragment Shader", .length = WGPU_STRLEN}};

  WGPUShaderModule fragmentShader =
      wgpuDeviceCreateShaderModule(device, &fragmentShaderDesc);

  // 创建顶点缓冲区
  WGPUBufferDescriptor vertexBufferDesc = {
      .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
      .size =
          1000 * 6 * 15 *
          sizeof(
              float), // 1000 rectangles, 6 vertices each, 15 floats per vertex
      .mappedAtCreation = false};
  context->vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);

  // 创建索引缓冲区
  WGPUBufferDescriptor indexBufferDesc = {
      .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
      .size = 1000 * 6 * sizeof(uint32_t), // 1000 rectangles, 6 indices each
      .mappedAtCreation = false};
  context->indexBuffer = wgpuDeviceCreateBuffer(device, &indexBufferDesc);

  // 创建统一缓冲区
  WGPUBufferDescriptor uniformBufferDesc = {
      .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
      .size = sizeof(float) * 16, // 4x4 matrix
      .mappedAtCreation = false};
  context->uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);

  // 顶点属性配置 (包含z_index)
  WGPUVertexAttribute vertexAttributes[6] = {
      {.format = WGPUVertexFormat_Float32x2,
       .offset = 0,
       .shaderLocation = 0}, // position
      {.format = WGPUVertexFormat_Float32x4,
       .offset = sizeof(float) * 2,
       .shaderLocation = 1}, // color
      {.format = WGPUVertexFormat_Float32x2,
       .offset = sizeof(float) * 6,
       .shaderLocation = 2}, // local_pos
      {.format = WGPUVertexFormat_Float32x2,
       .offset = sizeof(float) * 8,
       .shaderLocation = 3}, // rect_size
      {.format = WGPUVertexFormat_Float32x4,
       .offset = sizeof(float) * 10,
       .shaderLocation = 4}, // corner_radius
      {.format = WGPUVertexFormat_Float32,
       .offset = sizeof(float) * 14,
       .shaderLocation = 5} // z_index
  };

  WGPUVertexBufferLayout vertexBufferLayout = {
      .arrayStride = sizeof(float) * 15, // 2+4+2+2+4+1 = 15 floats
      .stepMode = WGPUVertexStepMode_Vertex,
      .attributeCount = 6,
      .attributes = vertexAttributes};

  // 深度模板状态
  WGPUDepthStencilState depthStencilState = {
      .format = WGPUTextureFormat_Depth24Plus,
      .depthWriteEnabled = true,
      .depthCompare = WGPUCompareFunction_LessEqual,
      .stencilFront = {0},
      .stencilBack = {0},
      .stencilReadMask = 0,
      .stencilWriteMask = 0,
      .depthBias = 0,
      .depthBiasSlopeScale = 0.0,
      .depthBiasClamp = 0.0};

  // 创建渲染管线
  WGPURenderPipelineDescriptor pipelineDesc = {
      .label = {.data = "Rectangle Pipeline", .length = WGPU_STRLEN},
      .vertex = {.module = vertexShader,
                 .entryPoint = {.data = "vs_main", .length = WGPU_STRLEN},
                 .bufferCount = 1,
                 .buffers = &vertexBufferLayout},
      .primitive = {.topology = WGPUPrimitiveTopology_TriangleList,
                    .stripIndexFormat = WGPUIndexFormat_Undefined,
                    .frontFace = WGPUFrontFace_CCW,
                    .cullMode = WGPUCullMode_None},
      .depthStencil = &depthStencilState,
      .multisample = {.count = 1, .mask = ~0u, .alphaToCoverageEnabled = false},
      .fragment = &(WGPUFragmentState){
          .module = fragmentShader,
          .entryPoint = {.data = "fs_main", .length = WGPU_STRLEN},
          .targetCount = 1,
          .targets = &(WGPUColorTargetState){
              .format = WGPUTextureFormat_BGRA8Unorm,
              .blend =
                  &(WGPUBlendState){
                      .color = {WGPUBlendOperation_Add,
                                WGPUBlendFactor_SrcAlpha,
                                WGPUBlendFactor_OneMinusSrcAlpha},
                      .alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One,
                                WGPUBlendFactor_OneMinusSrcAlpha}},
              .writeMask = WGPUColorWriteMask_All}}};

  context->rectanglePipeline =
      wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

  // 清理着色器模块
  wgpuShaderModuleRelease(vertexShader);
  wgpuShaderModuleRelease(fragmentShader);
  // wgpuTextureViewRelease(context->depthTextureView);

  Log("Clay WebGPU渲染器初始化完成\n");
  return context;
}

bool Clay_WebGPU_LoadFont(Clay_WebGPU_Context *context, const char *fontPath,
                          int fontSize) {
  if (!context || !context->textRenderer)
    return false;

  int fontId =
      text_renderer_load_font(context->textRenderer, fontPath, fontSize);
  if (fontId < 0) {
    Log("字体加载失败: %s\n", fontPath);
    return false;
  }

  // 如果这是第一个字体，设为默认字体
  if (context->defaultFontId < 0) {
    context->defaultFontId = fontId;
    text_renderer_set_default_font(context->textRenderer, fontId);
  }

  Log("字体加载成功: %s (ID: %d)\n", fontPath, fontId);
  return true;
}

bool Clay_WebGPU_SetDefaultFont(Clay_WebGPU_Context *context, int fontId) {
  if (!context || !context->textRenderer)
    return false;

  if (text_renderer_set_default_font(context->textRenderer, fontId)) {
    context->defaultFontId = fontId;
    return true;
  }

  return false;
}

void Clay_WebGPU_UpdateScreenSize(Clay_WebGPU_Context *context,
                                  uint32_t screenWidth, uint32_t screenHeight) {
  if (!context)
    return;

  context->screenWidth = screenWidth;
  context->screenHeight = screenHeight;

  // 重新创建深度纹理
  if (context->depthTexture) {
    wgpuTextureViewRelease(context->depthTextureView);
    wgpuTextureRelease(context->depthTexture);
  }

  WGPUTextureDescriptor depthTextureDesc = {
      .usage = WGPUTextureUsage_RenderAttachment,
      .dimension = WGPUTextureDimension_2D,
      .size = {screenWidth, screenHeight, 1},
      .format = WGPUTextureFormat_Depth24Plus,
      .mipLevelCount = 1,
      .sampleCount = 1};
  context->depthTexture =
      wgpuDeviceCreateTexture(context->device, &depthTextureDesc);
  context->depthTextureView =
      wgpuTextureCreateView(context->depthTexture, NULL);

  // 更新文本渲染器的屏幕尺寸
  if (context->textRenderer) {
    text_renderer_update_screen_size(context->textRenderer, screenWidth,
                                     screenHeight);
  }

  Log("WebGPU屏幕尺寸已更新为: %dx%d\n", screenWidth, screenHeight);
}

void Clay_WebGPU_RenderText(Clay_WebGPU_Context *context,
                            WGPURenderPassEncoder renderPass,
                            Clay_TextRenderData *textData,
                            Clay_BoundingBox bbox, float z_index) {
  if (!context || !context->textRenderer || !renderPass || !textData)
    return;

  text_renderer_render_clay_text(context->textRenderer, renderPass, textData,
                                 bbox, z_index);
}

void Clay_WebGPU_PrintTextStats(Clay_WebGPU_Context *context) {
  if (!context || !context->textRenderer)
    return;

  text_renderer_print_stats(context->textRenderer);
}

// 简单的矩形批处理结构
typedef struct {
  float vertices[1000 * 6 *
                 15]; // 最多1000个矩形，每个6顶点，15浮点数（包含z_index）
  int vertex_count;
} RectangleBatch;

void Clay_WebGPU_Render(Clay_WebGPU_Context *context,
                        Clay_RenderCommandArray renderCommands) {
  if (!context)
    return;

  static int frame_count = 0;
  frame_count++;

  Log("=== 开始渲染帧 %d，总共 %d 个渲染命令 ===\n", frame_count,
      renderCommands.length);

  WGPUCommandEncoderDescriptor encoderDesc = {
      .label = {.data = "Clay Command Encoder", .length = WGPU_STRLEN}};
  WGPUCommandEncoder encoder =
      wgpuDeviceCreateCommandEncoder(context->device, &encoderDesc);

  WGPURenderPassColorAttachment colorAttachment = {
      .view = context->targetView,
      .resolveTarget = NULL,
      .clearValue = {0.1f, 0.1f, 0.1f, 1.0f}, // 深灰色背景
      .loadOp = WGPULoadOp_Clear,
      .storeOp = WGPUStoreOp_Store};

  WGPUTextureDescriptor depthTextureDesc = {
      .usage = WGPUTextureUsage_RenderAttachment,
      .dimension = WGPUTextureDimension_2D,
      .size = {context->screenWidth, context->screenHeight, 1},
      .format = WGPUTextureFormat_Depth24Plus,
      .mipLevelCount = 1,
      .sampleCount = 1};

  // 如果屏幕尺寸改变，重新创建深度纹理
  if (context->screenWidth != depthTextureDesc.size.width ||
      context->screenHeight != depthTextureDesc.size.height) {
    if (context->depthTexture) {
      wgpuTextureViewRelease(context->depthTextureView);
      wgpuTextureRelease(context->depthTexture);
    }
    context->depthTexture =
        wgpuDeviceCreateTexture(context->device, &depthTextureDesc);
    context->depthTextureView =
        wgpuTextureCreateView(context->depthTexture, NULL);
  }

  WGPURenderPassDepthStencilAttachment depthStencilAttachment = {
      .view = context->depthTextureView,
      .depthClearValue = 1.0f,
      .depthLoadOp = WGPULoadOp_Clear,
      .depthStoreOp = WGPUStoreOp_Store,
      .depthReadOnly = false,
      .stencilLoadOp = WGPULoadOp_Undefined,
      .stencilStoreOp = WGPUStoreOp_Undefined,
      .stencilClearValue = 0,
      .stencilReadOnly = true};
  WGPURenderPassDescriptor renderPassDesc = {
      .label = {.data = "Clay Render Pass", .length = WGPU_STRLEN},
      .colorAttachmentCount = 1,
      .colorAttachments = &colorAttachment,
      .depthStencilAttachment = &depthStencilAttachment};

  WGPURenderPassEncoder renderPass =
      wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

  // 初始化矩形批处理
  RectangleBatch rectangleBatch = {0};
  int rectangle_count = 0;

  // 开始文本渲染帧
  text_renderer_begin_frame(context->textRenderer);

  // 遍历所有渲染命令
  for (uint32_t i = 0; i < renderCommands.length; i++) {
    Clay_RenderCommand *renderCommand =
        Clay_RenderCommandArray_Get(&renderCommands, i);

    Log("处理渲染命令 %d，类型: %d\n", i, renderCommand->commandType);

    switch (renderCommand->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
      rectangle_count++;
      Clay_RectangleRenderData *rectangleData =
          &renderCommand->renderData.rectangle;
      Clay_BoundingBox bbox = renderCommand->boundingBox;

      Log("矩形 #%d: 原始颜色RGBA(%.0f,%.0f,%.0f,%.0f)\n", rectangle_count,
          (float)rectangleData->backgroundColor.r,
          (float)rectangleData->backgroundColor.g,
          (float)rectangleData->backgroundColor.b,
          (float)rectangleData->backgroundColor.a);

      // 转换为NDC坐标
      float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
      float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
      float x2 =
          ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
      float y2 =
          1.0f - ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

      float r = rectangleData->backgroundColor.r / 255.0f;
      float g = rectangleData->backgroundColor.g / 255.0f;
      float b = rectangleData->backgroundColor.b / 255.0f;
      float a = rectangleData->backgroundColor.a / 255.0f;

      // 检查是否有无效的坐标或颜色
      if (bbox.width <= 0 || bbox.height <= 0) {
        Log("警告：矩形 #%d 尺寸无效 (%.1fx%.1f)，跳过渲染\n", rectangle_count,
            bbox.width, bbox.height);
        break;
      }

      if (a <= 0.0f) {
        Log("警告：矩形 #%d 透明度为0，跳过渲染\n", rectangle_count);
        break;
      }

      // 检查批处理是否还有空间（每个矩形6个顶点，每个顶点14个float）
      if (rectangleBatch.vertex_count + 6 * 15 > 1000 * 6 * 15) {
        Log("警告：矩形批处理已满，跳过剩余矩形\n");
        break;
      }

      // 添加顶点数据到批处理
      float *vertices = &rectangleBatch.vertices[rectangleBatch.vertex_count];

      float corner_radii[4] = {rectangleData->cornerRadius.topLeft,
                               rectangleData->cornerRadius.topRight,
                               rectangleData->cornerRadius.bottomRight,
                               rectangleData->cornerRadius.bottomLeft};

      // 将zIndex转换为-1到1之间的值，用于深度测试
      // 将 zIndex 映射到 [0,1]，数值越大深度越小，保证更高 zIndex 渲染在最前
      float normalized_z = 1.0f - (float)renderCommand->zIndex / 32767.0f;

      float vertex_data[6][15] = {
          // pos, color, local_pos, rect_size, corner_radius, z_index
          {x1, y1, r, g, b, a, 0, 0, bbox.width, bbox.height, corner_radii[0],
           corner_radii[1], corner_radii[2], corner_radii[3], normalized_z},
          {x2, y1, r, g, b, a, 1, 0, bbox.width, bbox.height, corner_radii[0],
           corner_radii[1], corner_radii[2], corner_radii[3], normalized_z},
          {x1, y2, r, g, b, a, 0, 1, bbox.width, bbox.height, corner_radii[0],
           corner_radii[1], corner_radii[2], corner_radii[3], normalized_z},
          {x2, y1, r, g, b, a, 1, 0, bbox.width, bbox.height, corner_radii[0],
           corner_radii[1], corner_radii[2], corner_radii[3], normalized_z},
          {x2, y2, r, g, b, a, 1, 1, bbox.width, bbox.height, corner_radii[0],
           corner_radii[1], corner_radii[2], corner_radii[3], normalized_z},
          {x1, y2, r, g, b, a, 0, 1, bbox.width, bbox.height, corner_radii[0],
           corner_radii[1], corner_radii[2], corner_radii[3], normalized_z},
      };

      memcpy(vertices, vertex_data, sizeof(vertex_data));
      rectangleBatch.vertex_count += 6 * 15;

      Log("+ 矩形 #%d 已添加到批处理: 位置(%.1f,%.1f) 尺寸(%.1fx%.1f) "
          "NDC(%.3f,%.3f-%.3f,%.3f) 颜色(%.2f,%.2f,%.2f,%.2f)\n",
          rectangle_count, bbox.x, bbox.y, bbox.width, bbox.height, x1, y1, x2,
          y2, r, g, b, a);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_BORDER: {
      // 边框渲染（简化实现，渲染为4个矩形）
      Clay_BorderRenderData *borderData = &renderCommand->renderData.border;
      Clay_BoundingBox bbox = renderCommand->boundingBox;

      float r = borderData->color.r / 255.0f;
      float g = borderData->color.g / 255.0f;
      float b = borderData->color.b / 255.0f;
      float a = borderData->color.a / 255.0f;

      // 上边框
      if (borderData->width.top > 0) {
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y2 = 1.0f - ((bbox.y + borderData->width.top) /
                           (float)context->screenHeight) *
                              2.0f;

        float vertices[] = {
            x1,          y1, r,          g,           b,  a,
            0,           0,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x1,          y2, r,          g,           b,  a,
            0,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x2,          y2, r,          g,           b,  a,
            1,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x1,          y2, r,
            g,           b,  a,          0,           1,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f};

        size_t verticesSize = sizeof(vertices);
        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             verticesSize);
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, verticesSize);
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
      }

      // 下边框
      if (borderData->width.bottom > 0) {
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - ((bbox.y + bbox.height - borderData->width.bottom) /
                           (float)context->screenHeight) *
                              2.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        float vertices[] = {
            x1,          y1, r,          g,           b,  a,
            0,           0,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x1,          y2, r,          g,           b,  a,
            0,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x2,          y2, r,          g,           b,  a,
            1,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x1,          y2, r,
            g,           b,  a,          0,           1,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f};

        size_t verticesSize = sizeof(vertices);
        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             verticesSize);
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, verticesSize);
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
      }

      // 左边框
      if (borderData->width.left > 0) {
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float x2 =
            ((bbox.x + borderData->width.left) / (float)context->screenWidth) *
                2.0f -
            1.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        float vertices[] = {
            x1,          y1, r,          g,           b,  a,
            0,           0,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x1,          y2, r,          g,           b,  a,
            0,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x2,          y2, r,          g,           b,  a,
            1,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x1,          y2, r,
            g,           b,  a,          0,           1,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f};

        size_t verticesSize = sizeof(vertices);
        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             verticesSize);
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, verticesSize);
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
      }

      // 右边框
      if (borderData->width.right > 0) {
        float x1 = ((bbox.x + bbox.width - borderData->width.right) /
                    (float)context->screenWidth) *
                       2.0f -
                   1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        float vertices[] = {
            x1,          y1, r,          g,           b,  a,
            0,           0,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x1,          y2, r,          g,           b,  a,
            0,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x2,          y2, r,          g,           b,  a,
            1,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x1,          y2, r,
            g,           b,  a,          0,           1,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f};

        size_t verticesSize = sizeof(vertices);
        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             verticesSize);
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, verticesSize);
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
      }
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
      Clay_TextRenderData *textData = &renderCommand->renderData.text;
      Clay_BoundingBox bbox = renderCommand->boundingBox;

      // 将zIndex规范化到[0,1]范围，数值越大深度越小（越靠前）
      float normalized_z = 1.0f - (float)renderCommand->zIndex / 32767.0f;

      // 累积文本到批次，不立即渲染
      Clay_WebGPU_RenderText(context, renderPass, textData, bbox, normalized_z);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
      // 图像渲染由独立的图像渲染模块处理
      // 这里不需要处理，图像会在主渲染循环中统一处理
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
      Clay_ClipRenderData *clipData = &renderCommand->renderData.clip;
      (void)clipData; // Mark as used to suppress compiler warning
      Clay_BoundingBox bbox = renderCommand->boundingBox;

      // 计算裁剪矩形（转换为像素坐标）
      uint32_t x = (uint32_t)bbox.x;
      uint32_t y = (uint32_t)bbox.y;
      uint32_t width = (uint32_t)bbox.width;
      uint32_t height = (uint32_t)bbox.height;

      // 确保裁剪矩形在屏幕范围内
      x = (x < context->screenWidth) ? x : context->screenWidth - 1;
      y = (y < context->screenHeight) ? y : context->screenHeight - 1;
      width = ((x + width) <= context->screenWidth)
                  ? width
                  : (context->screenWidth - x);
      height = ((y + height) <= context->screenHeight)
                   ? height
                   : (context->screenHeight - y);

      // 设置裁剪矩形（WebGPU使用从顶部开始的Y坐标）
      wgpuRenderPassEncoderSetScissorRect(renderPass, x, y, width, height);

      Log("设置裁剪区域: 位置(%u,%u) 尺寸(%ux%u)\n", x, y, width, height);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
      // 重置裁剪矩形为整个屏幕
      wgpuRenderPassEncoderSetScissorRect(
          renderPass, 0, 0, context->screenWidth, context->screenHeight);
      Log("重置裁剪区域为整个屏幕\n");
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
      Clay_CustomRenderData *customData = &renderCommand->renderData.custom;
      Clay_BoundingBox bbox = renderCommand->boundingBox;

      // 调用自定义渲染回调（如果提供）
      if (renderCommand->userData) {
        // 创建自定义渲染上下文
        Clay_CustomRenderContext customContext = {
            .renderPass = renderPass,
            .boundingBox = bbox,
            .userData = customData->customData,
            .screenWidth = context->screenWidth,
            .screenHeight = context->screenHeight};

        // 执行用户自定义渲染
        Clay_CustomRenderCallback callback =
            (Clay_CustomRenderCallback)renderCommand->userData;
        callback(&customContext);
        Log("执行自定义渲染: 位置(%.1f,%.1f) 尺寸(%.1fx%.1f)\n", bbox.x, bbox.y,
            bbox.width, bbox.height);
      } else {
        // 如果没有自定义回调，渲染一个占位矩形
        float x1 = (bbox.x / (float)context->screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (bbox.y / (float)context->screenHeight) * 2.0f;
        float x2 =
            ((bbox.x + bbox.width) / (float)context->screenWidth) * 2.0f - 1.0f;
        float y2 =
            1.0f -
            ((bbox.y + bbox.height) / (float)context->screenHeight) * 2.0f;

        // 使用紫色作为占位符颜色
        float r = 0.6f, g = 0.4f, b = 0.8f, a = 0.7f;

        float vertices[] = {
            x1,          y1, r,          g,           b,  a,
            0,           0,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x1,          y2, r,          g,           b,  a,
            0,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x2,          y1, r,
            g,           b,  a,          1,           0,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f,
            x2,          y2, r,          g,           b,  a,
            1,           1,  bbox.width, bbox.height, 0,  0,
            0,           0,  0.0f,       x1,          y2, r,
            g,           b,  a,          0,           1,  bbox.width,
            bbox.height, 0,  0,          0,           0,  0.0f};

        size_t verticesSize = sizeof(vertices);
        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             verticesSize);
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, verticesSize);
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

        Log("渲染自定义占位符: 位置(%.1f,%.1f) 尺寸(%.1fx%.1f)\n", bbox.x,
            bbox.y, bbox.width, bbox.height);
      }
      break;
    }
    default:
      break;
    }
  }

  // 渲染所有收集的矩形（批处理渲染）
  if (rectangleBatch.vertex_count > 0) {
    Log("开始批处理渲染 %d 个矩形 (%d 个顶点)\n",
        rectangleBatch.vertex_count / (6 * 15),
        rectangleBatch.vertex_count / 15);

    wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0,
                         rectangleBatch.vertices,
                         rectangleBatch.vertex_count * sizeof(float));
    wgpuRenderPassEncoderSetPipeline(renderPass, context->rectanglePipeline);
    wgpuRenderPassEncoderSetVertexBuffer(
        renderPass, 0, context->vertexBuffer, 0,
        rectangleBatch.vertex_count * sizeof(float));
    wgpuRenderPassEncoderDraw(renderPass, rectangleBatch.vertex_count / 15, 1,
                              0, 0);

    Log("✓ 批处理渲染完成！\n");
  }

  // 刷新任何剩余的文本批次
  Log("准备刷新文本批次...\n");
  if (context->textRenderer &&
      context->textRenderer->current_batch.char_count > 0) {
    Log("检测到 %d 个字符在文本批次中，准备渲染...\n",
        context->textRenderer->current_batch.char_count);
    text_renderer_flush_batch(context->textRenderer, renderPass);
  } else {
    Log("文本批次为空，无需渲染\n");
  }

  // 结束文本渲染帧
  text_renderer_end_frame(context->textRenderer);

  wgpuRenderPassEncoderEnd(renderPass);

  Log("=== 渲染帧 %d 完成，共渲染 %d 个矩形 ===\n", frame_count,
      rectangle_count);

  WGPUCommandBufferDescriptor commandBufferDesc = {
      .label = {.data = "Clay Command Buffer", .length = WGPU_STRLEN}};
  WGPUCommandBuffer commandBuffer =
      wgpuCommandEncoderFinish(encoder, &commandBufferDesc);

  wgpuQueueSubmit(context->queue, 1, &commandBuffer);

  // 清理资源
  // wgpuTextureViewRelease(context->depthTextureView);
  // wgpuTextureRelease(context->depthTexture);
  wgpuCommandBufferRelease(commandBuffer);
  wgpuRenderPassEncoderRelease(renderPass);
  wgpuCommandEncoderRelease(encoder);
}

void Clay_WebGPU_Cleanup(Clay_WebGPU_Context *context) {
  if (!context)
    return;

  // 清理文本渲染器
  text_renderer_destroy(context->textRenderer);

  // 清理缓冲区
  if (context->vertexBuffer)
    wgpuBufferRelease(context->vertexBuffer);
  if (context->indexBuffer)
    wgpuBufferRelease(context->indexBuffer);
  if (context->uniformBuffer)
    wgpuBufferRelease(context->uniformBuffer);

  // 清理深度纹理
  if (context->depthTextureView)
    wgpuTextureViewRelease(context->depthTextureView);
  if (context->depthTexture)
    wgpuTextureRelease(context->depthTexture);

  // 清理渲染管线
  if (context->rectanglePipeline)
    wgpuRenderPipelineRelease(context->rectanglePipeline);

  free(context);
  Log("Clay WebGPU渲染器已清理\n");
}