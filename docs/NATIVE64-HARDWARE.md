# 0.14.0：原生 64 位应用与实体 PC

本版面向 x86-64 实体 PC 的 UEFI 启动、地址空间和标准控制器兼容。
交付的 GPT/FAT32 镜像可写入专用 U 盘；内核与应用执行 ELF64/长模式。
BIOS 的 32 位 Multiboot 跳板用于进入长模式，ABI 1 用于旧程序兼容。

0.15.0 保留以下地址与硬件路径，并增加现代图形会话、内核自动锁屏和
管理员电源权限；UEFI ACPI 表发现已修正。当前桌面入口与安全边界见
[DESKTOP-SECURITY](DESKTOP-SECURITY.md)，以下 0.14.0 表格保留作迁移说明。

## 已解除的地址限制

| 项目 | 0.13.1 | 0.14.0 原生路径 |
| --- | --- | --- |
| 系统调用 | 参数/返回低 32 位 | RAX/RBX/RCX/RDX 原生宽度，负错误符号扩展 |
| 用户页表 | 固定 1–2 GiB 窗口 | 低半规范地址，支持跨 PD/PT/PDPT/PML4 |
| ELF 映像 | 从 1 GiB 起、16 MiB 窗口 | 从 4 GiB 起，映像上界 64 GiB |
| 用户堆 | 512 MiB 固定窗口 | 从 64 GiB 起、上界 64 TiB；实际受可用 RAM 限制 |
| 用户栈 | 2 GiB 以下 | 栈顶 `0x7ffffff00000`，8 页及未映射保护页 |
| 图形/窗口/音频指针 | u32 | 新操作使用 u64；旧结构按旧操作解析 |
| 时钟、进程统计 | u32 | CLOCK64/INFO64/TASK64 与内部 u64 tick |
| MMIO、DMA | 部分地址截断/统一低地址 | 完整物理地址；按硬件寻址能力选择分配范围 |

内核的物理别名、堆和 GOP 窗口迁到 supervisor 高半规范地址。用户映射
不能覆盖这些共享映射；W^X、NX、逐页验证和失败回滚继续生效。应用编译
使用 `-mcmodel=large`，链接 `user/linker64.ld`。旧低地址 ELF64 根据段地址
选择原布局；旧调用号和二进制结构保持兼容。参考 [ABI](ABI.md) 和 [SDK](SDK.md)。

## 实体固件与设备改动

- 64 MiB FAT32 ESP，使用标准 `EFI/BOOT/BOOTX64.EFI` 回退路径。
- 保留可用 GOP 当前模式；不适合桌面时枚举并尝试受支持模式，失败后继续
  下一个。支持 RGBX/BGRX 及不重叠的连续通道掩码，内核完成像素转换。
  帧缓冲和 BAR 可以从非整页地址开始，映射计算包含页内偏移。
- xHCI 的环、上下文、事件完成指针、暂存表使用原生地址；AC64 控制器可
  分配高地址 DMA，未声明 AC64 的控制器保留低 4 GiB 页。
- NVMe PRP、受支持 Intel 网卡的环/描述符使用 64 位 DMA。AHCI 按 CAP.S64A、
  HDA 按 GCAP.64OK 选择高地址或低地址页。
- 关机读取校验过的 FADT/DSDT 静态 `_S5_` 包和 PM1 控制寄存器；重启优先
  使用 ACPI Reset GAS，再用 PC reset-control/8042 回退。支持 I/O 与内存
  GAS；没有完整 AML 解释器，动态 `_S5_` 和硬件精简 ACPI 睡眠寄存器尚未支持。
- 正常启动不再耗尽并清零所有 RAM 做压力自检；0.15.0 起该测试只在单独诊断内核的 `nv.test=1` 模式运行。

## 上机

运行 `make -j4 all`、`make test-host`、`make media`；输出
`build/x86_64/nuvora-uefi-media.img`。预编译启动包中的同名镜像可直接使用。
写盘步骤见 [BOOT-MEDIA](BOOT-MEDIA.md)，请选择专用测试 U 盘。
使用 x64 UEFI 并关闭 Secure Boot，键鼠优先 xHCI Boot HID 或 PS/2。
镜像没有自动安装器；启动后 `/home` 默认在 RAM 中，USB 存储挂载未实现。
持久保存需要单独的受支持 AHCI/NVMe 盘和 Nuvora 数据卷。

记录电脑/主板、CPU、RAM、UEFI 版本和启动设置，以及显卡、网卡、存储与
USB 控制器的 PCI/USB ID。启动后用 `firmament`、`silicon`、`ports --scan`、
`partitions`、`volumes`、`net`、`wave --test` 检查设备；故障地址日志保留完整 64 位。

## 实际边界

当前仍管理最高 128 GiB **物理地址范围**，内核元数据堆 8 MiB、32 个任务，
GOP 映射最多 64 MiB。用户申请立即提交物理页，没有交换、按需分页或通用 mmap。
这些是明确资源上限；64 TiB 堆是虚拟范围，并不等于已拥有同等 RAM。
像素、IPv4、协议编号等字段继续使用合适的 32 位类型。

4Kn 逻辑扇区、USB 存储、Intel VMD/RST、内置 PCI Wi-Fi、通用 HID、IOMMU、
APIC/SMP、GPU 加速、Secure Boot 和 POSIX/Linux/Windows 二进制兼容尚未实现。
ARM64 仍是独立 bringup，未移植本版桌面 ABI。具体设备范围见 [DEVICES](DEVICES.md)。

本次有构建、UBSan 宿主夹具和 QEMU/OVMF 启动回归；没有实体电脑接入，
不能据此声称任意实体机已通过。实体机型号验证需要在目标机器实际启动、
检查外设和电源路径，结果应与合成/模拟回归分开记录。
