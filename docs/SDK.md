# x86-64 应用接口与图形桌面（开发版）

Nuvora 的 x64 应用接口现在有公开头文件 `include/nv/abi.h`、
`include/nv/sdk.h` 和 `include/nv/gfx.h`。外部程序可以编译为独立的
ELF64，使用文件/进程调用、`NV_KEY` 键盘事件、`NV_SUB_DISPLAY` 像素绘制
和 `NV_SUB_INPUT` 鼠标事件、`NV_SUB_AUDIO` PCM 音频输出。
`user/desktop.c` 是实际使用这些接口的桌面程序。它是文件浏览器的第一版，
不是多窗口合成器，也不能运行 Linux、Windows 或 Qt/KDE 应用。

## 兼容约定

- ABI 仍为 `NV_ABI_VERSION=1`；0–30 号调用及旧结构布局不变。新增
  `NV_DEVCTL` 子系统 4（显示）、5（输入）和 6（音频），不改变旧程序调用行为。新增功能以子系统和操作号
  扩展；结构改变时应新增操作或提高对应子系统的 `api_version`，不能覆盖
  旧字段。`NV_INFO.abi` 与 `NV_DISPLAY_INFO.api_version` 分别查询核心 ABI
  和显示协议，应用必须查询后使用。
- x86-64 入口为 `int user_main(const char *args)`；启动器在 `RBX` 传递
  参数字符串，应用返回退出码。系统调用执行 `int 0x81`，EAX 为操作号，
  EBX/ECX/EDX 为三个 32 位参数，EAX 返回有符号值。负值是 `-NV_E...`。
  用户指针必须在当前进程映射的 1–2 GiB 区间，不接受 64 位高地址。
- 程序使用静态 ELF64，没有动态链接器、POSIX socket/libc、共享库、
  进程间窗口协议或用户态直接显存映射。新应用先以源码加入 `APPS`，
  通过 `scripts/mkarchive.py` 随内核映像嵌入 `/apps/`；当前数据文件系统
  不支持直接安装和执行任意外部 ELF。
- 对 ARM64 仍只有独立 QEMU `virt` 移植基线，尚无此应用 ABI。

## 像素显示

```c
struct nv_display_info info;
int r = nv_display_info(&info); /* 无受支持固件像素帧缓冲时返回 -NV_ENODEV */
if (r == 0 && info.api_version == NV_DISPLAY_API_VERSION) {
    r = nv_display_acquire();  /* 单一进程独占；占用返回 -NV_EBUSY */
    /* 填充应用私有像素数组后提交矩形，结束时释放。 */
    nv_display_release();
}
```

`nv_display_present` 接受固定 24 字节 `nv_display_present`：

| 字段 | 单位 / 约束 |
| --- | --- |
| `x, y, width, height` | 目标像素矩形；全在当前模式内，宽高必须非零 |
| `stride` | 输入每行跨度（字节），至少 `width * 4` |
| `pixels` | 用户地址，覆盖最后一行的全部像素；内核先验证所有可读页 |

每次实际复制的像素不超过 `info.max_copy_bytes`（目前 1 MiB），
整个画面用多个矩形或水平条提交；用户缓冲区保持私有，不能直接访问 GOP
显存。`format` 为 `NV_DISPLAY_BGRX8` 时每个小端 `u32` 为 `0x00RRGGBB`，
`NV_DISPLAY_RGBX8` 时为 `0x00BBGGRR`；`nv_display_rgb` 将 `0xRRGGBB`
转换为对应字值。没有硬件加速或分辨率切换。租约与旧的 `NV_SURFACE`
字符屏互斥：未取得租约提交返回 `-NV_EACCESS`；`EXIT` 和成功 `EXEC`
会自动归还租约及恢复先前字符控制台。

UEFI GOP 在内核中以 supervisor-only 映射，最长 64 MiB；该映射
位于独立 PML4 区间，不占用原先低地址的 8 MiB RAM。只映射有效
帧缓冲页。BIOS 没有有效像素帧缓冲时 `desktop` 返回到 Loom；
固件给出过大画面、未知像素格式或不可映射地址时同样不开放显示接口。

## 鼠标输入

USB xHCI 上的 Boot Protocol 鼠标通过 `NV_SUB_INPUT=5` 提供相对坐标。
`nv_input_info(&info)` 返回 16 字节，`api_version=1`，`pointer_devices`
为当前可用设备数（现为 0 或 1）。`nv_pointer_poll(&event)` 也写入
16 字节：有事件返回 1，无事件返回 0；只有像素屏租约持有者可读取，
否则返回 `-NV_EACCESS`。`dx`、`dy` 为有符号相对移动，`wheel` 目前为 0；
`buttons` 的低三位分别是左/右/中键。按下、释放和移动均保留为独立事件；
缓冲满时丢弃最旧事件。获取或释放显示租约会清空待处理事件。
接口号与结构定义见 `include/nv/abi.h`，包装函数见 `include/nv/sdk.h`。

## 应用示例

`examples/pixel_demo.c` 演示查询、获取、绘制和释放。使用项目的
GCC/binutils 工具链（无 libc、无 red zone），先 `make -j4`，然后：

```sh
gcc -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -ffreestanding \
  -fno-builtin -fno-pie -fno-pic -fno-stack-protector -std=c11 -O2 \
  -Iinclude -c examples/pixel_demo.c -o build/x86_64/user/pixel_demo.o
ld -m elf_x86_64 --gc-sections -z max-page-size=4096 -T user/linker64.ld \
  -o build/x86_64/apps/pixel_demo.elf build/x86_64/user/pixel_demo.o \
  build/x86_64/user/start64.o
```

要让它出现在 `/apps`，先复制示例为 `user/pixel_demo.c`，再把
`pixel_demo` 加到 Makefile 的 `APPS` 后重新 `make`。
如果应用使用绘图字体和字符串功能，可一同链接 `common/string.o`。

## PCM 音频

```c
struct nv_audio_info audio;
if (nv_audio_info(&audio) == 0 &&
    audio.api_version == NV_AUDIO_API_VERSION && audio.outputs) {
    /* pcm 包含 48 kHz、16-bit little-endian、交错双声道数据；
       一次最多 3072 字节，长度必须为 4 的倍数。 */
    int written = nv_audio_write(pcm, bytes);
}
```

`NV_SUB_AUDIO=6` 的 INFO 输出 24 字节：版本、输出数量（0 或 1）、
采样率 48000、声道数 2、`NV_AUDIO_S16LE` 和单次最大写入 3072 字节。
WRITE 接受 `{u32 pixels, u32 bytes}` 8 字节请求，把用户 PCM 复制到受保护的
DMA 页，播放完再返回写入字节数。空设备返回 `-NV_ENODEV`；不完整帧、
零字节或超长请求返回 `-NV_EINVAL`；坏用户地址返回 `-NV_EFAULT`。
接口没有录音、混音、音量控制、设备切换和异步缓冲契约。
`wave FILE.wav` 播放符合此格式的 RIFF/WAVE PCM 文件；`wave --test`
产生一秒 440 Hz 测试音。图形 `media` 应用另用 minimp3 和 pl_mpeg
解码 MP3 与 MPEG-1/MP2，再写入同一 PCM 接口；其代码示例见
`user/media.c`。文件系统的普通文件上限为 4 MiB。
控制器、模拟和实机边界见 [DEVICES.md](DEVICES.md)。

## 桌面入口和后续边界

在 Loom 输入 `desktop`。USB Boot 鼠标可单击选中文件、双击进入文件夹
或打开 `.txt`/`.nvd` 文档及 `.wav`/`.mp3`/`.mpg`/`.mpeg` 媒体；也可以用方向键选择文件，Enter 进入文件夹，
以 Folio 打开文档或以 Media 播放媒体；Backspace 返回上级；1–4 进入已挂载的 C:–F:，
5–7 分别进入系统根目录、应用和临时目录；
F1 显示操作说明，F2 创建空白文档，F3 打开 Media，F10 打开开始菜单，
F5 刷新目录，F6 将数据盘快照保存，Esc 返回 Loom。桌面在启动 Folio/Media 前归还像素屏，子程序退出后重新获取；
不同程序始终不能同时直接写屏幕。

当前没有触控、多个应用窗口、合成服务、Unicode 字体、
剪贴板协议。多窗口的下一步是定义用户态窗口消息和进程间
通信，由桌面进程统一合成；不应把任意应用的 GPU/MMIO 写权限放入
这个 ABI。实机 GOP、不同显卡固件和高分辨率显示尚未验证。
