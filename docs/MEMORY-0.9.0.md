# 0.9.0 物理页与小对象分配

Nuvora Core 在 x86-64 和 ARM64 的现有内存图、页所有权位图之上，使用同一份 `common/page_buddy.c` 维护对齐的 2^order 空闲块。第 0 阶直接读取原有的忙闲位图；第 1–18 阶各用一位记录两个子块是否均为空闲。每次分配或归还仅刷新受影响的祖先块，释放碎片后可以重新申请完整的对齐块。索引占用少于每个受管物理页一位；x64 按内存图上界从已映射的内核堆分配索引，ARM64 在内核映像中预留索引。上限为 64 GiB RAM，最大单次对齐块为 1 GiB。

- `page_alloc()` 优先使用 4 GiB 以上的普通页，空闲时才退回 4 GiB 以下；`page_alloc_below(limit)` 保持 USB DMA 等设备的显式低地址约束。
- `page_alloc_order(order)` 申请 2^order 个对齐、清零的连续物理页；`page_alloc_run(count)` 的非整幂请求继续从空闲位图寻找连续区，并同步更新分阶索引。
- 固件、内核映像、页表及其他保留范围在索引建立前从可用 RAM 中剔除；所有权位图区分可归还页与保留页，重复归还或归还非分配页会触发诊断。x64 内核堆的物理页仍由页表单独映射并固定。
- `common/slab.c` 按 16、32、64、128、256、512、1024、2048 字节划分小对象。每张物理页有独立的对象占用位图；最后一个对象归还时立即归还整张页。x64 的 `kmalloc` 对小对象使用该分配器，取不到新页时仍可使用原有的已映射内核堆；较大对象沿用 8 MiB 虚拟堆的可合并空闲链表。ARM64 自检直接用同一 slab 实现验证跨页对象分配和回收。

物理页分配与 slab 的元数据操作发生在当前单 CPU 内核中；x64 系统调用期间关闭可屏蔽中断，ARM64 启动路径没有并发任务。未来启用多核或内核抢占之前必须增加分配器锁或每 CPU 缓存，并验证中断上下文的调用限制。x64 `heap_used` 包括内核堆索引与已分配的 slab 对象；`heap_total` 仍描述固定的 8 MiB 虚拟堆，slab 物理页另计入空闲物理页数。

本版**没有**实现 Linux 的 memblock 早期分配器、完整 GFP 标志、NUMA 与每 CPU 页缓存、内存回收、换页、内存压缩、动态内存热插拔、内核 vmalloc 或通用页缓存。ARM64 尚无进程地址空间和动态 ELF 装载，不能运行 x64 的用户软件。两种架构共享的是物理页索引与 slab 小对象模块，不宣称复制或完整兼容 Linux 内存管理。源码为独立实现，没有从附带的 Linux 归档复制代码。

测试入口：`make ARCH=x86_64 test-host` 执行 UBSan 边界夹具；`make ARCH=aarch64 test` 在 64/256/1024/5120 MiB 下实际启动 ARM64 镜像；x64 完整启动、内存耗尽、碎片化、存储和 ISO 回归由 `python3 scripts/test.py --iso` 执行。仅最终测试记录可作为本版验证依据。

设计参考：[Linux Physical Memory](https://docs.kernel.org/mm/physical_memory.html) 和 [Linux Memory Allocation Guide](https://docs.kernel.org/core-api/memory-allocation.html)。
