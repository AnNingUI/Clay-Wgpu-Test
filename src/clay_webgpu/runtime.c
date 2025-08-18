#include "runtime.h"
#include "../DEV.h"
#include "../renderer/image_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <unistd.h>
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif

WGPUSurface CreateSurface(WGPUInstance instance, GLFWwindow *window) {
#ifdef _WIN32
  HWND hwnd = glfwGetWin32Window(window);
  WGPUSurfaceSourceWindowsHWND surfaceDesc = {
      .chain = {.sType = WGPUSType_SurfaceSourceWindowsHWND},
      .hwnd = hwnd,
      .hinstance = GetModuleHandle(NULL)};
  WGPUSurfaceDescriptor desc = {.nextInChain = (WGPUChainedStruct *)&surfaceDesc};
  return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(__linux__)
  Display *display = glfwGetX11Display();
  Window x11Window = glfwGetX11Window(window);
  WGPUSurfaceSourceXlibWindow surfaceDesc = {
      .chain = {.sType = WGPUSType_SurfaceSourceXlibWindow},
      .display = display,
      .window = x11Window};
  WGPUSurfaceDescriptor desc = {.nextInChain = (WGPUChainedStruct *)&surfaceDesc};
  return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(__APPLE__)
  void *metalLayer = glfwGetCocoaWindow(window);
  WGPUSurfaceDescriptorFromMetalLayer surfaceDesc = {
      .chain = {.sType = WGPUSType_SurfaceDescriptorFromMetalLayer},
      .layer = metalLayer};
  WGPUSurfaceDescriptor desc = {.nextInChain = (WGPUChainedStruct *)&surfaceDesc};
  return wgpuInstanceCreateSurface(instance, &desc);
#else
  return NULL;
#endif
}

void OnAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata1, void *userdata2) {
  if (status == WGPURequestAdapterStatus_Success) {
    *(WGPUAdapter *)userdata1 = adapter;
  } else {
    Log("Failed to request adapter: %.*s\n", (int)message.length, message.data);
  }
}

void OnDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata1, void *userdata2) {
  if (status == WGPURequestDeviceStatus_Success) {
    *(WGPUDevice *)userdata1 = device;
  } else {
    Log("Failed to request device: %.*s\n", (int)message.length, message.data);
  }
}

bool InitializeWebGPU(RuntimeContext *ctx) {
  WGPUInstanceDescriptor instanceDesc = {0};
  ctx->instance = wgpuCreateInstance(&instanceDesc);
  if (!ctx->instance) {
    Log("Failed to create WebGPU instance\n");
    return false;
  }
  Log("WebGPU instance created successfully\n");

  ctx->surface = CreateSurface(ctx->instance, ctx->window);
  if (!ctx->surface) {
    Log("Failed to create WebGPU surface\n");
    return false;
  }
  Log("WebGPU surface created successfully\n");

  WGPURequestAdapterOptions adapterOptions = {
      .compatibleSurface = ctx->surface,
      .powerPreference = WGPUPowerPreference_HighPerformance};

  WGPUAdapter adapter = NULL;
  wgpuInstanceRequestAdapter(ctx->instance, &adapterOptions,
                             (WGPURequestAdapterCallbackInfo){
                                 .mode = WGPUCallbackMode_AllowProcessEvents,
                                 .callback = OnAdapterRequestEnded,
                                 .userdata1 = &adapter,
                             });

  while (!adapter) {
    wgpuInstanceProcessEvents(ctx->instance);
  }

  if (!adapter) {
    Log("Failed to get WebGPU adapter\n");
    return false;
  }
  Log("WebGPU adapter obtained successfully\n");

  WGPUAdapterInfo adapterInfo;
  wgpuAdapterGetInfo(adapter, &adapterInfo);
  printf("Adapter: %.*s\n", (int)adapterInfo.description.length, adapterInfo.description.data);
  printf("Vendor: %.*s\n", (int)adapterInfo.vendor.length, adapterInfo.vendor.data);
  printf("Device: %.*s\n", (int)adapterInfo.device.length, adapterInfo.device.data);

  WGPUDeviceDescriptor deviceDesc = {0};
  wgpuAdapterRequestDevice(adapter, &deviceDesc,
                           (WGPURequestDeviceCallbackInfo){
                               .mode = WGPUCallbackMode_AllowProcessEvents,
                               .callback = OnDeviceRequestEnded,
                               .userdata1 = &ctx->device,
                           });

  while (!ctx->device) {
    wgpuInstanceProcessEvents(ctx->instance);
  }

  if (!ctx->device) {
    Log("Failed to get WebGPU device\n");
    wgpuAdapterRelease(adapter);
    return false;
  }
  Log("WebGPU device obtained successfully\n");

  ctx->queue = wgpuDeviceGetQueue(ctx->device);

  WGPUSurfaceCapabilities capabilities;
  wgpuSurfaceGetCapabilities(ctx->surface, adapter, &capabilities);

  WGPUTextureFormat selectedFormat = WGPUTextureFormat_BGRA8Unorm;
  bool formatSupported = false;
  for (size_t i = 0; i < capabilities.formatCount; i++) {
    if (capabilities.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
      formatSupported = true;
      break;
    }
  }
  if (!formatSupported) {
    Log("BGRA8Unorm format not supported, using first available format\n");
    selectedFormat = capabilities.formats[0];
  }

  ctx->surfaceConfig = (WGPUSurfaceConfiguration){
      .device = ctx->device,
      .usage = WGPUTextureUsage_RenderAttachment,
      .format = selectedFormat,
      .width = ctx->windowWidth,
      .height = ctx->windowHeight,
      .presentMode = WGPUPresentMode_Fifo,
      .alphaMode = WGPUCompositeAlphaMode_Auto};

  wgpuSurfaceConfigure(ctx->surface, &ctx->surfaceConfig);

  wgpuDevicePoll(ctx->device, true, NULL);

  wgpuSurfaceCapabilitiesFreeMembers(capabilities);
  wgpuAdapterRelease(adapter);

  return true;
}

void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
  RuntimeContext *ctx = (RuntimeContext *)glfwGetWindowUserPointer(window);
  ctx->scrollOffset.y += (float)yoffset * 20.0f;
  if (ctx->scrollOffset.y > 0) {
    ctx->scrollOffset.y = 0;
  }
}

void WindowResizeCallback(GLFWwindow *window, int width, int height) {
  RuntimeContext *ctx = (RuntimeContext *)glfwGetWindowUserPointer(window);
  if (width <= 0 || height <= 0) return;
  if (ctx->device) wgpuDevicePoll(ctx->device, true, NULL);
  for (int i = 0; i < 3; i++) {
    wgpuSurfacePresent(ctx->surface);
    wgpuDevicePoll(ctx->device, true, NULL);
  }
  ctx->windowWidth = width;
  ctx->windowHeight = height;
  ctx->surfaceConfig.width = width;
  ctx->surfaceConfig.height = height;
  wgpuSurfaceConfigure(ctx->surface, &ctx->surfaceConfig);
  Clay_SetLayoutDimensions((Clay_Dimensions){width, height});
  if (ctx->clayRenderer) Clay_WebGPU_UpdateScreenSize(ctx->clayRenderer, width, height);
}

RuntimeContext* Runtime_Init(GLFWwindow* window, uint32_t width, uint32_t height) {
  RuntimeContext* ctx = malloc(sizeof(RuntimeContext));
  if (!ctx) return NULL;
  ctx->window = window;
  ctx->windowWidth = width;
  ctx->windowHeight = height;
  ctx->scrollOffset = (Clay_Vector2){0,0};
  ctx->layoutFunc = NULL;
  ctx->userData = NULL;
  if (!InitializeWebGPU(ctx)) {
    free(ctx);
    return NULL;
  }
  ctx->clayRenderer = Clay_WebGPU_Initialize(ctx->device, ctx->queue, NULL, width, height);
  if (!ctx->clayRenderer) {
    Runtime_Destroy(ctx);
    return NULL;
  }
  ctx->imageRenderer = image_renderer_create(ctx->device, ctx->queue);
  return ctx;
}

void Runtime_SetLayoutCallback(RuntimeContext* ctx, void (*layoutFunc)(void* userData), void* userData) {
  if (ctx) {
    ctx->layoutFunc = layoutFunc;
    ctx->userData = userData;
  }
}

void Runtime_Run(RuntimeContext* ctx) {
  if (!ctx) return;
  glfwSetWindowUserPointer(ctx->window, ctx);
  glfwSetScrollCallback(ctx->window, ScrollCallback);
  glfwSetFramebufferSizeCallback(ctx->window, WindowResizeCallback);
  while (!glfwWindowShouldClose(ctx->window)) {
    glfwPollEvents();
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(ctx->surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
      Log("Failed to get surface texture: %d\n", surfaceTexture.status);
      wgpuSurfacePresent(ctx->surface);
      wgpuDevicePoll(ctx->device, true, NULL);
      VSleep(16);
      continue;
    }
    if (!surfaceTexture.texture) {
      wgpuSurfacePresent(ctx->surface);
      continue;
    }
    WGPUTextureView backBuffer = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    if (!backBuffer) {
      wgpuSurfacePresent(ctx->surface);
      continue;
    }
    double mouseX, mouseY;
    glfwGetCursorPos(ctx->window, &mouseX, &mouseY);
    bool mousePressed = glfwGetMouseButton(ctx->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    Clay_SetLayoutDimensions((Clay_Dimensions){ctx->windowWidth, ctx->windowHeight});
    Clay_SetPointerState((Clay_Vector2){mouseX, mouseY}, mousePressed);
    if (ctx->layoutFunc) ctx->layoutFunc(ctx->userData);
    Clay_RenderCommandArray renderCommands = Clay_EndLayout();
    WGPUCommandEncoderDescriptor encoderDesc = {.label = {.data = "Main Command Encoder", .length = WGPU_STRLEN}};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx->device, &encoderDesc);
    ctx->clayRenderer->targetView = backBuffer;
    Clay_WebGPU_Render(ctx->clayRenderer, renderCommands);
    if (ctx->imageRenderer) {
      WGPURenderPassColorAttachment colorAttachment = {
          .view = backBuffer,
          .resolveTarget = NULL,
          .clearValue = {0.0f, 0.0f, 0.0f, 0.0f},
          .loadOp = WGPULoadOp_Load,
          .storeOp = WGPUStoreOp_Store};
      WGPURenderPassDepthStencilAttachment depthStencilAttachment = {
          .view = ctx->clayRenderer->depthTextureView,
          .depthClearValue = 0.0f,  // 修改为0.0f以匹配深度测试
          .depthLoadOp = WGPULoadOp_Load,
          .depthStoreOp = WGPUStoreOp_Store,
          .depthReadOnly = false,
          .stencilLoadOp = WGPULoadOp_Undefined,
          .stencilStoreOp = WGPUStoreOp_Undefined,
          .stencilClearValue = 0,
          .stencilReadOnly = true
      };
      
      WGPURenderPassDescriptor renderPassDesc = {
          .label = {.data = "Image Render Pass", .length = WGPU_STRLEN},
          .colorAttachmentCount = 1,
          .colorAttachments = &colorAttachment,
          .depthStencilAttachment = &depthStencilAttachment};
      WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
      image_renderer_process_clay_commands(ctx->imageRenderer, renderPass, renderCommands, ctx->windowWidth, ctx->windowHeight);
      wgpuRenderPassEncoderEnd(renderPass);
      wgpuRenderPassEncoderRelease(renderPass);
    }
    WGPUCommandBufferDescriptor commandBufferDesc = {.label = {.data = "Main Command Buffer", .length = WGPU_STRLEN}};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDesc);
    wgpuQueueSubmit(ctx->queue, 1, &commandBuffer);
    wgpuCommandBufferRelease(commandBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuDevicePoll(ctx->device, false, NULL);
    wgpuTextureViewRelease(backBuffer);
    wgpuSurfacePresent(ctx->surface);
  }
}

void Runtime_Destroy(RuntimeContext* ctx) {
  if (!ctx) return;
  if (ctx->device) wgpuDevicePoll(ctx->device, true, NULL);
  if (ctx->clayRenderer) {
    Clay_WebGPU_Cleanup(ctx->clayRenderer);
    ctx->clayRenderer = NULL;
  }
  if (ctx->device) wgpuDevicePoll(ctx->device, true, NULL);
  ctx->queue = NULL;
  if (ctx->device) {
    wgpuDeviceRelease(ctx->device);
    ctx->device = NULL;
  }
  if (ctx->surface) {
    wgpuSurfaceRelease(ctx->surface);
    ctx->surface = NULL;
  }
  if (ctx->instance) {
    wgpuInstanceRelease(ctx->instance);
    ctx->instance = NULL;
  }
  if (ctx->imageRenderer) {
    image_renderer_destroy(ctx->imageRenderer);
    ctx->imageRenderer = NULL;
  }
  free(ctx);
}

WGPUDevice Runtime_GetDevice(RuntimeContext* ctx) { return ctx ? ctx->device : NULL; }
WGPUQueue Runtime_GetQueue(RuntimeContext* ctx) { return ctx ? ctx->queue : NULL; }