#include "image_renderer.h"
#include "../DEV.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdlib.h>
#include <string.h>

// 前向声明
static void create_default_texture_and_sampler(ImageRenderer* renderer);

// 图像渲染着色器
static const char *imageVertexShaderWGSL =
    "struct VertexInput {\n"
    "    @location(0) position: vec2<f32>,\n"
    "    @location(1) texCoord: vec2<f32>,\n"
    "}\n"
    "\n"
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    @location(0) texCoord: vec2<f32>,\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_main(input: VertexInput) -> VertexOutput {\n"
    "    var output: VertexOutput;\n"
    "    output.position = vec4<f32>(input.position, 0.0, 1.0);\n"
    "    output.texCoord = input.texCoord;\n"
    "    return output;\n"
    "}\n";

static const char *imageFragmentShaderWGSL =
    "@group(0) @binding(0) var textureSampler: sampler;\n"
    "@group(0) @binding(1) var texture: texture_2d<f32>;\n"
    "\n"
    "struct FragmentInput {\n"
    "    @location(0) texCoord: vec2<f32>,\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(input: FragmentInput) -> @location(0) vec4<f32> {\n"
    "    return textureSample(texture, textureSampler, input.texCoord);\n"
    "}\n";

// 创建图像渲染器
ImageRenderer* image_renderer_create(WGPUDevice device, WGPUQueue queue) {
    ImageRenderer* renderer = malloc(sizeof(ImageRenderer));
    memset(renderer, 0, sizeof(ImageRenderer));

    renderer->device = device;
    renderer->queue = queue;

    // 创建着色器模块
    WGPUShaderSourceWGSL vertexShaderSource = {
        .chain = {.sType = WGPUSType_ShaderSourceWGSL},
        .code = {.data = imageVertexShaderWGSL, .length = WGPU_STRLEN}};

    WGPUShaderModuleDescriptor vertexShaderDesc = {
        .nextInChain = (const WGPUChainedStruct *)&vertexShaderSource,
        .label = {.data = "Image Vertex Shader", .length = WGPU_STRLEN}};
    WGPUShaderModule vertexShader = wgpuDeviceCreateShaderModule(device, &vertexShaderDesc);

    WGPUShaderSourceWGSL fragmentShaderSource = {
        .chain = {.sType = WGPUSType_ShaderSourceWGSL},
        .code = {.data = imageFragmentShaderWGSL, .length = WGPU_STRLEN}};

    WGPUShaderModuleDescriptor fragmentShaderDesc = {
        .nextInChain = (const WGPUChainedStruct *)&fragmentShaderSource,
        .label = {.data = "Image Fragment Shader", .length = WGPU_STRLEN}};
    WGPUShaderModule fragmentShader = wgpuDeviceCreateShaderModule(device, &fragmentShaderDesc);

    // 顶点属性配置
    WGPUVertexAttribute vertexAttributes[2] = {
        {.format = WGPUVertexFormat_Float32x2, .offset = 0, .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x2, .offset = sizeof(float) * 2, .shaderLocation = 1}
    };

    WGPUVertexBufferLayout vertexBufferLayout = {
        .arrayStride = sizeof(float) * 4, // 2 for position, 2 for texCoord
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = 2,
        .attributes = vertexAttributes};

    // 创建绑定组布局
    WGPUBindGroupLayoutEntry bindGroupLayoutEntries[2] = {
        {.binding = 0, .visibility = WGPUShaderStage_Fragment, .sampler = {.type = WGPUSamplerBindingType_Filtering}},
        {.binding = 1, .visibility = WGPUShaderStage_Fragment, .texture = {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D}}
    };
    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {
        .label = {.data = "Image Bind Group Layout", .length = WGPU_STRLEN},
        .entryCount = 2,
        .entries = bindGroupLayoutEntries};
    renderer->textureBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

    // 创建管线布局
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {
        .label = {.data = "Image Pipeline Layout", .length = WGPU_STRLEN},
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &renderer->textureBindGroupLayout};
    renderer->imagePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // 创建混合状态
    WGPUBlendState blendState = {
        .color = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_SrcAlpha, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha},
        .alpha = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha}
    };

    WGPUColorTargetState colorTargetState = {
        .format = WGPUTextureFormat_BGRA8Unorm,
        .blend = &blendState,
        .writeMask = WGPUColorWriteMask_All
    };

    // 创建渲染管线
    WGPURenderPipelineDescriptor pipelineDesc = {
        .label = {.data = "Image Pipeline", .length = WGPU_STRLEN},
        .layout = renderer->imagePipelineLayout,
        .vertex = {.module = vertexShader, .entryPoint = {.data = "vs_main", .length = WGPU_STRLEN}, .bufferCount = 1, .buffers = &vertexBufferLayout},
        .fragment = &(WGPUFragmentState){.module = fragmentShader, .entryPoint = {.data = "fs_main", .length = WGPU_STRLEN}, .targetCount = 1, .targets = &colorTargetState},
        .primitive = {.topology = WGPUPrimitiveTopology_TriangleStrip, .stripIndexFormat = WGPUIndexFormat_Undefined, .frontFace = WGPUFrontFace_CCW, .cullMode = WGPUCullMode_None},
        .multisample = {.count = 1, .mask = ~0u, .alphaToCoverageEnabled = false},
        .depthStencil = NULL
    };
    renderer->imagePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // 创建顶点缓冲区
    renderer->vertexBuffer = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
        .label = {.data = "Image Vertex Buffer", .length = WGPU_STRLEN},
        .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
        .size = 1000 * 6 * 4 * sizeof(float), // 支持1000个四边形
        .mappedAtCreation = false});

    // 创建默认纹理和采样器
    create_default_texture_and_sampler(renderer);

    // 清理着色器模块
    wgpuShaderModuleRelease(vertexShader);
    wgpuShaderModuleRelease(fragmentShader);

    Log("图像渲染器创建成功\n");
    return renderer;
}

// 创建默认纹理和采样器
static void create_default_texture_and_sampler(ImageRenderer* renderer) {
    // 创建默认采样器
    WGPUSamplerDescriptor samplerDesc = {
        .label = {.data = "Image Sampler", .length = WGPU_STRLEN},
        .addressModeU = WGPUAddressMode_Repeat,
        .addressModeV = WGPUAddressMode_Repeat,
        .addressModeW = WGPUAddressMode_Repeat,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 1.0f,
        .compare = WGPUCompareFunction_Undefined,
        .maxAnisotropy = 1
    };
    renderer->defaultSampler = wgpuDeviceCreateSampler(renderer->device, &samplerDesc);

    // 创建默认纹理 (1x1 白色像素)
    WGPUTextureDescriptor textureDesc = {
        .label = {.data = "Default Texture", .length = WGPU_STRLEN},
        .size = {.width = 1, .height = 1, .depthOrArrayLayers = 1},
        .mipLevelCount = 1,
        .sampleCount = 1,
        .dimension = WGPUTextureDimension_2D,
        .format = WGPUTextureFormat_RGBA8Unorm,
        .usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding
    };
    renderer->defaultTexture = wgpuDeviceCreateTexture(renderer->device, &textureDesc);

    WGPUTextureViewDescriptor viewDesc = {
        .format = WGPUTextureFormat_RGBA8Unorm,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1
    };
    renderer->defaultTextureView = wgpuTextureCreateView(renderer->defaultTexture, &viewDesc);

    // 上传白色像素数据
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    WGPUTexelCopyBufferLayout dataLayout = {
        .offset = 0,
        .bytesPerRow = 4,
        .rowsPerImage = 1
    };

    WGPUTexelCopyTextureInfo destination = {
        .texture = renderer->defaultTexture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    wgpuQueueWriteTexture(renderer->queue, &destination, whitePixel, sizeof(whitePixel), &dataLayout, &textureDesc.size);

    // 创建默认绑定组
    WGPUBindGroupEntry bindGroupEntries[2] = {
        {.binding = 0, .sampler = renderer->defaultSampler},
        {.binding = 1, .textureView = renderer->defaultTextureView}
    };
    WGPUBindGroupDescriptor bindGroupDesc = {
        .label = {.data = "Default Image Bind Group", .length = WGPU_STRLEN},
        .layout = renderer->textureBindGroupLayout,
        .entryCount = 2,
        .entries = bindGroupEntries
    };
    renderer->defaultBindGroup = wgpuDeviceCreateBindGroup(renderer->device, &bindGroupDesc);
}

// 销毁图像渲染器
void image_renderer_destroy(ImageRenderer* renderer) {
    if (!renderer) return;

    if (renderer->vertexBuffer) wgpuBufferRelease(renderer->vertexBuffer);
    if (renderer->imagePipeline) wgpuRenderPipelineRelease(renderer->imagePipeline);
    if (renderer->imagePipelineLayout) wgpuPipelineLayoutRelease(renderer->imagePipelineLayout);
    if (renderer->textureBindGroupLayout) wgpuBindGroupLayoutRelease(renderer->textureBindGroupLayout);
    if (renderer->defaultBindGroup) wgpuBindGroupRelease(renderer->defaultBindGroup);
    if (renderer->defaultTextureView) wgpuTextureViewRelease(renderer->defaultTextureView);
    if (renderer->defaultTexture) wgpuTextureRelease(renderer->defaultTexture);
    if (renderer->defaultSampler) wgpuSamplerRelease(renderer->defaultSampler);

    free(renderer);
    Log("图像渲染器已销毁\n");
}

// 创建WebGPU纹理
WebGPUImage* image_renderer_create_texture(ImageRenderer* renderer, const char* imagePath) {
    if (!renderer || !imagePath) return NULL;

    // 使用stb_image加载图片
    int width, height, channels;
    unsigned char* imageData = stbi_load(imagePath, &width, &height, &channels, 4);
    if (!imageData) {
        Log("加载图片失败: %s - %s\n", imagePath, stbi_failure_reason());
        return NULL;
    }

    // 创建WebGPU纹理
    WGPUTextureDescriptor textureDesc = {
        .label = {.data = "Loaded Image Texture", .length = WGPU_STRLEN},
        .size = {.width = width, .height = height, .depthOrArrayLayers = 1},
        .mipLevelCount = 1,
        .sampleCount = 1,
        .dimension = WGPUTextureDimension_2D,
        .format = WGPUTextureFormat_RGBA8Unorm,
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst
    };
    WGPUTexture texture = wgpuDeviceCreateTexture(renderer->device, &textureDesc);

    // 上传图片数据到纹理
    WGPUTexelCopyBufferLayout dataLayout = {
        .offset = 0,
        .bytesPerRow = width * 4,
        .rowsPerImage = height
    };

    WGPUTexelCopyTextureInfo destination = {
        .texture = texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    wgpuQueueWriteTexture(renderer->queue, &destination, imageData, width * height * 4, &dataLayout, &textureDesc.size);
    stbi_image_free(imageData);

    // 创建纹理视图
    WGPUTextureViewDescriptor viewDesc = {
        .format = WGPUTextureFormat_RGBA8Unorm,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1
    };
    WGPUTextureView textureView = wgpuTextureCreateView(texture, &viewDesc);

    // 创建绑定组
    WGPUBindGroupEntry bindGroupEntries[2] = {
        {.binding = 0, .sampler = renderer->defaultSampler},
        {.binding = 1, .textureView = textureView}
    };
    WGPUBindGroupDescriptor bindGroupDesc = {
        .layout = renderer->textureBindGroupLayout,
        .entryCount = 2,
        .entries = bindGroupEntries
    };
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(renderer->device, &bindGroupDesc);

    // 创建WebGPUImage结构体
    WebGPUImage* webgpuImage = malloc(sizeof(WebGPUImage));
    webgpuImage->texture = texture;
    webgpuImage->width = width;
    webgpuImage->height = height;

    // 存储绑定组在texture的userData中（需要扩展结构体）
    // 这里简化处理，实际使用时可能需要更复杂的管理
    
    wgpuTextureViewRelease(textureView);
    wgpuBindGroupRelease(bindGroup);

    Log("创建纹理成功: %s (%dx%d)\n", imagePath, width, height);
    return webgpuImage;
}

// 销毁WebGPU纹理
void image_renderer_destroy_texture(WebGPUImage* image) {
    if (!image) return;
    if (image->texture) wgpuTextureRelease(image->texture);
    free(image);
}

// 渲染纹理
void image_renderer_render_texture(ImageRenderer* renderer, 
                                 WGPURenderPassEncoder renderPass,
                                 WebGPUImage* image,
                                 Clay_BoundingBox boundingBox,
                                 Clay_Color tintColor,
                                 Clay_CornerRadius cornerRadius,
                                 uint32_t windowWidth,
                                 uint32_t windowHeight) {
    if (!renderer || !renderPass || !image || windowWidth == 0 || windowHeight == 0) return;

    // 计算NDC坐标，使用实际窗口尺寸
    float x1 = (boundingBox.x / (float)windowWidth) * 2.0f - 1.0f;
    float y1 = 1.0f - (boundingBox.y / (float)windowHeight) * 2.0f;
    float x2 = ((boundingBox.x + boundingBox.width) / (float)windowWidth) * 2.0f - 1.0f;
    float y2 = 1.0f - ((boundingBox.y + boundingBox.height) / (float)windowHeight) * 2.0f;

    // 创建顶点数据
    float vertices[] = {
        // 位置 (x, y)     纹理坐标 (u, v)
        x1, y1, 0.0f, 0.0f,  // 左上
        x2, y1, 1.0f, 0.0f,  // 右上
        x1, y2, 0.0f, 1.0f,  // 左下
        x2, y2, 1.0f, 1.0f   // 右下
    };

    // 更新顶点缓冲区
    wgpuQueueWriteBuffer(renderer->queue, renderer->vertexBuffer, 0, vertices, sizeof(vertices));

    // 创建纹理视图和绑定组
    WGPUTextureViewDescriptor viewDesc = {
        .format = WGPUTextureFormat_RGBA8Unorm,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1
    };
    WGPUTextureView textureView = wgpuTextureCreateView(image->texture, &viewDesc);

    WGPUBindGroupEntry bindGroupEntries[2] = {
        {.binding = 0, .sampler = renderer->defaultSampler},
        {.binding = 1, .textureView = textureView}
    };
    WGPUBindGroupDescriptor bindGroupDesc = {
        .layout = renderer->textureBindGroupLayout,
        .entryCount = 2,
        .entries = bindGroupEntries
    };
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(renderer->device, &bindGroupDesc);

    // 设置渲染状态
    wgpuRenderPassEncoderSetPipeline(renderPass, renderer->imagePipeline);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, renderer->vertexBuffer, 0, sizeof(vertices));
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, NULL);

    // 绘制四边形（4个顶点，使用triangle strip）
    wgpuRenderPassEncoderDraw(renderPass, 4, 1, 0, 0);

    // 清理资源
    wgpuTextureViewRelease(textureView);
    wgpuBindGroupRelease(bindGroup);
}

// 处理Clay渲染命令
void image_renderer_process_clay_commands(ImageRenderer* renderer,
                                        WGPURenderPassEncoder renderPass,
                                        Clay_RenderCommandArray renderCommands,
                                        uint32_t windowWidth,
                                        uint32_t windowHeight) {
    if (!renderer || !renderPass || windowWidth == 0 || windowHeight == 0) return;

    for (uint32_t i = 0; i < renderCommands.length; i++) {
        Clay_RenderCommand *renderCommand = Clay_RenderCommandArray_Get(&renderCommands, i);
        
        if (renderCommand->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            Clay_ImageRenderData* imageData = &renderCommand->renderData.image;
            WebGPUImage* webgpuImage = (WebGPUImage*)imageData->imageData;
            
            if (webgpuImage) {
                Clay_Color tintColor = imageData->backgroundColor;
                // 设置默认着色为白色
                if (tintColor.r == 0 && tintColor.g == 0 && tintColor.b == 0 && tintColor.a == 0) {
                    tintColor = (Clay_Color) {255, 255, 255, 255};
                }
                
                image_renderer_render_texture(renderer, renderPass, webgpuImage,
                                            renderCommand->boundingBox, tintColor,
                                            imageData->cornerRadius, windowWidth, windowHeight);
            }
        }
    }
}