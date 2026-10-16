# 桌面语言运行时与 Minecraft 路线

这一阶段把主线改为 **让成熟运行时复用操作系统兼容接口**。已有的 Java
bootstrap 保留作回归夹具；继续扩充那个受限字节码子集，无法短期替代完整
OpenJDK、类库与 LWJGL。本轮新建 Linux x86-64 静态进程兼容层，已在 Nuvora
Ring 3 实际运行 musl C、C++ 核心语法示例、Lua 5.4.8、QuickJS 2025-09-13
和 MicroPython 1.26.1。
**OpenJDK 与 Minecraft 目前仍未启动**。

## Java 版本如何兼容

操作系统提供文件、虚拟内存、线程、动态链接、网络和图形接口，Java 字节码与
类库由匹配版本的 JVM 负责。目标是使 Java 8、17、21 等各自的成熟运行时共用
这些接口；不能把识别 class major 当成“所有 Java 版本兼容”。不同 JDK 的
系统调用、类库与本地库仍需分别测试，未来版本同样要验证。

首个完整游戏目标固定为 **Minecraft Java 1.21.1 / Java 21 / LWJGL 3.3.3**，
便于建立可重复的基线。版本与依赖来自 Mojang 官方元数据，URL、摘要和坐标在
[minecraft-target.json](minecraft-target.json)。没有附带游戏客户端或资源。
这不是对其他版本兼容性的承诺。

## 现在可运行的示例

普通构建不下载运行时；已验证的 ELF 与来源清单在
[tests/fixtures/compat](../tests/fixtures/compat)。先关闭对应虚拟机，然后导入
私有数据盘：

```sh
python3 scripts/import_media.py --disk build/x86_64/nuvora-store.img \
  tests/fixtures/compat/*.elf examples/compat/world.lua examples/compat/world.js \
  examples/compat/world.py examples/compat/terrain.py
```

在 Nuvora Terminal 或 Loom 运行：

```text
compat /home/runtime_check.elf "two words" ""
compat /home/language.elf
compat /home/lua.elf /home/world.lua "hello world"
compat /home/js.elf /home/world.js "hello world"
compat /home/python.elf /home/world.py "hello world"
```

`compat` 把自己的进程替换为目标，保留 PID、父进程、用户身份和工作目录。
`scatter compat ...` 后，`gather PID` 与 `quench PID` 控制实际运行时，不会留下
额外的等待包装进程。参数支持空字符串、空格、引号与反斜线，仍受原生 256 字节
参数串约束。相对文件写入当前目录；桌面一般为 `/home/users/用户名`。

| 语言 / 库 | 本轮真实验证 | 仍有限制 |
| --- | --- | --- |
| C / musl 1.2.5 | TLS、malloc/calloc/realloc/free、stdio、文件、seek/stat、内存映射、时钟 | 仅下表 OS 接口；不是完整 POSIX/libc 功能保证 |
| C++ | 类、模板、构造、musl I/O 与内存 | 示例未链接 libstdc++；STL、RTTI、C++ 异常尚未验证 |
| Lua 5.4.8 | 原版解释器、表、协程、GC、pcall 异常、浮点/libm、文件 | OS 线程、动态模块、进程控制、完整 TTY 和实时时钟未提供 |
| JavaScript / QuickJS | 原版引擎、类、TypedArray、BigInt、Map、Promise、异常 | 小型嵌入提供 print/scriptArgs；无 Node.js、DOM、模块加载或 worker |
| Python / MicroPython 1.26.1 | 原版解释器、Python 模块导入、继承、生成器、UTF-8 字符串、任意精度整数、math、JSON、regex、struct/array、GC、文件与 stat | 不是 CPython；没有 pip、C 扩展、完整标准库、线程、信号或 socket；本轮只验证脚本与 `-c` |
| Java bootstrap | 原先的有界多类夹具继续通过 | 仅 [JAVA.md](JAVA.md) 子集；它不是 OpenJDK |
| OpenJDK / CPython / Node / Go / Rust 标准运行时 | 尚未完成接入 | 需要动态链接、线程、信号、文件映射等后续能力 |

Lua/JS/Python 的 16×16 整数网格只是语言回归夹具，不能作为 Minecraft 运行证据。
MicroPython 使用独立配置的 Unix 端口：关闭 native emitter、FFI、线程和外部库，
采用 bytecode、double 浮点和 MPZ 大整数，x86-64 默认 GC 堆 2 MiB。内存不足会抛出
MemoryError，脚本递归仍受运行时栈检查限制；没有执行步数配额，可通过 shell 终止。
当前 Python 文件验证覆盖 `open`、上下文管理器和 `os.stat`；Linux 侧的目录列举、
chdir、mkdir、删除/重命名尚未接入，可先用原生 Loom 管理目录。未验收交互 REPL，
Ctrl-C 信号和完整终端控制仍缺失，不能据此宣称任意 Python 项目可直接运行。
QuickJS 嵌入设 16 MiB 引擎堆、128 KiB 引擎栈、执行中断与待处理任务预算；
没有开放阻塞 Atomics.wait。Lua 使用原版 GC；native/C/Lua 进程由调度器抢占，
可通过 shell 终止，没有为所有本地程序实现指令配额。

## 本轮底层边界

- x86-64 `SYSCALL` 进入专用内核栈，再用 `IRETQ` 返回。屏蔽 IF/TF/DF/IOPL/NT/AC，
  不使用用户栈执行内核代码；普通原生进程进入此接口得到 Linux `ENOSYS`。
- 只装载无 `PT_INTERP`、无 `DT_NEEDED` 的 x86-64 静态 PIE。映射到 4 GiB 起的
  用户区；rcrt1 自行重定位和初始化 musl TLS，内核构建 argc/argv/env/auxv、
  16 字节对齐栈及硬件随机 AT_RANDOM。要求硬件 RNG 可用；无预测随机替代。
- Linux 进程栈 256 KiB；TLS 的 FS base 随进程切换保存/恢复，原生进程恢复为 0。
  暂无多线程 clone 或共享地址空间的线程生命周期。
- 匿名私有 mmap、byte brk、munmap 中间拆分与复用；mprotect 支持 R、RW、RX，
  拒绝 W+X。映射/堆操作限制在 64 MiB 用户叶页预算和 32 段 mmap；物理 OOM
  可更早发生。尚无 PROT_NONE、MAP_FIXED、文件映射、mremap 或 madvise。
  分配与元数据拆分失败回滚，不能覆盖内核或其他进程。原生 ABI 原有资源规则
  继续适用，这不是对恶意程序所有原生调用的额外全局配额。
- 内存保护先检查整个范围，再改页属性；Linux errno 与 Nuvora 错误码显式转换。
  所有用户指针、长度与标量均检查，descriptor 高位不会悄悄别名成有效 fd。
- 文件和设备仍调用 Nuvora 原有授权接口，兼容模式没有提升用户权限。
  新增原生 `NV_COMPAT_SPAWN` / `NV_COMPAT_EXEC`，不改变旧操作编号或 ABI 1/2 布局。

| Linux 接口 | 实现范围 |
| --- | --- |
| read/write/readv/writev、open/openat、close、lseek、fstat | 使用现有文件/Terminal 路径；openat 仅 AT_FDCWD，最大 16 descriptors；短 I/O 保留 |
| stat/lstat/newfstatat、readlink/readlinkat | 路径元数据无需占用 fd；复用账户授权；at 接口仅 AT_FDCWD，fstatat 支持 0/AT_SYMLINK_NOFOLLOW；原生文件系统没有符号链接，readlink 对已存在的授权路径返回 EINVAL，缺失/私有路径保留相应错误 |
| fcntl | GETFD/SETFD、GETFL/SETFL 的 CLOEXEC、APPEND、NONBLOCK；没有 dup 或文件锁；Linux execve 未实现 |
| ioctl | 无 TTY 模拟；有效 fd 返回 ENOTTY，非法 fd 返回 EBADF |
| mmap/brk/munmap/mprotect | 上述有界匿名映射与权限；每次范围操作最大 64 MiB |
| arch_prctl | SET_FS/GET_FS，拒绝 supervisor / 非法地址；没有 SET_GS |
| getpid/gettid、uid/gid、uname、getcwd | 单线程身份；uname 明示 Nuvora 兼容子集；getcwd 返回 `/home/...` 路径 |
| clock_gettime、nanosleep、sched_yield | 100 Hz 单调/CPU 时钟与睡眠；CLOCK_REALTIME 未实现 |
| getrandom | 硬件随机，每次最多 256 字节；检查 flags 和输出范围 |
| futex、set_tid_address、prlimit64 | 仅单线程所需的 WAKE/不匹配 WAIT、返回当前 tid、查询固定栈上限；不能据此创建或等待线程 |
| exit/exit_group | 退出并由现有进程回收机制释放地址空间 |

未知功能返回 ENOSYS/其他明确错误，没有用“返回成功”替代线程、信号或动态库。
这不是 Linux 发行版、完整 Linux ABI、POSIX 认证或任意桌面应用兼容声明。

## 构建、来源与验证

```sh
make compat-examples  # 可选：GCC/G++、make，首次需下载已固定 SHA256 的源码
python3 scripts/build_compat_runtimes.py --output build/compat-examples
make -j4 diagnostics esp
make test-host
python3 tests/compat_boot_test.py
python3 tests/compat_desktop_test.py
```

构建脚本使用 musl 的静态 PIE rcrt1、未修改的 Lua/QuickJS/MicroPython 源码。
MicroPython 的 Nuvora 构建配置位于 `examples/compat/python-port`，没有自行重写 Python 语义。
C/C++ 示例和 QuickJS 嵌入在 [examples/compat](../examples/compat)。清单保存源码包
摘要、包装代码摘要、编译器与 ELF 摘要；许可证随夹具提供。正常构建不需要宿主
JDK 或联网。宿主回归实际运行同一个 Lua/JS/C++/Python ELF，再与客体输出核对；
Python 示例还与宿主 CPython 的输出和持久化文件逐字节比对。源包使用固定 SHA256，
首次可选重建需要联网，普通构建直接使用已提交的夹具。

本轮还提供只读诊断工具，能检查真正的 JDK launcher / libjvm：

```sh
python3 scripts/runtime_probe.py /path/to/jdk/bin/java /path/to/jdk/lib/server/libjvm.so
```

它列出 PT_INTERP、DT_NEEDED 和 TLS 等要求，**不执行代码，不把格式符合当成
运行成功**。本轮 OpenJDK 21 实测仍要求 glibc loader、libjli、libstdc++ 等共享库。

## 接下来如何到 Minecraft

1. 完成匿名地址预留/提交、文件映射和共享库装载，运行真正 JDK 的 `java -version`。
2. 实现线程/共享地址空间、阻塞 futex、信号与相应 TLS 生命周期；用匹配的 JDK
   验证异常、GC、类库、JAR、网络/TLS 与文件持久化，再运行真实服务端作中间验证。
3. 为 LWJGL/JNI 接入 GLFW 的窗口/输入、OpenGL 与 OpenAL。先验证真实图形 API
   和材质渲染；当前 GOP 软件帧缓冲没有提供这些接口。
4. 验证原版客户端启动、进入世界、输入、保存/重载及长时间稳定性，再扩展 JDK
   与游戏版本矩阵。Python、Node、Rust/Go 等复用已经测试过的平台能力。

最终验收是实际玩到 Minecraft；本轮语言测试、依赖分析与截图都没有算作这个
最终目标已经完成。当前结果与限制见 [TESTING.md](TESTING.md)。
