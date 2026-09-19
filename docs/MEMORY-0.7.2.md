# Nuvora 0.7.2 内存管理修复

基于上一轮交付的 0.7.1，源码与该版本的 SHA-256 指纹核对一致后开始修改。本轮只处理 Nuvora，原附 Linux 材料未修改，也不参与构建。

## 两项已复现的问题

| 问题 | 失败证据 | 修复 |
| --- | --- | --- |
| 连续缓冲跨 1 GiB 时指针不连续 | 以物理地址 `0x3ffff000` 开始分配两页，模拟当前进程的 1 GiB 用户映射。旧分配器清零后用户页从 `0x5a` 变成 `0x00`，实际后一页 RAM 仍是 `0xa5`。真实内核在进程页表下使用这种缓冲时可能触发页故障或破坏可写用户页。 | `phys_ptr` 对全部非零物理地址使用同一个 supervisor 别名，不在 1 GiB 处分段切换；NULL 仍表示分配失败。 |
| 可用 RAM 充足但碎片化导致启动失败 | 32 MiB QEMU 的内存表加入 6 个页大小的保留空洞后，0.7.1 报 `cannot reserve kernel heap` 并以预期 panic 代码 35 退出；原因是要求连续 8 MiB 物理 RAM。 | 8 MiB 内核堆改为 1 TiB 虚拟窗口中的 2048 个独立物理页。全部取得后才发布映射并固定页；中途失败则全部归还。相同内存空洞配置现在完成 130 项用户态断言。 |

第一项复现直接编译真实 `kernel/memory.c`，在宿主用独立映射模拟物理 RAM 和用户页；它是明确的地址空间夹具，并非伪装成实体机器测试。第二项使用真正的 QEMU guest 内核；旧版本失败日志保留在 `build/x86_64/test-results/baseline-0.7.1-fragmented.log`。

## 实现与生命周期

`kernel/kernel.h` 定义连续物理别名和独立内核堆窗口。`kernel/memory64.c` 建立 supervisor/NX 堆页表，在每个进程根页表中共享；`kernel/memory.c` 的 `page_pin` 将堆页转为永久内核保留页，普通释放和耗尽自检均不能回收它们。失败路径在映射发布前清空所有临时叶表并归还已取页面。

进程页表与物理分配器原有的正常回收路径保持原职责；本轮新增跨页表边界的反复扩缩容来核对清零、失效映射和页面计数。没有通过提高资源常量掩盖泄漏。

## 验证结果

2026-09-19，GCC 13.3.0、GNU ld 2.42、QEMU 8.2.2 TCG、Ubuntu OVMF。

| 验证 | 结果 |
| --- | --- |
| x64 完整矩阵 | 52 组通过，每轮用户 probe 130 项成功 |
| i686 完整矩阵 | 41 组通过，每轮用户 probe 123 项成功 |
| 源码回归 | 6 组通过，UBSan 开启；新增真实分配器与堆页表夹具 |
| 堆操作 | 12000 轮确定性分配/释放组合，检查已分配内容、零初始化、对齐、耗尽及最终合并恢复 |
| 堆映射失败 | 分别在取得 0、1、17、1024、2047 页后返回 OOM；确认无页泄漏、无已发布映射、无提前固定页 |
| 用户堆回收 | 12 轮 1/511/512/513/1023/1024 页扩缩容，回收后页计数恢复，原地址拒绝访问，再分配为零 |
| 内存隔离 | x64 用户进程读取物理别名和内核堆均被隔离；堆叶表保持 NX 且各级 supervisor-only |
| RAM 配置 | 两架构 32/64/128/256 MiB；x64 另有 1 GiB、5 GiB及 32 MiB 内存空洞 |
| 集成路径 | 低内存 EXEC/SPAWN/GROW 回滚、进程退出、USB 插拔和环回绕、快照、BIOS/UEFI/ISO 启动均通过 |

每次 probe 的 SSE #XM 递交仍有 1 项明确 SKIP，未计作该硬件异常已验证。6 组源码测试包含模拟固件、端口和页分配输入；QEMU 集成日志另行记录。验证不覆盖实体硬件长期运行、SMP 或所有固件组合。

## 复现入口

```sh
make -j4 all esp
make test-host
python3 scripts/test.py --phase memory
make iso iso-uefi
python3 scripts/test.py --iso
make ARCH=i686 -j4 all iso
NV_ARCH=i686 python3 scripts/test.py --iso
```

`nv.memory-test=fragmented` 只有与 `nv.test=1` 同时存在才会加入测试空洞，自动矩阵仅在 x64 的 32 MiB guest 使用。正常启动不修改内存表。完整结果见各架构 `test-results/RESULTS.md`、`results.json`、`execution.json`，源码与发布产物由 `build-fingerprint.json` 固定。

## 容量与接口边界

x64 物理管理上限仍为 64 GiB，i686 为 192 MiB；用户窗口仍为 1–2 GiB、每进程堆 4 MiB、内核堆 8 MiB。1 TiB 是内核堆的虚拟地址，不表示需要或管理了 1 TiB RAM。设备 DMA 继续使用物理地址和显式低地址限制，不能把内核堆指针直接交给设备；数据镜像和文件系统容量规则未改变。
