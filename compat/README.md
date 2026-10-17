# 可选的系统兼容模块

其他系统的 ABI 适配和运行时移植集中在本目录。Nuvora 原生进程、桌面、驱动、
文件系统与账户服务直接使用原生接口；兼容模块调用这些服务，并沿用账户权限检查。

| 目录 | 内容 |
| --- | --- |
| `linux/` | Linux x86-64 系统调用、ELF/TLS 装载、VMA、线程/futex、信号与进程状态 |
| `apps/` | 原生 `compat` 启动命令 |
| `tools/` | 可选运行时构建、固定摘要下载与独立数据盘准备工具 |
| `examples/` | musl、C++、Lua、QuickJS、MicroPython 与 JNI 接口验证代码 |
| `fixtures/` | 经摘要校验的静态/动态运行时夹具及许可证 |
| `tests/` | 宿主边界测试和真实客体运行测试 |
| `minecraft/` | 官方游戏版本、依赖摘要与验收范围 |
| `docs/` | 兼容接口、运行时使用与实际限制 |

`bridge.h`、`process_state.h`、`task.h` 和 `entry64.S` 是原生内核与可选模块的边界。
内核中的 ABI 专用生命周期逻辑位于 `task.h`；正常的原生调度、保存/恢复、
内存回收及进程退出仍由原生内核负责。现阶段仅实现 Linux 用户态 ABI，
没有移入其他系统内核。普通构建不下载运行时或游戏文件。

## 独立原生构建

```sh
make COMPAT=none -j4 diagnostics esp
python3 start.py --compat none --uefi --window
```

PowerShell 对应 `./start.ps1 run -Compat none -Uefi -Window`。

产物独立位于 `build/x86_64-native/`，不会与默认构建缓存混用。
这个配置不编译 `linux/*.c`，不装入兼容命令，也不在原生任务中保存 Linux VMA、
信号表或线程组状态。未启用的外来系统调用返回 ENOSYS。

默认兼容构建保持现有行为：

```sh
make COMPAT=linux -j4 diagnostics esp
python3 compat/tests/compat_boot_test.py
python3 compat/tests/dynamic_boot_test.py
python3 compat/tools/prepare_openjdk_probe.py
python3 compat/tests/openjdk_boot_test.py
```

普通构建不会准备或下载 Minecraft。官方客户端的实验启动盘需要单独创建：

```sh
python3 compat/tools/prepare_minecraft_probe.py --version 1.21.1
python3 compat/tests/minecraft_client_probe.py \
  --disk build/minecraft-probe/1.21.1/data.img --require-playable
```

`--require-playable` 在可玩验收未通过时返回失败；日志与 JSON 会保存实际结果。
目前 **MC 仍不可玩**，没有进入世界或完成图形、输入、保存验收。
运行时的已测范围和 Minecraft 目标见 [运行时说明](docs/RUNTIME.md)。
