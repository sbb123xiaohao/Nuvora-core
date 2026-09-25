# CPU 稳定性与 NVIDIA 支持准备（0.6.0 历史说明）

下述设备能力描述保留 0.6.0 阶段的记录。当前只构建 x64 和 ARM64；i686 代码已移除，当前功能以 [架构说明](ARCHITECTURE.md) 和 [0.9.0 内存管理](MEMORY-0.9.0.md) 为准。

启动后输入 `silicon` 查看 CPU，`firmament` 查看 ACPI/PCIe 平台，`prism` 查看显卡。对应命令都支持 `--help`。终端仍使用 ASCII，中文说明在本文件中。

## CPU 已实现的部分

两种内核在进入 C 代码前检查 EFLAGS.ID 和 CPUID。i686 要求 FPU、CMOV；x64 额外要求 MSR、PAE、FXSR、SSE、SSE2、长模式与 NX。缺少条件时向 VGA 和 COM1 写出 `CPU UNSUPPORTED`，关闭中断并停机，不继续执行不受支持的指令。

CPU 查询返回厂家、品牌、family/model/stepping、CPUID 原始功能位、物理地址位数，以及内核实际启用的状态。`Online CPUs: 1` 指真实运行的启动 CPU；CPUID 报告的线程数不等于已经启动了 SMP。

| 路径 | 保存方式 | 启用范围 |
| --- | --- | --- |
| x64 | FXSAVE64 / FXRSTOR64，512 字节、16 字节对齐 | x87、MMX、XMM0–15、MXCSR |
| i686，有 FXSR | FXSAVE / FXRSTOR，同样使用 512 字节区 | x87；硬件具备时启用 MMX、XMM0–7 与 MXCSR |
| i686，没有 FXSR | FNSAVE / FRSTOR，108 字节有效状态 | x87；硬件具备时包括 MMX |

每次切换进程先保存离开的进程，再恢复目标进程；不采用延迟浮点切换。新进程的寄存器数据清零，x87 标签为空、控制字为 `0x037f`，MXCSR 为 `0x1f80`。成功 EXEC 重置这些状态，失败 EXEC 保留调用者的状态。内核本身继续以整数指令编译，不能在中断或内核代码里随意加入浮点/SIMD 运算。

AVX、XSAVE、AVX-512 和 AMX 状态尚未实现，CR4.OSXSAVE 保持关闭；即使 CPU 报告 AVX，也不把它标为操作系统已支持。`fault avx` 验证未启用的 AVX 指令以 #UD 隔离到用户进程。

`forge vector` 在两个并发用户进程中放入不同的 x87、MMX、XMM 和舍入模式数据，验证时钟抢占、yield、sleep、父子隔离和 EXEC。x64 检查全部 16 个 XMM 寄存器，i686 检查 8 个。新进程检查空标签及清零的寄存器数据，防止继承前一进程的内容。

仍未实现 APIC/SMP、CPU 热插拔、微码更新或完整的推测执行漏洞防护；本版最多管理 128 MiB RAM。不同虚拟 CPU 模型的测试不能替代实体 Intel/AMD 设备验证。

## NVIDIA 已准备的部分

`kernel/pci.c` 为 USB 和显卡提供共用的 PCI 配置访问与枚举。扫描 segment 0 的 256 个总线并识别多功能设备。0.6.0 从校验通过的 ACPI MCFG 建立 PCIe ECAM 单页访问窗口，覆盖设备可读取 4096 字节配置空间；未覆盖总线保留 CF8/CFC。两种路径都在关中断区间内完成，适用于当前单 CPU 内核。`nv.no-ecam=1` 可禁用 ECAM。没有多 segment、资源重新分配或显卡热插拔支持。

`kernel/gpu.c` 保存最多 16 个 display-class（PCI class 03）设备的启动快照。`common/pci_decode.c` 是独立的只读配置解析器，输出：

- BDF、vendor/device ID、revision、subsystem ID、显示子类别；vendor `0x10de` 标为 NVIDIA，NVIDIA 音频等非显示功能不算显卡。
- 最多 6 个 BAR：I/O、32 位内存、64 位内存、prefetchable、未分配或无效编码；64 位 BAR 的高半部分不会误算成另一个 BAR。
- PCI command/status、旧式 IRQ 行和引脚，以及 MSI、MSI-X、PCIe 能力标记与已报告的链路状态。
- ECAM 可用时读取 AER、ACS、ATS、SR-IOV、Resizable BAR、PASID、DPC 扩展能力标记。
- 损坏、越界、未对齐、成环或过长的 legacy/extended capabilities 链有界终止；不支持的 PCI 头类型跳过资源解析。

读取不会给显卡启用总线主控，不写 GPU 寄存器，不读取显存，不执行 ROM，也不加载固件。不会通过向 BAR 写入全 1 来测大小，因此 **BAR 大小与显存容量显示为未知**。i686 也能报告 64 位 BAR 地址，但并未获得映射 4 GiB 以上地址的能力。MSI、AER、Resizable BAR 等出现在输出中只表示设备声明该能力，不表示内核已启用中断、错误恢复、地址转换、隔离或大 BAR。

0.5.1 额外整合 GPU BAR 的内核专用首页面映射准备，复用既有映射并先验证完整用户输出，详见 [DEVCTL.md](DEVCTL.md)。准备页表不会读写设备，窗口长度不表示真实 BAR 长度，也不会给用户程序提供映射地址。

**当前 NVIDIA 状态是“识别、资源查询与内核映射准备”，没有原生显示驱动、模式切换、2D/3D 加速、CUDA、视频解码或独显切换。** 后续版本加入了 UEFI/GOP 文本和基础像素接口，其显示输出由固件提供，不能据此声称原生 NVIDIA 显示驱动或现代 NVIDIA 实机启动已通过验证；参见 [SDK.md](SDK.md)。

## 下一步接口与实现顺序

| 下一阶段 | 需要完成的实际工作 |
| --- | --- |
| 帧缓冲输出 | UEFI GOP 基础像素接口已加入；后续验证实体固件、缓存属性、多模式切换和图形终端回退 |
| 平台与 PCI 资源 | 已有 ACPI 根表/MCFG 与 ECAM 基础；仍需 AML 资源、设备资源分配、可映射的大 BAR、安全中断路由、MSI/MSI-X、DMA 与设备停止/复位 |
| 选定 NVIDIA 型号 | 依据实际 PCI ID 选择有文档/固件的型号，建立实机测试环境和设备生命周期 |
| 原生设备驱动 | 设备寄存器、VRAM/通道/显存管理、命令提交、超时恢复、固件接口和调度隔离 |

NVIDIA 的[开源 GPU 内核模块](https://github.com/NVIDIA/open-gpu-kernel-modules)面向 Linux，并要求匹配的 GSP 固件与用户态组件；不能直接作为 Nuvora 驱动链接。本包没有加入这些模块或固件，也没有将规划中的接口伪装成可工作的驱动。

## 验证边界

完整矩阵见 [TESTING.md](TESTING.md)。NVIDIA ID、64 位 BAR、4 KiB 扩展能力和损坏链的 25 项检查使用宿主上的合成配置数据，执行的解析代码与内核一致；另有 11 项 ACPI 合成固件表检查与 QEMU q35 ECAM/强制回退启动。模拟设备不是实体 NVIDIA GPU。

QEMU 8.2.2 TCG 的 SSE 除零会记录 MXCSR 异常位，但本次运行没有递交 #XM。测试只在确认 TCG hypervisor 标识与未屏蔽异常位后报告明确的 `SKIP`，不把 #XM 递交计为验证成功；无 SSE 的旧 CPU 也明确跳过该项。x87 #MF、寄存器隔离和 AVX #UD 都实际执行。
