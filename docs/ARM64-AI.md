# ARM64 与 AI 工作负载基线（0.9.0）

当前可构建目标只有 `x86_64` 和 `aarch64`。本文件描述已运行的功能与仍需实现的接口，避免把 CPU 自检、设备识别或大磁盘镜像称作完整训练系统。

| 能力 | x86-64 | ARM64（QEMU virt） |
| --- | --- | --- |
| 启动 | BIOS/GRUB 和 UEFI；完整 Loom 用户环境 | ARM64 Image、设备树移交、PL011 串口；单个内嵌 EL0 工作负载 |
| 内存 | 启动内存图；最多管理 64 GiB RAM；单进程按页申请 512 MiB 用户堆；分阶页分配与小对象 slab | 读取 DTB 内存和保留范围；最多管理 64 GiB；共享分阶页索引和小对象 slab |
| 隔离 | 私有进程页表、用户代码 RO、用户数据 NX | EL1 页表，内核代码 RO、数据 NX；EL0 代码 RO、计算缓冲和栈 NX；验证 EL0 不能读取内核代码 |
| 向量计算 | SSE 状态跨进程保存；未启用 AVX/XSAVE | 单个 EL0 程序执行 NEON 整数点积，SVC 报告结果；无上下文切换 |
| 设备/存储 | ATA PIO、只读显卡识别；16 MiB 快照槽 | 仅 DTB 发现的 PL011；无磁盘、GPU 或 NPU 驱动 |

ARM64 启动入口在 `arch/aarch64/boot.S`，使用 `Image` 的 64 字节头，接收 x0 中的设备树指针，从 EL2 进入 EL1。解析器验证头部、结构块、字符串表和属性边界，使用 `memory`、`reserved-memory` 与 `/memreserve/` 建立所有权位图，通过 `compatible=arm,pl011` 寻找串口。页表以 1 GiB / 2 MiB 块映射普通 RAM，对内核镜像区域细分到 4 KiB，并把串口映射为不可执行的设备内存。分配器只释放自己发出的页面，拒绝释放内核或固件保留页。

EL0 测试程序有独立的可写计算缓冲和栈；向量指令在用户态执行，经 SVC 递交点积结果。异常入口验证故意发起的内核代码页读取被硬件阻断。测试完成通过 QEMU virt 的 PSCI HVC 关机。当前还没有动态 ELF 装载、进程调度、FP/NEON 进程状态保存、IRQ/GIC、PSCI 多核启动或 ARM64 的完整 `NV_ABI_VERSION=1` 实现。该路径只承诺所列 QEMU `virt` 基线，不代表通用 ARM64 主板支持。

x64 的 `GROW` 仍是**立即逐页分配**：请求失败会回滚全部页表与页，已提交的用户 break 不变。用户堆虚拟区从 `0x50000000` 到 `0x70000000`，最多 512 MiB，受可用 RAM 和同一 1–2 GiB 用户地址窗口限制。当前 x64 普通文件上限 64 MiB；新盘最高 128 MiB 的快照槽与磁盘容量不会自动变成训练数据集或交换空间。

## 可运行验证

```sh
make ARCH=aarch64 CROSS=aarch64-linux-gnu- test
make ARCH=x86_64 test-host
NV_ARCH=x86_64 python3 scripts/test.py --phase memory
```

ARM64 在 64、256、1024、5120 MiB 四种 RAM 配置下验证设备树、页表、1 MiB 缓冲清零与归还、分阶页合并、slab 页回收、EL0 NEON、SVC 和隔离，共每轮 15 个断言。x64 的内存阶段覆盖 32 MiB 至 5 GiB，实际检查 64 MiB 与 256 MiB 模型缓冲申请、逐页读写、回收及低内存失败路径。所有这些验证使用 QEMU TCG；64 GiB 是分配器配置的上限，没有进行 64 GiB 实机压力认证。分配器机制与 Linux 功能边界见 [MEMORY-0.9.0.md](MEMORY-0.9.0.md)。

## 后续达到模型训练所需

1. 将 x64 用户地址与系统调用指针改为完整 64 位，增加按需提交、内存映射文件和可靠的资源配额/OOM 行为，并将相同 ABI 移植到 ARM64。
2. 为 ARM64 移植 ELF 装载、进程与调度器，保存和恢复 NEON/FP 状态；x64 才能在实现相应状态管理后启用 AVX/XSAVE。随后加入 SMP、锁、定时器与调度压力测试。
3. 建立 DMA 隔离、块设备队列、持久化大文件及网络传输；GPU/NPU 计算还需要具体设备驱动、固件、用户运行时、故障恢复和真实硬件验证。

上述能力没有用空占位函数对外宣称可用。Linux 资料包不是本项目内核，也未参与这轮构建。

引导行为参考 [QEMU virt 设备树与 RAM 说明](https://www.qemu.org/docs/master/system/arm/virt) 和 [ARM64 Image 启动协议](https://docs.kernel.org/arch/arm64/booting.html)；实现是独立的 Nuvora 代码。
