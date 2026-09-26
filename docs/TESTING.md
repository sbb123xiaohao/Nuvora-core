# 测试与复现

## 当前源码扩展：NVMe、USB 鼠标与 HDA 音频

当前环境实际执行 x64 `make -j4` 以及 `make test-host`，通过 14 组
UBSan 宿主源码回归。新增 NVMe 夹具模拟控制器寄存器、PCI、队列和 DMA，
使用真实双分区 GPT 数据镜像，验证扇区格式、读/写/Flush、非快照槽拒绝、
队列回绕及故障停用。USB 鼠标夹具验证 Boot 报告有符号位移、按键释放、
事件队列回绕和清空；桌面夹具检查选中命中和渲染分块一致性。
HDA 夹具验证真实驱动在模拟 MMIO、codec verb、DMA 上的路由选择、
双描述符播放和用户缓冲校验；实际绘制 1280×800、640×480 界面截图
并人工检查。`forge probe devctl` 在之前 137 项基础上增加 2 项音频断言，
x64 guest 运行器预期 **139 项**。当前环境没有 QEMU、OVMF、mtools
或实体硬件，因此没有本轮 guest/UEFI/实体 SSD、鼠标和音频的运行通过记录。下方较早的
131 项 guest 与 8–11 组宿主数字均属以前源码的历史验证，不适用于当前扩展。
设备范围和进一步验证要求见 [DEVICES.md](DEVICES.md)。

本轮 Media 扩展的 `make test-host` 共 16 组，使用合成的真实 MP3
（44.1 kHz）和 MPEG-1 Program Stream（160×120、25 fps、MP2 音轨）
验证解码输出，比较图形完整/逐块绘制结果，并检查 GPT 镜像导入的
CRC、双槽提交、重复文件拒绝和显式替换。FFmpeg 仅用于生成已提交的
测试样本，不进入目标系统。还需在 QEMU UEFI/HDA 上验证播音连续性、
长文件以及实体声卡路径。

**像素桌面扩展的验证边界：**x64 `make -j4` 已通过；宿主截图夹具
以实际 `user/desktop_ui.h` 绘制 1280×800、640×480 的布局并人工检查；
`make test-host` 的桌面组对四种分辨率、两种像素格式比较整屏与分块绘制，
并用 UBSan 检查边界与盘符选中状态。
`forge probe devctl` 新增 4 个显示相关断言，故 QEMU 运行器将预期数从
131 改为 135；当前环境没有 QEMU/OVMF，不能将历史的 131 项运行成绩
当成本轮 135 项的通过结果。真实 UEFI GOP、显示复制和按键启动流程
仍需 QEMU 与实体设备运行验证。

**实体网络扩展的验证边界：**本轮 `make -j4` 通过，`make test-host`
现为 11 组 UBSan 宿主测试。新增 `tests/network_test.c` 将实际
`kernel/net.c` 接入模拟的物理 NIC，覆盖 PCI 型号识别、DHCP
Discover/Offer/Request/Ack、ARP 邻居缓存、UDP 收包、IPv4 校验和错误与
链路断开清理。USB CDC-ECM/CDC 控制和 I225/I226 DMA 寄存器路径仅通过
编译和代码检查；没有实体网卡或 QEMU，因此尚无 Wi-Fi 入网或实体有线
收发的运行记录。之前记录的 QEMU 结果不能替代本轮网络验证。

**GPT 分区源码扩展的验证边界：**本轮 `make -j4` 与 `make test-host`
已在当前宿主执行，新增第 9 组测试使用真实 `mkgptdisk.py` 生成的双分区
GPT 镜像和实际 `kernel/disk.c` 的 ATA 端口模拟器，检查扫描、几何及写入
范围；RAM 文件夹具另外覆盖 C:/D:/ 路径和快照隔离。下方原版 0.9.0
的 QEMU、OVMF、ARM64 运行数字是以前版本的记录，**不能当成本扩展的
虚拟机启动结果**；当前环境缺少 QEMU/ARM64 交叉编译器，需复验。

x64 与 ARM64 在 QEMU 8.2.2 TCG 模拟器中实际执行；编译器为 GCC 13.3.0，链接器为 GNU ld 2.42。虚拟硬件覆盖 PC 与 q35、单 CPU、VGA、COM1、PS/2、PIT/PIC，存储测试连接 IDE primary master 数据镜像；q35 额外挂载 ISA IDE 桥，以保持内核传统 ATA PIO 端口与 q35 的 AHCI 默认设备隔离。本轮 UEFI 检查使用 Ubuntu OVMF 的 `OVMF_CODE_4M.fd` 与配套 VARS，通过 pflash 加载（可用 `NV_OVMF` 指定固件），数据镜像与 ESP 分别挂在 IDE primary master 和 slave。

## 结果范围

| 架构 | 用户态断言 / 每次启动 | 内存配置 | 宿主集成检查 |
| --- | --- | --- | --- |
| x86-64 | 131 项 | 32 / 64 / 128 / 256 MiB，另有 1 GiB 与 5 GiB（跨 4 GiB 边界）回归 | 完整回归 53 组通过（含 BIOS/UEFI ISO） |
| ARM64 | 15 项 / 单个 EL0 工作负载 | 64 / 256 / 1024 / 5120 MiB | 4 组（设备树、分阶页与 slab、页表、NEON、SVC 与隔离） |

执行记录分别位于 `build/x86_64/test-results/` 和 `build/aarch64/test-results/`。`RESULTS.md` 与 `results.json` 由测试程序根据成功执行结果生成。每次 probe 的完整日志含逐项 PASS 记录；宿主必须同时收到 QEMU 正确的退出码并找到零失败汇总，才把该轮判为通过。

## CPU / 平台 / 显卡矩阵

| 架构 | 成功运行完整 probe 的 CPU 模型 | 启动前拒绝的 CPU 配置 |
| --- | --- | --- |
| x64 | 默认 qemu64、core2duo、Nehalem、phenom、max | qemu64 分别关闭 nx、pae、fxsr、sse2、lm、msr、fpu、cmov |
| ARM64 | QEMU virt 上的 cortex-a57 | 尚无 ARM64 CPU 特征拒绝矩阵 |

这是 QEMU TCG CPU 模型验证，不表示对应实体处理器已经认证。

每次 probe 有 1 项硬件异常递交检查明确跳过：有 SSE 的 QEMU 8.2.2 TCG 记录了未屏蔽除零的 MXCSR 位，但没有递交 #XM；无 SSE 的模型不执行 SSE。日志以 `SKIP` 单列，成功断言只校验其结果分类，不能解读为 #XM 递交已通过。物理 CPU 上若出现相同问题，测试不会将其当作 TCG 跳过。

两个 `vector` 工作进程验证全部 x87 / MMX 数据和 XMM0–7（x64 为 XMM0–15）、控制字与 MXCSR 在定时器抢占、yield、sleep、SPAWN 与 EXEC 中的隔离。父进程保持另一组浮点数据；新进程检查清零状态。另有真实 x87 #MF 和 AVX #UD 进程故障隔离。

显卡分别测试默认 QEMU VGA、`-vga none` 和同一设备号上 0/1 两个 function 的 VGA。PCI 解析器另在宿主执行 25 项合成配置检查，含 NVIDIA 显示/音频区分、64 位 BAR 超过 4 GiB、BAR 高半合并、无效末尾 BAR、legacy/extended 能力链环、越界与未对齐，以及 AER、Resizable BAR、ACS、ATS、SR-IOV、PASID、DPC 标记。

ACPI 解析器执行 11 项合成固件检查，覆盖 RSDP 双校验和、EBDA 优先、XSDT 到 RSDT 回退、坏 MCFG 校验和、部分记录、未对齐、重叠、容量边界和不可读地址。q35 guest 必须从 MCFG 启用 `0xb0000000` 的 0–255 总线 ECAM、通过完整 probe，再以 `nv.no-ecam=1` 启动并通过 CF8 回退读取同一显卡。没有实体 NVIDIA GPU 或实体主板 ACPI/ECAM 测试；宿主解析器检查不属于 QEMU guest 检查。`--phase hardware` 单独执行此阶段。

## DEVCTL 整合回归

x64 包含 14 项设备控制断言：操作路由、保留接口的 ENOSYS、完整 16 字节输出边界、只读/跨页/溢出与高位指针、索引错误时响应清零、600 次失败请求后仍能映射、600 次成功请求复用窗口、MMIO 保持 supervisor-only。无显卡/单显卡/多功能显卡三种场景还分别运行两个独立 `forge probe devctl` 子进程，核对发现快照保持一致；无显卡时明确跳过实际映射检查。

## 检查内容

- 系统调用：未知调用号、越界和溢出指针、内核地址、只读输出、跨页输出的完整检查、过长字符串和有界 I/O。
- 平台：固定平台 ABI、ACPI/MCFG/ECAM 状态一致性、q35 4 KiB PCIe 配置访问、显式禁用后的 CF8/CFC 回退，以及合成坏表隔离。
- 内存：物理分配器自检、堆块合并、用户页零初始化、分配/收缩、错误返回在 64 位指针中的保留、页表回收；x64 另在 1 GiB 与 5 GiB（跨 4 GiB 边界）配置下运行完整用户态断言，覆盖 2 MiB 大页、独立 supervisor 物理别名、64 位页地址与 DMA 掩码约束。
- 文件：目录与相对路径、排他创建、读写/seek、稀疏区清零、追加、截断、复制和重命名、目录环、忙状态、句柄耗尽、虚拟设备与状态文件；规范路径恰好 191 字节时可用，再增加节点会被拒绝，文件重命名拒绝目录后缀。
- 加载：无效文件、越界 ELF 程序头、内核地址入口，以及失败路径上已分配页的回收。
- 进程：启动、等待、只领取一次状态、40 次重复创建/回收、进程槽耗尽与恢复、未领取退出状态时内存释放、孤儿进程退出回收。
- EXEC：内核地址参数拒绝、缺失文件、20 次映射后失败的完整回滚、12 次原地替换；验证 PID、父子关系、cwd、文件偏移保留，旧映像与屏幕租约释放。
- 内核保护：主动触发内核栈上下保护页；下界溢出必须经独立应急栈报告 double fault，上界写入必须报告 page fault；PID 1 异常必须给出明确停止诊断。故障注入仅在 `nv.test=1` 下启用，这三项预期 QEMU 退出码为 35，不能与普通测试失败混为一谈。
- 真实内存压力：在 32 MiB 虚拟机中用多个用户进程占满物理页，连续 8 轮 EXEC/SPAWN/GROW 都返回内存不足，调用者保持有效；清理后核对物理页、内核堆、文件节点和进程计数，并重新启动应用。
- 抢占：两个不调用任何系统调用的忙循环都获得 CPU tick；测试进程能够恢复并终止它们。
- 故障：空指针、访问内核内存、写代码段、特权指令、除零、无效操作码、用户栈保护页、x87 未屏蔽除零 #MF 和未启用的 AVX #UD。
- x64 专项：64 位代码、高位参数拒绝、r8–r15 等寄存器的高 32 位跨调度保存、NX 阻止从堆和栈执行。
- 存储：两次提交后恢复最新代次、未提交修改丢弃、`/tmp` 清空、实际重启、四类快照损坏回退、非本项目磁盘全盘 SHA-256 保持不变；4 TiB NVSTORE2 稀疏镜像的 anchor/重启恢复往返覆盖 u64 总扇区数与 LBA48 命令模式，但快照槽位于低 LBA，不能据此声称做过高 LBA 实盘传输。高 LBA 的寄存器字节序列由下述端口夹具验证。
- UEFI（x64，需 OVMF/edk2 固件与 mtools 生成的 ESP）：stub 完成启动（携带 .reloc 基址重定位表、多卷回退）、内核到达 Ring 3，`horizon`/`origin` 可用；anchor 后重启从数据盘恢复 `/home`，并运行完整 `trial`；UEFI El Torito ISO 光驱启动同样进入用户态。
- 使用流程：在 Loom 中执行 `trial` 并回到提示符；模拟 PS/2 键盘输入成功；保存真实 VGA 截图。
- USB：PCI 控制器分类、xHCI 描述符与字符串、键盘 Boot Protocol、鼠标/存储只读识别、两级 Hub、66 次根端口热插拔、拔除后的 DMA 页回收和事件/命令环回绕。`--phase usb` 单独执行这组检查。
- 帮助：当前脚本准备逐一查询 35 个命令的 `--help`、`help`/`atlas` 别名、错误参数和字面量 `--help` 文件内容；10 个用户程序以 `forge APP --help` 正常退出。`--phase help` 单独执行这组检查，尚待本轮 QEMU 运行。
- Folio：创建 `.nvd`，输入并选择文本，应用粗体和一级标题，关闭后重新打开校验，导出 `.rtf` 并检查标题字号和粗体控制字。
- 启动介质：从 GRUB BIOS ISO 的虚拟光驱进入用户态，成功运行一个独立 ELF 子进程；x64 另从 UEFI ISO（OVMF）进入用户态。

每个内核还在进入用户态前运行物理页、堆和页权限的内部自检；测试启动还会耗尽全部可分配物理页，逐一验证仅有 0–4 页可用时，页表与内核栈的部分分配回滚。内部自检没有额外计入上述用户态断言数字。

## 复现命令

```sh
make -j4
make iso
make esp        # UEFI：需要 mtools
make iso-uefi
python3 scripts/test.py --iso

make ARCH=aarch64 CROSS=aarch64-linux-gnu- -j4
make ARCH=aarch64 test
```

缺少制作 ISO 的工具时，`make test` 仍可运行直接启动及存储测试，只跳过 ISO 光驱检查；缺少 OVMF 固件时 UEFI 组打印跳过原因；x64 测试前必须用 mtools 构建 ESP，缺失产物会报错。发布打包要求实际完成 UEFI 组，不能用跳过结果通过。也可通过 `--phase storage` 或 `--phase iso` 定向诊断；这些定向执行会生成仅包含相应阶段的结果表，不能作为完整测试矩阵。

测试使用 `build/ARCH/test-results/` 下的专用临时镜像，不使用 `start.py` 的 `nuvora-store.img`。完整回归会写入 `build-fingerprint.json`，记录已测试内核、用户程序、全部 C/头文件/汇编/链接脚本、构建及测试脚本、EFI/ESP 与 ISO 的 SHA-256；`execution.json` 另记录完整阶段、ISO 检查和成功组数；打包时逐项核对，防止把测试后改变的二进制当作已验证版本。发布包保留执行日志和截图，不附带故意损坏的测试磁盘；重新运行会自动生成它们。

## 尚未验证

没有验证真实主板的 ACPI/ECAM 固件差异、真实 PCIe/NVIDIA 硬件、实体主板 UEFI 固件（含 Secure Boot 与带重定位的加载场景）、KVM、其他虚拟机品牌、多核、网络设备、NVMe、长时间运行、所有低内存组合，以及真实磁盘突然断电的全部时序；USB 验证使用 QEMU xHCI/UHCI、键盘、鼠标、Hub 和虚拟存储设备。这些结果证明本次实现覆盖的功能路径实际运行过，不是生产级完整内核认证。

## 源码边界夹具

构建 x64 内核后执行 `make test-host`，或 `python3 scripts/test_regressions.py`。0.9.0 共 8 组通过，采用 GCC UBSan，直接包含被修复的 Nuvora C 源码；固件与 I/O 回调是明确的模拟输入，并非实体硬件结果。日志由测试程序输出。

| 组 | 覆盖 |
| --- | --- |
| 地址 | 跨 4 GiB 的对齐、NX 地址掩码、物理地址与内核指针往返 |
| 分阶索引 | 4000 轮随机碎片分配、保留空洞、对齐块与分阶合并，逐次对照朴素空闲扫描 |
| 分配器 | 连续缓冲跨 1 GiB 时用户页不变、保留区、DMA 上界、碎片化分配失败和恢复、12000 轮堆操作及 slab 空页归还 |
| 堆页表 | 分散的物理页、0/1/17/1024/2047 页后注入 OOM 的完整回滚、固定页、NX/supervisor 权限、共享映射不随进程销毁 |
| RAM 文件内存 | 4 种快照恢复长度，在充足/临界/不足堆预算下扩容；容量封顶、数据与稀疏区、失败原子性、完整回收 |
| ATA | `0x123456789abc` 的 LBA48 端口序列、LBA28 边界、flush 命令、签名/版本/几何错误 |
| 快照 | 低 RAM 无法恢复时禁止覆盖、极小槽位缓冲边界、扇区尾部清零 |
| UEFI | 分段读取与 rewind、退出服务重试内存图更新、恶意 ELF、内存所有权、段复制和 BSS 清零 |

本轮 OVMF 的 8 MiB 地址附近存在 ACPI NVS，固定 BSS 中的 8 MiB 堆使旧 ELF 跨入该保留区。修复后堆从可用 RAM 分配，最终 UEFI 直接启动、数据恢复和纯光驱 ISO 启动均实际通过。测试期间没有另外附加 ESP 来代替 UEFI 光驱启动。

## 0.7.2 内存专项复现

`python3 scripts/test.py --phase memory` 执行内存矩阵、内核保护页、低内存压力和 x64 碎片化启动。定向运行不代替发布所需的 `--iso` 完整矩阵。

x64 的 `nv.test=1 nv.memory-test=fragmented` 在启动内存表中加入 6 个页大小的保留空洞（8–28 MiB，每隔 4 MiB），自动用 32 MiB QEMU 执行。0.7.1 在该配置出现 `cannot reserve kernel heap`；0.7.2 通过完整用户态检查。夹具只在两个测试参数同时存在时启用，普通启动不会加入空洞。

x64 在 0.8.0 执行 12 轮 1/511/512/513/1023/2048 页的用户堆扩缩容，核对再分配清零、释放后地址拒绝访问、空闲页计数恢复；x64 另验证直接访问物理映射别名和 1 TiB 内核堆只能终止子进程，不能读取内核内容。根因、失败证据和变更范围见 [MEMORY-0.7.2.md](MEMORY-0.7.2.md)。

## 0.7.3 文件扩容复现

`--phase memory` 与 `--phase storage` 均包含 32 MiB 的恢复文件扩容检查。运行器写入带有效校验和、含 131071 字节文件的专用快照；启动后以 `forge probe file-growth` 执行 7 项检查，要求追加后的堆占用维持 128 KiB、数据完整、超限拒绝、删除全部回收。x64 日志为 `restored-file-growth.log`，独立于常规 probe 的 131 项；ARM64 尚无此文件系统。

宿主夹具使用真实 ramfs 与堆分配器，旧 0.7.2 在恰好 128 KiB 可用时返回 ENOMEM 的日志也随包保留。详细根因见 [MEMORY-0.7.3.md](MEMORY-0.7.3.md)。
