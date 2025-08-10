- [] 解决 WSL N卡渲染失败问题
```bash
$ glxinfo -B
```
```bash
name of display: :0
display: :0  screen: 0
direct rendering: Yes
Extended renderer info (GLX_MESA_query_renderer):
    Vendor: Mesa (0xffffffff)
    Device: llvmpipe (LLVM 19.1.1, 256 bits) (0xffffffff)
    Version: 25.0.7
    Accelerated: no
    Video memory: 7560MB
    Unified memory: yes
    Preferred profile: core (0x1)
    Max core profile version: 4.5
    Max compat profile version: 4.5
    Max GLES1 profile version: 1.1
    Max GLES[23] profile version: 3.2
Memory info (GL_ATI_meminfo):
    VBO free memory - total: 0 MB, largest block: 0 MB
    VBO free aux. memory - total: 6717 MB, largest block: 6717 MB
    Texture free memory - total: 0 MB, largest block: 0 MB
    Texture free aux. memory - total: 6717 MB, largest block: 6717 MB
    Renderbuffer free memory - total: 0 MB, largest block: 0 MB
    Renderbuffer free aux. memory - total: 6717 MB, largest block: 6717 MB
Memory info (GL_NVX_gpu_memory_info):
    Dedicated video memory: 0 MB
    Total available memory: 7560 MB
    Currently available dedicated video memory: 0 MB
OpenGL vendor string: Mesa
OpenGL renderer string: llvmpipe (LLVM 19.1.1, 256 bits)
OpenGL core profile version string: 4.5 (Core Profile) Mesa 25.0.7-0ubuntu0.24.04.1
OpenGL core profile shading language version string: 4.50
OpenGL core profile context flags: (none)
OpenGL core profile profile mask: core profile

OpenGL version string: 4.5 (Compatibility Profile) Mesa 25.0.7-0ubuntu0.24.04.1
OpenGL shading language version string: 4.50
OpenGL context flags: (none)
OpenGL profile mask: compatibility profile

OpenGL ES profile version string: OpenGL ES 3.2 Mesa 25.0.7-0ubuntu0.24.04.1
OpenGL ES profile shading language version string: OpenGL ES GLSL ES 3.20
```

```bash
$ ./run.sh       
```
```bash
Aborted (core dumped)
```

Log
```txt
thread '<unnamed>' panicked at src/lib.rs:598:5:
Error in wgpuSurfaceConfigure: Validation Error

Caused by:
  Invalid surface

note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace
fatal runtime error: failed to initiate panic, error 5
```