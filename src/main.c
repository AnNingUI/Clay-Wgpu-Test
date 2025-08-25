#include "DEV.h"
#include "clay_webgpu/runtime.h"
#include "components/components.h"
#include <stdio.h>

static FILE *logFile = NULL;

void SetupLogging() {
  logFile = fopen("log.txt", "w");
  if (logFile) {
    freopen("log.txt", "w", stdout);
    freopen("log.txt", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
  }
}

void RestoreConsole() {
  if (logFile) {
    fclose(logFile);
    logFile = NULL;
  }
}

void HandleClayErrors(Clay_ErrorData errorData) {
  Log("Clay Error: %s\n", errorData.errorText.chars);
}

Clay_Dimensions MeasureText(Clay_StringSlice text,
                            Clay_TextElementConfig *config, void *userData) {
  RuntimeContext *ctx = (RuntimeContext *)userData;
  if (!ctx || !ctx->clayRenderer) {
    return (Clay_Dimensions){.width = text.length * config->fontSize * 0.6f,
                             .height = config->fontSize};
  }
  return unified_adapter_measure_clay_text(text, config, ctx->clayRenderer);
}

// 图片数据结构和管理
typedef struct {
  int textureIndex;  // 在统一渲染器中的纹理索引
  int width;
  int height;
} UnifiedImage;

// 全局图片变量
static UnifiedImage *test_img = NULL;

// 使用统一渲染器加载图片
UnifiedImage *load_image_unified(UnifiedAdapter *adapter, const char *imagePath) {
  if (!adapter) return NULL;
  
  int textureIndex = unified_adapter_load_texture(adapter, imagePath);
  if (textureIndex < 0) {
    Log("加载图像失败: %s\n", imagePath);
    return NULL;
  }
  
  // 创建图片结构并获取真实纹理尺寸
  UnifiedImage *image = malloc(sizeof(UnifiedImage));
  if (!image) return NULL;
  
  image->textureIndex = textureIndex;
  
  // 获取纹理的真实尺寸
  if (!unified_adapter_get_texture_dimensions(adapter, textureIndex, &image->width, &image->height)) {
    Log("警告: 无法获取纹理尺寸，使用默认值\n");
    image->width = 128;   // 默认宽度
    image->height = 128;  // 默认高度
  }
  
  Log("图像加载成功: %s (纹理索引: %d, 尺寸: %dx%d)\n", imagePath, textureIndex, image->width, image->height);
  return image;
}

static void onPrimaryButtonClick() { Log("点击了主要按钮\n"); }
static void onSecondaryButtonClick() { Log("点击了次要按钮\n"); }
static void onAccentButtonClick() { Log("点击了强调按钮\n"); }

Clay_Vector2 GetChildOffset(RuntimeContext *ctx) { return ctx->scrollOffset; }

void AppLayout(void *userData) {
  RuntimeContext *ctx = (RuntimeContext *)userData;
  Clay_ElementId primaryButtonId = CLAY_ID("PrimaryButton");
  Clay_ElementId secondaryButtonId = CLAY_ID("SecondaryButton");
  Clay_ElementId accentButtonId = CLAY_ID("AccentButton");
  Clay_BeginLayout();
  CLAY({.id = CLAY_ID("MainContainer"),
        .clip = {.vertical = true, .childOffset = GetChildOffset(ctx)},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = BACKGROUND_COLOR}) {
    HeaderComponent(CLAY_STRING("Clay 响应式 UI 示例"));
    AnimatedSidebar();

    CLAY_TEXT(CLAY_STRING("Test UI"),
              CLAY_TEXT_CONFIG(
                  {.fontId = 0, .fontSize = 20, .textColor = PRIMARY_COLOR}));
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = {20, 20, 20, 20},
                     .childGap = 20,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
      ResponsiveCardGrid();
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
        CLAY_TEXT(CLAY_STRING("你好世界！这是中文文本渲染测试。"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = 0, .fontSize = 16, .textColor = TEXT_COLOR}));
        Clay_Context *context = Clay_GetCurrentContext();
        int currentWidth = Clay_GetLayoutDirectionWidth(context);
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
                  .cornerRadius = CLAY_CORNER_RADIUS(5)}){};
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
                  .cornerRadius = CLAY_CORNER_RADIUS(5)}){};
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
                  .cornerRadius = CLAY_CORNER_RADIUS(5)}){};
            CLAY_TEXT(
                CLAY_STRING("动态组件重排"),
                CLAY_TEXT_CONFIG(
                    {.fontId = 0, .fontSize = 14, .textColor = TEXT_COLOR}));
          }
        }
        CLAY_TEXT(CLAY_STRING("这是图片渲染测试。"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = 0, .fontSize = 16, .textColor = TEXT_COLOR}));
        
        // 显示实际图片或占位符
        Log("检查图片状态: test_img = %s\n", test_img ? "有效" : "NULL");
        if (test_img != NULL) {
          Log("创建Clay图片元素: 尺寸 %dx%d, 纹理索引 %d\n", 
              test_img->width, test_img->height, test_img->textureIndex);
          // 使用Clay的图片元素
          CLAY({.id = CLAY_ID("TestImage"),
                .layout = {.sizing = {CLAY_SIZING_FIXED(test_img->width), 
                                     CLAY_SIZING_FIXED(test_img->height)}},
                .image = {.imageData = test_img}}) {
            Log("Clay图片元素已创建\n");     
          };
        } else {
          Log("图片未加载，显示占位符\n");
          // 图片未加载时显示占位符
          CLAY({.id = CLAY_ID("ImagePlaceholder"),
                .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(150)}},
                .backgroundColor = (Clay_Color){100, 100, 100, 255},
                .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
            CLAY_TEXT(CLAY_STRING("图片加载中..."),
                      CLAY_TEXT_CONFIG({.fontId = 0, .fontSize = 14, .textColor = {255, 255, 255, 255}}));
          }
        }
      }
    }
  }
}

void Clean(RuntimeContext *ctx, GLFWwindow *window) {
  Runtime_Run(ctx);
  Runtime_Destroy(ctx);
  glfwDestroyWindow(window);
  glfwTerminate();
  RestoreConsole();
}

int main() {
  // App Init
  SetupLogging();
  if (!glfwInit()) {
    Log("Failed to initialize GLFW\n");
    return -1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window =
      glfwCreateWindow(1280, 720, "Clay WebGPU Test", NULL, NULL);
  if (!window) {
    Log("Failed to create window\n");
    glfwTerminate();
    return -1;
  }
  RuntimeContext *ctx = Runtime_Init(window, 1280, 720);
  if (!ctx) {
    Log("Failed to initialize runtime\n");
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }
  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));
  Clay_Initialize(arena, (Clay_Dimensions){1280, 720},
                  (Clay_ErrorHandler){HandleClayErrors});
  Clay_SetMeasureTextFunction(MeasureText, ctx);
  Clay_WebGPU_LoadFont(ctx->clayRenderer, "assets/fonts/simhei.ttf", 16);
  // 加载测试图片
  test_img = load_image_unified(ctx->clayRenderer, "assets/img/10.png");
  
  Log("OIT透明渲染系统已启用\n");

  // App UI
  Runtime_SetLayoutCallback(ctx, AppLayout, ctx);

  // App Clean
  Clean(ctx, window);
  return 0;
}
