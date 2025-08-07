
#ifndef CLAY_COMPONENTS_H
#define CLAY_COMPONENTS_H
#include "clay.h"

int Clay_GetLayoutDirectionWidth(Clay_Context *ctx);

// 定义颜色常量
extern const Clay_Color PRIMARY_COLOR;    // Steel Blue
extern const Clay_Color SECONDARY_COLOR;  // Slate Blue
extern const Clay_Color ACCENT_COLOR;     // Crimson
extern const Clay_Color BACKGROUND_COLOR; // White Smoke
extern const Clay_Color CARD_COLOR;       // White
extern const Clay_Color TEXT_COLOR;       // Dark Gray

typedef void (*ButtonClickListener)(void);
typedef struct {
  Clay_String text;
  Clay_Color backgroundColor;
  Clay_ElementId buttonId;
  ButtonClickListener on_click;
} ButtonData;

Clay_Color DarkenColor(Clay_Color color, float factor);
void CardComponent(Clay_String title, Clay_String content);
void ButtonComponent(ButtonData *data);
void HeaderComponent(Clay_String title);
void ResponsiveCardGrid();

#endif