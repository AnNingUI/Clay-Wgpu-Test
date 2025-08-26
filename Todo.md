renderer: 通过 WebGPU 对接 Clay 的 渲染指令
clay_webgpu: 对接 Clay 的 WebGPU 资源分配与清理（包括对GLFW的对接）
components: 与运行时无关的纯 Clay 组件
main: 主程序入口

对于依赖问题

stb系列只可以被 renderer依赖
glfw 只能被 main和 Runtime 依赖