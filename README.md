# Nuvora Core 0.10.0

## 0.10.0 大文件与流式媒体

新数据盘使用 **NVSTORE3 磁盘块存储**，不再把全盘内容装进 128 MiB
快照，也不再对新盘设置 64 MiB 单文件上限。支持 64 位大小/偏移、稀疏文件、
写时复制和双元数据根恢复；文件内容分块读写，`anchor` 只提交目录与块索引。
默认创建 8 GiB 稀疏数据镜像，可用 `--disk-size` 选择更大容量。
**旧盘保留原格式，需要迁移到新镜像才能解除旧格式限制。**

Media 支持流式 MP3、MP2、FLAC、扩展 WAV/RF64 和 MPEG-1/MP2 视频，
去掉视频整份载入与 640×480 的额外限制。桌面显示 64 位容量和 GiB 等单位。
MP4/MKV 等格式可通过宿主 FFmpeg 转换导入，尚非系统内原生解码。

[使用、迁移、磁盘格式和验证记录](docs/STORAGE-0.10.md)。当前通过 19 组
宿主回归，包含 10 GiB 稀疏文件、160 MiB 连续数据、失败回滚与 720p 解码；
本轮没有 QEMU/实机验证。硬件、元数据、用户地址空间仍有明确边界。

## 实体网络开发版

x86-64 新增 Intel I225/I226 PCIe 实体有线卡与 USB CDC-ECM 收发驱动，
以及 ARP、IPv4、DHCP、UDP 和 ICMP 应答。接入刷入 Espressif USB Dongle
固件的 ESP32-S2/S3 设备时，可用 `net wifi scan`、`net wifi join SSID`
经 USB CDC 命令口配置 Wi-Fi，并通过 USB ECM 网卡获得地址。
笔记本内置 PCI Wi-Fi 仍仅识别，实体设备尚未上机验证。
支持清单、操作方式及限制见 [实体网络说明](docs/NETWORK.md)。

## GPT 分区开发版

新建 x64 数据镜像现在使用真正的 GPT 分区表，**默认一个 C: 数据分区**。
首次建盘可用 `python3 start.py --disk-size 128 --partitions 2` 创建 C: 和 D:
两个独立分区。`partitions` 查看 GPT 分区，`volumes` 查看已挂载分区；
`C:/notes` 对应兼容路径 `/home/notes`，`D:/notes` 对应 `/drives/D/notes`。
`anchor` 依次提交各盘的独立快照。旧 NVSTORE1/2 整盘镜像仍作为 C: 使用，
不会自动转换或重写分区表。

优先扫描 IDE primary master；无有效 Nuvora 数据卷时扫描首个 PCIe NVMe 控制器
的 512 字节 NVM namespace。最多列出 32 个 GPT 条目，最多挂载 4 个
带有效 Nuvora 格式头的分区。其他格式只列出，不挂载或写入。项目尚无安装器，
也不支持给已有磁盘在线缩容、分盘或挂载 NTFS/FAT。`--partitions` 仅对
**新镜像**生效。详见 [GPT 分区说明](docs/PARTITIONS.md)和
[设备支持范围](docs/DEVICES.md)。

用 C 和汇编从零编写的实验操作系统内核。x86-64 版本有 BIOS/GRUB 与 UEFI 启动、Loom 用户环境及文件和磁盘快照；ARM64 版本是独立的 QEMU `virt` 引导、内存与 EL0/NEON 运行基线。两种构建均为 64 位；不再构建 x86 32 位版本。

这是可运行、可继续开发的内核初版，**尚未达到 Linux 的完整程度**。它不能运行 Linux 应用，也不能替代日用系统。当前验证目标仍是 QEMU 的单核 PC / ARM `virt` 虚拟机；实体网络、NVMe、USB 鼠标、HDA 音频和像素桌面尚未上机测试，多核与 AI 加速器驱动等缺口在本文末尾列出。

## 开放应用接口与图形桌面（x86-64 开发版）

- 公开 `include/nv/abi.h`、`include/nv/sdk.h` 和 `include/nv/gfx.h`：保留 ABI 1 原有调用号和结构。`NV_SUB_DISPLAY` 管理 UEFI GOP 像素租约和矩形提交，`NV_SUB_INPUT` 提供相对鼠标事件，`NV_SUB_AUDIO` 提供有界 PCM 输出；用户程序不能直接映射固件显存或声卡 MMIO。
- Loom 输入 `desktop` 打开图形文件管理器：USB Boot 鼠标单击选择、双击打开，也可用方向键和 Enter。`.txt`/`.nvd` 由 Folio 打开，`.wav`/`.wave`/`.mp3`/`.mp2`/`.flac`/`.mpg`/`.mpeg` 由 Media 打开。顶栏有 Media 快捷入口；底栏 N Start 打开应用菜单，包含 Media、Files、Folio 和返回 Loom。F3 打开 Media，F10 打开开始菜单；1–4 进入 C:–F:，5–7 进入系统目录；F1 快捷键，F2 新建，F5 刷新，F6 保存，Esc 退出。启动子程序时归还屏幕租约。
- 该桌面目前没有触控、多窗口合成、Unicode 字体或 GPU 加速；BIOS 文本模式不提供像素屏。接口与构建方式见 [应用 SDK](docs/SDK.md)。

## 音频输出（x86-64 开发版）

- PCI Intel HDA 控制器可通过模拟输出 pin 到 DAC 的 codec 路径播放 48 kHz、双声道、16-bit PCM；`wave --test` 发出一秒测试音，`wave /home/sample.wav` 播放相应格式的短 WAV。无需图形桌面，BIOS 文本模式也可调用。
- 图形 Media 应用支持 MP3、独立 MP2、FLAC、PCM/float WAV 和 RF64，以及 MPEG-1 Program Stream 视频（可带 MP2 音轨）。读取和解码分块进行；没有 640×480 或 64 MiB 的播放器额外限制。Space 暂停，Esc 返回文件列表；无 HDA 可看无声视频。MP4/H.264/AAC 需要宿主转换。内核仍是同步分块 HDA，块间可能有间隙；尚无混音器、USB/蓝牙/HDMI 音频、录音或连续 DMA 环。
- QEMU 可用 `python3 start.py --uefi --audio --window` 启动图形桌面并附加 HDA 设备（先构建 `make esp`）；实际扬声器和耳机路径仍需实体声卡验证。接口、设备范围见 [SDK](docs/SDK.md) 与 [设备说明](docs/DEVICES.md)。

宿主机的媒体文件可在**虚拟机关机后**导入专用 GPT 数据镜像；不会改动启动镜像或宿主系统分区：

```sh
make -j4
make disk esp
python3 scripts/import_media.py --disk build/x86_64/nuvora-store.img song.mp3 clip.mpg
python3 start.py --uefi --audio --window
```

已有同名文件默认拒绝，传 `--replace` 才替换。导入器也接受普通数据文件和空文件；
文件名当前最多 31 字节 ASCII。存储一种格式不代表已安装对应应用。
新 NVSTORE3 盘按块导入并保留旧提交；旧 NVSTORE1/2 继续使用原有容量。
转换导入：`python3 scripts/prepare_media.py input.mp4 movie.mpg --disk build/x86_64/nuvora-store.img`。
旧盘迁移：`python3 scripts/migrate_store.py old.img new.img --size 65536`。
详见 [大文件存储与媒体](docs/STORAGE-0.10.md)。

## 0.9.0 物理页与小对象分配

- x64 和 ARM64 使用共享的分阶空闲块索引：2^order 个对齐物理页可分配、拆分和归还后合并；保留页和 DMA 低地址约束继续有效。x64 优先从 4 GiB 以上的普通 RAM 分配，ARM64 在 QEMU `virt` 上从设备树给出的可用 RAM 分配。
- 两种架构共享 16–2048 字节的小对象 slab，实现对象清零、占用验证和空页归还；x64 的 `kmalloc` 仍有可合并的固定虚拟堆作为大对象及内存紧张时的回退路径。
- 移除剩余的 i686 内核和用户态条件代码；仅保留 x64 BIOS 启动所需的 32 位汇编入口，随即切换到 64 位内核。实际机制、测试和未实现的 Linux 内存管理功能见 [0.9.0 内存管理](docs/MEMORY-0.9.0.md)。

## 0.8.0 ARM64 与计算内存

- x86-64 每进程可按页申请的用户堆由 4 MiB 扩为 **512 MiB**；用户地址窗口仍为 1–2 GiB，ABI 1 不变。大缓冲区测试在 256 MiB 虚拟机上申请并归还 64 MiB，在 1/5 GiB 虚拟机上申请并归还 256 MiB，检查页清零、边界与无泄漏回滚。
- `make ARCH=aarch64` 构建符合 ARM64 Image 引导格式的镜像；QEMU `virt` 从设备树读取 RAM 和 PL011 地址，初始化物理页所有权位图、EL1 页表与受保护的内核映射。一个隔离的 EL0 向量程序使用 NEON 计算点积并经 SVC 汇报；故意读取内核代码页会触发权限异常。64/256/1024/5120 MiB QEMU 回归均运行真正的 ARM64 机器码。
- 当前 ARM64 目标是**内核移植基线**：还没有 x64 的 ELF 加载器、Loom、抢占调度、文件/磁盘驱动和完整私有 ABI。x64 仍只有单核与 SSE 上下文保存；没有 AVX/XSAVE、多 GPU 驱动、CUDA 或可直接运行的模型训练框架。详见 [ARM64 与 AI 工作负载](docs/ARM64-AI.md)。

## 0.7.3 文件缓冲内存修复

修复快照恢复后的文件扩容可能超过 128 KiB 上限、浪费堆空间并提前返回内存不足的问题。新增真实分配器压力夹具和双架构快照恢复回归，核对失败时内容保留及删除后的完整回收。失败复现和本轮结果见 [MEMORY-0.7.3.md](docs/MEMORY-0.7.3.md)。

## 0.7.2 内存管理修复

修复连续缓冲跨 1 GiB 时内核指针不连续的问题；x64 内核堆改由分散物理页组成，避免 RAM 总量充足但缺少连续 8 MiB 时启动失败。堆映射共享给各进程且只允许内核访问，分配中途失败会全部回滚。修复前的失败复现和本轮结果见 [MEMORY-0.7.2.md](docs/MEMORY-0.7.2.md)。

## 0.7.1 修复

修复跨 4 GiB 地址对齐、用户地址空间与物理映射冲突、NX 页回收、UEFI 服务表与 PE32+ 格式、固件内存所有权和启动栈，以及低内存恢复失败后可能覆盖已有快照的问题。x64 内核堆改从可用 RAM 分配，避免固定内核映像覆盖 OVMF 的 ACPI 保留区。完整修复清单、测试结果和保留的容量边界见 [REVIEW-0.7.1.md](docs/REVIEW-0.7.1.md)。

本次只修复 Nuvora；外层原附的 Linux 压缩包保留原内容，不参与 Nuvora 的构建或链接。

## 0.7.0 UEFI、大内存与大存储

- **UEFI 启动（x64）**：自包含 EFI stub（`arch/x86_64/uefi.c`）从 ESP 读取 ELF64 内核，把 EFI 内存图、GOP 帧缓冲和配置表中的 ACPI RSDP 经中性 `boot_info` 移交内核，`ExitBootServices` 后进入长模式。stub 携带 PE32+ 基址重定位表（`.reloc`），首选基址被占用时固件可自行搬移；启动介质定位失败时会枚举全部 SimpleFileSystem 卷。帧缓冲控制台用内核内置点阵字体渲染 80×25 文本，Folio 同样可用；串口输出保持不变。`make` 产出 `BOOTX64.EFI`，`make esp` 打包 ESP 镜像，`make iso-uefi` 产出纯 UEFI ISO；`start.py --uefi` 直接以 OVMF/edk2 固件启动。
- **大内存**：x64 物理管理上限 128 MiB → **64 GiB**（前 32 MiB 4 KiB 页保护内核镜像，其余 2 MiB 大页恒等映射，物理页地址 64 位化）；旧版 i686 曾提高至 192 MiB，现已移除。USB 等设备 DMA 仍由显式掩码约束在 4 GiB 以下。1 GiB 与 5 GiB（跨 4 GiB 边界）配置纳入完整用户态回归。
- **大存储**：ATA PIO 驱动支持 **LBA48** 与 64 位 LBA/扇区计数；新 `NVSTORE2` 镜像头自带槽位几何与 u64 总扇区数，快照槽 **1 MiB → 16 MiB** 且快照缓冲按可用内存动态伸缩，数据镜像默认 64 MiB、可到 TiB 级。旧 8 MiB `NVSTORE1` 镜像继续可用。

本轮启动移交统一为 `include/nv/bootinfo.h` 的 `boot_info`；已知 RSDP 地址的 ACPI 直通解析仍全量校验签名、长度与校验和。详见 [CHANGELOG.md](docs/CHANGELOG.md)。

## 构建后运行

安装 Python 3 和 QEMU，编译相应架构后运行：

```sh
make -j4
python3 start.py
```

默认启动 x64（BIOS/GRUB 路径，256 MiB RAM）。程序会在 `build/x86_64/` 新建专用的 512 MiB 数据镜像；已有镜像会保留原有几何与内容。退出模拟器：**Ctrl+A，然后按 X**。

```sh
python3 start.py --arch aarch64 --memory 256  # ARM64 virt 引导与 EL0/NEON 自检
python3 start.py --uefi                 # x64 UEFI 启动（需要 OVMF/edk2 固件）
python3 start.py --window               # VGA/GOP 窗口
python3 start.py --audio --window       # 附加 HDA 音频和图形窗口
python3 start.py --memory 5120          # 5 GiB 虚拟机内存
python3 start.py --disk-size 1024       # 新建 1 GiB 数据镜像
python3 start.py --disk-size 128 --partitions 2  # 首次建盘创建 C:、D:
python3 start.py --uefi --window --disk-bus nvme  # 以 PCIe NVMe 接入同一 GPT 镜像
python3 start.py --cpu core2duo
python3 start.py --machine q35
python3 start.py --machine q35 --no-ecam
```

`--uefi` 需要 UEFI 固件：QEMU 自带的 `edk2-x86_64-code.fd` 或发行版 OVMF 包会被自动查找，也可用 `NV_OVMF=/path/to/OVMF_CODE.fd` 指定。UEFI 运行还需要 `build/x86_64/esp.img`（构建产出，需 mtools）。`--window` 使用图形窗口和 QEMU 键盘设备，需要宿主机的 QEMU 图形后端。默认串口模式可直接在终端使用。当前命令行支持 ASCII 和美式键盘布局，中文说明在本文件和 `docs/` 中。

Arch Linux 安装运行与构建依赖：

```sh
sudo pacman -S --needed base-devel python qemu-system-x86 qemu-system-aarch64 aarch64-linux-gnu-gcc grub xorriso mtools edk2-ovmf
```

Debian / Ubuntu：

```sh
sudo apt install build-essential binutils gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu python3 qemu-system-x86 qemu-system-arm grub-pc-bin grub-common xorriso mtools ovmf
```

源码构建脚本面向 x86-64 Linux 宿主。ARM64 需要 AArch64 GNU 交叉工具链，模拟器可由 `NV_QEMU_ARM64` 指定；x64 模拟器可由 `NV_QEMU` 指定。预编译内核可在其他宿主上由相应 QEMU 模拟运行，这些宿主未纳入本包测试。

## 启动后试一遍

```text
help
silicon
firmament
prism
forge vector
ports
ports --scan
origin
horizon
nest notes
weave notes/hello "My first Nuvora kernel"
unfold notes/hello
stitch notes/hello "Running in Ring 3"
glance notes
forge pulse
scatter spin
sparks
quench 3
gather 3
anchor
rest
```

`quench` 和 `gather` 后面的 PID 以 `scatter` 的实际输出为准。`spin` 是故意不调用系统调用的死循环，用来观察时钟抢占；`quench` 可以终止它。

**Loom 命令修改的文件需要执行 `anchor`，才能把 C:（`/home`）及其他已挂载分区保存到数据镜像。Folio 的保存和导出会调用同一提交操作。** `/tmp` 在重启后清空；上一次 `anchor` 之后未保存的修改也会丢弃。`rest` 关机，`renew` 重启，二者不会自动保存。

完整的 36 条 Loom 命令见 [命令手册](docs/COMMANDS.md)。每个命令都支持 `命令 --help`；`help` 和旧别名 `atlas` 显示总表。命令行位于 `user/loom.c`，实际运行在 Ring 3，并通过内核系统调用完成操作。输入 `folio` 可打开全文编辑器；Folio 的快捷键和文档格式见 [命令手册](docs/COMMANDS.md)。

## 构建、测试和 ISO

```sh
make -j4
make esp            # x64 测试指纹包含 ESP
make test-host      # 19 组源码边界夹具，使用 UBSan
make test
make iso            # BIOS/GRUB ISO
make esp            # UEFI ESP 镜像（需要 mtools）
make iso-uefi       # 纯 UEFI El Torito ISO
python3 scripts/test.py --iso
```

ARM64 基线（QEMU virt）：

```sh
make ARCH=aarch64 CROSS=aarch64-linux-gnu- -j4
make ARCH=aarch64 test
python3 start.py --arch aarch64 --memory 256
```

不需要宿主 libc 或第三方 C 库；源码采用 freestanding 编译和直接链接。UEFI 构建同样只用 gcc/binutils，ESP 打包需要 mtools。`make clean` 只删除当前架构的 `build/ARCH/`，其中包括该架构的数据镜像；有保存内容时先备份镜像。

每个架构的 `build/ARCH/` 中包含：

| 文件 | 用途 |
| --- | --- |
| `nuvora.elf` | 内核符号文件；x64 为 ELF64 / AMD64，ARM64 为 ELF64 / AArch64 |
| `boot.elf` | x64 Multiboot 启动容器 |
| `Image` | ARM64 QEMU `virt` 原始引导镜像 |
| `BOOTX64.EFI` | x64 UEFI stub（PE32+，由 `mkuefi.py` 从独立链接的 stub ELF 生成） |
| `esp.img` | UEFI 系统分区镜像（FAT，含 `BOOTX64.EFI` 与内核 ELF） |
| `nuvora-core-0.9.0-x86_64.iso` | x64 BIOS / GRUB 启动 ISO |
| `nuvora-core-0.9.0-x86_64-uefi.iso` | x64 纯 UEFI El Torito ISO |
| `apps/*.elf` | x64 独立用户态程序；ARM64 尚未提供 ELF 应用 |
| `test-results/` | 真实虚拟机执行日志、结果表和截图 |

**x64 的 `boot.elf` 使用 ELF32 容器承载 32 位引导入口和 64 位内核机器码。** 入口主动启用 PAE、长模式和 NX，再调用 64 位 C 内核；用户程序加载的是 ELF64。该容器用于兼容 Multiboot v1，不表示内核运行在 32 位模式。

BIOS ISO 之外，x64 另有 UEFI 启动路径：`BOOTX64.EFI` 首选基址 0x02000000 并携带基址重定位表（由 `mkuefi.py` 从 `--emit-relocs` 链接的 stub ELF 生成），固件在首选基址被占用时可自行搬移；已在 QEMU OVMF/edk2 固件上验证，尚未在实体主板固件上认证。

## 当前已实现

| 模块 | 实现范围 |
| --- | --- |
| CPU 与启动 | x64：GDT/TSS/IDT/PIC/PIT，x87/MMX/SSE 上下文隔离；ARM64：Image/DTB 启动、EL1 与 EL0 入口、NEON 自检 |
| 固件启动 | x64 Multiboot v1 BIOS/GRUB 及 UEFI stub；ARM64 为 QEMU `virt` 的 Image 启动，未提供 UEFI |
| ACPI 与 PCIe | RSDP/RSDT/XSDT/MCFG 校验解析（扫描或 EFI 直通），segment 0 ECAM 4 KiB 配置空间，CF8/CFC 回退 |
| 内存 | x64：最高 64 GiB 物理页、512 MiB 用户堆、8 MiB 内核堆、分阶页与小对象 slab、失败回滚；ARM64：设备树可用范围、最高 64 GiB、共享分阶页/slab 与 EL0 隔离 |
| x64 保护 | 四级页表、完整 64 位寄存器保存、只读代码页、堆和栈 NX |
| 进程 | x64：Ring 3 轮转抢占及完整生命周期；ARM64：一个静态 EL0 工作负载，尚无多进程调度 |
| 可执行文件 | x64 ELF64 静态程序加载、范围检查和失败回滚；ARM64 尚无 ELF 加载器 |
| 系统调用 | x64 原创 ABI 及 DEVCTL；ARM64 仅有测试用的计算结果/退出 SVC |
| 文件系统 | 512 个节点；NVSTORE3 为 64 位稀疏块文件，按需读写；旧盘和 /tmp 保留 64 MiB 限制 |
| 虚拟节点 | `/dev/null`、`/dev/zero`、`/dev/console`，`/sys` 动态状态 |
| 磁盘 | IDE 主盘 ATA PIO，或首个 PCIe NVMe 控制器的 512B namespace；GPT 分区识别；NVSTORE3 双元数据根 + 全卷文件数据区；兼容 NVSTORE1/2 旧快照 |
| 命令环境 | 36 条 Loom 命令，统一 `--help`、引号、转义、后台进程和错误反馈 |
| 文档 | Folio 全屏编辑器；`.nvd` 原生格式；RTF/Word 可读导出 |
| 显示 | BIOS 下 VGA 文本；UEFI 下 GOP 线性帧缓冲加内置点阵字体；串口常开 |
| 显卡准备 | NVIDIA / 通用 PCI display 识别、32/64 位 BAR、PCIe 扩展能力、内核专用映射准备；尚无原生 GPU 驱动 |
| USB | xHCI 描述符/Hub/热插拔、USB Boot 键盘与鼠标、CDC-ECM 和 ESP USB Dongle CDC 控制；USB 存储仍只识别 |
| 网络 | x64 Intel I225/I226 PCIe DMA、USB CDC-ECM、ARP/IPv4/DHCP/UDP/ICMP 应答；PCI Wi-Fi 仅识别 |
| 音视频 | x64 HDA 48 kHz 双声道输出；Media 流式 MP3/MP2/FLAC、WAV/RF64 与 MPEG-1/MP2；720p 宿主解码验证，未实机验证 |
| 验证 | 旧版 x64 每次 131 项 QEMU 用户态检查；当前源码预期 139 项待复验。19 组宿主源码回归通过；ARM64 旧版在 64/256/1024/5120 MiB 配置下各 15 项 |

x64 在 32、64、128、256 MiB、1 GiB 与 5 GiB 配置下执行完整内存回归；ARM64 的 EL0 自检在 64、256 MiB、1 GiB 和 5 GiB 执行。64 GiB 是两种实现各自的管理上限，并非 64 GiB 实机认证。详见 [测试说明](docs/TESTING.md)。

## 明确的限制

- 单核、单用户研究环境；内核执行期间不被抢占，没有 SMP 锁、用户账户或完整权限模型。
- 两个内核目前最多各管理 64 GiB 物理内存。x64 ABI v1 仍使用 1–2 GiB 用户地址窗口及 512 MiB 用户堆；USB DMA 固定使用 4 GiB 以下页。ARM64 只有单工作负载的移植基线，尚无 Loom、磁盘与完整 ABI。
- UEFI 仅支持 x64、BIOS 启动的 ISO 之外另有纯 UEFI El Torito ISO；都没有 Secure Boot。UEFI stub 已携带基址重定位表并在启动介质上提供多卷回退，在 QEMU OVMF/edk2 验证，未在实体主板固件认证。
- 暂无 ACPI AML/电源管理、原生 PCI Wi-Fi、TCP/IPv6、AHCI、录音与多设备音频混合、GPU 加速、多窗口桌面和 Unicode 终端；NVMe 仅支持一个 512B namespace，USB 鼠标仅支持 Boot Protocol，不能挂载 USB 存储文件系统；Folio 目前使用 ASCII，不能导入 `.docx` 或外部 `.rtf`。
- 暂无 `fork`、管道、套接字、动态链接、POSIX/Linux ABI 或通用文件系统格式支持。
- x87/MMX/SSE 状态已经隔离；AVX/XSAVE、AVX-512/AMX、多核与微码更新尚未实现。SSE #XM 递交受 QEMU TCG 限制而明确跳过，尚未通过实机验证。
- x64 文件系统共 512 个节点，内核元数据堆 8 MiB、每进程用户堆 512 MiB。NVSTORE3 的实际内容受磁盘/空闲块约束，单卷元数据区为 2 MiB，碎片化区间扫描尚待优化。旧盘和 `/tmp` 仍保留原文件上限；旧盘需另存迁移，扩大已有镜像不会自动扩容卷。没有 swap、通用页缓存、符号链接、ACL 或 Linux/Windows 文件系统兼容。
- 测试通过不构成生产级安全或可靠性证明。没有进行真实硬件、长期压力或断电时序的全面认证。

架构细节见 [ARCHITECTURE.md](docs/ARCHITECTURE.md)，系统调用见 [ABI.md](docs/ABI.md)，下一阶段范围见 [ROADMAP.md](docs/ROADMAP.md)。

## 许可与参考

Nuvora 原创源码采用 MIT。Media 集成 CC0 的 minimp3 与 MIT 的 pl_mpeg，版本与许可见 [third_party/README-media.md](third_party/README-media.md)。ISO 使用的 GRUB 2.12 属于独立第三方组件，其 GPL 许可和对应 Ubuntu 源码一并附在 `third_party/`。QEMU、编译器及其他宿主工具没有打包进来。

实现参考处理器、Multiboot 和 ELF 公开规范，链接集中在 [SOURCES.md](docs/SOURCES.md)。
