const std = @import("std");

// Although this function looks imperative, note that its job is to
// declaratively construct a build graph that will be executed by an external
// runner.
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    var name = std.ArrayList(u8).init(b.allocator);

    if (target.query.cpu_arch) |arch| {
        name.appendSlice(arch.genericName()) catch |err| {
            std.debug.panic("Failed to append arch name: {}", .{err});
        };
    } else {
        name.appendSlice("x86_64") catch |err| {
            std.debug.panic("Failed to append default arch name: {}", .{err});
        };
    }

    const exe = b.addExecutable(.{ .name = name.items, .target = target, .optimize = optimize });

    const cFiles = [_][]const u8{ "src/main.c", "src/DEV.c", "src/renderer/renderer.c", "src/renderer/text_renderer.c", "src/components/components.c" };

    const cFlags = [_][]const u8{
        "-std=c99",
        // UTF-8 编码
        "-D_UNICODE",
        "-DUNICODE",
    };

    exe.addCSourceFiles(.{
        .files = &cFiles,
        .flags = &cFlags,
    });

    exe.addIncludePath(b.path("include"));

    // 根据目标操作系统调整编译配置
    const os_tag = target.result.os.tag;

    // 设置平台特定的库路径和链接库
    switch (os_tag) {
        .windows => {
            // Windows 特定配置 我的第三方 include 都放在了 ucrt64 下
            const UCRT64_LIB = std.process.getEnvVarOwned(b.allocator, "UCRT64_LIB") catch |err| switch (err) {
                error.EnvironmentVariableNotFound => "d:/Software/Dev/msys64/ucrt64/lib",
                else => std.debug.panic("Failed to get UCRT64_LIB: {}", .{err}),
            };
            exe.addLibraryPath(.{ .cwd_relative = UCRT64_LIB });

            const UCRT64_INCLUDE = std.process.getEnvVarOwned(b.allocator, "UCRT64_INCLUDE") catch |err| switch (err) {
                error.EnvironmentVariableNotFound => "d:/Software/Dev/msys64/ucrt64/include",
                else => std.debug.panic("Failed to get UCRT64_INCLUDE: {}", .{err}),
            };
            exe.addIncludePath(.{ .cwd_relative = UCRT64_INCLUDE });

            exe.linkSystemLibrary("c++");
            exe.linkSystemLibrary("glfw3");
            exe.linkSystemLibrary("wgpu_native");

            // Windows 系统库
            exe.linkSystemLibrary("gdi32");
            exe.linkSystemLibrary("user32");
            exe.linkSystemLibrary("kernel32");
            exe.linkSystemLibrary("ws2_32");
            exe.linkSystemLibrary("ole32");
            exe.linkSystemLibrary("oleaut32");
            exe.linkSystemLibrary("opengl32");
            exe.linkSystemLibrary("d3dcompiler");
            exe.linkSystemLibrary("shell32");
            exe.linkSystemLibrary("propsys");
            exe.linkSystemLibrary("runtimeobject");
            exe.linkSystemLibrary("userenv");
        },
        .linux => {
            // Linux 特定配置
            exe.linkSystemLibrary("stdc++");
            exe.linkSystemLibrary("glfw");
            exe.linkSystemLibrary("wgpu_native");

            // Linux 系统库
            exe.linkSystemLibrary("m");
            exe.linkSystemLibrary("dl");
            exe.linkSystemLibrary("pthread");
            exe.linkSystemLibrary("X11");
            exe.linkSystemLibrary("Xrandr");
            exe.linkSystemLibrary("Xinerama");
            exe.linkSystemLibrary("Xcursor");
            exe.linkSystemLibrary("Xi");
        },
        .macos => {
            // macOS 特定配置
            exe.linkSystemLibrary("c++");
            exe.linkSystemLibrary("glfw");
            exe.linkSystemLibrary("wgpu_native");

            // macOS 框架
            exe.linkFramework("Cocoa");
            exe.linkFramework("CoreVideo");
            exe.linkFramework("IOKit");
            exe.linkFramework("Metal");
            exe.linkFramework("MetalKit");
            exe.linkFramework("QuartzCore");
        },
        else => {
            // 其他系统使用默认配置
            exe.linkSystemLibrary("c++");
            exe.linkSystemLibrary("glfw3");
            exe.linkSystemLibrary("wgpu_native");
        },
    }

    exe.linkLibC();
    b.installArtifact(exe);
}
