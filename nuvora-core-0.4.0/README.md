# Nuvora Core 0.4.0

用 C 和汇编从零编写的实验操作系统内核，**默认运行于真正的 x86-64 长模式**，同时保留 i686 版本。包含可启动内核、独立用户态程序、自定义 Loom 命令行、文件系统和磁盘快照。

这是可运行、可继续开发的内核初版，**尚未达到 Linux 的完整程度**。它不能运行 Linux 应用，也不能替代日用系统。当前交付目标是 QEMU 的单核 PC 虚拟机；驱动、联网、多核、UEFI 等缺口在本文末尾明确列出。

## 0.4.0 核心完善

本轮加入 xHCI USB 控制器和外设识别、Hub 递归枚举、USB 键盘输入、事务式 `EXEC`、内核栈保护和 i686 双重故障应急栈，并修复未领取退出状态的进程占用内存、完整路径超限仍可创建、失败映射残留空页表等问题。版本变更和升级方式见 [CHANGELOG.md](docs/CHANGELOG.md)。

## 直接运行预编译版本

解压后进入本目录。安装 Python 3 和 QEMU，然后运行：

```sh
python3 start.py
```

默认启动 x64。程序会在 `build/x86_64/` 新建专用的 8 MiB 数据镜像；已有镜像会保留。退出模拟器：**Ctrl+A，然后按 X**。

```sh
python3 start.py --arch i686
python3 start.py --window
```

`--window` 使用 VGA 窗口和 QEMU 键盘设备，需要宿主机的 QEMU 图形后端。默认串口模式可直接在终端使用。当前命令行支持 ASCII 和美式键盘布局，中文说明在本文件和 `docs/` 中。

Arch Linux 安装运行与构建依赖：

```sh
sudo pacman -S --needed base-devel python qemu-system-x86 grub xorriso
```

Debian / Ubuntu：

```sh
sudo apt install build-essential binutils python3 qemu-system-x86 grub-pc-bin grub-common xorriso
```

源码构建脚本目前面向 x86-64 Linux 宿主。预编译内核可以由其他宿主上的 QEMU 模拟运行；这些宿主未纳入本包测试。可通过 `NV_QEMU` 环境变量指定 QEMU 可执行文件的完整路径。

## 启动后试一遍

```text
help
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

**Loom 命令修改的文件需要执行 `anchor`，才能把 `/home` 保存到数据镜像。Folio 的保存和导出会自动提交 `/home`。** `/tmp` 在重启后清空；上一次 `anchor` 之后未保存的修改也会丢弃。`rest` 关机，`renew` 重启，二者不会自动保存。

完整的 28 条 Loom 命令见 [命令手册](docs/COMMANDS.md)。每个命令都支持 `命令 --help`；`help` 和旧别名 `atlas` 显示总表。命令行位于 `user/loom.c`，实际运行在 Ring 3，并通过内核系统调用完成操作。输入 `folio` 可打开全文编辑器；Folio 的快捷键和文档格式见 [命令手册](docs/COMMANDS.md)。

## 构建、测试和 ISO

```sh
make -j4
make test
make iso
python3 scripts/test.py --iso
```

32 位版本：

```sh
make ARCH=i686 -j4
make ARCH=i686 test
make ARCH=i686 iso
NV_ARCH=i686 python3 scripts/test.py --iso
```

不需要宿主 libc 或第三方 C 库；源码采用 freestanding 编译和直接链接。`make clean` 只删除当前架构的 `build/ARCH/`，其中包括该架构的数据镜像；有保存内容时先备份镜像。

每个架构的 `build/ARCH/` 中包含：

| 文件 | 用途 |
| --- | --- |
| `nuvora.elf` | 内核符号文件；x64 版本是 ELF64 / AMD64 |
| `boot.elf` | 提供给 Multiboot 引导器和 QEMU 的启动文件 |
| `nuvora-core-0.4.0-ARCH.iso` | 已验证的 BIOS / GRUB 启动 ISO |
| `apps/*.elf` | 该架构的独立用户态程序 |
| `test-results/` | 真实虚拟机执行日志、结果表和截图 |

**x64 的 `boot.elf` 使用 ELF32 容器承载 32 位引导入口和 64 位内核机器码。** 入口主动启用 PAE、长模式和 NX，再调用 64 位 C 内核；用户程序加载的是 ELF64。该容器用于兼容 Multiboot v1，不表示内核运行在 32 位模式。

ISO 当前仅支持 **BIOS 启动**，没有 UEFI 或 Secure Boot。默认运行脚本不会操作宿主机分区或安装引导器。

## 当前已实现

| 模块 | 实现范围 |
| --- | --- |
| CPU 与启动 | i686 保护模式、x86-64 长模式；GDT、TSS、IDT；PIC、PIT |
| 内存 | 物理页所有权检查、可合并内核堆、独立页表、带双侧保护页的内核栈、内存耗尽回滚 |
| x64 保护 | 四级页表、完整 64 位寄存器保存、只读代码页、堆和栈 NX |
| 进程 | Ring 3、轮转抢占、休眠、让出、创建、原地替换执行、等待、终止、退出内存及时回收 |
| 可执行文件 | ELF32 / ELF64 静态程序加载，范围检查和失败回滚 |
| 系统调用 | 27 项原创 ABI 操作，USB 查询和用户指针边界校验 |
| 文件系统 | 分层内存文件树、目录、相对路径、文件句柄、读写、移动和删除 |
| 虚拟节点 | `/dev/null`、`/dev/zero`、`/dev/console`，`/sys` 动态状态 |
| 磁盘 | IDE 主盘 ATA PIO；`/home` 双槽 CRC32 快照与旧版本恢复 |
| 命令环境 | 28 条 Loom 命令，统一 `--help`、引号、转义、后台进程和错误反馈 |
| 文档 | Folio 全屏编辑器；`.nvd` 原生格式；RTF/Word 可读导出 |
| USB | xHCI 描述符/Hub/热插拔、USB Boot 键盘；鼠标和存储设备只识别 |
| 验证 | x64 每次 104 项、i686 每次 100 项用户态检查；完整 USB 与帮助回归另行记录 |

两种架构均在 32、64、128、256 MiB 配置下实际启动测试。256 MiB 配置用于确认内核的 128 MiB 管理上限正常生效，不表示本版管理了全部 256 MiB。详见 [测试说明](docs/TESTING.md)。

## 明确的限制

- 单核、单用户研究环境；内核执行期间不被抢占，没有 SMP 锁、用户账户或完整权限模型。
- 两种架构目前均最多管理 128 MiB 物理内存。x64 ABI v1 仍使用 1–2 GiB 的用户地址窗口，没有大地址空间支持。
- 暂无网络栈、NVMe、AHCI、音频、GPU 加速、桌面和 Unicode 终端；USB 目前支持 xHCI 枚举、Hub、键盘输入和设备描述符，不能挂载 USB 存储文件系统；Folio 目前使用 ASCII，不能导入 `.docx` 或外部 `.rtf`。
- 暂无 `fork`、管道、套接字、动态链接、POSIX/Linux ABI 或通用文件系统格式支持。
- 暂无浮点和 SIMD 上下文切换；相关指令会终止触发它们的用户进程。
- 文件系统与快照是本项目的简化格式，不是 ext4；只在指定 QEMU 硬件模型上验证。
- 测试通过不构成生产级安全或可靠性证明。没有进行真实硬件、长期压力或断电时序的全面认证。

架构细节见 [ARCHITECTURE.md](docs/ARCHITECTURE.md)，系统调用见 [ABI.md](docs/ABI.md)，下一阶段范围见 [ROADMAP.md](docs/ROADMAP.md)。

## 许可与参考

Nuvora 原创源码采用 MIT。ISO 使用的 GRUB 2.12 属于独立第三方组件，其 GPL 许可和对应 Ubuntu 源码一并附在 `third_party/`。QEMU、编译器及其他宿主工具没有打包进来。

实现参考处理器、Multiboot 和 ELF 公开规范，链接集中在 [SOURCES.md](docs/SOURCES.md)。
