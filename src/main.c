#include "DEV.h"
#include "components/components.h"
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

// 添加文件指针用于日志记录
static FILE *logFile = NULL;

// 重定向输出到文件的函数
void SetupLogging() {
  logFile = fopen("log.txt", "w");
  if (logFile) {
    // 重定向 stdout 和 stderr 到文件
    freopen("log.txt", "w", stdout);
    freopen("log.txt", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0); // 禁用缓冲，确保立即写入
    setvbuf(stderr, NULL, _IONBF, 0);
  }
}

// 恢复控制台输出的函数
void RestoreConsole() {
  if (logFile) {
    fclose(logFile);
    logFile = NULL;
  }
}

// 删除自定义的成功状态定义，使用标准定义

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
  Log("Clay Error: %s\n", errorData.errorText.chars);
}

// 文本测量函数 - 使用文本渲染器进行准确测量
Clay_Dimensions MeasureText(Clay_StringSlice text,
                            Clay_TextElementConfig *config, void *userData) {
    
    AppContext *app = (AppContext *)userData;
    if (!app || !app->clayRenderer || !app->clayRenderer->textRenderer) {
        // 回退到简单估计
        return (Clay_Dimensions){.width = text.length * config->fontSize * 0.6f,
                                 .height = config->fontSize};
    }
    
    // 使用文本渲染器测量文本
    float width = text_renderer_measure_string_width(
        app->clayRenderer->textRenderer, 
        text.chars, 
        config->fontId, 
        text.length);
    
    float height = text_renderer_get_line_height(
        app->clayRenderer->textRenderer, 
        config->fontId);
    
    return (Clay_Dimensions){.width = width, .height = height};
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
    Log("Failed to request adapter: %.*s\n", (int)message.length,
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
    Log("Failed to request device: %.*s\n", (int)message.length,
           message.data);
  }
}

// WebGPU初始化 - 使用现代API
bool InitializeWebGPU(AppContext *app) {
  // 创建WebGPU实例
  WGPUInstanceDescriptor instanceDesc = {0};
  app->instance = wgpuCreateInstance(&instanceDesc);
  if (!app->instance) {
    Log("Failed to create WebGPU instance\n");
    return false;
  }
  Log("WebGPU instance created successfully\n");

  // 创建表面
  app->surface = CreateSurface(app->instance, app->window);
  if (!app->surface) {
    Log("Failed to create WebGPU surface\n");
    return false;
  }
  Log("WebGPU surface created successfully\n");

  // 请求适配器 - 添加更详细的错误处理
  WGPURequestAdapterOptions adapterOptions = {
      .compatibleSurface = app->surface,
      .powerPreference = WGPUPowerPreference_HighPerformance};

  WGPUAdapter adapter = NULL;
  wgpuInstanceRequestAdapter(app->instance, &adapterOptions,
                             (WGPURequestAdapterCallbackInfo){
                                 .mode = WGPUCallbackMode_AllowProcessEvents,
                                 .callback = OnAdapterRequestEnded,
                                 .userdata1 = &adapter,
                             });

  // 等待适配器请求完成
  while (!adapter) {
    wgpuInstanceProcessEvents(app->instance);
  }

  if (!adapter) {
    Log("Failed to get WebGPU adapter\n");
    return false;
  }
  Log("WebGPU adapter obtained successfully\n");

  // 检查适配器特性
  WGPUAdapterInfo adapterInfo;
  wgpuAdapterGetInfo(adapter, &adapterInfo);
  Log("Adapter: %.*s\n", (int)adapterInfo.description.length,
         adapterInfo.description.data);
  Log("Vendor: %.*s\n", (int)adapterInfo.vendor.length,
         adapterInfo.vendor.data);
  Log("Device: %.*s\n", (int)adapterInfo.device.length,
         adapterInfo.device.data);

  // 请求设备
  WGPUDeviceDescriptor deviceDesc = {0};
  wgpuAdapterRequestDevice(adapter, &deviceDesc,
                           (WGPURequestDeviceCallbackInfo){
                               .mode = WGPUCallbackMode_AllowProcessEvents,
                               .callback = OnDeviceRequestEnded,
                               .userdata1 = &app->device,
                           });

  // 等待设备请求完成
  while (!app->device) {
    wgpuInstanceProcessEvents(app->instance);
  }

  if (!app->device) {
    Log("Failed to get WebGPU device\n");
    wgpuAdapterRelease(adapter);
    return false;
  }
  Log("WebGPU device obtained successfully\n");

  app->queue = wgpuDeviceGetQueue(app->device);

  // 获取表面能力并选择合适的格式
  WGPUSurfaceCapabilities capabilities;
  wgpuSurfaceGetCapabilities(app->surface, adapter, &capabilities);

  // 强制使用BGRA8Unorm格式（Clay渲染器期望的格式）
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

  app->surfaceConfig =
      (WGPUSurfaceConfiguration){.device = app->device,
                                 .usage = WGPUTextureUsage_RenderAttachment,
                                 .format = selectedFormat,
                                 .width = app->windowWidth,
                                 .height = app->windowHeight,
                                 .presentMode = WGPUPresentMode_Fifo,
                                 .alphaMode = WGPUCompositeAlphaMode_Auto};

  wgpuSurfaceConfigure(app->surface, &app->surfaceConfig);

  // 等待配置完成
  wgpuDevicePoll(app->device, true, NULL);

  wgpuSurfaceCapabilitiesFreeMembers(capabilities);
  wgpuAdapterRelease(adapter);

  return true;
}

// Event Callback
static void onPrimaryButtonClick() { Log("点击了主要按钮\n"); }

static void onSecondaryButtonClick() { Log("点击了次要按钮\n"); }

static void onAccentButtonClick() { Log("点击了强调按钮\n"); }

// 创建应用程序UI布局
void CreateAppLayout(AppContext *app) {
  // Button ID 用于处理事件
  Clay_ElementId primaryButtonId = CLAY_ID("PrimaryButton");
  Clay_ElementId secondaryButtonId = CLAY_ID("SecondaryButton");
  Clay_ElementId accentButtonId = CLAY_ID("AccentButton");

  Clay_BeginLayout();
  CLAY({.id = CLAY_ID("MainContainer"),
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = BACKGROUND_COLOR}) {
    // 标题栏
    HeaderComponent(CLAY_STRING("Clay 响应式 UI 示例"));
    CLAY_TEXT(CLAY_STRING("Test UI"),
              CLAY_TEXT_CONFIG(
                  {.fontId = 0, .fontSize = 20, .textColor =
                  PRIMARY_COLOR}));

    // 内容区域
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = {20, 20, 20, 20},
                     .childGap = 20,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
    // 响应式卡片网格
    ResponsiveCardGrid();

    // // 按钮区域
    CLAY({.layout = {
              .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60)},
              .layoutDirection = CLAY_LEFT_TO_RIGHT,
              .childGap = 15,
              .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {

      ButtonComponent(&(ButtonData){.text = CLAY_STRING("主要按钮"),
                                    .backgroundColor = PRIMARY_COLOR,
                                    .buttonId = primaryButtonId,
                                    .on_click = onPrimaryButtonClick});
      ButtonComponent(&(ButtonData){.text = CLAY_STRING("次要按钮"),
                                    .backgroundColor = SECONDARY_COLOR,
                                    .buttonId = secondaryButtonId,
                                    .on_click = onSecondaryButtonClick});
      ButtonComponent(&(ButtonData){.text = CLAY_STRING("强调按钮"),
                                    .backgroundColor = ACCENT_COLOR,
                                    .buttonId = accentButtonId,
                                    .on_click = onAccentButtonClick});
    }

    // 信息区域
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = {20, 20, 20, 20},
                     .childGap = 15,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = CARD_COLOR,
          .cornerRadius = CLAY_CORNER_RADIUS(10)}) {
      CLAY({.layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30)}}}) {
        CLAY_TEXT(
            CLAY_STRING("中文文本测试"),
            CLAY_TEXT_CONFIG(
                {.fontId = 0, .fontSize = 20, .textColor = PRIMARY_COLOR}));
      }
      
      // 中文文本测试
      CLAY_TEXT(
          CLAY_STRING("你好世界！这是中文文本渲染测试。"),
          CLAY_TEXT_CONFIG(
              {.fontId = 0, .fontSize = 16, .textColor = TEXT_COLOR}));

      // 根据窗口大小动态调整布局
      Clay_Context *context = Clay_GetCurrentContext();
      int currentWidth = Clay_GetLayoutDirectionWidth(context);

      // 在小屏幕上使用垂直布局，在大屏幕上使用水平布局
      Clay_LayoutDirection featureLayoutDirection =
          (currentWidth < 768) ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT;

      CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                       .layoutDirection = featureLayoutDirection,
                       .childGap = 15}}) {
        CLAY({.layout = {
                  .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(20)},
                  .layoutDirection = CLAY_LEFT_TO_RIGHT,
                  .childGap = 10}}) {
          CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(10),
                                      CLAY_SIZING_FIXED(10)}},
                .backgroundColor = ACCENT_COLOR,
                .cornerRadius = CLAY_CORNER_RADIUS(5)}) {}
          CLAY_TEXT(
              CLAY_STRING("基于窗口大小的自适应布局"),
              CLAY_TEXT_CONFIG(
                  {.fontId = 0, .fontSize = 14, .textColor = TEXT_COLOR}));
        }

        CLAY({.layout = {
                  .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(20)},
                  .layoutDirection = CLAY_LEFT_TO_RIGHT,
                  .childGap = 10}}) {
          CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(10),
                                      CLAY_SIZING_FIXED(10)}},
                .backgroundColor = ACCENT_COLOR,
                .cornerRadius = CLAY_CORNER_RADIUS(5)}) {}
          CLAY_TEXT(
              CLAY_STRING("百分比尺寸支持"),
              CLAY_TEXT_CONFIG(
                  {.fontId = 0, .fontSize = 14, .textColor = TEXT_COLOR}));
        }

        CLAY({.layout = {
                  .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(20)},
                  .layoutDirection = CLAY_LEFT_TO_RIGHT,
                  .childGap = 10}}) {
          CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(10),
                                      CLAY_SIZING_FIXED(10)}},
                .backgroundColor = ACCENT_COLOR,
                .cornerRadius = CLAY_CORNER_RADIUS(5)}) {}
          CLAY_TEXT(
              CLAY_STRING("动态组件重排"),
              CLAY_TEXT_CONFIG(
                  {.fontId = 0, .fontSize = 14, .textColor = TEXT_COLOR}));
        }
      }
    }

    }
  }
}

// 窗口大小改变回调
void WindowResizeCallback(GLFWwindow *window, int width, int height) {
  AppContext *app = (AppContext *)glfwGetWindowUserPointer(window);

  if (width <= 0 || height <= 0) {
    return;
  }

  // 强制等待所有操作完成
  if (app->device) {
    wgpuDevicePoll(app->device, true, NULL);
  }

  // 多次Present以确保清理
  for (int i = 0; i < 3; i++) {
    wgpuSurfacePresent(app->surface);
    wgpuDevicePoll(app->device, true, NULL);
  }

  app->windowWidth = width;
  app->windowHeight = height;
  app->surfaceConfig.width = width;
  app->surfaceConfig.height = height;

  wgpuSurfaceConfigure(app->surface, &app->surfaceConfig);
  Clay_SetLayoutDimensions((Clay_Dimensions){width, height});

  // 更新Clay渲染器的屏幕尺寸
  if (app->clayRenderer) {
    Clay_WebGPU_UpdateScreenSize(app->clayRenderer, width, height);
  }
}

// 主循环 - 使用现代Surface API
void RunApp(AppContext *app) {
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();

    // 获取当前纹理
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(app->surface, &surfaceTexture);

    // 修复：检查正确的成功状态值
    if (surfaceTexture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
      Log("Failed to get surface texture: %d\n", surfaceTexture.status);

      // 强制Present清理状态
      wgpuSurfacePresent(app->surface);

      // 强制等待GPU完成
      wgpuDevicePoll(app->device, true, NULL);

// 限制帧率，避免过快循环
#ifdef _WIN32
      Sleep(16);
#else
      usleep(16000);
#endif

      continue;
    }

    // 正常渲染流程...
    if (!surfaceTexture.texture) {
      wgpuSurfacePresent(app->surface);
      continue;
    }

    WGPUTextureView backBuffer =
        wgpuTextureCreateView(surfaceTexture.texture, NULL);
    if (!backBuffer) {
      wgpuSurfacePresent(app->surface);
      continue;
    }

    // UI渲染逻辑
    double mouseX, mouseY;
    glfwGetCursorPos(app->window, &mouseX, &mouseY);
    bool mousePressed =
        glfwGetMouseButton(app->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    Clay_SetLayoutDimensions(
        (Clay_Dimensions){app->windowWidth, app->windowHeight});
    Clay_SetPointerState((Clay_Vector2){mouseX, mouseY}, mousePressed);
    Clay_UpdateScrollContainers(true, (Clay_Vector2){0, 0}, 0.016f);

    CreateAppLayout(app);
    Clay_RenderCommandArray renderCommands = Clay_EndLayout();

    app->clayRenderer->targetView = backBuffer;
    Clay_WebGPU_Render(app->clayRenderer, renderCommands);

    // 确保渲染完成
    wgpuDevicePoll(app->device, false, NULL);
    wgpuTextureViewRelease(backBuffer);
    wgpuSurfacePresent(app->surface);
  }
}

// 清理资源
void CleanupApp(AppContext *app) {
  // 首先等待所有GPU操作完成
  if (app->device) {
    wgpuDevicePoll(app->device, true, NULL);
  }

  // 清理Clay渲染器（包含GPU资源）
  if (app->clayRenderer) {
    Clay_WebGPU_Cleanup(app->clayRenderer);
    app->clayRenderer = NULL;
  }

  // 再次等待设备完成所有操作
  if (app->device) {
    wgpuDevicePoll(app->device, true, NULL);
  }

  // 释放队列引用（通常不需要显式释放）
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
  SetupLogging(); // 设置日志记录

  AppContext app = {0};
  app.windowWidth = 1200;
  app.windowHeight = 800;

  // 初始化GLFW
  if (!glfwInit()) {
    Log("Failed to initialize GLFW\n");
    return -1;
  }

  // 创建窗口
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  app.window = glfwCreateWindow(app.windowWidth, app.windowHeight,
                                "Clay Native App with WebGPU", NULL, NULL);

  if (!app.window) {
    Log("Failed to create window\n");
    glfwTerminate();
    return -1;
  }

  // 设置窗口用户指针和回调
  glfwSetWindowUserPointer(app.window, &app);
  glfwSetWindowSizeCallback(app.window, WindowResizeCallback);

  // 初始化WebGPU
  if (!InitializeWebGPU(&app)) {
    Log("Failed to initialize WebGPU\n");
    CleanupApp(&app);
    return -1;
  }

  // 初始化Clay
  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));
  Clay_Initialize(arena, (Clay_Dimensions){app.windowWidth, app.windowHeight},
                  (Clay_ErrorHandler){HandleClayErrors});
  Clay_SetMeasureTextFunction(MeasureText, &app);

  // 初始化Clay WebGPU渲染器
  app.clayRenderer = Clay_WebGPU_Initialize(app.device, app.queue, NULL,
                                            app.windowWidth, app.windowHeight);

  if (!app.clayRenderer) {
    Log("Failed to initialize Clay WebGPU renderer\n");
    CleanupApp(&app);
    RestoreConsole();
    return -1;
  }

  // 使用新的文本渲染系统加载字体 - 优先加载支持中文的字体
  Log("=== 开始加载字体 ===\n");
  
  // 检查系统字体目录
#ifdef _WIN32
  Log("检测到Windows系统，检查系统字体目录...\n");
  const char *fontPaths[] = {
      "./fonts/msyh.ttc",           // 项目目录下的微软雅黑
      "./fonts/simhei.ttf",         // 项目目录下的黑体
      "./fonts/simsun.ttc",         // 项目目录下的宋体
      "./fonts/arial.ttf",          // 项目目录下的Arial
      "C:/Windows/Fonts/msyh.ttc",     // 系统微软雅黑
      "C:/Windows/Fonts/msyhbd.ttc",   // 系统微软雅黑粗体
      "C:/Windows/Fonts/simhei.ttf",   // 系统黑体
      "C:/Windows/Fonts/simsun.ttc",   // 系统宋体
      "C:/Windows/Fonts/simkai.ttf",   // 系统楷体
      "C:/Windows/Fonts/arial.ttf",    // 系统Arial
      "C:/Windows/Fonts/arialuni.ttf", // Arial Unicode（支持中文）
      "C:/Windows/Fonts/calibri.ttf",
      "C:/Windows/Fonts/segoeui.ttf",
#elif defined(__APPLE__)
  Log("检测到macOS系统，检查系统字体目录...\n");
  const char *fontPaths[] = {
      "./fonts/PingFang.ttc",       // 项目目录下的苹方
      "./fonts/STHeiti Medium.ttc", // 项目目录下的华文黑体
      "/System/Library/Fonts/Arial.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/STHeiti Medium.ttc",  // 华文黑体
      "/System/Library/Fonts/STHeiti Light.ttc",   // 华文细黑
      "/System/Library/Fonts/PingFang.ttc",        // 苹方
#elif defined(__linux__)
  Log("检测到Linux系统，检查系统字体目录...\n");
  const char *fontPaths[] = {
      "./fonts/NotoSansCJK-Regular.ttc", // 项目目录下的Noto CJK
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/arial.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", // Android fallback
      "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",    // Noto CJK
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
#endif
      NULL};

  bool fontLoaded = false;
  for (int i = 0; fontPaths[i] != NULL; i++) {
    // 检查文件是否存在
    FILE *testFile = fopen(fontPaths[i], "rb");
    if (testFile) {
      fclose(testFile);
      Log("字体文件存在: %s\n", fontPaths[i]);
      
      if (Clay_WebGPU_LoadFont(app.clayRenderer, fontPaths[i], 16)) {
        Log("✓ 成功加载字体: %s\n", fontPaths[i]);
        fontLoaded = true;
        break;
      } else {
        Log("✗ 无法加载字体: %s (文件存在但加载失败)\n", fontPaths[i]);
      }
    } else {
      Log("字体文件不存在: %s\n", fontPaths[i]);
    }
  }

  if (!fontLoaded) {
    Log("警告: 无法加载任何字体文件，将尝试加载系统默认字体\n");

    // 尝试加载更多系统字体作为fallback
    const char *fallbackPaths[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/verdana.ttf",
        "C:/Windows/Fonts/consola.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Times.ttc",
#elif defined(__linux__)
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
        NULL
    };

    for (int i = 0; fallbackPaths[i] != NULL; i++) {
      Log("尝试fallback字体: %s\n", fallbackPaths[i]);
      if (Clay_WebGPU_LoadFont(app.clayRenderer, fallbackPaths[i], 16)) {
        Log("成功加载fallback字体: %s\n", fallbackPaths[i]);
        fontLoaded = true;
        break;
      }
    }

    if (!fontLoaded) {
      Log("错误: 无法加载任何字体文件，文本渲染将无法正常工作\n");
    }
  }

  // 运行应用
  RunApp(&app);

  // 清理资源
  CleanupApp(&app);

  // 恢复控制台输出
  RestoreConsole();

  return 0;
}
