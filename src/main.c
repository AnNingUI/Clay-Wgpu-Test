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
  if (!ctx || !ctx->clayRenderer || !ctx->clayRenderer->textRenderer) {
    return (Clay_Dimensions){.width = text.length * config->fontSize * 0.6f,
                             .height = config->fontSize};
  }
  float width = text_renderer_measure_string_width(
      ctx->clayRenderer->textRenderer, text.chars, config->fontId, text.length);
  float height = text_renderer_get_line_height(ctx->clayRenderer->textRenderer,
                                               config->fontId);
  return (Clay_Dimensions){.width = width, .height = height};
}

WebGPUImage *load_image(ImageRenderer *renderer, const char *imagePath) {
  WebGPUImage *image = image_renderer_create_texture(renderer, imagePath);
  if (!image) {
    Log("加载图像失败: %s\n", imagePath);
    return NULL;
  }
  Log("加载图像成功: %s (%dx%d)\n", imagePath, image->width, image->height);
  return image;
}

WebGPUImage *test_img = NULL;

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
  AnimatedSidebar();
  CLAY({.id = CLAY_ID("MainContainer"),
        .clip = {.vertical = true, .childOffset = GetChildOffset(ctx)},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = BACKGROUND_COLOR}) {
    HeaderComponent(CLAY_STRING("Clay 响应式 UI 示例"));

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
        Clay_LayoutConfig imageLayout = {
            .sizing = {CLAY_SIZING_FIXED(test_img->width),
                       CLAY_SIZING_FIXED(test_img->height)}};
        Clay_ElementDeclaration imageElement = {
            .id = CLAY_ID("TestImage"),
            .layout = imageLayout,
            .image = {.imageData = test_img}};
        CLAY(imageElement);
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
  test_img = load_image(ctx->imageRenderer, "assets/img/10.png");

  // App UI
  Runtime_SetLayoutCallback(ctx, AppLayout, ctx);

  // App Clean
  Clean(ctx, window);
  return 0;
}
