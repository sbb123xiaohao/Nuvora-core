# x86-64 应用接口与图形桌面（开发版）

Nuvora 的 x64 应用接口现在有公开头文件 `include/nv/abi.h`、
`include/nv/sdk.h` 和 `include/nv/gfx.h`。外部程序可以编译为独立的
ELF64，使用文件/进程调用、`NV_KEY` 键盘事件、`NV_SUB_DISPLAY` 像素绘制
和 `NV_SUB_INPUT` 鼠标事件、`NV_SUB_AUDIO` PCM 音频输出及 `NV_SUB_WINDOW` 窗口事件。
`user/desktop.c` 是实际使用这些接口的桌面程序，已在同一进程内合成文件管理、
纯文本编辑；`NV_SUB_WINDOW` 将独立进程的画面加入同一桌面。Media、Folio 和
完整终端使用该接口。Linux、Windows 或 Qt/KDE 二进制仍不兼容。

## 兼容约定

- ABI 为 `NV_ABI_VERSION=2`；0–33 号调用及旧二进制结构布局不变。新增
  `NV_DEVCTL` 子系统 4（显示）、5（输入）、6（音频）和 7（窗口），不改变旧程序调用行为。新增功能以子系统和操作号
  扩展；结构改变时应新增操作或提高对应子系统的 `api_version`，不能覆盖
  旧字段。`NV_INFO.abi` 与 `NV_DISPLAY_INFO.api_version` 分别查询核心 ABI
  和显示协议，应用必须查询后使用。
- x86-64 入口为 `int user_main(const char *args)`；启动器在 `RBX` 传递
  参数字符串，应用返回退出码。系统调用执行 `int 0x81`，RAX 为
  `NV_CALL_NATIVE | 操作号`，RBX/RCX/RDX 为三个完整 64 位参数，RAX
  返回有符号 64 位值。负值是 `-NV_E...`。用户指针须处于当前进程的有效
  映射；新应用映像从 4 GiB 起、堆从 64 GiB 起、栈在低半规范地址顶部。
- 程序使用符合本项目 ABI 的静态 ELF64，没有动态链接器、POSIX
  socket/libc、共享库或用户态直接显存映射。可把源码加入 `APPS`，
  通过 `scripts/mkarchive.py` 随内核映像嵌入 `/apps/`；符合加载器约束的
  ELF 也可复制到数据卷后用 `NV_SPAWN`/`NV_EXEC` 运行。加载器会临时读取
  分页或磁盘文件，分配失败不修改原文件；没有通用软件包安装器。
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

`nv_display_present` 使用 `NV_DISPLAY_PRESENT64`，接受 32 字节的
`nv_display_present`（u32 reserved 必须为零，pixels 为 u64）。旧操作
`NV_DISPLAY_PRESENT` 仍接受 24 字节的 `nv_display_present32`：

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

USB xHCI 上的 Boot Protocol 鼠标通过 `NV_SUB_INPUT=5` 提供相对坐标；
QEMU 图形窗口使用 USB Tablet，提供绝对坐标。
`nv_input_info(&info)` 返回 16 字节，`api_version=1`，`pointer_devices`
为当前可用设备数（现为 0 或 1）。`nv_pointer_poll(&event)` 也写入
16 字节：有事件返回 1，无事件返回 0；只有像素屏租约持有者可读取，
否则返回 `-NV_EACCESS`。`buttons` 的低三位分别是左/右/中键；最高位
`NV_POINTER_ABSOLUTE` 表示 `dx`、`dy` 是 0–32767 的绝对坐标，
应用应将其缩放至显示尺寸。不带标记时是有符号相对位移。
`wheel` 目前为 0。连续同按键状态的移动合并，按键变化保留独立事件；
缓冲满时丢弃最旧事件。获取或释放显示租约会清空待处理事件。
接口号与结构定义见 `include/nv/abi.h`，包装函数见 `include/nv/sdk.h`。

## 应用示例

`examples/pixel_demo.c` 演示查询、获取、绘制和释放。使用项目的
GCC/binutils 工具链（无 libc、无 red zone），先 `make -j4`，然后：

```sh
gcc -m64 -mcmodel=large -mno-red-zone -mgeneral-regs-only -ffreestanding \
  -fno-builtin -fno-pie -fno-pic -fno-stack-protector -std=c11 -O2 \
  -Iinclude -c examples/pixel_demo.c -o build/x86_64/user/pixel_demo.o
ld -m elf_x86_64 --gc-sections -z max-page-size=4096 -T user/linker64.ld \
  -o build/x86_64/apps/pixel_demo.elf build/x86_64/user/pixel_demo.o \
  build/x86_64/user/start64.o
```

要让它出现在 `/apps`，先复制示例为 `user/pixel_demo.c`，再把
`pixel_demo` 加到 Makefile 的 `APPS` 后重新 `make`。
如果应用使用绘图字体和字符串功能，可一同链接 `build/x86_64/user/string.o`，
它使用与高地址应用一致的 large code model。

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
SDK 使用 WRITE64 的 `{u64 pixels, u32 bytes, u32 reserved}` 16 字节请求，
reserved 必须为零；旧 WRITE 仍接受 `nv_audio_write32` 的 8 字节布局。
内核把用户 PCM 复制到受保护的
DMA 页，播放完再返回写入字节数。空设备返回 `-NV_ENODEV`；不完整帧、
零字节或超长请求返回 `-NV_EINVAL`；坏用户地址返回 `-NV_EFAULT`。
`nv_audio_get_volume(&level)` 和 `nv_audio_set_volume(0..100)` 控制全局 PCM
输出衰减；100 保留原样，0 静音；它作用于所有使用该 PCM 接口的程序，
不改变用户缓冲区。超出范围返回 `-NV_EINVAL`。
接口没有录音、多音源混合、按应用音量、设备切换和异步缓冲契约。
`wave FILE.wav` 播放符合此格式的 RIFF/WAVE PCM 文件；`wave --test`
产生一秒 440 Hz 测试音。图形 `media` 应用另用 minimp3 和 pl_mpeg
流式解码 MP3、MP2、FLAC、WAV/RF64 与 MPEG-1/MP2，再写入同一 PCM 接口；其代码示例见
`user/media.c`。NVSTORE3 文件内容按磁盘块存储，旧盘仍受原快照格式限制。
控制器、模拟和实机边界见 [DEVICES.md](DEVICES.md)。

## IPv4 网络

`NV_DEVCTL` 的 `NV_SUB_NET` 面向驱动与小型用户程序，地址字段以网络字节序的
32 位整数表示 IPv4。`NV_NET_INFO` 查看接口，`NV_NET_DHCP` 或
`NV_NET_STATIC` 配置地址。`NV_NET_UDP_SEND/RECV` 提供已有单包 UDP 接口；
新 `NV_NET_PING` 用 `struct nv_net_ping` 主动发 echo，并在后续调用返回
`1` 时带回 TTL 与回复字节数。`NV_NET_TCP_OPEN/SEND/RECV/CLOSE` 使用
`struct nv_net_tcp`，最多复制 `NV_NET_DATA_MAX`（1024）字节：OPEN 返回
`0` 表示握手尚未完成、`1` 表示已连接；RECV 返回 `-NV_EAGAIN` 表示暂无
数据、`0` 表示正常结束、正数表示本次读取长度。连接归当前进程所有，
进程退出或 exec 时会清理；目前系统只允许一个活动 TCP 连接。

Loom 的 `ping` 和 `wget` 使用这些调用；域名解析由应用使用 DNS/UDP 完成。
这是轮询、有限窗口的实验接口，尚无 TLS、IPv6、POSIX socket 或通用
网络服务。具体设备与联机验证边界见 [NETWORK.md](NETWORK.md)。

## 桌面入口和后续边界

UEFI GOP 有效时默认打开 `desktop`，先显示首次设置/登录页，登录后显示空桌面；
在 Loom 中也可输入 `desktop`。界面用 DejaVu Sans/Sans Bold，编辑区用 Sans Mono；灰度字形在构建前
已生成到 `user/desktop_font.h`，渲染时用 8 位覆盖率做软件抗锯齿，
普通构建不依赖宿主字体或 Pillow；内核字符终端仍用原有点阵字体。
Files 与 Text Editor 保留在桌面进程中；Terminal、Media、Folio 是独立进程。
标题栏拖动、边缘缩放、最小化、最大化和任务栏操作由桌面处理。
Win/Super 或 F10 打开 Start；Win-Tab 或 F12 打开窗口总览。
Alt-Tab 按最近使用顺序选择窗口，松开 Alt 提交，Shift 反向、Esc 取消。
Win-左右平铺，Win-上最大化，Win-下恢复（自由窗口则最小化）；
Alt-F4 请求关闭，Alt-F9/F10 最小化和最大化。
Files 新建、改名与删除；删除确认需要点击 Delete 或按 D。
纯文本编辑器单文档仍为 128 KiB ASCII；Folio 使用原 80×25 文档模型并在
像素窗口里渲染，关闭时可保存、丢弃或继续编辑。终端运行完整 Loom 命令集，
通过继承的 stdin/stdout/stderr 接收子进程输出；`NV_CTL_CLEAR` 只清除该终端。
原生窗口应用可使用 `NV_CTL_SYNC` 保存数据卷。

### 键盘修饰键（0.12.0）

`NV_KEY` 的低 12 位仍为按键码，`NV_KEY_META=65536` 表示 Win/Super。
`NV_KEY_MODIFIERS=320` 表示修饰键状态改变；高位携带改变后的
Shift/Ctrl/Alt/Meta 状态，按下和松开都会产生事件，`NV_KEY_DIRECT`
区分完整硬件事件与普通串口字节。PS/2 与多个 USB Boot 键盘的状态合并；
USB 移除也会更新状态。字符 stdin 跳过修饰键事件和 Alt/Meta 组合。

桌面先处理这些事件和全局快捷键。Start、总览、切换器显示时暂停
客户端键盘焦点，接管鼠标并释放已有捕获；关闭覆盖界面后恢复焦点。
应用仍可发布新画面，总览读取完整的已提交画面，最小化窗口使用最近
可用的缓存。当前搜索限于注册应用及其 ASCII 名称/关键词，不执行查询文本。

## 原生窗口协议（0.11.0）

`NV_DEVCTL` 的子系统 7，版本 `NV_WINDOW_API_VERSION=1`。这是 Nuvora 原生
协议，尚不兼容 Wayland/X11。桌面仍是软件合成器，使用 GOP 显示，不提供 GPU
加速、Unicode 字库、剪贴板或通用触控。

| 操作 | 请求 | 行为 |
| --- | --- | --- |
| INFO | `nv_window_info` | 版本、当前服务 PID、窗口数与单次复制上限 |
| SERVER_ACQUIRE / RELEASE | 空指针 | 持有显示租约的桌面注册或释放服务 |
| CREATE | `nv_window_create` | 标题、内容尺寸；输出 id 和初始配置序号 |
| BEGIN | `nv_window_frame` | 按当前配置序号和尺寸准备后缓冲 |
| UPLOAD64 | `nv_window_pixels` | 上传整行矩形，每次最多 1 MiB；支持 stride 与 u64 pixels |
| COMMIT | `nv_window_id` | 全部行到齐才交换前后缓冲，生成新的画面代数 |
| POLL | `nv_window_event` | 配置、焦点、键盘、窗口本地鼠标坐标和关闭请求 |
| DESTROY | `nv_window_id` | 释放窗口及其页向量 |
| ENUM / READ64 | `nv_window_entry` / `nv_window_pixels` | 仅服务可枚举并复制已提交画面 |
| CONFIGURE / SEND | `nv_window_configure` / `nv_window_event` | 仅服务可更改内容尺寸、可见/焦点状态并投递输入 |
| BIND_STDIO / TEXT_READ | `nv_window_id` / `nv_window_text` | 窗口进程绑定继承的字符控制台；仅服务读取字符表面 |

原生 `nv_window_pixels` 为 40 字节，reserved 为零；旧 UPLOAD/READ 保留
32 字节的 `nv_window_pixels32`。旧源码若直接使用旧操作号，应使用对应
32 类型；新 SDK 的上传/读取函数自动选择 64 位操作。

应用先查询服务并创建窗口；没有桌面时可以沿用独占显示接口。
应用提交 `BEGIN -> UPLOAD（整行分块）-> COMMIT`。缺行的提交返回 EINVAL，
缩放使旧配置的提交返回 EAGAIN，应处理最新 CONFIGURE 后重画。桌面 READ
携带 ENUM 得到的 generation；复制期间若画面变化返回 EAGAIN，必须重试整个
快照，不能显示已复制的一部分。像素格式遵循 DISPLAY_INFO。

前、后缓冲通过独立物理页分配，避免依赖大块连续 RAM；分配失败保留前缓冲。
客户端不能读取或投递其他窗口的消息。退出或 exec 自动销毁其窗口；服务退出
销毁服务的全部窗口。输入队列合并同一按钮状态下的鼠标移动，保留按下/释放
转换；关闭、配置和焦点状态不占用普通输入队列。键盘只发给焦点窗口，鼠标按下
后捕获到释放；移动到窗口外时本地坐标可为负数。

SDK 的 `nv_window_*` 函数提供基本调用，`user/window_client.h` 提供事件适配，
`user/media.c` 是独立像素应用示例；`user/text_window.h` 展示字符程序的适配。


## 64 位文件接口（0.10.0）

ABI 1 追加调用号，旧编号及旧结构布局保持不变：

| 调用 | 参数 | 返回 |
| --- | --- | --- |
| NV_SEEK64 | ebx=fd, ecx=nv_seek64*, edx=0 | 0 或负错误，position 输出新 u64 位置 |
| NV_STAT64 | ebx=fd, ecx=nv_stat64*, edx=0 | 0 或负错误，size/allocated/kind |
| NV_LIST64 | ebx=path, ecx=index, edx=nv_dirent64* | 1 条目、0 结束、负错误 |

`nv_seek64` 含 offset:i64、position:u64、origin:u32、reserved:u32；origin 为
0/1/2（起点/当前位置/末尾），reserved 必须为零。SDK 提供
`nv_seek_file64`、`nv_stat_file64`、`nv_list_dir64`，偏移不受旧寄存器宽度限制。
`nv_stat64.allocated` 在新格式表示分配块字节数，旧格式表示驻留文件缓冲大小，
不表示卷空闲空间。旧 SEEK 超过 INT_MAX 返回 E2BIG；旧 LIST 大小饱和到 UINT_MAX。
`NV_VOLUME.snapshot_limit=0` 表示 NVSTORE3 全卷数据区，非“零容量”。
READ/WRITE 仍每次最多 16384 字节，可循环处理任意可表示的大文件。
格式、迁移与示例见 [STORAGE-0.10.md](STORAGE-0.10.md)。

## 账户与会话

`NV_SUB_ACCOUNT=8` 由内核维护用户身份、持久密码散列和私有目录访问控制。
应用仅查询公开的 INFO/LIST，登录与修改由 PID 1 或可信桌面管理。
结构、异步派生协议及权限边界见 [ACCOUNTS.md](ACCOUNTS.md)。
