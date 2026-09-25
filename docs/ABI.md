# Nuvora ABI 1

ABI 与 Linux 不兼容。共同常量和结构以 `include/nv/abi.h` 为准，用户侧封装在 `user/runtime.h`。

x64 使用 `int 0x81`。调用号在 EAX，前三个参数在 EBX / ECX / EDX，返回值读取 EAX 的有符号 32 位值。x64 内核保存完整 64 位寄存器，但 ABI 1 的调用号、参数和用户地址仍限制为 32 位；非零高位返回 `-NV_EINVAL`。普通用户地址必须位于当前进程实际映射的 1–2 GiB 窗口内。

除 `CLOCK` 的无符号 tick 位模式外，负数表示 `-NV_E...`，非负数表示结果。零长度 I/O 不解引用缓冲区。每次 I/O 上限为 16384 字节。

| 号 | 操作 | EBX | ECX | EDX | 成功结果 |
| --- | --- | --- | --- | --- | --- |
| 0 | EMIT | fd | 输入缓冲区 | 字节数 | 写入字节数 |
| 1 | TAKE | fd | 输出缓冲区 | 字节数 | 读取字节数；文件结尾为 0 |
| 2 | OPEN | 路径字符串 | 打开标志 | 0 | fd |
| 3 | CLOSE | fd | 0 | 0 | 0 |
| 4 | SEEK | fd | 有符号偏移 | 0/1/2：开始/当前位置/结尾 | 新位置 |
| 5 | LIST | 目录路径 | 从 0 起的索引 | `nv_dirent*` | 有条目为 1，结束为 0 |
| 6 | MKDIR | 目录路径 | 0 | 0 | 0 |
| 7 | REMOVE | 路径 | 0 | 0 | 0 |
| 8 | MOVE | 源路径 | 目标路径 | 0 | 0 |
| 9 | CHDIR | 目录路径 | 0 | 0 | 0 |
| 10 | GETCWD | 输出缓冲区 | 容量，含结尾 NUL | 0 | 0 |
| 11 | SPAWN | 程序路径 | 参数字符串 | 0 | PID |
| 12 | WAIT | 子进程 PID | 0 | 0 | 子进程退出状态 |
| 13 | EXIT | 退出状态低 8 位 | 0 | 0 | 不返回 |
| 14 | SLEEP | 毫秒，最多 86400000 | 0 | 0 | 0 |
| 15 | YIELD | 0 | 0 | 0 | 0 |
| 16 | GROW | 有符号页数 | 0 | 0 | 改变前的 break 地址 |
| 17 | INFO | `nv_info*` | 0 | 0 | 0 |
| 18 | TASK | 进程槽索引 0–31 | `nv_taskinfo*` | 0 | 有条目为 1，无条目为 0 |
| 19 | STOP | PID | 0 | 0 | 0 |
| 20 | CLOCK | 0 | 0 | 0 | 无符号 32 位 tick |
| 21 | CONTROL | 子操作 | 子参数 | 0 | 0 或不返回 |
| 22 | SURFACE | 屏幕操作 | `nv_surface*` 或 0 | 0 | 0 |
| 23 | KEY | 0 | 0 | 0 | 键码 |
| 24 | REPLACE | 源路径 | 目标路径 | 0 | 0 |
| 25 | EXEC | 程序路径 | 参数字符串 | 0 | 进入新程序，不返回 |
| 26 | USB | 子操作（1 控制器、2 设备、3 重扫） | 索引 | 输出结构指针 | USB 信息或 0/1 |
| 27 | HARDWARE | 子操作（1 CPU、2 GPU、3 PLATFORM） | 索引 | 输出结构指针 | 有条目 1，无条目 0 |
| 28 | DEVCTL | 子系统编号 | 子系统操作 | 固定大小输入/输出缓冲区 | 按操作返回结果 |
| 29 | VOLUME | 卷索引 0–3 | `nv_volume_info*` | 0 | 有挂载卷为 1，否则 0 |
| 30 | PARTITION | 列表索引 0–31 | `nv_partition_info*` | 0 | 有分区记录为 1，否则 0 |

`VOLUME` 和 `PARTITION` 仅由 x64 内核实现，所有输出指针先验证整个可写结构体。
`PARTITION` 报告经过 GPT CRC、范围和重叠检查的记录，格式未知的记录不会挂载。
数据镜像默认一个 GPT C: 分区，旧 NVSTORE 整盘镜像继续兼容为 C:；
每卷快照独立，但 `anchor` 不是跨卷事务。接口仍为 ABI 1 的末尾追加调用号。

打开标志：READ=1、WRITE=2、CREATE=4、TRUNC=8、APPEND=16、EXCL=32。CREATE/TRUNC/APPEND 要求 WRITE，EXCL 要求 CREATE。追加总是使用当时文件结尾，即使此前调用 SEEK。

CONTROL：1 保存 `/home`，2 关机，3 重启，4 清空 VGA，5 测试退出。除清屏外通常仅 PID 1 可以调用；持有屏幕租约的进程也可执行保存（Folio 使用此能力）；测试退出还要求启动参数 `nv.test=1`，通过 QEMU `isa-debug-exit` 返回宿主状态。正常测试成功宿主退出码 33，失败为 35。

`USB` 的 `CONTROLLERS` 子操作使用 `ECX` 索引（0–7）和 `EDX` 的 `struct nv_usb_controller*`；`DEVICES` 使用 0–31 索引和 `struct nv_usb_device*`。返回 1 表示条目存在，0 表示该索引当前为空。`RESCAN` 要求 `ECX=EDX=0`，处理根端口和 Hub 的连接变化。所有输出结构在写入前进行完整用户可写页检查，越界或只读指针返回 `-NV_EFAULT`，索引越界返回 `-NV_EINVAL`。控制器状态为 `unsupported`、`running` 或 `failed`；xHCI 驱动读取标准 USB 描述符并配置 Hub 和 USB Boot Protocol 键盘。鼠标、U 盘等其他设备只记录识别信息，本版没有 USB 存储文件系统挂载或鼠标指针接口。

STOP 允许 PID 1 管理其他进程；普通进程只能终止自己的子进程；任何进程均不能通过 STOP 终止 PID 1 或自己。WAIT 对退出状态只允许领取一次。未领取状态的进程会保留 zombie 元数据，但它的用户页、页表和内核栈在安全切换后释放。主动终止状态为 143；CPU 异常状态为 `128 + vector`。

`GROW` 以 4096 字节页为单位，正数增加、负数归还、零查询。x64 分配上限为每进程 512 MiB；失败不推进 break。返回值与 `sbrk` 的形状相似，但不是 POSIX 接口。运行库将错误符号扩展到宿主指针宽度，可用 `(iptr)result < 0` 判断。

程序入口由 `user/start64.S` 转入 `int user_main(const char *args)`。x64 内部 C 调用遵循 SysV AMD64 整数调用约定；系统调用使用上述独立约定。x64 程序必须禁止 red zone。内核 C 和随包通用应用继续以整数指令编译；专用用户程序可在查询能力后使用 x87/MMX/SSE，不能启用 AVX/XSAVE 等尚未支持的状态。浮点 C 调用约定和完整 libc 不在当前 ABI 范围。ARM64 当前只有测试用 SVC，还没有这一 ABI。

## Folio 屏幕与提交调用

ABI 1 还包括三个 Folio 专用调用。`SURFACE` 的 EBX 是操作（1 获取、2 提交、3 释放），ECX 是 `nv_surface*`；获取和释放传零指针，提交时指针指向 4004 字节的 `80×25` 单元表和光标索引。单元低字节必须是 ASCII 32–126，高字节是 VGA 颜色属性；光标值 2000 隐藏光标。一个进程持有屏幕时其他进程获取租约返回 `-NV_EBUSY`，无租约提交返回 `-NV_EACCESS`；进程退出或成功 EXEC 会自动释放。

`KEY` 无参数，成功返回 ASCII、导航键或功能键；低 12 位是键值，`NV_KEY_SHIFT`、`NV_KEY_CTRL` 和 `NV_KEY_ALT` 表示修饰键。无键可读时返回 `-NV_EAGAIN`。`REPLACE` 接受源临时文件和目标路径，在内核关闭中断的系统调用内完成文件替换（Folio 使用同目录临时文件），目标打开、受保护或类型不兼容时失败。

## EXEC 的提交语义

`exec_program(path, args)` 接受与 SPAWN 相同的静态 ELF 和参数字符串。路径按调用进程当前目录解析，成功后从新程序入口继续，保留 PID、父进程、已有子进程、当前目录、累计 CPU tick 及 fd 3–15 的打开标志和偏移。用户堆和浮点/SIMD 状态重置，旧用户页和页表释放；新程序从独立零初始化的 BSS、堆和栈开始，原屏幕租约释放。

加载新映像期间旧映像仍然存在，因此需要足够的临时物理内存。任何检查或分配失败都返回错误，原程序、堆、文件句柄和屏幕租约保持有效。此版本没有 close-on-exec 标志、动态链接器或环境变量接口。已有调用号保持不变，ABI 版本仍为 1。

## HARDWARE 查询

调用号 27 不改变已有 0–26 号调用。CPU 子操作要求 ECX=0，EDX 指向 140 字节的 `nv_cpu_info`；GPU 子操作要求 ECX 为 0–15，EDX 指向 176 字节的 `nv_gpu_info`；PLATFORM 子操作要求 ECX=0，EDX 指向 64 字节的 `nv_platform_info`。完整缓冲区必须处处可写，否则在任何写入前返回 `-NV_EFAULT`。错误操作/索引返回 `-NV_EINVAL`。CPU 与 PLATFORM 返回 1；GPU 返回 1 表示存在，0 表示该索引没有设备且不修改输出。

字段与位标记以 `include/nv/abi.h` 为准。所有数字为固定 32 位字段；64 位 BAR 与 ECAM 基址由 `high` / `low` 拼接，保持既定的固定字段布局。保留字段为零。CPU `usable` 表示内核启用的状态，原始 CPUID 特性不自动等于可用功能。GPU `state=NV_GPU_DISCOVERED` 仅表示发现设备；`capabilities` 和 `ext_capabilities` 不代表中断、隔离或加速已经启用。

`nv_platform_info.flags` 报告 ACPI、XSDT、MCFG、ECAM、CF8 回退及命令行禁用 ECAM 的状态。`config_bytes` 为当前覆盖设备可读取的配置空间大小：ECAM 为 4096，纯 CF8 为 256。`mcfg_entries` 是固件记录数，`ecam_regions` 是实际启用数，`rejected_entries` 包含坏校验/长度/范围/重叠、非 segment 0、超出架构物理地址和 ECAM/CF8 交叉检查不一致的项目。当前只使用 segment 0；第一个活动范围的总线与基址写入结构。详细范围见 [CPU-GPU.md](CPU-GPU.md)。

## DEVCTL 扩展分发

`NV_DEVCTL=28` 保留原有 0–27 号接口。CPU/GPU/网络子系统分别为 1/2/3；当前 CPU/网络控制及 GPU SET_MODE/PRESENT/SUBMIT 返回 `-NV_ENOSYS`。GPU MAP_BAR 使用 **16 字节** `union nv_gpu_map_bar_io`，将 8 字节请求覆盖为 16 字节响应。先检查完整输出，再准备并复用内核专用映射，不返回用户态地址；失败时有效缓冲区收到全零响应，EFAULT 不写入。完整约定和代码示例见 [DEVCTL.md](DEVCTL.md)。
