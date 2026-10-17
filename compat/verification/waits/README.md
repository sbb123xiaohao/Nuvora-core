# 睡眠与信号回归

基线为完整的 `e0d2f59d1dd00b34d649acffa45c7bb0804a2083`；2026-10-17。
使用 Arch GCC 16.2.1、QEMU 11.1.1 TCG 与 OVMF，没有实体机测试。

`before-fix.log` 保留首次宿主断言失败和旧客体的失败结果。旧客体运行的
musl ELF 与 `guest-dynamic.log` 所运行的 ELF 相同：十秒 nanosleep 受到
线程信号时，旧内核没有取消等待并返回 EINTR，新内核通过该项验证。
首次宿主断言是在原夹具中只增加复现断言后执行，行号对应当时的夹具。

修复后的边界覆盖见 `sanitizers.log`；完整宿主回归见 `host-regressions.log`。
`guest-dynamic.log`、`guest-openjdk.log` 是实际客体串口日志，另附默认配置
和原生独立配置的启动测试摘要。`summary.json` 记录源码、夹具和构建产物摘要，
不包含数据盘、JDK 安装目录或编译缓存。

动态夹具使用已有的固定 musl 1.2.5 工具链重建，仅更新 `dynamic.elf`，
未修改 linker、DSO 或许可证。源码及 SHA256 在
[`compat/fixtures/dynamic/manifest.json`](../../fixtures/dynamic/manifest.json)。
可从固定源包完整重建，再运行 Linux 参考和实际客体测试：

```sh
python3 compat/tools/build_dynamic_runtimes.py --output compat/fixtures/dynamic
python3 compat/tests/dynamic_tools_test.py
make -j4 diagnostics esp
make test-host
python3 compat/tests/dynamic_boot_test.py
make COMPAT=none -j4 diagnostics esp
python3 compat/tests/native_independence_test.py
```

源码测试 `compat/tests/linux_test.c` 检查 EINTR、精确 rem、SA_RESTART、
pending 等待取消、输出撤销和别名；`linux_threads_test.c` 使用实际任务生命周期
检查默认忽略/屏蔽的信号、SIGKILL、定时唤醒和引用回收。

相对睡眠的时钟粒度为 10 ms；尚未实现完整 POSIX 信号重启与 job control。
本轮没有新增 socketpair 或图形后端，没有重新测试 Minecraft，也没有达到可玩验收。
