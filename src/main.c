
#define CLAY_IMPLEMENTATION
#include "renderer/renderer.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif

#ifndef WGPUSurfaceGetCurrentTextureStatus_Success
#define WGPUSurfaceGetCurrentTextureStatus_Success 0
#endif

// 应用程序上下文结构
typedef struct {
  GLFWwindow *window;
  WGPUInstance instance;
  WGPUDevice device;
  WGPUQueue queue;
  WGPUSurface surface;
  WGPUSurfaceConfiguration surfaceConfig;
  Clay_WebGPU_Context *clayRenderer;
  uint32_t windowWidth;
  uint32_t windowHeight;
} AppContext;

// 颜色常量定义
const Clay_Color COLOR_BACKGROUND$1 = {240, 240, 245, 255};
const Clay_Color COLOR_SIDEBAR = {200, 200, 210, 255};
const Clay_Color COLOR_BUTTON = {100, 150, 200, 255};
const Clay_Color COLOR_BUTTON_HOVER = {120, 170, 220, 255};
const Clay_Color COLOR_TEXT = {50, 50, 50, 255};
const Clay_Color COLOR_WHITE = {255, 255, 255, 255};

// Clay错误处理函数
void HandleClayErrors(Clay_ErrorData errorData) {
  printf("Clay Error: %s\n", errorData.errorText.chars);
}

// 文本测量函数
Clay_Dimensions MeasureText(Clay_StringSlice text,
                            Clay_TextElementConfig *config, void *userData) {
  return (Clay_Dimensions){.width = text.length * config->fontSize * 0.6f,
                           .height = config->fontSize};
}

// 创建WebGPU表面 - 跨平台实现
WGPUSurface CreateSurface(WGPUInstance instance, GLFWwindow *window) {
#ifdef _WIN32
  HWND hwnd = glfwGetWin32Window(window);

  WGPUSurfaceSourceWindowsHWND surfaceDesc = {
      .chain = {.sType = WGPUSType_SurfaceSourceWindowsHWND},
      .hwnd = hwnd,
      .hinstance = GetModuleHandle(NULL)};
  WGPUSurfaceDescriptor desc = {.nextInChain =
                                    (WGPUChainedStruct *)&surfaceDesc};
  return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(__linux__)
  Display *display = glfwGetX11Display();
  Window x11Window = glfwGetX11Window(window);
  WGPUSurfaceDescriptorFromXlibWindow surfaceDesc = {
      .chain = {.sType = WGPUSType_SurfaceDescriptorFromXlibWindow},
      .display = display,
      .window = x11Window};
  WGPUSurfaceDescriptor desc = {.nextInChain =
                                    (WGPUChainedStruct *)&surfaceDesc};
  return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(__APPLE__)
  void *metalLayer = glfwGetCocoaWindow(window);
  WGPUSurfaceDescriptorFromMetalLayer surfaceDesc = {
      .chain = {.sType = WGPUSType_SurfaceDescriptorFromMetalLayer},
      .layer = metalLayer};
  WGPUSurfaceDescriptor desc = {.nextInChain =
                                    (WGPUChainedStruct *)&surfaceDesc};
  return wgpuInstanceCreateSurface(instance, &desc);
#else
  return NULL;
#endif
}

// WebGPU适配器请求回调函数
void OnAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                           WGPUStringView message, void *userdata1,
                           void *userdata2) {
  if (status == WGPURequestAdapterStatus_Success) {
    *(WGPUAdapter *)userdata1 = adapter;
  } else {
    printf("Failed to request adapter: %.*s\n", (int)message.length,
           message.data);
  }
}

// WebGPU设备请求回调函数
void OnDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device,
                          WGPUStringView message, void *userdata1,
                          void *userdata2) {
  if (status == WGPURequestDeviceStatus_Success) {
    *(WGPUDevice *)userdata1 = device;
  } else {
    printf("Failed to request device: %.*s\n", (int)message.length,
           message.data);
  }
}

// WebGPU初始化 - 使用现代API
bool InitializeWebGPU(AppContext *app) {
  // 创建WebGPU实例
  WGPUInstanceDescriptor instanceDesc = {0};
  app->instance = wgpuCreateInstance(&instanceDesc);
  if (!app->instance) {
    printf("Failed to create WebGPU instance\n");
    return false;
  }

  // 创建表面
  app->surface = CreateSurface(app->instance, app->window);
  if (!app->surface) {
    printf("Failed to create WebGPU surface\n");
    return false;
  }

  // 请求适配器 - 使用异步回调方式
  WGPURequestAdapterOptions adapterOptions = {.compatibleSurface =
                                                  app->surface};

  WGPUAdapter adapter = NULL;
  wgpuInstanceRequestAdapter(app->instance, &adapterOptions,
                             (WGPURequestAdapterCallbackInfo){
                                 .mode = WGPUCallbackMode_AllowProcessEvents,
                                 .callback = OnAdapterRequestEnded,
                                 .userdata1 = &adapter,
                             });

  if (!adapter) {
    printf("Failed to get WebGPU adapter\n");
    return false;
  }

  // 请求设备 - 使用异步回调方式
  WGPUDeviceDescriptor deviceDesc = {0};
  wgpuAdapterRequestDevice(adapter, &deviceDesc,
                           (WGPURequestDeviceCallbackInfo){
                               .mode = WGPUCallbackMode_AllowProcessEvents,
                               .callback = OnDeviceRequestEnded,
                               .userdata1 = &app->device,
                           });

  if (!app->device) {
    printf("Failed to get WebGPU device\n");
    return false;
  }

  app->queue = wgpuDeviceGetQueue(app->device);

  // 配置表面 - 替代已弃用的SwapChain
  WGPUSurfaceCapabilities capabilities;
  wgpuSurfaceGetCapabilities(app->surface, adapter, &capabilities);

  app->surfaceConfig = (WGPUSurfaceConfiguration){
      .device = app->device,
      .usage = WGPUTextureUsage_RenderAttachment,
      .format = capabilities.formats[0], // 使用第一个支持的格式
      .width = app->windowWidth,
      .height = app->windowHeight,
      .presentMode = WGPUPresentMode_Fifo};

  wgpuSurfaceConfigure(app->surface, &app->surfaceConfig);
  wgpuAdapterRelease(adapter);

  return true;
}

// 创建应用程序UI布局
void CreateAppLayout(AppContext *app) {
  Clay_BeginLayout();

  // 主容器
  CLAY({.id = CLAY_ID("MainContainer"),
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                   .layoutDirection = CLAY_LEFT_TO_RIGHT,
                   .padding = CLAY_PADDING_ALL(16),
                   .childGap = 16},
        .backgroundColor = COLOR_BACKGROUND$1}) {
    // 侧边栏
    CLAY({.id = CLAY_ID("Sidebar"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(250), CLAY_SIZING_GROW(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .padding = CLAY_PADDING_ALL(16),
                     .childGap = 12},
          .backgroundColor = COLOR_SIDEBAR}) {
      // 标题
      CLAY_TEXT(CLAY_STRING("Clay Native App"),
                CLAY_TEXT_CONFIG({.fontSize = 24, .textColor = COLOR_TEXT}));

      for (int i = 0; i < 5; i++) {
        char buttonText[32];
        Clay_String buttonString = CLAY_STRING("");

        snprintf(buttonText, sizeof(buttonText), "Button %d", i + 1);

        CLAY({.id = CLAY_IDI_LOCAL("Button", i), // 使用CLAY_IDI_LOCAL
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40)},
                         .padding = CLAY_PADDING_ALL(8),
                         .childAlignment = {CLAY_ALIGN_X_CENTER,
                                            CLAY_ALIGN_Y_CENTER}},
              .backgroundColor =
                  Clay_Hovered() ? COLOR_BUTTON_HOVER : COLOR_BUTTON}) {
          buttonString.chars = buttonText;
          buttonString.length = sizeof(buttonText);
          CLAY_TEXT(buttonString, CLAY_TEXT_CONFIG({
                                      .fontSize = 16,
                                      .textColor = COLOR_WHITE,
                                  }));
        }
      }
    }

    // 主内容区域
    CLAY({.id = CLAY_ID("MainContent"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .padding = CLAY_PADDING_ALL(24),
                     .childGap = 16},
          .backgroundColor = COLOR_WHITE}) {
      CLAY_TEXT(CLAY_STRING("Welcome to Clay Native App"),
                CLAY_TEXT_CONFIG({.fontSize = 32, .textColor = COLOR_TEXT}));

      CLAY_TEXT(CLAY_STRING("This is a native application built with Clay UI "
                            "library and WebGPU renderer."),
                CLAY_TEXT_CONFIG({.fontSize = 16, .textColor = COLOR_TEXT}));

      // 内容卡片
      CLAY({.id = CLAY_ID("ContentCard"),
            .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200)},
                       .padding = CLAY_PADDING_ALL(20)},
            .backgroundColor = {245, 245, 250, 255}}) {
        CLAY_TEXT(CLAY_STRING("Content area with responsive layout"),
                  CLAY_TEXT_CONFIG({.fontSize = 18, .textColor = COLOR_TEXT}));
      }
    }
  }
}

// 窗口大小改变回调
void WindowResizeCallback(GLFWwindow *window, int width, int height) {
  AppContext *app = (AppContext *)glfwGetWindowUserPointer(window);
  app->windowWidth = width;
  app->windowHeight = height;

  // 重新配置表面
  app->surfaceConfig.width = width;
  app->surfaceConfig.height = height;

  // 在重新配置之前先确保设备队列完成所有操作
  if (app->device) {
    wgpuDevicePoll(app->device, false, NULL);
  }

  wgpuSurfaceConfigure(app->surface, &app->surfaceConfig);
  Clay_SetLayoutDimensions((Clay_Dimensions){width, height});
}

// 主循环 - 使用现代Surface API
void RunApp(AppContext *app) {
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();

    // 获取当前纹理 - 使用新的Surface API
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(app->surface, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
      printf("Failed to get surface texture\n");
      break;
    }

    WGPUTextureView backBuffer =
        wgpuTextureCreateView(surfaceTexture.texture, NULL);

    // 更新Clay状态
    double mouseX, mouseY;
    glfwGetCursorPos(app->window, &mouseX, &mouseY);
    bool mousePressed =
        glfwGetMouseButton(app->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    Clay_SetLayoutDimensions(
        (Clay_Dimensions){app->windowWidth, app->windowHeight});
    Clay_SetPointerState((Clay_Vector2){mouseX, mouseY}, mousePressed);
    Clay_UpdateScrollContainers(true, (Clay_Vector2){0, 0}, 0.016f);

    // 创建UI布局
    CreateAppLayout(app);
    Clay_RenderCommandArray renderCommands = Clay_EndLayout();

    // 更新渲染器目标视图
    app->clayRenderer->targetView = backBuffer;

    // 渲染
    Clay_WebGPU_Render(app->clayRenderer, renderCommands);

    // 呈现 - 使用新的Surface API
    wgpuSurfacePresent(app->surface);

    // 正确释放资源
    wgpuTextureViewRelease(backBuffer);
    // 注意：根据WebGPU规范，wgpuSurfacePresent已经处理了texture的释放
    // 我们不应该再手动释放surfaceTexture.texture
  }

  // 确保设备队列完成所有操作
  if (app->device) {
    wgpuDevicePoll(app->device, true, NULL);
  }
}

// 清理资源
void CleanupApp(AppContext *app) {
  // 首先清理Clay渲染器
  if (app->clayRenderer) {
    Clay_WebGPU_Cleanup(app->clayRenderer);
    app->clayRenderer = NULL;
  }

  // 等待设备完成所有操作
  if (app->device) {
    wgpuDevicePoll(app->device, true, NULL);
  }

  // 释放队列（通常不需要显式释放，因为它来自设备）
  app->queue = NULL;

  // 先释放设备再释放表面
  if (app->device) {
    wgpuDeviceRelease(app->device);
    app->device = NULL;
  }

  // 最后释放表面和实例
  if (app->surface) {
    wgpuSurfaceRelease(app->surface);
    app->surface = NULL;
  }

  if (app->instance) {
    wgpuInstanceRelease(app->instance);
    app->instance = NULL;
  }

  if (app->window) {
    glfwDestroyWindow(app->window);
    app->window = NULL;
  }

  glfwTerminate();
}

// 主函数
int main() {
  AppContext app = {0};
  app.windowWidth = 1200;
  app.windowHeight = 800;

  // 初始化GLFW
  if (!glfwInit()) {
    printf("Failed to initialize GLFW\n");
    return -1;
  }

  // 创建窗口
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  app.window = glfwCreateWindow(app.windowWidth, app.windowHeight,
                                "Clay Native App with WebGPU", NULL, NULL);

  if (!app.window) {
    printf("Failed to create window\n");
    glfwTerminate();
    return -1;
  }

  // 设置窗口用户指针和回调
  glfwSetWindowUserPointer(app.window, &app);
  glfwSetWindowSizeCallback(app.window, WindowResizeCallback);

  // 初始化WebGPU
  if (!InitializeWebGPU(&app)) {
    printf("Failed to initialize WebGPU\n");
    CleanupApp(&app);
    return -1;
  }

  // 初始化Clay
  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));
  Clay_Initialize(arena, (Clay_Dimensions){app.windowWidth, app.windowHeight},
                  (Clay_ErrorHandler){HandleClayErrors});
  Clay_SetMeasureTextFunction(MeasureText, NULL);

  // 初始化Clay WebGPU渲染器
  app.clayRenderer = Clay_WebGPU_Initialize(app.device, app.queue, NULL,
                                            app.windowWidth, app.windowHeight);

  if (!app.clayRenderer) {
    printf("Failed to initialize Clay WebGPU renderer\n");
    CleanupApp(&app);
    return -1;
  }

  // 运行应用
  RunApp(&app);

  // 清理资源
  CleanupApp(&app);

  return 0;
}
