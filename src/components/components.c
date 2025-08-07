

#pragma once
#define CLAY_IMPLEMENTATION
#include "components.h"

// 定义颜色常量
const Clay_Color PRIMARY_COLOR = {70, 130, 180, 255};     // Steel Blue
const Clay_Color SECONDARY_COLOR = {106, 90, 205, 255};   // Slate Blue
const Clay_Color ACCENT_COLOR = {220, 20, 60, 255};       // Crimson
const Clay_Color BACKGROUND_COLOR = {245, 245, 245, 255}; // White Smoke
const Clay_Color CARD_COLOR = {255, 255, 255, 255};       // White
const Clay_Color TEXT_COLOR = {51, 51, 51, 255};          // Dark Gray

Clay_Color DarkenColor(Clay_Color color, float factor) {
  Clay_Color darkColor;
  darkColor.r = color.r * factor;
  darkColor.g = color.g * factor;
  darkColor.b = color.b * factor;
  darkColor.a = color.a;
  return darkColor;
}

int Clay_GetLayoutDirectionWidth(Clay_Context *ctx) {
  return ctx->layoutDimensions.width;
}

void CardComponent(Clay_String title, Clay_String content) {
  CLAY({.layout =
            {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(150)},
                .padding = CLAY_PADDING_ALL(20),
                .childGap = 12,
            },
        .backgroundColor = CARD_COLOR,
        .cornerRadius = CLAY_CORNER_RADIUS(10),
        .border = {.color = {200, 200, 200, 255}, .width = {1, 1, 1, 1, 0}}}) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(24)}}}) {
      CLAY_TEXT(title,
                CLAY_TEXT_CONFIG(
                    {.fontId = 0, .fontSize = 18, .textColor = PRIMARY_COLOR}));
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {
      CLAY_TEXT(content,
                CLAY_TEXT_CONFIG(
                    {.fontId = 0, .fontSize = 14, .textColor = TEXT_COLOR}));
    }
  }
}

void ButtonComponent(ButtonData *data) {
  Clay_String text = data->text;
  Clay_Color backgroundColor = data->backgroundColor;
  Clay_ElementId buttonId = data->buttonId;
  ButtonClickListener on_click = data->on_click;

  // 使用静态变量存储点击状态
  static Clay_ElementId lastPressedId = {0};
  static bool isClicked = false;

  Clay_Context *context = Clay_GetCurrentContext();
  Clay_PointerData pointerInfo = context->pointerInfo;

  // Down
  bool isPressed = Clay_PointerOver(buttonId) &&
                   pointerInfo.state == CLAY_POINTER_DATA_PRESSED;

  // Up
  bool isReleased = Clay_PointerOver(buttonId) &&
                    pointerInfo.state == CLAY_POINTER_DATA_RELEASED;

  // 根据按钮状态选择颜色
  Clay_Color buttonColor =
      isPressed ? DarkenColor(backgroundColor, 0.7f) : backgroundColor;
  if (isPressed && on_click != NULL && !isClicked) {
    on_click();
    isClicked = true;
    lastPressedId = buttonId;
  }

  if (isReleased && lastPressedId.id == buttonId.id) {
    isClicked = false;
    lastPressedId = (Clay_ElementId){0};
  }

  CLAY(
      {.id = buttonId,
       .layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(40)},
                  .padding = CLAY_PADDING_ALL(10),
                  .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
       .backgroundColor = buttonColor,
       .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
    CLAY_TEXT(text, CLAY_TEXT_CONFIG({.fontId = 0,
                                      .fontSize = 14,
                                      .textColor = {255, 255, 255, 255}}));
  }
}

void HeaderComponent(Clay_String title) {
  CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(80)},
                   .padding = {20, 20, 0, 0},
                   .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = PRIMARY_COLOR}) {
    CLAY_TEXT(title, CLAY_TEXT_CONFIG({.fontId = 0,
                                       .fontSize = 28,
                                       .textColor = {255, 255, 255, 255}}));
  }
}

void ResponsiveCardGrid() {
  // 根据窗口宽度决定卡片布局方式
  Clay_Context *context = Clay_GetCurrentContext();
  int windowWidth = (int)context->layoutDimensions.width;

  // 在小屏幕上使用垂直布局，在大屏幕上使用水平布局
  Clay_LayoutDirection direction =
      (windowWidth < 768) ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT;
  uint16_t gap = (windowWidth < 768) ? 15 : 20;

  CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(180)},
                   .layoutDirection = direction,
                   .childGap = gap}}) {
    // 在小屏幕上卡片占满宽度，大屏幕上各占一半
    Clay_SizingAxis cardWidth =
        (windowWidth < 768) ? CLAY_SIZING_GROW(0) : CLAY_SIZING_PERCENT(0.5f);

    CLAY({.layout = {.sizing = {cardWidth, CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(15)},
          .backgroundColor = CARD_COLOR,
          .cornerRadius = CLAY_CORNER_RADIUS(10)}) {
      CLAY({.layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(24)},
                .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY_TEXT(
            CLAY_STRING("响应式设计"),
            CLAY_TEXT_CONFIG(
                {.fontId = 0, .fontSize = 18, .textColor = PRIMARY_COLOR}));
      }
      CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                       .childGap = 8}}) {
        CLAY_TEXT(CLAY_STRING("当前窗口宽度:"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = 0, .fontSize = 14, .textColor = TEXT_COLOR}));
        CLAY_TEXT(
            Clay__IntToString(windowWidth),
            CLAY_TEXT_CONFIG(
                {.fontId = 0, .fontSize = 14, .textColor = ACCENT_COLOR}));
      }
    }

    CLAY({.layout = {.sizing = {cardWidth, CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(15)},
          .backgroundColor = {127, 127, 255, 255},
          .cornerRadius = CLAY_CORNER_RADIUS(10)}) {
      CLAY({.layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(24)},
                .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY_TEXT(
            CLAY_STRING("自适应布局"),
            CLAY_TEXT_CONFIG(
                {.fontId = 0, .fontSize = 18, .textColor = PRIMARY_COLOR}));
      }
      CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                       .childGap = 8}}) {
        CLAY_TEXT(CLAY_STRING("布局方向:"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = 0, .fontSize = 14, .textColor = TEXT_COLOR}));
        CLAY_TEXT(
            (direction == CLAY_LEFT_TO_RIGHT) ? CLAY_STRING("横向")
                                              : CLAY_STRING("纵向"),
            CLAY_TEXT_CONFIG(
                {.fontId = 0, .fontSize = 14, .textColor = ACCENT_COLOR}));
      }
    }
  }
}
