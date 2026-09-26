# Nuvora Core 0.9.0

## 0.9.0 文件存储与内存改进（宿主回归）

- x64 文件节点从 128 扩至 512，普通文件上限从 4 MiB 扩至 64 MiB。
  新建文件以独立 4 KiB 页存放改动，稀疏空洞不分配数据页；已保存文件
  按需从活动快照槽读取，修改后首次读入相应页面。保存成功会释放脏页。
- 保持 NVSS0001 快照字节格式，改为 512 字节流式 CRC、保存和启动恢复，
  不再为整个分区申请连续快照缓冲。提交头仍在数据刷盘后写入；
  两个非空槽均无效时禁止覆盖。新建 512 MiB 单分区盘采用两份
  128 MiB 槽，现有 1/16 MiB 槽不改写。
- 默认新盘 512 MiB、虚拟机 RAM 256 MiB；关机导入脚本接受任意扩展名的
  数据文件。宿主 UBSan 16 组通过，包含 8 MiB 稀疏文件保存/重启、
  64 MiB 边界、低内存写入回滚、提交失败与 CRC 回退。
  本次环境没有 QEMU/实体硬件验证，尚非通用 Linux/Windows 文件系统。

## 0.9.0 Media 播放器与开始菜单（宿主样本验证）

- 加入图形 Media 应用：MP3 解码并软件重采样为 HDA 所需的 48 kHz 双声道 PCM；
  MPEG-1 Program Stream 显示视频，可选 MP2 音轨；保留 48 kHz PCM WAV。
  使用原有独占像素租约和音频接口，不支持 MP4/H.264/AAC。
- 桌面顶栏 Media 快捷入口、文件关联、F3 快捷键及 F10/鼠标可用的 N Start
  菜单。文件上限从 128 KiB 扩为 4 MiB，文件增长对非整幂容量按需上取整；
  `import_media.py` 可在虚拟机关机后向 GPT 镜像的非活动快照槽导入媒体。
- 16 组宿主回归含真实 MP3/MPEG-1/MP2 解码样本、图形逐块绘制和
  双槽导入校验；当前环境无 QEMU/OVMF，实际 HDA 播放及 UEFI 点击流程
  仍待运行验证。

## 0.9.0 HDA 音频与文件管理器修订（宿主夹具验证）

- 新增 PCI HDA 模拟 pin/DAC 输出，48 kHz 双声道 16-bit PCM 的版本化
  `NV_SUB_AUDIO` 接口、用户态 `wave` WAV 播放和测试音；QEMU 可用
  `start.py --audio` 附加 HDA 设备。仅短文件、单输出、同步分块播放。
- 文件管理器显示路径、文件类型、字节数和真实声卡状态；`.wav` 可双击播放，
  操作失败指出具体动作，移除演示版号和展示式配色。
- x64 构建和 14 组 UBSan 宿主回归通过；guest 预期 139 项待 QEMU/实机运行。
  实体 HDA、耳机插拔、连续播放和 UEFI 桌面仍未运行验证。

## 0.9.0 NVMe 数据盘与鼠标桌面扩展（宿主夹具验证）

- 首个 PCIe NVMe 控制器的 512B NVM namespace 支持单扇区读/写与 Flush；
  沿用 GPT/NVSTORE 分区校验和仅限快照槽的写入策略；I/O 出错后停止使用。
- xHCI Boot Mouse 三字节输入与受屏幕租约保护的 `NV_SUB_INPUT` ABI；
  桌面单击选中、双击打开，保留已有键盘操作。
- `make -j4` 和 13 组 UBSan 宿主回归通过；guest 预期 137 项未在本环境运行。
  尚无 QEMU NVMe/xHCI 启动或实体设备验证。范围见 [DEVICES.md](DEVICES.md)。

## 0.9.0 应用接口与像素桌面扩展（待 UEFI 运行验证）

- 在兼容 ABI 1 基础上公开 x64 SDK 头文件；新增 `NV_SUB_DISPLAY=4` 模式查询、独占租约、按矩形提交用户像素缓冲和释放。拒绝越界/无效用户缓冲，进程退出时恢复字符屏。
- UEFI GOP 帧缓冲移到独立 supervisor 页表空间，上限 64 MiB，不再占用低物理内存中的 8 MiB 虚拟窗口。共享原有 5×7 字体表。
- 新增 `desktop` 键盘图形文件浏览器和外部程序源码示例；支持浏览、进入目录、启动 Folio 及保存数据盘。尚无鼠标、多窗口、Unicode 或 GPU 模式设置。
- x64 构建和宿主回归结果见 [TESTING.md](TESTING.md)；当前环境没有 QEMU/实体 UEFI 验证。

## 0.9.0 实体网络源码扩展（待设备验证）

- x64 新增 Intel I225-V/LM、I226-V/LM PCIe DMA 轮询驱动；xHCI 新增
  USB CDC-ECM 数据口和 ESP USB Dongle 示例固件的 CDC 控制口。
- 新增 ARP/IPv4、DHCP 地址申请、ICMP echo 应答、UDP 收发与 `net` 命令；
  ESP USB Dongle 可使用 `net wifi scan` / `net wifi join SSID` 配置设备无线侧。
- 10 组 UBSan 宿主回归通过；无 QEMU、实体硬件验证。普通笔记本内置 Wi-Fi
  不具备驱动；具体支持和配置限制见 [NETWORK.md](NETWORK.md)。

## 0.9.0 GPT 分区源码扩展（尚未完成 QEMU 复验）

- 新数据盘默认 protective MBR + 主/备 GPT + 一个 C: Nuvora 分区；首次建盘可指定 2–4 个分区。
- x64 扫描和校验 GPT，报告其他类型分区，只挂载有有效 NVSTORE2 头的 Nuvora 分区；旧 NVSTORE1/2 整盘镜像兼容。
- C:/、D:/ 等路径与独立快照，Loom 增加 `volumes`、`partitions`；宿主 GPT/卷边界夹具运行通过。
- 没有系统安装器、现有磁盘的在线改分区、NTFS/FAT 或更多控制器支持。

- 两种 64 位架构共享分阶物理页空闲索引与 16–2048 字节 slab 小对象分配器；x64 优先使用普通 RAM、保留 4 GiB 以下 DMA 页，ARM64 验证碎片、合并与 slab 页回收。详见 [MEMORY-0.9.0.md](MEMORY-0.9.0.md)。
- 清理仅支持旧 i686 内核的分支和用户程序条件代码；x64 BIOS 的 32 位引导跳板仍用于进入长模式，不提供 32 位内核或应用。

## 0.8.0

- 构建目标收敛为 x86-64 和 ARM64；移除 i686 的构建、启动和用户程序入口。x64 的 32 位 Multiboot 加载容器仍用于进入长模式，运行内核和用户程序均为 64 位。
- x64 用户堆由 4 MiB 增至 512 MiB；实际运行 64 MiB、256 MiB 模型缓冲，逐页验证清零、写入、释放后物理页计数和低内存回滚。保持 ABI 1 的 1–2 GiB 用户地址窗口、64 GiB 物理管理上限及存储格式。
- ARM64 新增 QEMU virt Image/DTB 引导、EL1 分级页表、物理页所有权、设备树发现的 PL011、EL0 用户代码/数据隔离、SVC 异常入口和用户态 NEON 点积。64/256/1024/5120 MiB 四档模拟运行，各 10 项断言通过；5 GiB 跨过物理 4 GiB 边界。
- ARM64 仍是计算/内存移植基线，尚无 x64 用户环境、完整 ABI、调度器、磁盘、网络或 GPU/NPU 驱动。AI 训练的完整内核与运行时要求见 [ARM64-AI.md](ARM64-AI.md)。

## 0.7.3

- 修复快照恢复文件的非整幂容量扩容超过单文件上限，导致堆空间浪费和可避免的 ENOMEM。
- 增加真实 ramfs 与内核堆联合测试，覆盖 4 种恢复长度、充足/临界/不足内存、内容保留、稀疏区清零及删除回收。
- 增加双架构 32 MiB 磁盘快照启动回归；每架构独立检查 7 项。完整执行矩阵 53/42 组、宿主源码回归 7 组通过；常规 probe 保持 130/123 项。

根因及修复前的失败证据见 [MEMORY-0.7.3.md](MEMORY-0.7.3.md)。

## 0.7.2

本轮专注内存管理，修复两项有失败复现的问题；Linux 材料未修改，ABI 和磁盘格式保持不变。

- `phys_ptr` 对全部非零物理页统一使用 supervisor 别名，修复连续缓冲跨过 1 GiB 后指向用户页的错误；保留 NULL 分配失败语义。
- x64 的 8 MiB 内核堆使用独立的 1 TiB 虚拟窗口，逐页建立映射，支持碎片化物理 RAM。分配成功后才发布映射并固定物理页，失败时回滚；进程共享该 supervisor/NX 映射，销毁进程不会回收堆页。
- 新增真实分配器与堆页表夹具、12000 轮堆操作、5 个 OOM 注入点、32 MiB 内存空洞启动、用户堆跨页表边界反复扩缩，以及用户态读取内核映射隔离检查。
- x64 每轮用户 probe 130 项、i686 123 项；完整矩阵为 52/41 组，UBSan 源码夹具为 6 组。SSE #XM 递交仍单独标记 SKIP。

详细失败复现、实现和验证范围见 [MEMORY-0.7.2.md](MEMORY-0.7.2.md)。

## 0.7.1

本轮仅修复 Nuvora；保留原附 Linux 材料的字节内容。ABI 1、NVSTORE1/NVSTORE2 与 `.nvd` 格式保持兼容。

- 修复 64 位对齐掩码截断、物理地址与内核虚拟指针混用、NX 位误当页地址，以及内核栈/MMIO 窗口覆盖区被重复分配。新增 supervisor 物理映射别名，并用于页表、用户缓冲复制和 xHCI DMA 页的 CPU 访问。
- 修复 i686 帧缓冲状态缺失导致的链接失败。x64 的 8 MiB 内核堆改从可用 RAM 分配并保留，缩小固定加载映像，避开 OVMF 的 ACPI NVS 区。
- 修复 EFI Boot Services 表缺失 `SignalEvent` 导致的函数偏移错误、ELF 头布局/读文件位置、PE32+ 可选头和重定位提取。EFI 使用 large code model 和 DIR64 重定位。
- UEFI 在写入前校验全部 ELF 段与入口；检查目标 RAM 的所有权，重试退出服务时刷新内存图；使用 stub 和内核自有栈完成移交，避免覆盖固件保留数据。
- 修复 NVSTORE 签名/版本不一致和几何溢出检查、ATA 寻址能力边界与 flush 命令选择。低内存无法恢复有效快照时禁止覆盖，限制小槽缓冲，写入前清零扇区尾部。
- 修复 ESP 构建的参数顺序、错误被忽略与非原子覆盖；支持 OVMF CODE/VARS pflash，修复 q35 无数据盘时的 ESP 接口；启动器新增 `--memory`。
- 增加 4 组直接包含真实 C 源码的 UBSan 边界夹具。修正 4 TiB 测试目录和测试范围说明；UEFI ISO 检查只从光驱启动。打包要求完整回归完成记录和源码/二进制 SHA-256 一致。

本轮实际结果见 [TESTING.md](TESTING.md) 和构建目录中的执行日志。下列 0.7.0 及更早段落是原包的历史说明，不能作为本轮验证证据；其中旧的重定位描述已由上面的 DIR64 实现取代。

## 0.7.0（原包记录）

本版加入 UEFI 启动、GiB 级物理内存管理和更大的数据盘，保持 ABI 1、`.nvd` 文档与旧 `NVSTORE1` 数据盘兼容。

- x64 新增独立 UEFI 启动路径：`arch/x86_64/uefi.c` 是自包含 EFI 应用，负责从 ESP 读取 ELF64 内核并加载到 1 MiB，把 EFI 内存图、GOP 帧缓冲和配置表中的 ACPI RSDP 经中性 `boot_info` 结构移交内核，再调用 `ExitBootServices` 进入长模式。PE32+ 镜像由 `scripts/mkuefi.py` 从 `--emit-relocs` 链接的 stub ELF 生成，携带基址重定位表（首选基址被占用时固件可自行搬移）；读取内核 ELF 失败时枚举全部 SimpleFileSystem 卷回退（覆盖 El Torito 光驱启动等固件差异）。`make` 产出 `build/x86_64/BOOTX64.EFI`，`make esp` 用 mtools 打包 ESP 镜像，`make iso-uefi` 产出纯 UEFI El Torito ISO；`start.py --uefi` / `run.py --uefi` 通过 OVMF/edk2 固件启动，可用 `NV_OVMF` 指定固件文件。
- 帧缓冲控制台：UEFI 启动时使用 GOP 线性帧缓冲渲染 80×25 文本（内核内置 5×7 点阵字体，专用 8 MiB supervisor 映射窗口），Folio 全屏界面同样可用；BIOS VGA 与串口路径行为不变。
- 大内存：x64 物理管理上限从 128 MiB 提升到 **64 GiB**——前 32 MiB 用 4 KiB 页实施内核镜像的只读/NX 保护，其余用 2 MiB 大页恒等映射，物理页地址全部 64 位化（`page_alloc` 返回 `uptr`）。设备 DMA 走独立的 `page_alloc_below(4 GiB)` 显式掩码，xHCI 环与上下文仍按"低于 4 GiB"的保守边界分配。内存耗尽自检改为 donor 页加所有权位图回收，不再随 RAM 容量线性占用内核堆。i686 上限提升到 192 MiB（受非 PAE 页表与内核栈窗口约束）。新增 1 GiB 与 5 GiB（跨 4 GiB 边界）配置下的完整用户态断言回归。
- 大存储：ATA PIO 驱动在驱动器报告支持时使用 LBA48 命令，`disk_read`/`disk_write` 接受 64 位 LBA；`NVSTORE2` 头的总扇区数扩展为 u64，镜像可超过 2 TiB。快照槽 1 MiB → 16 MiB，快照缓冲由 `page_alloc_run` 连续物理页动态分配（上限 16 MiB、受空闲内存四分之一约束，失败时降级到小缓冲）。旧 `NVSTORE1` 8 MiB 镜像仍可直接使用（保留 1 MiB 快照上限）。
- 启动移交统一为 `include/nv/bootinfo.h` 的 `boot_info`：Multiboot 路径在 C 入口前完成同样的转换与校验；`common/acpi.c` 新增已知 RSDP 地址的直通解析入口（签名/长度/校验和仍然全部验证）。

实现与测试边界：UEFI 路径在 QEMU OVMF/edk2 固件上验证，重定位表覆盖绝对寻址（DIR64/HIGHLOW），未在实体主板固件上认证；x64 超过 64 GiB 的物理内存仍不管理；帧缓冲控制台不提供 VGA 硬件光标之外的加速。本轮实现参考了用户提供的 Linux 7.2.6 源码中 EFI stub 的 ExitBootServices 重试与内存类型归类、`locate_handle_buffer` 枚举回退以及 x86_64 直接映射的大页策略，代码按 Nuvora 的分配器、错误模型和测试接口独立编写，未复制或链接 Linux 源码。

升级前关闭旧虚拟机并备份 `build/ARCH/nuvora-store.img`，将旧镜像复制到新包同名位置。发布包不附带用户数据镜像。

## 0.6.0

本版补齐现代 x86 平台发现的第一层，并保持 ABI 1、`NVSTORE1` 数据盘和 `.nvd` 文档兼容。

- 新增 RSDP 的 EBDA/高 BIOS 搜索、RSDT/XSDT 回退和 MCFG 解析；所有表检查签名、长度、整表校验和、记录边界、地址溢出、对齐、总线范围与重叠。
- 新增 segment 0 PCIe ECAM。每个 function 可读取 4096 字节配置空间，未覆盖总线继续使用 CF8/CFC；启用前交叉检查两条路径。`nv.no-ecam=1` 与 `start.py --no-ecam` 提供恢复回退。
- MMIO 区最后一页改为 supervisor-only 临时固件/ECAM 映射窗口，其余 511 页供 xHCI 与设备 BAR 使用；i686 明确拒绝 4 GiB 以上映射。
- GPU 只读快照新增 AER、ACS、ATS、SR-IOV、Resizable BAR、PASID、DPC 扩展能力；坏偏移、环和过长链有界停止。能力标记不代表对应硬件功能已启用。
- ABI 1 的 `NV_HARDWARE` 新增 PLATFORM 子操作和固定 64 字节结构；新命令 `firmament` 显示 ACPI、MCFG、ECAM 与回退状态，31 个命令全部保留统一 `--help`。
- 两架构增加 ACPI 11 项和 PCI 25 项合成解析测试、QEMU q35 ECAM/禁用回退及完整 guest probe。x64 / i686 每轮分别为 126 / 121 项用户态断言，完整宿主矩阵为 46 / 40 组。

实现时参考了用户提供的 Linux 7.2.6 源码中的表查找、MCFG 区域表示和 x86 MMCONFIG 映射策略，但代码按 Nuvora 的小型分页、错误模型和测试接口独立编写；发布包不包含 Linux 源码或目标文件。当前仍没有 AML、电源管理、PCI 资源重新分配、多 segment、MSI/MSI-X 启用、IOMMU、UEFI 或原生 NVIDIA 驱动。

升级前关闭旧虚拟机并备份 `build/ARCH/nuvora-store.img`，将旧镜像复制到新包同名位置。发布包不附带用户数据镜像。

## 0.5.1

整合 `files.zip` 的 `abi.h`、`kernel.h`、`cpu.c`、`gpu.c`、`syscall.c`，保留用户新增的设备控制框架。

- 新增 `NV_DEVCTL=28`，通过子系统与操作编号分发到 CPU/GPU，预留网络接口。旧系统调用、文件格式和 ABI 版本保持兼容。
- 保留 GPU MAP_BAR 的一页内核映射准备，增加完整输出校验与按 BAR 缓存，修复失败请求及重复调用耗尽 MMIO 空间的问题。映射失败时长度为 0，不暴露内核指针。
- 增加 16 字节输入/输出 union 及用户态 `devctl` / `gpu_prepare_bar` 封装。通用错误文本可用于设备控制；anchor 缺少数据盘仍提供具体说明。
- CPU 控制、网络和 GPU SET_MODE/PRESENT/SUBMIT 明确返回 ENOSYS；未知子系统/操作返回 EINVAL。没有新增动态模块加载、显示加速或网络能力。
- 新增设备控制指针、重复请求和权限回归；`forge probe devctl` 可定向执行。x64 / i686 分别每轮 125 / 120 项用户态断言，完整宿主矩阵仍为 44 / 38 组。

GPU 映射窗口仍不是已知的 BAR 容量；不会访问 GPU 寄存器或让用户态访问 MMIO。详细契约、整合修正和限制见 [DEVCTL.md](DEVCTL.md)。SSE #XM 与实体 NVIDIA 的验证限制保持不变。

升级前关闭旧虚拟机并备份 `build/ARCH/nuvora-store.img`，将旧镜像复制到新包同名位置。发布包不附带用户数据镜像。

## 0.5.0


本版完善 CPU 启动兼容性与进程浮点状态隔离，并为 NVIDIA 显卡建立只读 PCI 发现和资源查询基础。x64 / i686 同时交付源码、预编译内核与 BIOS ISO。

- 在 C 入口前检查 CPU 必要特性，不满足条件时向 VGA/串口输出明确错误并停止。
- 支持 x87、MMX、SSE 状态的立即保存/恢复；x64 保存全部 16 个 XMM，i686 按 CPU 能力使用 FXSAVE 或传统 FNSAVE。新进程清零状态，成功 EXEC 重置，失败 EXEC 保留。
- 共享 PCI 访问与多功能设备枚举供 USB/显卡使用，加入 NVIDIA 标识、32/64 位 BAR、MSI/MSI-X/PCIe 能力查询；显卡路径没有寄存器写入或驱动加载。
- 新命令 `silicon`、`prism`，新测试程序 `vector`；30 个命令和 8 个程序均有帮助。运行脚本支持 `--cpu MODEL`。
- ABI 1 追加 `NV_HARDWARE=27`，旧调用号、`.nvd`、`NVSTORE1` 和既有用户数据格式保持兼容。
- 验证不同 Intel/AMD QEMU CPU 模型、缺失特性时的拒绝路径、浮点隔离、无显卡与多功能显卡、合成 NVIDIA 配置解析，并回归存储、USB、Folio 与帮助。SSE #XM 在 QEMU TCG 中明确标为跳过，未作实机验证。

用法：`silicon`、`prism`、`forge vector`；详细功能边界见 [CPU-GPU.md](CPU-GPU.md)，执行记录见 [TESTING.md](TESTING.md)。NVIDIA 尚无原生显示、3D 或 CUDA 驱动；CPU 仍为单核调度。

升级时关闭旧虚拟机、备份 `build/ARCH/nuvora-store.img`，将旧数据镜像复制到新包相同架构的同名位置。发布包不含用户数据镜像。新内置程序会使用追加的系统调用，应与新内核配套运行。

## 0.4.0


本版在保留 0.3.0 ABI 和数据盘兼容性的基础上加入 xHCI USB 设备识别、USB Boot Protocol 键盘输入和统一命令帮助系统。

## 变化

| 模块 | 变化及结果 |
| --- | --- |
| USB 控制器 | PCI 扫描识别 UHCI/OHCI/EHCI/xHCI；xHCI 建立 DMA 命令环、事件环和设备控制传输；不支持的控制器明确显示 unsupported。 |
| USB 外设 | 读取设备、配置、接口和字符串描述符，显示 VID/PID、速度、类别、序列号、Hub 父子端口；Hub 递归枚举并处理连接变化。 |
| USB 输入 | USB Boot Protocol 键盘支持普通键、修饰键、导航键、功能键、Caps Lock、自动重复，可用于 Loom 和 Folio；鼠标和 U 盘当前仅识别。 |
| 设备安全 | DMA 页固定来自内核物理页分配器，控制器失败后停止并隔离页；拔除设备先完成 Disable Slot 再归还资源。USB 查询指针和索引完整校验。 |
| 命令帮助 | 新增 `help`、`ports`；`atlas` 保留为别名。28 个 Loom 命令都接受 `命令 --help`，显示用途、语法、示例和限制，不执行副作用。7 个内置程序也接受 `forge APP --help`。 |
| 测试 | 增加 x64/i686 的 USB 描述符、键盘、Hub、事件/命令环回绕、66 次热插拔、只读 U 盘和完整帮助回归。 |

## 使用

启动后输入：

```text
help
ports
ports --scan
ports --help
forge folio --help
```

`python3 start.py` 默认接入 QEMU xHCI、USB 键盘和 USB 鼠标；`python3 start.py --no-usb` 可复现没有 USB 控制器的启动。实际硬件支持范围取决于 xHCI 控制器实现，本版没有 UEFI、SMP、网络或 Linux ABI。

0.4.0 保留 ABI 1 中 0–25 的调用编号，追加 `NV_USB` 为 26；原有 `.nvd` 文档、`NVSTORE1` 数据盘和 0.3.0 程序接口保持兼容。升级时先关闭旧虚拟机并备份旧包内的 `build/ARCH/nuvora-store.img`，再将需要使用的数据盘复制到新包相同架构的同名位置。发布包没有附带运行中的用户数据盘。

## 0.3.0


本轮完善内核的进程生命周期、地址空间管理和异常诊断。x64 与 i686 均提供源码、预编译内核、BIOS ISO 和实际执行日志。

## 核心变化

| 模块 | 变化及结果 |
| --- | --- |
| 程序执行 | 新增 `NV_EXEC` / `exec_program`。先完整加载新映像，成功后替换当前程序；失败时调用者继续运行。保留 PID、父子关系、cwd、文件句柄及偏移。 |
| 进程退出 | 在离开退出进程的栈后回收用户页、页表和内核栈。父进程暂未 WAIT 时只保留退出状态，不再占用整套内存。 |
| 内核栈 | 每进程 16 KiB 栈由物理页分配，上下各保留一页不映射；x64 栈页为 NX。 |
| 严重故障 | x64 使用 IST，i686 新增专用 TSS/task gate。实际栈溢出后可以在应急栈输出双重故障诊断；PID 1 退出也会明确报告。 |
| 物理页 | 独立记录分配所有权，检测释放保留页或重复释放；位图从上次搜索位置继续扫描。 |
| 内存不足 | 页表和内核栈的部分分配会完整回滚。EXEC、SPAWN 和 GROW 失败不泄漏页、不破坏原程序。 |
| 文件路径 | 创建前检查解析后的完整绝对路径，最长 191 字节；相对路径不能绕过上限。文件重命名拒绝以 `/` 结尾的目标路径。 |
| PID 分配 | 每次选取 PID 时检查是否占用，回绕后继续保持正数且不复用仍在等待领取的退出状态。 |
| 发布验证 | 打包核对已测试内核、程序、测试脚本和 ISO 的 SHA-256，并校验 ZIP 中每个文件；不混入旧版 ISO。 |

## 实际验证

修改前，新加入的回归用例在旧内核上复现了两项失败：未领取状态的进程保留内存，以及超长完整路径仍可创建。

修改后，两种架构分别在 32、64、128、256 MiB 虚拟机配置下通过全部用户态检查：x64 每轮 98 项，i686 每轮 94 项。每种架构共有 18 组宿主检查，包含三项预期故障诊断、真实用户进程内存耗尽、磁盘恢复、Folio 编辑保存和 BIOS ISO 启动。完整结果见 `build/ARCH/test-results/RESULTS.md`。

内存压力测试连续八轮确认 EXEC/SPAWN/GROW 返回内存不足，原程序继续有效；释放压力进程后核对页、堆、节点和进程计数，并再次启动程序。EXEC 另有十二轮重复替换及二十轮无效 ELF 回滚验证。

## 使用与升级

启动方式不变：安装 Python 3、QEMU 后在解压目录执行 `python3 start.py --window`，默认启动 x64。进入 Loom 后用 `origin` 查看版本，用 `folio /home/report.nvd` 创建或编辑文档。

0.3.0 保留 ABI 1 的已有调用编号，`EXEC` 追加为 25；原有 `.nvd` 文档与 `NVSTORE1` 数据盘格式保持兼容。升级时先关闭旧虚拟机并备份旧包内的 `build/ARCH/nuvora-store.img`，再将需要使用的数据盘复制到新包相同架构的同名位置。发布包没有附带运行中的用户数据盘。

本版仍为实验内核：单核、最多管理 128 MiB RAM，没有网络、UEFI、SMP、Linux ABI 或中文输入。内核异常会停止系统并报告，尚未实现 init 自动重启、按需分页、内存配额或 OOM 自动终止策略。
