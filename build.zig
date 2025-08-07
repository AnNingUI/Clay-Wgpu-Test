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
            // 在构建脚本中，内存不足通常是致命错误，可以直接 panic
            std.debug.panic("Failed to append arch name: {}", .{err});
        };
    } else {
        name.appendSlice("x86_64") catch |err| {
            std.debug.panic("Failed to append default arch name: {}", .{err});
        };
    }

    const exe = b.addExecutable(.{ .name = name.items, .target = target, .optimize = optimize });

    const cFiles = [_][]const u8{ "src/main.c", "src/renderer/renderer.c" };

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

    // 链接 ucrt64 lib // 我这里是 msys2的 ucrt64，如果你的libwgpu_native.dll.a 在其他地方，请修改这里
    const UCRT64_LIB = std.process.getEnvVarOwned(b.allocator, "UCRT64_LIB") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => "d:/Software/Dev/msys64/ucrt64/lib",
        else => std.debug.panic("Failed to get UCRT64_LIB: {}", .{err}),
    };
    exe.addLibraryPath(.{ .cwd_relative = UCRT64_LIB });

    // 引入 ucrt64 Include // 我这里是 msys2的 ucrt64，如果你的webgpu/webgpu.h 在其他地方，请修改这里
    const UCRT64_INCLUDE = std.process.getEnvVarOwned(b.allocator, "UCRT64_INCLUDE") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => "d:/Software/Dev/msys64/ucrt64/include",
        else => std.debug.panic("Failed to get UCRT64_INCLUDE: {}", .{err}),
    };
    exe.addIncludePath(.{ .cwd_relative = UCRT64_INCLUDE });

    exe.linkSystemLibrary("c++");
    exe.linkSystemLibrary("glfw3");
    exe.linkSystemLibrary("wgpu_native"); // 添加 WebGPU 库

    // 添加 FreeType 库支持
    exe.linkSystemLibrary("freetype");

    // 添加 Windows 系统库
    exe.linkSystemLibrary("gdi32");
    exe.linkSystemLibrary("user32");
    exe.linkSystemLibrary("kernel32");
    exe.linkSystemLibrary("ws2_32"); // Windows Sockets API
    exe.linkSystemLibrary("ole32"); // COM Library
    exe.linkSystemLibrary("oleaut32"); // OLE Automation
    exe.linkSystemLibrary("opengl32"); // OpenGL
    exe.linkSystemLibrary("d3dcompiler"); // DirectX Shader Compiler
    exe.linkSystemLibrary("shell32"); // Shell API (解决GetUserProfileDirectoryW)
    exe.linkSystemLibrary("propsys"); // Property System (解决VariantToPropVariant, PropVariantToBSTR)
    exe.linkSystemLibrary("runtimeobject"); // Runtime Object (解决RoOriginateErrorW)
    exe.linkSystemLibrary("userenv"); // User Environment (解决GetUserProfileDirectoryW)

    exe.linkLibC();
    b.installArtifact(exe);
}
