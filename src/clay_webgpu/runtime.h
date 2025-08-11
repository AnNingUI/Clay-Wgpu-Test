#ifndef CLAY_WEBGPU_RUNTIME_H
#define CLAY_WEBGPU_RUNTIME_H

#include <webgpu/webgpu.h>
#include "../renderer/renderer.h"
#include "../renderer/image_renderer.h"
#include "clay.h"
#include <GLFW/glfw3.h>

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
  Clay_Vector2 scrollOffset;
  ImageRenderer *imageRenderer;
  void (*layoutFunc)(void* userData);
  void* userData;
} RuntimeContext;

RuntimeContext* Runtime_Init(GLFWwindow* window, uint32_t width, uint32_t height);
void Runtime_SetLayoutCallback(RuntimeContext* ctx, void (*layoutFunc)(void* userData), void* userData);
void Runtime_Run(RuntimeContext* ctx);
void Runtime_Destroy(RuntimeContext* ctx);
// Optional APIs
WGPUDevice Runtime_GetDevice(RuntimeContext* ctx);
WGPUQueue Runtime_GetQueue(RuntimeContext* ctx);

#endif