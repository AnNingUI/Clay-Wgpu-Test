## TODO
1. NodeColorEmoji 无法 渲染 Emoji

- FILE: src/main.c
- CODE-LINE: 271
```c
  Clay_WebGPU_LoadFont(ctx->clayRenderer, "assets/fonts/NotoColorEmoji.ttf", 16);
```

- FILE: src/components/components.c
- CODE-LINE: 119
```c
    ButtonData menuButton = {.text = CLAY_STRING("❤️"), // ❤️
```