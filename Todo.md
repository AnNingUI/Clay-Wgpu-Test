# WebGPU 适配 Clay 前端渲染系统重构计划 - 解决 Z-index 透明渲染问题

## 核心问题

1. 顶层 Z-Index 元素透明时无法显示被遮挡的非矩形着色器
2. 同一 Z-index 层级下浮动元素对文字和图片无遮挡效果
3. 同一 Z-index 层级下子元素背景受父元素 alpha 值影响

## 解决方案

- 实现 RenderLayer 层级管理，将 Z-index 层级的元素分组，并按照顺序渲染，解决 Z-index 透明渲染问题。
- 实现融合渲染技术，将矩形上的文字与图片统一到唯一一个 shader 中处理，对于单独没有父矩形元素的文字与图片则默认与当前 Layer 的背景矩形融合

## 注意:

1. 修改时需要严格注意类型问题

2. 修改时需要严格遵顼第三方库类型
   外部第三方库:

- **clay.h**: "include/clay.h"
- **std-image**: "include/stb_image.h"
- **std-truetype**: "include/stb_truetype.h"

- **webgpu-native**:
- - **webgpu\webgpu.h** : "D:\Software\Dev\msys64\ucrt64\include\webgpu\webgpu.h"
- - **webgpu\wgpu.h** : "D:\Software\Dev\msys64\ucrt64\include\webgpu\wgpu.h"

include 文件夹未第三方库目录，按照开闭原则，请勿在此增删改文件

在执行任务前必须向将 这些第三方库 通过记忆存入永久上下文中，以免出现类型字段错误

**GLFW**

3. 严格遵守不准简化实现，不准使用占位符代替的规则

4. 修改 runtime 时，需要将所有用于渲染的资源准备好再 Clay_WebGPU_Render

5. 不需要考虑向后兼容性，新版用不到的代码直接删除

6. 要将关注点分离，每个文件不要产生过渡依赖，每个文件只负责自己的事情
   [text, image, rect] -> [layer, Add-Shader] -> [renderer] -> [Runtime-With-GLFW] -> [main]

7. 本项目基于 zig 编译

```bash
# 编译
zig build

# 运行
./zig-out/bin/x86_64-windows-clay_webgpu.exe
```

8. WebGPU 是异步并发渲染的，所以需要将数据收集后一次性并发渲染，而不是逐个渲染，不然永远只会渲染最后一个元素

   - 所以不可以选择直接渲染某个元素，而是直接渲染某个层

9. 每次添加或删除一个函数或结构体字段时，都要注意依赖关系的清理与创建，语法要符合 C99 标准

   - 每次添加新文件或删除文件都需要在`build.zig`中标注一下
   - 对文件的依赖关系进行清理与创建后要重新编译进行语法检查，通过才可以进行下一步

10. 为了防止循环依赖可以创建一个 `src/type/type_map.h` 文件，将所有类型迁移到这个头文件中共享

11. 本项目开发使用 Shell 为 "Powershell", 请注意命令格式，不要出现 `&&` 这样的链接多条命令方式

12. WGSL 代码需要严格符合 WebGPU 规范，对接 WGSL 也需要严格检查资源分配与清理

13. 在融合渲染中要求

- 完整实现参级渲染
- 完整实现字体渲染
- 完整实现图形渲染
- 完整处理颜色混合
- 完整处理文字颜色
- 完整处理图片颜色

14. 因为需要融合渲染，所以需要删除现在所有的 shader，来确保唯一 shader

15. 不要出现 TODO , 未实现占位等等 忽悠我的 注释

- 要分析整个完整架构，先确定 那些函数，那些结构体在哪个文件，确定下来就不可以更改，不可以添加与删除
  这样就可以做到没有重复实现与未实现函数，做到声明即可直接实现
