#include "renderer.h"
#include "../DEV.h"
#include <stdlib.h>
#include <string.h>

// 矩形渲染着色器
static const char *vertexShaderWGSL =
    "struct VertexInput {\n"
    "    @location(0) position: vec2<f32>,\n"
    "    @location(1) color: vec4<f32>,\n"
    "}\n"
    "\n"
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    @location(0) color: vec4<f32>,\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_main(input: VertexInput) -> VertexOutput {\n"
    "    var output: VertexOutput;\n"
    "    output.position = vec4<f32>(input.position, 0.0, 1.0);\n"
    "    output.color = input.color;\n"
    "    return output;\n"
    "}\n";

static const char *fragmentShaderWGSL =
    "struct FragmentInput {\n"
    "    @location(0) color: vec4<f32>,\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(input: FragmentInput) -> @location(0) vec4<f32> {\n"
    "    return input.color;\n"
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

  // 顶点属性配置
  WGPUVertexAttribute vertexAttributes[2] = {
      {.format = WGPUVertexFormat_Float32x2, .offset = 0, .shaderLocation = 0},
      {.format = WGPUVertexFormat_Float32x4, .offset = 8, .shaderLocation = 1}};

  WGPUVertexBufferLayout vertexBufferLayout = {
      .arrayStride = sizeof(float) * 6, // 2 for position, 4 for color
      .stepMode = WGPUVertexStepMode_Vertex,
      .attributeCount = 2,
      .attributes = vertexAttributes};

  // 创建矩形渲染管线布局
  WGPUPipelineLayoutDescriptor layoutDesc = {.bindGroupLayoutCount = 0,
                                             .bindGroupLayouts = NULL};
  WGPUPipelineLayout pipelineLayout =
      wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

  // 创建混合状态
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

  // 创建矩形渲染管线
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

  // 创建矩形渲染缓冲区（支持多个矩形批处理）
  context->vertexBuffer = wgpuDeviceCreateBuffer(
      device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Rectangle Vertex Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
          .size =
              1000 * 6 * 6 *
              sizeof(float), // 支持1000个矩形，每个6顶点，6浮点数(2位置+4颜色)
          .mappedAtCreation = false});

  context->indexBuffer = wgpuDeviceCreateBuffer(
      device,
      &(WGPUBufferDescriptor){
          .label = {.data = "Rectangle Index Buffer", .length = WGPU_STRLEN},
          .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
          .size = 1000 * 6 * sizeof(uint32_t), // 支持1000个矩形
          .mappedAtCreation = false});

  // 释放着色器模块
  wgpuShaderModuleRelease(vertexShader);
  wgpuShaderModuleRelease(fragmentShader);
  wgpuPipelineLayoutRelease(pipelineLayout);

  Log("Clay WebGPU渲染器初始化成功\n");
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

  if (context->textRenderer) {
    text_renderer_update_screen_size(context->textRenderer, screenWidth,
                                     screenHeight);
  }
}

void Clay_WebGPU_RenderText(Clay_WebGPU_Context *context,
                            WGPURenderPassEncoder renderPass,
                            Clay_TextRenderData *textData,
                            Clay_BoundingBox bbox) {
  if (!context || !context->textRenderer || !renderPass || !textData)
    return;

  text_renderer_render_clay_text(context->textRenderer, renderPass, textData,
                                 bbox);
}

void Clay_WebGPU_PrintTextStats(Clay_WebGPU_Context *context) {
  if (!context || !context->textRenderer)
    return;

  text_renderer_print_stats(context->textRenderer);
}

// 简单的矩形批处理结构
typedef struct {
  float vertices[6000 * 6]; // 最多1000个矩形，每个6顶点，6浮点数
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

  WGPURenderPassDescriptor renderPassDesc = {
      .label = {.data = "Clay Render Pass", .length = WGPU_STRLEN},
      .colorAttachmentCount = 1,
      .colorAttachments = &colorAttachment};

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

      // 检查批处理是否还有空间（每个矩形6个顶点，每个顶点6个float）
      if (rectangleBatch.vertex_count + 36 > 6000) {
        Log("警告：矩形批处理已满，跳过剩余矩形\n");
        break;
      }

      // 添加顶点数据到批处理：位置 + 颜色 (每个顶点6个float)
      float *vertices = &rectangleBatch.vertices[rectangleBatch.vertex_count];

      // 三角形1 (左上, 右上, 左下)
      vertices[0] = x1;
      vertices[1] = y1;
      vertices[2] = r;
      vertices[3] = g;
      vertices[4] = b;
      vertices[5] = a;
      vertices[6] = x2;
      vertices[7] = y1;
      vertices[8] = r;
      vertices[9] = g;
      vertices[10] = b;
      vertices[11] = a;
      vertices[12] = x1;
      vertices[13] = y2;
      vertices[14] = r;
      vertices[15] = g;
      vertices[16] = b;
      vertices[17] = a;

      // 三角形2 (右上, 右下, 左下)
      vertices[18] = x2;
      vertices[19] = y1;
      vertices[20] = r;
      vertices[21] = g;
      vertices[22] = b;
      vertices[23] = a;
      vertices[24] = x2;
      vertices[25] = y2;
      vertices[26] = r;
      vertices[27] = g;
      vertices[28] = b;
      vertices[29] = a;
      vertices[30] = x1;
      vertices[31] = y2;
      vertices[32] = r;
      vertices[33] = g;
      vertices[34] = b;
      vertices[35] = a;

      rectangleBatch.vertex_count += 36;

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

        float vertices[] = {x1, y1, r, g, b, a, x2, y1, r, g, b, a,
                            x1, y2, r, g, b, a, x2, y1, r, g, b, a,
                            x2, y2, r, g, b, a, x1, y2, r, g, b, a};

        wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0, vertices,
                             sizeof(vertices));
        wgpuRenderPassEncoderSetPipeline(renderPass,
                                         context->rectanglePipeline);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, context->vertexBuffer, 0, sizeof(vertices));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
      }

      // 可以继续实现其他边框...
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
      Clay_TextRenderData *textData = &renderCommand->renderData.text;
      Clay_BoundingBox bbox = renderCommand->boundingBox;

      // 累积文本到批次，不立即渲染
      Clay_WebGPU_RenderText(context, renderPass, textData, bbox);
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
      // TODO: 实现裁剪区域开始
      break;
    }

    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
      // TODO: 实现裁剪区域结束
      break;
    }

    default:
      break;
    }
  }

  // 渲染所有收集的矩形（批处理渲染）
  if (rectangleBatch.vertex_count > 0) {
    Log("开始批处理渲染 %d 个矩形 (%d 个顶点)\n",
        rectangleBatch.vertex_count / 36, rectangleBatch.vertex_count / 6);

    wgpuQueueWriteBuffer(context->queue, context->vertexBuffer, 0,
                         rectangleBatch.vertices,
                         rectangleBatch.vertex_count * sizeof(float));
    wgpuRenderPassEncoderSetPipeline(renderPass, context->rectanglePipeline);
    wgpuRenderPassEncoderSetVertexBuffer(
        renderPass, 0, context->vertexBuffer, 0,
        rectangleBatch.vertex_count * sizeof(float));
    wgpuRenderPassEncoderDraw(renderPass, rectangleBatch.vertex_count / 6, 1, 0,
                              0);

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

  // 清理渲染管线
  if (context->rectanglePipeline)
    wgpuRenderPipelineRelease(context->rectanglePipeline);

  free(context);
  Log("Clay WebGPU渲染器已清理\n");
}
