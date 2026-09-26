# 参考规范与来源

处理器和标准格式相关实现参考下列公开文档。项目自己的代码、命令名称、ABI 编排和快照格式在本次工作中编写，没有将 Linux 内核源码改名包装。

- [Intel 64 and IA-32 Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)：保护模式、长模式、描述符、分页、权限与异常的处理器规范入口。0.3.0/0.4.0 的内核栈保护、x64 IST 和 i686 双重故障 task gate 对应 Volume 3A 的分页、异常与任务管理章节。
- [GNU Multiboot header fields](https://www.gnu.org/software/grub/manual/multiboot/html_node/Header-magic-fields.html)：启动头标识和校验关系。实现使用 Multiboot v1，进入后由本内核自行初始化。
- [System V ABI — ELF Header](https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.eheader.html)：ELF32/ELF64 头部布局和机器类别。
- [System V ABI — Program Header](https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.pheader.html)：程序段字段，用于本项目的受限静态加载器。
- [QEMU system invocation](https://www.qemu.org/docs/master/system/invocation.html)：虚拟机启动、设备和串口配置参考。本次实际使用的是 QEMU 8.2.2，未把网页最新版本的结果当成本包测试结果。
- [T13 ATA drafts](https://www.t13.org/)：ATA 规范组织入口。本项目仅实现传统 PIO 子集，不声称完整符合 ATA 标准。
- [Arch Linux qemu-system-x86 package](https://archlinux.org/packages/extra/x86_64/qemu-system-x86/)：Arch 宿主依赖名称。
- [Rich Text Format 1.5 specification](https://www.biblioscape.com/rtf15_spec.htm)：RTF 控制字、分组、字体和段落格式的公开规范页面；Folio 只写入其中的 ASCII 子集。
- [Intel High Definition Audio Specification rev 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf)：HDA 控制器寄存器、即时命令、codec 动词、输出流与缓冲描述符格式。当前只实现模拟 PCM 输出子集。
- [QEMU HDA device documentation](https://github.com/qemu/qemu/blob/master/docs/qdev-device-use.txt)：`intel-hda` 与 `hda-duplex` 设备参数；用于创建可复验的虚拟声卡。

GRUB 是唯一随 ISO 提供的第三方引导组件，其来源、许可证及对应源码位置见 [third_party/README.md](../third_party/README.md)。QEMU 和编译器只用于构建、运行与验证，没有放进发布包。

## 0.5.0 CPU / 显卡准备参考

- [Intel 指令参考 Volume 2A](https://cdrdv2.intel.com/v1/dl/getContent/671199)：CPUID、FSAVE/FNSAVE、FRSTOR、FXSAVE、FXRSTOR；结合上述 SDM 的 CR0/CR4 和浮点异常章节。本轮读取了 092 版指令参考。
- [NVIDIA open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules)：核对其 Linux、GSP 固件及用户态组件依赖，用于界定移植准备范围；没有打包或复制 NVIDIA 驱动实现。
- [QEMU 8.2.2 SSE helpers](https://github.com/qemu/qemu/blob/v8.2.2/target/i386/ops_sse.h) 与 [浮点状态 helpers](https://github.com/qemu/qemu/blob/v8.2.2/target/i386/tcg/fpu_helper.c)：核对本次 TCG 的 SSE 异常位记录行为；具体 #XM 检查在执行日志中标记 SKIP，不算验证成功。

## 0.6.0 ACPI / PCIe 参考

- 用户提供的 `linux-7.2.6.tar.xz`：阅读 `drivers/acpi/acpica/tbxfroot.c` 的 EBDA/高 BIOS RSDP 查找顺序、`drivers/acpi/pci_mcfg.c` 的 MCFG 项表示，以及 `arch/x86/pci/mmconfig-shared.c`、`mmconfig_32.c`、`mmconfig_64.c` 的区域验证和按 function 映射方式。Linux 源码采用 GPL-2.0 系列许可；本项目没有复制、编译、链接或打包其中实现，只据此核对边界和回退策略。
- [ACPI Specification](https://uefi.org/specifications)：RSDP、RSDT/XSDT、通用表头校验和及 MCFG 布局。Nuvora 目前只解析发现所需的静态表，不执行 AML。
- [PCI Firmware Specification](https://pcisig.com/specifications)：增强配置访问机制的每总线/设备/function 地址布局。当前只支持 segment 0，并保留传统 x86 CF8/CFC 回退。

## 0.7.0 UEFI 启动参考

- [UEFI Specification](https://uefi.org/specifications)：EFI 系统表、Boot Services（GetMemoryMap、AllocatePool、ExitBootServices、LocateHandleBuffer）、Simple File System、Graphics Output Protocol、Loaded Image 协议与 PE32+ 镜像格式（Subsystem、SectionAlignment、基址重定位块）的规范来源。stub 只使用上述引导期服务，不调用 Runtime Services，也没有实现 Secure Boot。
- [Microsoft PE Format](https://learn.microsoft.com/windows/win32/debug/pe-format)：PE32+ 可选头、节表与基址重定位块布局，`scripts/mkuefi.py` 按该布局从 ELF 直接生成镜像（`.reloc` 由 `--emit-relocs` 保留的绝对重定位合成）。
- [T13 ATA drafts](https://www.t13.org/)：LBA48（READ/WRITE SECTORS EXT 0x24/0x34、IDENTIFY word 83/100-103）寄存器序列参考。
- 用户提供的 `linux-7.2.6.tar.xz`：阅读 `drivers/firmware/efi/libstub/efi-stub-helper.c` 的 ExitBootServices 重试策略（失败后复用缓冲重取内存图一次）与 `x86-stub.c` 的 EFI 内存类型到可用 RAM 的归类（Conventional/BootServices 视为可用）、`locate_handle_buffer` 按协议枚举回退，以及 `arch/x86/mm/init_64.c` 直接映射优先使用大页的策略。Linux 源码采用 GPL-2.0 系列许可；本项目没有复制、编译、链接或打包其中实现，只据此核对边界与回退策略，并沿用其"设备 DMA 用显式掩码约束在低位内存"的思路。

## 0.7.1 修复核对

- [UEFI 2.10 Boot Services](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)：服务表顺序、AllocatePages 的内存所有权、GetMemoryMap 与 ExitBootServices 的 MapKey 重试规则。
- 本轮没有修改、编译或链接 Linux 核心。外层原附 Linux 压缩包随修复包保留；上文“未打包其中实现”描述的是 Nuvora 子项目及其二进制，外层参考材料不属于 Nuvora 构建输入。
