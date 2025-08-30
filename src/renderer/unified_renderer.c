#include "unified_renderer.h"
#include "../DEV.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

// 引入STB库 - 避免重复定义
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD  // 避免SIMD冲突
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// 性能优化常量
#define UNIFIED_BATCH_SIZE 1024
#define UNIFIED_VERTEX_CACHE_SIZE 4096
#define UNIFIED_INDEX_CACHE_SIZE 6144

// 统一着色器WGSL代码
static const char* unified_vertex_shader = 
"struct VertexInput {\n"
"    @location(0) position: vec2<f32>,\n"
"    @location(1) texCoords: vec2<f32>,\n"
"    @location(2) color: vec4<f32>,\n"
"    @location(3) params: vec4<f32>,\n"      // [type, textureIndex, blendMode, reserved]
"    @location(4) geometry: vec4<f32>,\n"    // 几何参数
"    @location(5) bounds: vec4<f32>,\n"      // 包围盒
"    @location(6) zIndex: f32,\n"
"}\n"
"\n"
"struct VertexOutput {\n"
"    @builtin(position) position: vec4<f32>,\n"
"    @location(0) texCoords: vec2<f32>,\n"
"    @location(1) color: vec4<f32>,\n"
"    @location(2) params: vec4<f32>,\n"
"    @location(3) geometry: vec4<f32>,\n"
"    @location(4) bounds: vec4<f32>,\n"
"    @location(5) worldPos: vec2<f32>,\n"    // 世界坐标，用于矩形圆角计算
"}\n"
"\n"
"struct Uniforms {\n"
"    screenSize: vec2<f32>,\n"
"    time: f32,\n"
"    _padding: f32,\n"
"}\n"
"\n"
"@group(0) @binding(0) var<uniform> uniforms: Uniforms;\n"
"\n"
"@vertex\n"
"fn vs_main(input: VertexInput) -> VertexOutput {\n"
"    var output: VertexOutput;\n"
"    \n"
"    // 将屏幕坐标转换为NDC\n"
"    let ndc_x = (input.position.x / uniforms.screenSize.x) * 2.0 - 1.0;\n"
"    let ndc_y = 1.0 - (input.position.y / uniforms.screenSize.y) * 2.0;\n"
"    \n"
"    output.position = vec4<f32>(ndc_x, ndc_y, 0.0, 1.0);\n"
"    output.texCoords = input.texCoords;\n"
"    output.color = input.color;\n"
"    output.params = input.params;\n"
"    output.geometry = input.geometry;\n"
"    output.bounds = input.bounds;\n"
"    output.worldPos = input.position;\n"
"    \n"
"    return output;\n"
"}\n";

static const char* unified_fragment_shader =
"@group(1) @binding(0) var mainTexture: texture_2d<f32>;\n"
"@group(1) @binding(1) var texSampler: sampler;\n"
"\n"
"struct FragmentInput {\n"
"    @location(0) texCoords: vec2<f32>,\n"
"    @location(1) color: vec4<f32>,\n"
"    @location(2) params: vec4<f32>,\n"
"    @location(3) geometry: vec4<f32>,\n"
"    @location(4) bounds: vec4<f32>,\n"
"    @location(5) worldPos: vec2<f32>,\n"
"}\n"
"\n"
"// 圆角矩形SDF函数\n"
"fn sdf_rounded_box(p: vec2<f32>, size: vec2<f32>, radius: vec4<f32>) -> f32 {\n"
"    // 根据象限选择圆角半径\n"
"    var r: f32;\n"
"    if (p.x > 0.0) {\n"
"        if (p.y > 0.0) { r = radius.y; }  // top-right\n"
"        else { r = radius.z; }            // bottom-right\n"
"    } else {\n"
"        if (p.y > 0.0) { r = radius.x; }  // top-left\n"
"        else { r = radius.w; }            // bottom-left\n"
"    }\n"
"    \n"
"    let q = abs(p) - size + r;\n"
"    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0))) - r;\n"
"}\n"
"\n"
"@fragment\n"
"fn fs_main(input: FragmentInput) -> @location(0) vec4<f32> {\n"
"    let renderType = u32(input.params.x);\n"
"    let textureIndex = u32(input.params.y);\n"
"    let blendMode = u32(input.params.z);\n"
"    \n"
"    var finalColor = input.color;\n"
"    \n"
"    // 根据渲染类型处理\n"
"    switch renderType {\n"
"        case 0u: { // 矩形\n"
"            let center = input.bounds.xy + input.bounds.zw * 0.5;\n"
"            let p = input.worldPos - center;\n"
"            let halfSize = input.bounds.zw * 0.5;\n"
"            let cornerRadius = input.geometry;\n"
"            \n"
"            let d = sdf_rounded_box(p, halfSize, cornerRadius);\n"
"            let alpha = 1.0 - smoothstep(-1.0, 1.0, d);\n"
"            \n"
"            if (alpha < 0.001) {\n"
"                discard;\n"
"            }\n"
"            \n"
"            finalColor.a = finalColor.a * alpha;\n"
"        }\n"
"        case 1u: { // 文本\n"
"            let texColor = textureSample(mainTexture, texSampler, input.texCoords);\n"
"            finalColor.a = finalColor.a * texColor.r; // 使用红色通道作为alpha\n"
"            \n"
"            if (finalColor.a < 0.001) {\n"
"                discard;\n"
"            }\n"
"        }\n"
"        case 2u: { // 图片\n"
"            let texColor = textureSample(mainTexture, texSampler, input.texCoords);\n"
"            finalColor = finalColor * texColor;\n"
"            \n"
"            // 可选：应用圆角遮罩\n"
"            if (input.geometry.x > 0.0) {\n"
"                let center = input.bounds.xy + input.bounds.zw * 0.5;\n"
"                let p = input.worldPos - center;\n"
"                let halfSize = input.bounds.zw * 0.5;\n"
"                let cornerRadius = input.geometry;\n"
"                \n"
"                let d = sdf_rounded_box(p, halfSize, cornerRadius);\n"
"                let alpha = 1.0 - smoothstep(-1.0, 1.0, d);\n"
"                finalColor.a = finalColor.a * alpha;\n"
"            }\n"
"            \n"
"            if (finalColor.a < 0.001) {\n"
"                discard;\n"
"            }\n"
"        }\n"
"        case 3u: { // 边框\n"
"            let center = input.bounds.xy + input.bounds.zw * 0.5;\n"
"            let p = input.worldPos - center;\n"
"            let halfSize = input.bounds.zw * 0.5;\n"
"            let borderWidth = input.geometry.x;\n"
"            let cornerRadius = vec4<f32>(input.geometry.y, input.geometry.z, input.geometry.w, input.geometry.y);\n"
"            \n"
"            let outerD = sdf_rounded_box(p, halfSize, cornerRadius);\n"
"            let innerD = sdf_rounded_box(p, halfSize - vec2<f32>(borderWidth), cornerRadius);\n"
"            \n"
"            let outerAlpha = 1.0 - smoothstep(-1.0, 1.0, outerD);\n"
"            let innerAlpha = 1.0 - smoothstep(-1.0, 1.0, innerD);\n"
"            let borderAlpha = outerAlpha - innerAlpha;\n"
"            \n"
"            if (borderAlpha < 0.001) {\n"
"                discard;\n"
"            }\n"
"            \n"
"            finalColor.a = finalColor.a * borderAlpha;\n"
"        }\n"
"        default: {\n"
"            discard;\n"
"        }\n"
"    }\n"
"    \n"
"    return finalColor;\n"
"}\n";

// UTF-8解码
uint32_t unified_renderer_decode_utf8(const char **str) {
    const unsigned char *s = (const unsigned char*)*str;
    uint32_t codepoint = 0;
    
    if (s[0] < 0x80) {
        codepoint = s[0];
        *str += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *str += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *str += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | 
                   ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *str += 4;
    } else {
        codepoint = 0xFFFD; // 替换字符
        *str += 1;
    }
    
    return codepoint;
}

// 创建统一渲染器
UnifiedRenderer* unified_renderer_create(WGPUDevice device, WGPUQueue queue,
                                        uint32_t screenWidth, uint32_t screenHeight) {
    UnifiedRenderer *renderer = malloc(sizeof(UnifiedRenderer));
    memset(renderer, 0, sizeof(UnifiedRenderer));
    
    renderer->device = device;
    renderer->queue = queue;
    renderer->screenWidth = screenWidth;
    renderer->screenHeight = screenHeight;
    
    // 初始化渲染数据
    renderer->vertexCapacity = 1024; // 从最大值开始更小
    renderer->indexCapacity = 1536;  // 1024 * 1.5
    renderer->vertices = malloc(renderer->vertexCapacity * sizeof(UnifiedVertex));
    renderer->indices = malloc(renderer->indexCapacity * sizeof(uint16_t));
    renderer->batchCapacity = 32;    // 从256减少到32
    renderer->batches = malloc(renderer->batchCapacity * sizeof(UnifiedRenderBatch));
    
    // 创建深度纹理
    WGPUTextureDescriptor depthDesc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = {screenWidth, screenHeight, 1},
        .format = WGPUTextureFormat_Depth24Plus,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    renderer->depthTexture = wgpuDeviceCreateTexture(device, &depthDesc);
    renderer->depthTextureView = wgpuTextureCreateView(renderer->depthTexture, NULL);
    
    // 创建顶点缓冲区 - 使用最大尺寸以避免重新创建
    WGPUBufferDescriptor vertexBufferDesc = {
        .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
        .size = UNIFIED_MAX_VERTICES * sizeof(UnifiedVertex),
        .mappedAtCreation = false
    };
    renderer->vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    
    // 创建索引缓冲区 - 使用最大尺寸以避免重新创建
    WGPUBufferDescriptor indexBufferDesc = {
        .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
        .size = UNIFIED_MAX_INDICES * sizeof(uint16_t),
        .mappedAtCreation = false
    };
    renderer->indexBuffer = wgpuDeviceCreateBuffer(device, &indexBufferDesc);
    
    // 创建uniform缓冲区
    WGPUBufferDescriptor uniformBufferDesc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(renderer->uniforms),
        .mappedAtCreation = false
    };
    renderer->uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);
    
    // 创建字体atlas纹理
    WGPUTextureDescriptor atlasDesc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = {UNIFIED_ATLAS_SIZE, UNIFIED_ATLAS_SIZE, 1},
        .format = WGPUTextureFormat_R8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    renderer->fontAtlasTexture = wgpuDeviceCreateTexture(device, &atlasDesc);
    renderer->fontAtlasView = wgpuTextureCreateView(renderer->fontAtlasTexture, NULL);
    renderer->fontAtlasPixels = calloc(UNIFIED_ATLAS_SIZE * UNIFIED_ATLAS_SIZE, 1);
    
    // 创建采样器
    WGPUSamplerDescriptor samplerDesc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .maxAnisotropy = 1
    };
    renderer->defaultSampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    
    // 创建着色器模块
    WGPUShaderSourceWGSL vertexSource = {
        .chain = {.sType = WGPUSType_ShaderSourceWGSL},
        .code = {.data = unified_vertex_shader, .length = WGPU_STRLEN}
    };
    WGPUShaderModuleDescriptor vertexShaderDesc = {
        .nextInChain = (const WGPUChainedStruct*)&vertexSource
    };
    WGPUShaderModule vertexShaderModule = wgpuDeviceCreateShaderModule(device, &vertexShaderDesc);
    
    WGPUShaderSourceWGSL fragmentSource = {
        .chain = {.sType = WGPUSType_ShaderSourceWGSL},
        .code = {.data = unified_fragment_shader, .length = WGPU_STRLEN}
    };
    WGPUShaderModuleDescriptor fragmentShaderDesc = {
        .nextInChain = (const WGPUChainedStruct*)&fragmentSource
    };
    WGPUShaderModule fragmentShaderModule = wgpuDeviceCreateShaderModule(device, &fragmentShaderDesc);
    
    // 创建绑定组布局
    WGPUBindGroupLayoutEntry uniformLayoutEntry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = false,
            .minBindingSize = sizeof(renderer->uniforms)
        }
    };
    WGPUBindGroupLayoutDescriptor uniformLayoutDesc = {
        .entryCount = 1,
        .entries = &uniformLayoutEntry
    };
    renderer->uniformBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &uniformLayoutDesc);
    
    WGPUBindGroupLayoutEntry textureLayoutEntries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        }
    };
    WGPUBindGroupLayoutDescriptor textureLayoutDesc = {
        .entryCount = 2,
        .entries = textureLayoutEntries
    };
    renderer->textureBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &textureLayoutDesc);
    
    // 创建管线布局
    WGPUBindGroupLayout bindGroupLayouts[] = {
        renderer->uniformBindGroupLayout,
        renderer->textureBindGroupLayout
    };
    WGPUPipelineLayoutDescriptor layoutDesc = {
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = bindGroupLayouts
    };
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
    
    // 创建顶点属性
    WGPUVertexAttribute vertexAttributes[7] = {
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(UnifiedVertex, position), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(UnifiedVertex, texCoords), .shaderLocation = 1},
        {.format = WGPUVertexFormat_Float32x4, .offset = offsetof(UnifiedVertex, color), .shaderLocation = 2},
        {.format = WGPUVertexFormat_Float32x4, .offset = offsetof(UnifiedVertex, params), .shaderLocation = 3},
        {.format = WGPUVertexFormat_Float32x4, .offset = offsetof(UnifiedVertex, geometry), .shaderLocation = 4},
        {.format = WGPUVertexFormat_Float32x4, .offset = offsetof(UnifiedVertex, bounds), .shaderLocation = 5},
        {.format = WGPUVertexFormat_Float32, .offset = offsetof(UnifiedVertex, zIndex), .shaderLocation = 6}
    };
    
    WGPUVertexBufferLayout vertexBufferLayout = {
        .arrayStride = sizeof(UnifiedVertex),
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = 7,
        .attributes = vertexAttributes
    };
    
    // 创建渲染管线
    WGPUColorTargetState colorTarget = {
        .format = WGPUTextureFormat_BGRA8Unorm,
        .blend = &(WGPUBlendState){
            .color = {
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
                .operation = WGPUBlendOperation_Add
            },
            .alpha = {
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
                .operation = WGPUBlendOperation_Add
            }
        },
        .writeMask = WGPUColorWriteMask_All
    };
    
    WGPUFragmentState fragmentState = {
        .module = fragmentShaderModule,
        .entryPoint = {.data = "fs_main", .length = WGPU_STRLEN},
        .targetCount = 1,
        .targets = &colorTarget
    };
    
    WGPURenderPipelineDescriptor pipelineDesc = {
        .layout = pipelineLayout,
        .vertex = {
            .module = vertexShaderModule,
            .entryPoint = {.data = "vs_main", .length = WGPU_STRLEN},
            .bufferCount = 1,
            .buffers = &vertexBufferLayout
        },
        .fragment = &fragmentState,
        .depthStencil = NULL,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .multisample = {
            .count = 1,
            .mask = ~0,
            .alphaToCoverageEnabled = false
        }
    };
    
    renderer->pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    
    // 清理临时资源
    wgpuShaderModuleRelease(vertexShaderModule);
    wgpuShaderModuleRelease(fragmentShaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    // 设置字体atlas为第一个纹理
    renderer->textures[0].texture = renderer->fontAtlasTexture;
    renderer->textures[0].textureView = renderer->fontAtlasView;
    renderer->textures[0].width = UNIFIED_ATLAS_SIZE;
    renderer->textures[0].height = UNIFIED_ATLAS_SIZE;
    renderer->textures[0].inUse = true;
    strcpy(renderer->textures[0].debugName, "Font Atlas");
    renderer->fontAtlasTextureIndex = 0;
    renderer->textureCount = 1;
    
    // 初始化uniforms
    renderer->uniforms.screenSize[0] = (float)screenWidth;
    renderer->uniforms.screenSize[1] = (float)screenHeight;
    renderer->uniforms.time = 0.0f;
    renderer->uniforms._padding = 0.0f;
    
    Log("统一渲染器初始化完成\n");
    return renderer;
}

// 动态扩容函数
static bool unified_renderer_ensure_vertex_capacity(UnifiedRenderer *renderer, int requiredVertices) {
    if (renderer->vertexCount + requiredVertices <= renderer->vertexCapacity) {
        return true;
    }
    
    int newCapacity = renderer->vertexCapacity;
    while (newCapacity < renderer->vertexCount + requiredVertices) {
        newCapacity *= 2;
        if (newCapacity > UNIFIED_MAX_VERTICES) {
            newCapacity = UNIFIED_MAX_VERTICES;
            break;
        }
    }
    
    if (newCapacity <= renderer->vertexCapacity) {
        return false; // 已达到最大容量
    }
    
    UnifiedVertex *newVertices = realloc(renderer->vertices, newCapacity * sizeof(UnifiedVertex));
    if (!newVertices) {
        return false;
    }
    
    renderer->vertices = newVertices;
    renderer->vertexCapacity = newCapacity;
    return true;
}

static bool unified_renderer_ensure_index_capacity(UnifiedRenderer *renderer, int requiredIndices) {
    if (renderer->indexCount + requiredIndices <= renderer->indexCapacity) {
        return true;
    }
    
    int newCapacity = renderer->indexCapacity;
    while (newCapacity < renderer->indexCount + requiredIndices) {
        newCapacity *= 2;
        if (newCapacity > UNIFIED_MAX_INDICES) {
            newCapacity = UNIFIED_MAX_INDICES;
            break;
        }
    }
    
    if (newCapacity <= renderer->indexCapacity) {
        return false;
    }
    
    uint16_t *newIndices = realloc(renderer->indices, newCapacity * sizeof(uint16_t));
    if (!newIndices) {
        return false;
    }
    
    renderer->indices = newIndices;
    renderer->indexCapacity = newCapacity;
    return true;
}

static bool unified_renderer_ensure_batch_capacity(UnifiedRenderer *renderer, int requiredBatches) {
    if (renderer->batchCount + requiredBatches <= renderer->batchCapacity) {
        return true;
    }
    
    int newCapacity = renderer->batchCapacity * 2;
    UnifiedRenderBatch *newBatches = realloc(renderer->batches, newCapacity * sizeof(UnifiedRenderBatch));
    if (!newBatches) {
        return false;
    }
    
    renderer->batches = newBatches;
    renderer->batchCapacity = newCapacity;
    return true;
}

// 调整渲染器大小
void unified_renderer_resize(UnifiedRenderer *renderer, 
                           uint32_t screenWidth, uint32_t screenHeight) {
    if (!renderer) return;
    
    renderer->screenWidth = screenWidth;
    renderer->screenHeight = screenHeight;
    
    // 重新创建深度纹理
    if (renderer->depthTexture) {
        wgpuTextureRelease(renderer->depthTexture);
        wgpuTextureViewRelease(renderer->depthTextureView);
    }
    
    WGPUTextureDescriptor depthDesc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = {screenWidth, screenHeight, 1},
        .format = WGPUTextureFormat_Depth24Plus,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    renderer->depthTexture = wgpuDeviceCreateTexture(renderer->device, &depthDesc);
    renderer->depthTextureView = wgpuTextureCreateView(renderer->depthTexture, NULL);
    
    // 更新uniform数据
    renderer->uniforms.screenSize[0] = (float)screenWidth;
    renderer->uniforms.screenSize[1] = (float)screenHeight;
    
    Log("统一渲染器尺寸已更新: %dx%d\n", screenWidth, screenHeight);
}

// 销毁渲染器
void unified_renderer_destroy(UnifiedRenderer *renderer) {
    if (!renderer) return;
    
    // 释放WebGPU资源
    if (renderer->pipeline) wgpuRenderPipelineRelease(renderer->pipeline);
    if (renderer->vertexBuffer) wgpuBufferRelease(renderer->vertexBuffer);
    if (renderer->indexBuffer) wgpuBufferRelease(renderer->indexBuffer);
    if (renderer->uniformBuffer) wgpuBufferRelease(renderer->uniformBuffer);
    if (renderer->depthTexture) wgpuTextureRelease(renderer->depthTexture);
    if (renderer->depthTextureView) wgpuTextureViewRelease(renderer->depthTextureView);
    if (renderer->fontAtlasTexture) wgpuTextureRelease(renderer->fontAtlasTexture);
    if (renderer->fontAtlasView) wgpuTextureViewRelease(renderer->fontAtlasView);
    if (renderer->defaultSampler) wgpuSamplerRelease(renderer->defaultSampler);
    if (renderer->uniformBindGroupLayout) wgpuBindGroupLayoutRelease(renderer->uniformBindGroupLayout);
    if (renderer->textureBindGroupLayout) wgpuBindGroupLayoutRelease(renderer->textureBindGroupLayout);
    
    // 释放纹理
    for (int i = 1; i < renderer->textureCount; i++) {
        if (renderer->textures[i].inUse) {
            wgpuTextureRelease(renderer->textures[i].texture);
            wgpuTextureViewRelease(renderer->textures[i].textureView);
        }
    }
    
    // 释放字体资源
    for (int i = 0; i < renderer->fontCount; i++) {
        if (renderer->fonts[i].fontBuffer) {
            free(renderer->fonts[i].fontBuffer);
        }
    }
    
    // 释放内存
    free(renderer->vertices);
    free(renderer->indices);
    free(renderer->batches);
    free(renderer->fontAtlasPixels);
    free(renderer);
}

// 开始帧
void unified_renderer_begin_frame(UnifiedRenderer *renderer) {
    renderer->vertexCount = 0;
    renderer->indexCount = 0;
    renderer->batchCount = 0;
    renderer->frameCount++;
    renderer->drawCalls = 0;
    renderer->verticesRendered = 0;
    
    // 更新时间
    renderer->uniforms.time = (float)renderer->frameCount * 0.016f; // 假设60fps
    
    Log("=== 开始渲染帧 %d ===\n", renderer->frameCount);
}

// 结束帧并提交渲染
void unified_renderer_end_frame(UnifiedRenderer *renderer, WGPURenderPassEncoder renderPass) {
    Log("结束帧 %d: 顶点数 %d, 索引数 %d\n", renderer->frameCount, renderer->vertexCount, renderer->indexCount);
    if (renderer->vertexCount == 0) {
        Log("警告: 没有顶点数据可渲染\n");
        return;
    }
    
    // 更新uniforms
    wgpuQueueWriteBuffer(renderer->queue, renderer->uniformBuffer, 0, 
                        &renderer->uniforms, sizeof(renderer->uniforms));
    
    // 更新顶点和索引缓冲区 - 只更新实际使用的部分
    if (renderer->vertexCount > 0) {
        wgpuQueueWriteBuffer(renderer->queue, renderer->vertexBuffer, 0,
                            renderer->vertices, renderer->vertexCount * sizeof(UnifiedVertex));
    }
    if (renderer->indexCount > 0) {
        wgpuQueueWriteBuffer(renderer->queue, renderer->indexBuffer, 0,
                            renderer->indices, renderer->indexCount * sizeof(uint16_t));
    }
    
    // 如果字体atlas需要更新
    if (renderer->fontAtlasDirty) {
        unified_renderer_flush_font_atlas(renderer);
    }
    
    // 优化和排序批次
    unified_renderer_optimize_batches(renderer);
    unified_renderer_sort_batches_by_z(renderer);
    
    // 设置渲染状态
    wgpuRenderPassEncoderSetPipeline(renderPass, renderer->pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, renderer->vertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, renderer->indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    
    // 创建uniform绑定组 (每帧创建，避免缓存问题)
    WGPUBindGroupEntry uniformEntry = {
        .binding = 0,
        .buffer = renderer->uniformBuffer,
        .offset = 0,
        .size = sizeof(renderer->uniforms)
    };
    WGPUBindGroupDescriptor uniformBindGroupDesc = {
        .layout = renderer->uniformBindGroupLayout,
        .entryCount = 1,
        .entries = &uniformEntry
    };
    WGPUBindGroup uniformBindGroup = wgpuDeviceCreateBindGroup(renderer->device, &uniformBindGroupDesc);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, uniformBindGroup, 0, NULL);
    
    // 渲染所有批次
    for (int i = 0; i < renderer->batchCount; i++) {
        UnifiedRenderBatch *batch = &renderer->batches[i];
        
        // 根据批次的纹理索引选择正确的纹理
        WGPUTextureView textureView = renderer->fontAtlasView; // 默认使用字体atlas
        
        if (batch->textureIndex >= 0 && batch->textureIndex < renderer->textureCount && 
            renderer->textures[batch->textureIndex].inUse) {
            // 使用指定的图片纹理
            textureView = renderer->textures[batch->textureIndex].textureView;
        }
        
        // 创建纹理绑定组
        WGPUBindGroupEntry textureEntries[2] = {
            {
                .binding = 0,
                .textureView = textureView
            },
            {
                .binding = 1,
                .sampler = renderer->defaultSampler
            }
        };
        WGPUBindGroupDescriptor textureBindGroupDesc = {
            .layout = renderer->textureBindGroupLayout,
            .entryCount = 2,
            .entries = textureEntries
        };
        WGPUBindGroup textureBindGroup = wgpuDeviceCreateBindGroup(renderer->device, &textureBindGroupDesc);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 1, textureBindGroup, 0, NULL);
        
        // 绘制索引
        wgpuRenderPassEncoderDrawIndexed(renderPass, batch->indexCount, 1, batch->startIndex, 0, 0);
        
        // 清理纹理绑定组
        wgpuBindGroupRelease(textureBindGroup);
        
        renderer->drawCalls++;
        renderer->verticesRendered += batch->vertexCount;
    }
    
    // 清理uniform绑定组
    wgpuBindGroupRelease(uniformBindGroup);
    
    Log("帧 %d 完成: %d 个批次, %d 个绘制调用, %d 个顶点\n", 
        renderer->frameCount, renderer->batchCount, renderer->drawCalls, renderer->verticesRendered);
}

// 处理Clay渲染命令
void unified_renderer_process_clay_commands(UnifiedRenderer *renderer,
                                          Clay_RenderCommandArray renderCommands) {
    for (int i = 0; i < renderCommands.length; i++) {
        Clay_RenderCommand *command = &renderCommands.internalArray[i];
        
        switch (command->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
                unified_renderer_process_clay_rectangle(renderer, command);
                break;
            case CLAY_RENDER_COMMAND_TYPE_TEXT:
                unified_renderer_process_clay_text(renderer, command);
                break;
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
                unified_renderer_process_clay_image(renderer, command);
                break;
            case CLAY_RENDER_COMMAND_TYPE_BORDER:
                unified_renderer_process_clay_border(renderer, command);
                break;
            default:
                break;
        }
    }
}

// 处理矩形渲染命令
void unified_renderer_process_clay_rectangle(UnifiedRenderer *renderer,
                                           Clay_RenderCommand *command) {
    Clay_RectangleRenderData *rectData = &command->renderData.rectangle;
    Clay_BoundingBox bbox = command->boundingBox;
    
    Clay_CornerRadius cornerRadius = rectData->cornerRadius;
    Clay_Color color = rectData->backgroundColor;
    
    unified_renderer_add_rectangle(renderer,
                                 bbox.x, bbox.y, bbox.width, bbox.height,
                                 color, cornerRadius, command->zIndex);
}

// 处理文本渲染命令
void unified_renderer_process_clay_text(UnifiedRenderer *renderer,
                                       Clay_RenderCommand *command) {
    Clay_TextRenderData *textData = &command->renderData.text;
    Clay_BoundingBox bbox = command->boundingBox;
    
    const char *text = textData->stringContents.chars;
    int textLength = textData->stringContents.length;
    Clay_Color color = textData->textColor;
    uint16_t fontSize = textData->fontSize;
    
    // 优先使用文本配置中的字体ID，否则使用默认字体
    int fontId = (textData->fontId >= 0 && textData->fontId < renderer->fontCount) ? 
                 textData->fontId : renderer->defaultFontId;
    
    unified_renderer_add_text(renderer, text, textLength,
                             bbox.x, bbox.y, color, fontId, command->zIndex);
}

// 处理图片渲染命令
void unified_renderer_process_clay_image(UnifiedRenderer *renderer,
                                        Clay_RenderCommand *command) {
    Clay_ImageRenderData *imageData = &command->renderData.image;
    Clay_BoundingBox bbox = command->boundingBox;
    
    Log("处理图片渲染命令: 位置(%.1f, %.1f) 尺寸(%.1f x %.1f)\n", 
        bbox.x, bbox.y, bbox.width, bbox.height);
    
    if (imageData->imageData) {
        // imageData->imageData 应该指向 UnifiedImage 结构
        typedef struct {
            int textureIndex;
            int width;
            int height;
        } UnifiedImage;
        
        UnifiedImage *img = (UnifiedImage*)imageData->imageData;
        
        Log("图片数据: 纹理索引=%d, 尺寸=%dx%d\n", 
            img->textureIndex, img->width, img->height);
        
        // 使用白色作为默认tint，如果backgroundColor不为空则使用它作为tint
        Clay_Color tintColor = {255, 255, 255, 255};
        if (imageData->backgroundColor.a > 0) {
            tintColor = imageData->backgroundColor;
            Log("使用背景色作为tint: (%d, %d, %d, %d)\n", 
                tintColor.r, tintColor.g, tintColor.b, tintColor.a);
        } else {
            Log("使用默认白色tint\n");
        }
        
        unified_renderer_add_image(renderer, img->textureIndex,
                                 bbox.x, bbox.y, bbox.width, bbox.height,
                                 tintColor, imageData->cornerRadius, command->zIndex);
    } else {
        Log("警告: 图片数据为空，使用fallback矩形渲染\n");
        // 如果没有图片数据，则渲染为彩色矩形（fallback）
        Clay_CornerRadius cornerRadius = {0, 0, 0, 0};
        unified_renderer_add_rectangle(renderer,
                                     bbox.x, bbox.y, bbox.width, bbox.height,
                                     imageData->backgroundColor, cornerRadius, command->zIndex);
    }
}

// 处理边框渲染命令
void unified_renderer_process_clay_border(UnifiedRenderer *renderer,
                                         Clay_RenderCommand *command) {
    Clay_BorderRenderData *borderData = &command->renderData.border;
    Clay_BoundingBox bbox = command->boundingBox;
    
    float borderWidth = (borderData->width.top + borderData->width.bottom + 
                        borderData->width.left + borderData->width.right) / 4.0f;
    
    Clay_CornerRadius cornerRadius = {0, 0, 0, 0};
    unified_renderer_add_border(renderer,
                               bbox.x, bbox.y, bbox.width, bbox.height,
                               borderData->color, borderWidth, cornerRadius, command->zIndex);
}

// 添加矩形到渲染队列
void unified_renderer_add_rectangle(UnifiedRenderer *renderer,
                                  float x, float y, float width, float height,
                                  Clay_Color color, Clay_CornerRadius cornerRadius,
                                  float zIndex) {
    if (!unified_renderer_ensure_vertex_capacity(renderer, 4) ||
        !unified_renderer_ensure_index_capacity(renderer, 6)) {
        Log("警告: 无法扩容缓冲区，跳过矩形渲染\n");
        return;
    }
    
    Log("添加矩形: (%.1f, %.1f, %.1f, %.1f) 颜色: (%d, %d, %d, %d)\n", 
        x, y, width, height, color.r, color.g, color.b, color.a);
    
    // 转换颜色
    float r = unified_color_to_float(color.r);
    float g = unified_color_to_float(color.g);
    float b = unified_color_to_float(color.b);
    float a = unified_color_to_float(color.a);
    
    // 创建4个顶点
    int startVertex = renderer->vertexCount;
    
    // 左上
    renderer->vertices[startVertex + 0] = (UnifiedVertex){
        .position = {x, y},
        .texCoords = {0.0f, 0.0f},
        .color = {r, g, b, a},
        .params = {0.0f, 0.0f, 0.0f, 0.0f}, // type=rectangle, texture=0
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomRight, cornerRadius.bottomLeft},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 右上
    renderer->vertices[startVertex + 1] = (UnifiedVertex){
        .position = {x + width, y},
        .texCoords = {1.0f, 0.0f},
        .color = {r, g, b, a},
        .params = {0.0f, 0.0f, 0.0f, 0.0f},
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomRight, cornerRadius.bottomLeft},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 右下
    renderer->vertices[startVertex + 2] = (UnifiedVertex){
        .position = {x + width, y + height},
        .texCoords = {1.0f, 1.0f},
        .color = {r, g, b, a},
        .params = {0.0f, 0.0f, 0.0f, 0.0f},
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomRight, cornerRadius.bottomLeft},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 左下
    renderer->vertices[startVertex + 3] = (UnifiedVertex){
        .position = {x, y + height},
        .texCoords = {0.0f, 1.0f},
        .color = {r, g, b, a},
        .params = {0.0f, 0.0f, 0.0f, 0.0f},
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomRight, cornerRadius.bottomLeft},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 添加索引 (两个三角形)
    int startIndex = renderer->indexCount;
    renderer->indices[startIndex + 0] = startVertex + 0;
    renderer->indices[startIndex + 1] = startVertex + 1;
    renderer->indices[startIndex + 2] = startVertex + 2;
    renderer->indices[startIndex + 3] = startVertex + 0;
    renderer->indices[startIndex + 4] = startVertex + 2;
    renderer->indices[startIndex + 5] = startVertex + 3;
    
    renderer->vertexCount += 4;
    renderer->indexCount += 6;
    
    Log("矩形添加完成，当前顶点数: %d, 索引数: %d\n", renderer->vertexCount, renderer->indexCount);
}

// 添加文本到渲染队列
void unified_renderer_add_text(UnifiedRenderer *renderer,
                             const char *text, int textLength,
                             float x, float y, Clay_Color color,
                             int fontId, float zIndex) {
    Log("添加文本: '%.*s' 位置: (%.1f, %.1f) 字体ID: %d\n", textLength, text, x, y, fontId);
    
    if (fontId < 0 || fontId >= renderer->fontCount) {
        fontId = renderer->defaultFontId;
    }
    
    if (fontId < 0) {
        // 没有可用字体，渲染为矩形占位符
        Clay_CornerRadius cornerRadius = {0, 0, 0, 0};
        float width = textLength * 8.0f; // 估算宽度
        float height = 16.0f; // 估算高度
        Log("使用矩形占位符渲染文本\n");
        unified_renderer_add_rectangle(renderer, x, y, width, height, color, cornerRadius, zIndex);
        return;
    }
    
    UnifiedFont *font = &renderer->fonts[fontId];
    if (!font->loaded) return;
    
    float currentX = x;
    const char *textPtr = text;
    const char *textEnd = text + textLength;
    
    while (textPtr < textEnd && *textPtr) {
        uint32_t codepoint = unified_renderer_decode_utf8(&textPtr);
        if (codepoint == 0) break;
        
        UnifiedGlyph *glyph = unified_renderer_get_glyph(renderer, codepoint, fontId);
        if (!glyph || !glyph->valid) {
            currentX += 8.0f; // 跳过无效字符
            continue;
        }
        
        // 计算字符位置
        float glyphX = currentX + glyph->offsetX;
        // 正确计算基线位置：文本框顶部 + 字体上升高度 + 字形相对基线的偏移
        UnifiedFont *currentFont = &renderer->fonts[fontId];
        float baselineY = y + currentFont->ascent * currentFont->scale;
        float glyphY = baselineY + glyph->offsetY;
        
        // 添加字符四边形
        if (unified_renderer_ensure_vertex_capacity(renderer, 4) &&
            unified_renderer_ensure_index_capacity(renderer, 6)) {
            
            float r = unified_color_to_float(color.r);
            float g = unified_color_to_float(color.g);
            float b = unified_color_to_float(color.b);
            float a = unified_color_to_float(color.a);
            
            int startVertex = renderer->vertexCount;
            
            // 创建文本四边形的4个顶点
            renderer->vertices[startVertex + 0] = (UnifiedVertex){
                .position = {glyphX, glyphY},
                .texCoords = {glyph->uvX, glyph->uvY},
                .color = {r, g, b, a},
                .params = {1.0f, (float)renderer->fontAtlasTextureIndex, 0.0f, 0.0f}, // type=text
                .geometry = {0, 0, 0, 0},
                .bounds = {glyphX, glyphY, glyph->width, glyph->height},
                .zIndex = zIndex
            };
            
            renderer->vertices[startVertex + 1] = (UnifiedVertex){
                .position = {glyphX + glyph->width, glyphY},
                .texCoords = {glyph->uvX + glyph->uvWidth, glyph->uvY},
                .color = {r, g, b, a},
                .params = {1.0f, (float)renderer->fontAtlasTextureIndex, 0.0f, 0.0f},
                .geometry = {0, 0, 0, 0},
                .bounds = {glyphX, glyphY, glyph->width, glyph->height},
                .zIndex = zIndex
            };
            
            renderer->vertices[startVertex + 2] = (UnifiedVertex){
                .position = {glyphX + glyph->width, glyphY + glyph->height},
                .texCoords = {glyph->uvX + glyph->uvWidth, glyph->uvY + glyph->uvHeight},
                .color = {r, g, b, a},
                .params = {1.0f, (float)renderer->fontAtlasTextureIndex, 0.0f, 0.0f},
                .geometry = {0, 0, 0, 0},
                .bounds = {glyphX, glyphY, glyph->width, glyph->height},
                .zIndex = zIndex
            };
            
            renderer->vertices[startVertex + 3] = (UnifiedVertex){
                .position = {glyphX, glyphY + glyph->height},
                .texCoords = {glyph->uvX, glyph->uvY + glyph->uvHeight},
                .color = {r, g, b, a},
                .params = {1.0f, (float)renderer->fontAtlasTextureIndex, 0.0f, 0.0f},
                .geometry = {0, 0, 0, 0},
                .bounds = {glyphX, glyphY, glyph->width, glyph->height},
                .zIndex = zIndex
            };
            
            // 添加索引
            int startIndex = renderer->indexCount;
            renderer->indices[startIndex + 0] = startVertex + 0;
            renderer->indices[startIndex + 1] = startVertex + 1;
            renderer->indices[startIndex + 2] = startVertex + 2;
            renderer->indices[startIndex + 3] = startVertex + 0;
            renderer->indices[startIndex + 4] = startVertex + 2;
            renderer->indices[startIndex + 5] = startVertex + 3;
            
            renderer->vertexCount += 4;
            renderer->indexCount += 6;
        }
        
        currentX += glyph->advance;
    }
}

// 添加图片到渲染队列
void unified_renderer_add_image(UnifiedRenderer *renderer,
                              int textureIndex,
                              float x, float y, float width, float height,
                              Clay_Color tintColor, Clay_CornerRadius cornerRadius,
                              float zIndex) {
    // 动态扩容检查
    if (!unified_renderer_ensure_vertex_capacity(renderer, 4) ||
        !unified_renderer_ensure_index_capacity(renderer, 6)) {
        return;
    }
    
    // 验证纹理索引有效性
    if (textureIndex < 0 || textureIndex >= renderer->textureCount || 
        !renderer->textures[textureIndex].inUse) {
        // 如果纹理无效，回退到彩色矩形
        unified_renderer_add_rectangle(renderer, x, y, width, height, tintColor, cornerRadius, zIndex);
        return;
    }
    
    float r = unified_color_to_float(tintColor.r);
    float g = unified_color_to_float(tintColor.g);
    float b = unified_color_to_float(tintColor.b);
    float a = unified_color_to_float(tintColor.a);
    
    // 直接使用屏幕坐标，让顶点着色器进行NDC转换
    float x1 = x, y1 = y;                    // 左上角
    float x2 = x + width, y2 = y;            // 右上角  
    float x3 = x + width, y3 = y + height;   // 右下角
    float x4 = x, y4 = y + height;           // 左下角
    
    Log("添加图像顶点: (%f,%f) (%f,%f) (%f,%f) (%f,%f)\n", 
        x1, y1, x2, y2, x3, y3, x4, y4);
    
    // 添加四个顶点（左上、右上、右下、左下）
    int baseVertex = renderer->vertexCount;
    
    // 左上角
    renderer->vertices[renderer->vertexCount++] = (UnifiedVertex){
        .position = {x1, y1},
        .texCoords = {0.0f, 0.0f},
        .color = {r, g, b, a},
        .params = {UNIFIED_RENDER_TYPE_IMAGE, (float)textureIndex, UNIFIED_BLEND_ALPHA, 0.0f},
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomLeft, cornerRadius.bottomRight},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 右上角
    renderer->vertices[renderer->vertexCount++] = (UnifiedVertex){
        .position = {x2, y2},
        .texCoords = {1.0f, 0.0f},
        .color = {r, g, b, a},
        .params = {UNIFIED_RENDER_TYPE_IMAGE, (float)textureIndex, UNIFIED_BLEND_ALPHA, 0.0f},
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomLeft, cornerRadius.bottomRight},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 右下角
    renderer->vertices[renderer->vertexCount++] = (UnifiedVertex){
        .position = {x3, y3},
        .texCoords = {1.0f, 1.0f},
        .color = {r, g, b, a},
        .params = {UNIFIED_RENDER_TYPE_IMAGE, (float)textureIndex, UNIFIED_BLEND_ALPHA, 0.0f},
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomLeft, cornerRadius.bottomRight},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 左下角
    renderer->vertices[renderer->vertexCount++] = (UnifiedVertex){
        .position = {x4, y4},
        .texCoords = {0.0f, 1.0f},
        .color = {r, g, b, a},
        .params = {UNIFIED_RENDER_TYPE_IMAGE, (float)textureIndex, UNIFIED_BLEND_ALPHA, 0.0f},
        .geometry = {cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomLeft, cornerRadius.bottomRight},
        .bounds = {x, y, width, height},
        .zIndex = zIndex
    };
    
    // 添加索引（两个三角形：0-1-2, 0-2-3）
    renderer->indices[renderer->indexCount++] = baseVertex + 0;
    renderer->indices[renderer->indexCount++] = baseVertex + 1;
    renderer->indices[renderer->indexCount++] = baseVertex + 2;
    renderer->indices[renderer->indexCount++] = baseVertex + 0;
    renderer->indices[renderer->indexCount++] = baseVertex + 2;
    renderer->indices[renderer->indexCount++] = baseVertex + 3;
}

// 添加边框到渲染队列
void unified_renderer_add_border(UnifiedRenderer *renderer,
                               float x, float y, float width, float height,
                               Clay_Color color, float borderWidth,
                               Clay_CornerRadius cornerRadius, float zIndex) {
    // 动态扩容检查
    if (!unified_renderer_ensure_vertex_capacity(renderer, 4) ||
        !unified_renderer_ensure_index_capacity(renderer, 6)) {
        return;
    }
    
    float r = unified_color_to_float(color.r);
    float g = unified_color_to_float(color.g);
    float b = unified_color_to_float(color.b);
    float a = unified_color_to_float(color.a);
    
    int startVertex = renderer->vertexCount;
    
    // 创建边框四边形的4个顶点
    for (int i = 0; i < 4; i++) {
        float posX = (i == 1 || i == 2) ? x + width : x;
        float posY = (i >= 2) ? y + height : y;
        float texU = (i == 1 || i == 2) ? 1.0f : 0.0f;
        float texV = (i >= 2) ? 1.0f : 0.0f;
        
        renderer->vertices[startVertex + i] = (UnifiedVertex){
            .position = {posX, posY},
            .texCoords = {texU, texV},
            .color = {r, g, b, a},
            .params = {3.0f, 0.0f, 0.0f, 0.0f}, // type=border
            .geometry = {borderWidth, cornerRadius.topLeft, cornerRadius.topRight, cornerRadius.bottomLeft},
            .bounds = {x, y, width, height},
            .zIndex = zIndex
        };
    }
    
    // 添加索引
    int startIndex = renderer->indexCount;
    renderer->indices[startIndex + 0] = startVertex + 0;
    renderer->indices[startIndex + 1] = startVertex + 1;
    renderer->indices[startIndex + 2] = startVertex + 2;
    renderer->indices[startIndex + 3] = startVertex + 0;
    renderer->indices[startIndex + 4] = startVertex + 2;
    renderer->indices[startIndex + 5] = startVertex + 3;
    
    renderer->vertexCount += 4;
    renderer->indexCount += 6;
}

// 字体管理
int unified_renderer_load_font(UnifiedRenderer *renderer, const char *fontPath, int fontSize) {
    if (renderer->fontCount >= UNIFIED_MAX_FONTS) return -1;
    
    // 读取字体文件
    FILE *file = fopen(fontPath, "rb");
    if (!file) return -1;
    
    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    unsigned char *fontBuffer = malloc(fileSize);
    if (!fontBuffer) {
        fclose(file);
        return -1;
    }
    
    if (fread(fontBuffer, 1, fileSize, file) != fileSize) {
        free(fontBuffer);
        fclose(file);
        return -1;
    }
    fclose(file);
    
    // 初始化STB字体
    stbtt_fontinfo *fontInfo = malloc(sizeof(stbtt_fontinfo));
    if (!stbtt_InitFont(fontInfo, fontBuffer, 0)) {
        free(fontBuffer);
        free(fontInfo);
        return -1;
    }
    
    // 计算字体度量
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(fontInfo, &ascent, &descent, &lineGap);
    float scale = stbtt_ScaleForPixelHeight(fontInfo, fontSize);
    
    // 存储字体信息
    int fontId = renderer->fontCount++;
    UnifiedFont *font = &renderer->fonts[fontId];
    
    font->fontId = fontId;
    strncpy(font->fontPath, fontPath, sizeof(font->fontPath) - 1);
    font->fontSize = fontSize;
    font->scale = scale;
    font->lineHeight = (ascent - descent + lineGap) * scale;
    font->priority = renderer->fontCount; // 默认优先级为加载顺序
    font->fontBuffer = fontBuffer;
    font->fontBufferSize = fileSize;
    font->stbFont = fontInfo;
    font->ascent = ascent;
    font->descent = descent;
    font->lineGap = lineGap;
    font->loaded = true;
    
    Log("字体加载成功: %s (ID: %d, 大小: %d)\n", fontPath, fontId, fontSize);
    return fontId;
}

// 设置默认字体
bool unified_renderer_set_default_font(UnifiedRenderer *renderer, int fontId) {
    if (fontId < 0 || fontId >= renderer->fontCount) return false;
    renderer->defaultFontId = fontId;
    return true;
}

// 获取字形（支持字体回退）
UnifiedGlyph* unified_renderer_get_glyph(UnifiedRenderer *renderer,
                                        uint32_t codepoint, int fontId) {
    // 在缓存中查找
    for (int i = 0; i < UNIFIED_GLYPH_CACHE_SIZE; i++) {
        if (renderer->glyphCache[i].codepoint == codepoint && 
            renderer->glyphCache[i].fontId == fontId && renderer->glyphCache[i].valid) {
            renderer->glyphCacheHits++;
            return &renderer->glyphCache[i];
        }
    }
    
    // 缓存未命中，尝试从指定字体生成字形
    renderer->glyphCacheMisses++;
    if (unified_renderer_add_glyph_to_atlas(renderer, codepoint, fontId)) {
        // 再次查找
        for (int i = 0; i < UNIFIED_GLYPH_CACHE_SIZE; i++) {
            if (renderer->glyphCache[i].codepoint == codepoint && 
                renderer->glyphCache[i].fontId == fontId && renderer->glyphCache[i].valid) {
                return &renderer->glyphCache[i];
            }
        }
    }
    
    // 字体回退：如果指定字体无法渲染该字符，尝试其他字体
    for (int fallbackFontId = 0; fallbackFontId < renderer->fontCount; fallbackFontId++) {
        if (fallbackFontId == fontId) continue; // 跳过已尝试的字体
        
        // 检查缓存中是否已有该字符的其他字体版本
        for (int i = 0; i < UNIFIED_GLYPH_CACHE_SIZE; i++) {
            if (renderer->glyphCache[i].codepoint == codepoint && 
                renderer->glyphCache[i].fontId == fallbackFontId && renderer->glyphCache[i].valid) {
                Log("字体回退: 字符 U+%04X 从字体 %d 回退到字体 %d\n", codepoint, fontId, fallbackFontId);
                return &renderer->glyphCache[i];
            }
        }
        
        // 尝试从回退字体生成字形
        if (unified_renderer_add_glyph_to_atlas(renderer, codepoint, fallbackFontId)) {
            for (int i = 0; i < UNIFIED_GLYPH_CACHE_SIZE; i++) {
                if (renderer->glyphCache[i].codepoint == codepoint && 
                    renderer->glyphCache[i].fontId == fallbackFontId && renderer->glyphCache[i].valid) {
                    Log("字体回退成功: 字符 U+%04X 使用字体 %d\n", codepoint, fallbackFontId);
                    return &renderer->glyphCache[i];
                }
            }
        }
    }
    
    Log("警告: 字符 U+%04X 在所有字体中都无法渲染\n", codepoint);
    return NULL;
}

// 添加字形到atlas
bool unified_renderer_add_glyph_to_atlas(UnifiedRenderer *renderer,
                                        uint32_t codepoint, int fontId) {
    if (fontId < 0 || fontId >= renderer->fontCount) return false;
    
    UnifiedFont *font = &renderer->fonts[fontId];
    if (!font->loaded) return false;
    
    stbtt_fontinfo *fontInfo = (stbtt_fontinfo*)font->stbFont;
    
    // 首先检查字体是否包含该字符
    int glyphIndex = stbtt_FindGlyphIndex(fontInfo, codepoint);
    if (glyphIndex == 0) {
        // 字体不包含该字符
        return false;
    }
    
    // 获取字形信息
    int advance, lsb;
    stbtt_GetCodepointHMetrics(fontInfo, codepoint, &advance, &lsb);
    
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(fontInfo, codepoint, font->scale, font->scale, &x0, &y0, &x1, &y1);
    
    int width = x1 - x0;
    int height = y1 - y0;
    
    if (width <= 0 || height <= 0) {
        // 对于空白字符（如空格），仍然需要创建字形记录
        if (advance > 0) {
            // 创建空白字形
            int cacheIndex = -1;
            static int lru_counter = 0;
            
            for (int i = 0; i < UNIFIED_GLYPH_CACHE_SIZE; i++) {
                if (!renderer->glyphCache[i].valid) {
                    cacheIndex = i;
                    break;
                }
            }
            
            if (cacheIndex == -1) {
                cacheIndex = lru_counter % UNIFIED_GLYPH_CACHE_SIZE;
                lru_counter++;
            }
            
            UnifiedGlyph *glyph = &renderer->glyphCache[cacheIndex];
            glyph->codepoint = codepoint;
            glyph->fontId = fontId;
            glyph->uvX = 0.0f;
            glyph->uvY = 0.0f;
            glyph->uvWidth = 0.0f;
            glyph->uvHeight = 0.0f;
            glyph->width = 0;
            glyph->height = 0;
            glyph->offsetX = 0;
            glyph->offsetY = 0;
            glyph->advance = advance * font->scale;
            glyph->valid = true;
            
            return true;
        }
        return false;
    }
    
    // 检查atlas是否有足够空间
    if (renderer->fontAtlasX + width > UNIFIED_ATLAS_SIZE) {
        renderer->fontAtlasX = 0;
        renderer->fontAtlasY += renderer->fontAtlasRowHeight + 1;
        renderer->fontAtlasRowHeight = 0;
    }
    
    if (renderer->fontAtlasY + height > UNIFIED_ATLAS_SIZE) {
        Log("字体atlas空间不足\n");
        return false;
    }
    
    // 生成字形位图
    unsigned char *bitmap = malloc(width * height);
    stbtt_MakeCodepointBitmap(fontInfo, bitmap, width, height, width, 
                             font->scale, font->scale, codepoint);
    
    // 复制到atlas
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int atlasIndex = (renderer->fontAtlasY + y) * UNIFIED_ATLAS_SIZE + (renderer->fontAtlasX + x);
            int bitmapIndex = y * width + x;
            renderer->fontAtlasPixels[atlasIndex] = bitmap[bitmapIndex];
        }
    }
    
    free(bitmap);
    
    // 计算UV坐标
    float uvX = (float)renderer->fontAtlasX / UNIFIED_ATLAS_SIZE;
    float uvY = (float)renderer->fontAtlasY / UNIFIED_ATLAS_SIZE;
    float uvWidth = (float)width / UNIFIED_ATLAS_SIZE;
    float uvHeight = (float)height / UNIFIED_ATLAS_SIZE;
    
    // 找到空闲的缓存槽
    int cacheIndex = -1;
    static int lru_counter = 0;
    
    // 首先尝试找空闲槽
    for (int i = 0; i < UNIFIED_GLYPH_CACHE_SIZE; i++) {
        if (!renderer->glyphCache[i].valid) {
            cacheIndex = i;
            break;
        }
    }
    
    if (cacheIndex == -1) {
        // 缓存已满，使用简单的轮转替换策略
        cacheIndex = lru_counter % UNIFIED_GLYPH_CACHE_SIZE;
        lru_counter++;
    }
    
    // 存储字形信息
    UnifiedGlyph *glyph = &renderer->glyphCache[cacheIndex];
    glyph->codepoint = codepoint;
    glyph->fontId = fontId;
    glyph->uvX = uvX;
    glyph->uvY = uvY;
    glyph->uvWidth = uvWidth;
    glyph->uvHeight = uvHeight;
    glyph->width = width;
    glyph->height = height;
    glyph->offsetX = x0;
    glyph->offsetY = y0;  // 保持相对于基线的原始偏移
    glyph->advance = advance * font->scale;
    glyph->valid = true;
    
    // 更新atlas位置
    renderer->fontAtlasX += width + 1;
    if (height > renderer->fontAtlasRowHeight) {
        renderer->fontAtlasRowHeight = height;
    }
    
    renderer->fontAtlasDirty = true;
    return true;
}

// 刷新字体atlas到GPU
void unified_renderer_flush_font_atlas(UnifiedRenderer *renderer) {
    if (!renderer->fontAtlasDirty) return;
    
    wgpuQueueWriteTexture(renderer->queue,
        &(WGPUTexelCopyTextureInfo){
            .texture = renderer->fontAtlasTexture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All
        },
        renderer->fontAtlasPixels,
        UNIFIED_ATLAS_SIZE * UNIFIED_ATLAS_SIZE,
        &(WGPUTexelCopyBufferLayout){
            .offset = 0,
            .bytesPerRow = UNIFIED_ATLAS_SIZE,
            .rowsPerImage = UNIFIED_ATLAS_SIZE
        },
        &(WGPUExtent3D){
            .width = UNIFIED_ATLAS_SIZE,
            .height = UNIFIED_ATLAS_SIZE,
            .depthOrArrayLayers = 1
        });
    
    renderer->fontAtlasDirty = false;
}

// 优化批次
void unified_renderer_optimize_batches(UnifiedRenderer *renderer) {
    if (renderer->vertexCount == 0) {
        renderer->batchCount = 0;
        return;
    }
    
    renderer->batchCount = 0;
    
    // 按纹理索引分组创建批次
    int currentVertexIndex = 0;
    int currentIndexIndex = 0;
    
    while (currentVertexIndex < renderer->vertexCount) {
        if (!unified_renderer_ensure_batch_capacity(renderer, 1)) {
            Log("警告: 无法扩容批次数组\n");
            break;
        }
        
        // 获取当前顶点的纹理索引
        int currentTextureIndex = (int)renderer->vertices[currentVertexIndex].params[1];
        UnifiedBlendMode currentBlendMode = (UnifiedBlendMode)renderer->vertices[currentVertexIndex].params[2];
        
        // 计算这个批次的顶点范围
        int batchStartVertex = currentVertexIndex;
        int batchVertexCount = 0;
        
        // 找到所有使用相同纹理的连续顶点
        while (currentVertexIndex < renderer->vertexCount) {
            int vertexTextureIndex = (int)renderer->vertices[currentVertexIndex].params[1];
            UnifiedBlendMode vertexBlendMode = (UnifiedBlendMode)renderer->vertices[currentVertexIndex].params[2];
            
            if (vertexTextureIndex != currentTextureIndex || 
                vertexBlendMode != currentBlendMode) {
                break;
            }
            
            currentVertexIndex++;
            batchVertexCount++;
        }
        
        // 计算对应的索引范围（假设每4个顶点对应6个索引）
        int batchIndexCount = (batchVertexCount / 4) * 6;
        
        // 创建批次
        renderer->batches[renderer->batchCount] = (UnifiedRenderBatch){
            .textureIndex = currentTextureIndex,
            .blendMode = currentBlendMode,
            .startVertex = batchStartVertex,
            .vertexCount = batchVertexCount,
            .startIndex = currentIndexIndex,
            .indexCount = batchIndexCount,
            .minZ = 0.0f,
            .maxZ = 1.0f
        };
        
        currentIndexIndex += batchIndexCount;
        renderer->batchCount++;
        
        Log("创建批次 %d: 纹理索引=%d, 顶点=%d-%d(%d个), 索引=%d-%d(%d个)\n", 
            renderer->batchCount - 1, currentTextureIndex,
            batchStartVertex, batchStartVertex + batchVertexCount - 1, batchVertexCount,
            currentIndexIndex - batchIndexCount, currentIndexIndex - 1, batchIndexCount);
    }
    
    Log("批次优化完成: 总共 %d 个批次\n", renderer->batchCount);
}

// 按Z值排序批次
void unified_renderer_sort_batches_by_z(UnifiedRenderer *renderer) {
    // 当前简化实现不需要排序，因为只有一个批次
    // 实际实现中应该根据Z值对批次进行排序
}

// 测量文本宽度
float unified_renderer_measure_text(UnifiedRenderer *renderer, const char *text, 
                                   int fontId, int maxLength) {
    if (fontId < 0 || fontId >= renderer->fontCount) {
        fontId = renderer->defaultFontId;
    }
    
    if (fontId < 0) {
        return maxLength * 8.0f; // 估算宽度
    }
    
    UnifiedFont *font = &renderer->fonts[fontId];
    if (!font->loaded) return 0.0f;
    
    float width = 0.0f;
    const char *textPtr = text;
    const char *textEnd = text + maxLength;
    
    while (textPtr < textEnd && *textPtr) {
        uint32_t codepoint = unified_renderer_decode_utf8(&textPtr);
        if (codepoint == 0) break;
        
        UnifiedGlyph *glyph = unified_renderer_get_glyph(renderer, codepoint, fontId);
        if (glyph && glyph->valid) {
            width += glyph->advance;
        } else {
            width += 8.0f; // 默认字符宽度
        }
    }
    
    return width;
}

// 纹理管理
int unified_renderer_load_texture(UnifiedRenderer *renderer, const char *imagePath) {
    Log("开始加载纹理: %s\n", imagePath ? imagePath : "NULL");
    
    if (!renderer) {
        Log("错误: renderer为空\n");
        return -1;
    }
    if (!imagePath) {
        Log("错误: imagePath为空\n");
        return -1;
    }
    if (renderer->textureCount >= UNIFIED_MAX_TEXTURES) {
        Log("错误: 纹理数量已达上限 (%d/%d)\n", renderer->textureCount, UNIFIED_MAX_TEXTURES);
        return -1;
    }
    
    // 检查工作目录
    char currentDir[1024];
    #ifdef _WIN32
    if (_getcwd(currentDir, sizeof(currentDir)) != NULL) {
        Log("当前工作目录: %s\n", currentDir);
    } else {
        Log("无法获取当前工作目录\n");
    }
    #else
    if (getcwd(currentDir, sizeof(currentDir)) != NULL) {
        Log("当前工作目录: %s\n", currentDir);
    } else {
        Log("无法获取当前工作目录\n");
    }
    #endif
    
    // 检查文件是否存在
    if (access(imagePath, F_OK) != 0) {
        Log("错误: 图片文件不存在: %s\n", imagePath);
        return -1;
    }
    Log("文件存在性检查通过: %s\n", imagePath);
    
    // 使用STB加载图像
    int width, height, channels;
    Log("开始调用stbi_load...\n");
    unsigned char *imageData = stbi_load(imagePath, &width, &height, &channels, 4);
    if (!imageData) {
        Log("STB图像加载失败: %s (错误: %s)\n", imagePath, stbi_failure_reason());
        return -1;
    }
    Log("STB图像加载成功: %s (%dx%d, %d通道)\n", imagePath, width, height, channels);
    
    // 创建纹理
    WGPUTextureDescriptor textureDesc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = {width, height, 1},
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    
    WGPUTexture texture = wgpuDeviceCreateTexture(renderer->device, &textureDesc);
    if (!texture) {
        stbi_image_free(imageData);
        return -1;
    }
    
    // 上传图像数据
    wgpuQueueWriteTexture(renderer->queue,
        &(WGPUTexelCopyTextureInfo){
            .texture = texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All
        },
        imageData, width * height * 4,
        &(WGPUTexelCopyBufferLayout){
            .offset = 0,
            .bytesPerRow = width * 4,
            .rowsPerImage = height
        },
        &(WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        });
    
    // 创建纹理视图
    WGPUTextureView textureView = wgpuTextureCreateView(texture, NULL);
    
    // 存储纹理信息
    int textureIndex = renderer->textureCount++;
    renderer->textures[textureIndex].texture = texture;
    renderer->textures[textureIndex].textureView = textureView;
    renderer->textures[textureIndex].width = width;
    renderer->textures[textureIndex].height = height;
    renderer->textures[textureIndex].inUse = true;
    strncpy(renderer->textures[textureIndex].debugName, imagePath, 63);
    renderer->textures[textureIndex].debugName[63] = '\0';
    
    stbi_image_free(imageData);
    Log("纹理加载成功: %s (ID: %d, 尺寸: %dx%d)\n", imagePath, textureIndex, width, height);
    
    return textureIndex;
}

// 获取纹理尺寸
bool unified_renderer_get_texture_dimensions(UnifiedRenderer *renderer, int textureIndex, int *width, int *height) {
    if (!renderer || !width || !height) {
        return false;
    }
    
    if (textureIndex < 0 || textureIndex >= renderer->textureCount || 
        !renderer->textures[textureIndex].inUse) {
        return false;
    }
    
    *width = renderer->textures[textureIndex].width;
    *height = renderer->textures[textureIndex].height;
    
    return true;
}

int unified_renderer_create_texture_from_data(UnifiedRenderer *renderer, 
                                             const unsigned char *data,
                                             uint32_t width, uint32_t height,
                                             const char *debugName) {
    if (!renderer || !data || renderer->textureCount >= UNIFIED_MAX_TEXTURES) {
        return -1;
    }
    
    // 创建纹理
    WGPUTextureDescriptor textureDesc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = {width, height, 1},
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    
    WGPUTexture texture = wgpuDeviceCreateTexture(renderer->device, &textureDesc);
    if (!texture) {
        return -1;
    }
    
    // 上传数据
    wgpuQueueWriteTexture(renderer->queue,
        &(WGPUTexelCopyTextureInfo){
            .texture = texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All
        },
        data, width * height * 4,
        &(WGPUTexelCopyBufferLayout){
            .offset = 0,
            .bytesPerRow = width * 4,
            .rowsPerImage = height
        },
        &(WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        });
    
    WGPUTextureView textureView = wgpuTextureCreateView(texture, NULL);
    
    // 存储纹理信息
    int textureIndex = renderer->textureCount++;
    renderer->textures[textureIndex].texture = texture;
    renderer->textures[textureIndex].textureView = textureView;
    renderer->textures[textureIndex].width = width;
    renderer->textures[textureIndex].height = height;
    renderer->textures[textureIndex].inUse = true;
    if (debugName) {
        strncpy(renderer->textures[textureIndex].debugName, debugName, 63);
        renderer->textures[textureIndex].debugName[63] = '\0';
    } else {
        strcpy(renderer->textures[textureIndex].debugName, "Custom Texture");
    }
    
    return textureIndex;
}

void unified_renderer_release_texture(UnifiedRenderer *renderer, int textureIndex) {
    if (!renderer || textureIndex < 1 || textureIndex >= renderer->textureCount) {
        return; // 保护字体atlas (索引0)
    }
    
    UnifiedTextureSlot *slot = &renderer->textures[textureIndex];
    if (slot->inUse) {
        wgpuTextureRelease(slot->texture);
        wgpuTextureViewRelease(slot->textureView);
        memset(slot, 0, sizeof(UnifiedTextureSlot));
        Log("纹理已释放: ID %d\n", textureIndex);
    }
}

// 调试统计
void unified_renderer_print_stats(UnifiedRenderer *renderer) {
    Log("=== 统一渲染器统计 ===\n");
    Log("帧数: %d\n", renderer->frameCount);
    Log("顶点数: %d/%d\n", renderer->vertexCount, UNIFIED_MAX_VERTICES);
    Log("索引数: %d/%d\n", renderer->indexCount, UNIFIED_MAX_INDICES);
    Log("批次数: %d\n", renderer->batchCount);
    Log("绘制调用: %d\n", renderer->drawCalls);
    Log("字体数: %d/%d\n", renderer->fontCount, UNIFIED_MAX_FONTS);
    Log("字形缓存命中: %d\n", renderer->glyphCacheHits);
    Log("字形缓存未命中: %d\n", renderer->glyphCacheMisses);
}

// 重置统计
void unified_renderer_reset_stats(UnifiedRenderer *renderer) {
    renderer->glyphCacheHits = 0;
    renderer->glyphCacheMisses = 0;
    renderer->drawCalls = 0;
    renderer->verticesRendered = 0;
}