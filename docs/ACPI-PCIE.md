# ACPI 与 PCIe ECAM（0.6.0）

本模块为现代 x86 设备发现提供基础。它不执行 ACPI AML，也不管理电源；当前目标是安全取得固件 MCFG，给 segment 0 的 PCI/PCIe function 提供配置空间访问，并在不可信或不兼容固件上保留传统路径。

## 启动流程

1. 内存与 CPU 初始化完成后，内核通过临时 supervisor 映射读取物理固件数据。
2. 先读取 BDA 的 EBDA segment，在 EBDA 前 1 KiB 按 16 字节查找 RSDP；未找到时扫描 `0xe0000`–`0xfffff`。
3. 校验 RSDP 1.0 的前 20 字节；revision 2 以上还检查声明长度和扩展校验和。
4. 优先解析 XSDT；若地址、签名、长度或校验和无效，尝试 RSDT。
5. 只提取 MCFG。每张表必须通过完整 8 位校验和，长度必须包含完整的 16 字节 allocation 记录。
6. 每个记录检查非零且 1 MiB 对齐的基址、`start_bus <= end_bus`、地址加总线窗口不溢出、同 segment 总线范围不重叠，以及固定 8 项容量。
7. PCI 层只接受 segment 0、当前 CPU 物理地址宽度可达的项目。启用前比较起始 BDF 的 ECAM 和 CF8/CFC 读取值；不一致则拒绝该项。

解析器位于 `common/acpi.c`，不依赖内核全局状态，因此合成固件测试执行的就是生产解析代码。物理映射和 ABI 快照位于 `kernel/acpi.c`。

## 配置空间访问

ECAM 地址按下式计算：

```text
MCFG base + (bus << 20) + (device << 15) + (function << 12) + register
```

每个 function 占 4096 字节。内核在 `MMIO_BASE` 区域保留最后一页作为临时窗口，每次只映射当前 function；PTE 为 supervisor-only、不可缓存，x64 同时为 NX。访问期间关闭中断，避免当前单 CPU 内核中的窗口切换竞争。持久的 xHCI/GPU BAR 映射使用前 511 页，不会覆盖该窗口。

`pci_read()` 在 ECAM 覆盖范围内接受对齐的 `0x000`–`0xffc` 偏移；其他 segment-0 总线用 CF8/CFC，范围为 `0x00`–`0xfc`。`pci_write16()` 同样按覆盖范围选择路径，当前只供已有 xHCI 生命周期使用。系统没有向用户态暴露配置空间写接口。

## 查看与回退

在 Loom 中运行：

```text
firmament
firmament --help
prism
```

`firmament` 显示 RSDP revision、RSDT/XSDT、OEM、MCFG 固件项、活动/拒绝数、首个活动 ECAM 范围和 CF8 回退。`prism` 在 4 KiB 配置空间可用时额外列出 AER、ACS、ATS、SR-IOV、Resizable BAR、PASID 和 DPC。

如果某台机器的 ECAM 固件描述导致兼容问题，可在 Multiboot 命令行加入：

```text
nv.no-ecam=1
```

随包运行器可直接使用：

```sh
python3 start.py --machine q35 --no-ecam
```

此选项仍解析并报告 ACPI/MCFG，但不启用区域，PCI 访问保持在 256 字节 CF8/CFC。它是启动期选项，不能在运行中切换。

## 功能边界

当前没有 UEFI RSDP 交接、ACPI AML、`_CRS`/`_CBA`、FADT/MADT 中断或电源管理、多 PCI segment、总线资源重新分配、桥后热插拔、MSI/MSI-X 启用、AER 错误恢复、IOMMU 或 SR-IOV/Resizable BAR 控制。扩展能力只读报告设备声明，不改变设备状态。

QEMU 8.2.2 q35 已验证 MCFG `0xb0000000`、0–255 总线、两架构完整用户态 probe 和强制 CF8 回退。合成测试覆盖 11 个 ACPI 场景和 25 个 PCI/GPU 场景。真实主板固件和真实 PCIe/NVIDIA 设备仍需单独验证。
