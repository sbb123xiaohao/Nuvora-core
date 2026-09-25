# 设备控制扩展接口（0.5.1 引入，0.6.0 保持兼容）

0.5.1 版整合 `files.zip` 提供的五个文件：`abi.h`、`kernel.h`、`cpu.c`、`gpu.c`、`syscall.c`，引入 `NV_DEVCTL` 通用入口。当前开发版在原有 CPU/GPU 分发上增加了 `NV_SUB_NET` 网络和 `NV_SUB_DISPLAY` 固件像素帧缓冲功能；GPU BAR 请求约定见下文。

这是静态编译进内核的扩展分发接口，还不是可加载模块机制。

## 调用约定

ABI 版本保持 1，原有 0–27 号系统调用不变，追加 `NV_DEVCTL=28`：

| 寄存器 | 内容 |
| --- | --- |
| EAX | `NV_DEVCTL` |
| EBX | 子系统编号 |
| ECX | 该子系统的操作编号 |
| EDX | 操作规定的固定大小用户缓冲区 |

返回值读取 EAX 的有符号 32 位值。x64 同样不接受参数的非零高 32 位。`NV_USB` 和 `NV_HARDWARE` 的调用方式保持原样。

| 子系统 | 当前操作 | 结果 |
| --- | --- | --- |
| `NV_SUB_CPU=1` | 暂无控制操作 | `-NV_ENOSYS`；CPU 查询仍用 `NV_HARDWARE` |
| `NV_SUB_GPU=2` | `MAP_BAR=1` | 准备内核专用的一页 BAR 映射 |
| `NV_SUB_GPU=2` | `SET_MODE=2`、`PRESENT=3`、`SUBMIT=4` | `-NV_ENOSYS`，不读写请求缓冲区 |
| `NV_SUB_NET=3` | INFO、DHCP、STATIC、UDP_SEND、UDP_RECV、SELECT、WIFI_COMMAND、WIFI_READ | 见 [实体网络说明](NETWORK.md)；具体缓冲区见 `include/nv/abi.h` |
| `NV_SUB_DISPLAY=4` | INFO、ACQUIRE、PRESENT、RELEASE | 独占像素显示；契约、样例与桌面见 [SDK.md](SDK.md) |
| 未知子系统或未知 GPU 操作 | — | `-NV_EINVAL` |

## MAP_BAR 缓冲区

输入结构为 8 字节：`index` 是 `NV_HARDWARE` 返回的 GPU 索引，`bar` 为 0–5。响应占 16 字节，会覆盖同一个缓冲区：

| 字段 | 大小 | 含义 |
| --- | --- | --- |
| `ok` | 4 字节 | 成功为 1，失败为 0 |
| `reserved` | 4 字节 | 0 |
| `length` | 8 字节 | 本次内核映射窗口长度；成功为 4096，失败为 0 |

**调用者必须分配完整的 16 字节 `union nv_gpu_map_bar_io`，不能只分配 8 字节请求。** 两种架构均有编译期大小断言。内核在读取输入或分配映射前检查全部 16 字节可写；`-NV_EFAULT` 不修改任何字节，也不创建映射。缓冲区有效但参数或映射失败时，写入全零响应。

用户程序可以使用 `user/runtime.h` 中的封装：

```c
struct nv_gpu_map_bar_res result;
int status = gpu_prepare_bar(0, 0, &result);
if (status < 0) {
    report_error("GPU BAR preparation", status);
    return 1;
}
/* result.length is a kernel window size; no user-accessible address is returned. */
```

也可用 `devctl(subsystem, op, &buffer)` 调用其他已定义操作。封装不能让只读或无效的 C 输出对象变得可写，调用者仍需提供有效的 `struct nv_gpu_map_bar_res`。

## 映射范围与限制

只接受已经分配的内存 BAR，拒绝 I/O、空 BAR、64 位 BAR 的高半槽、无效编码和未分配资源。首地址必须按页对齐，并处于 CPU 和 x64 内核可寻址的范围；ARM64 目前没有 DEVCTL。

映射为 supervisor-only、不可缓存；x64 同时设为 NX。不向用户态返回内核虚拟地址，不授予用户态 MMIO 读写权，不读取或写入任何 GPU 寄存器，不启用总线主控，不执行固件。

每个启动快照中的 `(GPU index, BAR)` 只保留一份映射。重复调用和不同进程的调用复用它，退出进程不会释放设备映射；最多 16×6 页，且与 USB 共用 511 页的持久 MMIO 窗口。第 512 页由 0.6.0 保留给 ACPI/PCIe 临时映射，不会被 BAR 请求耗尽。当前没有 GPU 热插拔或 BAR 重新分配，地址只对本次启动快照有效。

**4096 只是页表窗口长度，不是已探测的 BAR 大小或显存容量。** BAR sizing、有效寄存器范围、访问权限、设备复位和真正的显示/提交驱动尚未完成；现阶段不得据此访问整页寄存器或显存。窗口准备失败返回 `-NV_EIO`，无可用内存 BAR 返回 `-NV_ENODEV`，索引错误返回 `-NV_EINVAL`。

原始修改在验证输出前创建映射，会在跨页输出失败时泄漏 MMIO 窗口；重复调用也会不断分配。本版先完整验证缓冲区，并缓存映射。失败响应的 `length` 改为 0，避免把失败结果误读为可用的 4096 字节区域。

## 验证

`forge probe devctl` 只运行设备控制回归；`trial` 包含这组检查。GPU 可映射时，x64 有 14 项断言，覆盖未知操作、预留操作、错误指针、输出只读/跨页/溢出、索引、清零错误响应、连续 600 次不完整输出请求、连续 600 次重复映射和用户态 MMIO 访问隔离。

QEMU 无显卡、单显卡和多功能显卡场景分别运行两个独立的测试进程，检查跨进程复用；无可映射 BAR 时，实际映射部分明确标记 SKIP。此前的 CPU、浮点隔离、USB、存储、Folio 和 ISO 启动回归继续保留。实体 NVIDIA GPU 尚未验证。
