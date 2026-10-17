# 桌面语言运行时与 Minecraft 路线

这一阶段把主线改为 **让成熟运行时复用操作系统兼容接口**。已有的 Java
bootstrap 保留作回归夹具；继续扩充那个受限字节码子集，无法短期替代完整
OpenJDK、类库与 LWJGL。Linux x86-64 兼容层已在 Nuvora Ring 3 运行
musl C、C++ 核心示例、Lua、QuickJS、MicroPython，以及真正的
**OpenJDK 21.0.11 / HotSpot**。Java 测试覆盖 JAR、类库、线程、GC、JNI 文件与
加密接口；HotSpot 即时编译了测试方法，实际计算结果通过校验。
**Minecraft 客户端目前仍不能运行**；图形、音频、网络、更多文件接口和游戏
所需的资源预算仍未完成。这里只验证固定的 x86-64 musl JDK，不保证所有 JDK。

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

## 运行真正的 OpenJDK 21

普通 OS 构建保持离线。以下可选 SDK 步骤从 Alpine 官方 HTTPS 下载固定 SHA256
的 OpenJDK 21.0.11+10 与 zlib，创建**新的私有数据盘**，拒绝覆盖已有盘：

```sh
python3 scripts/prepare_openjdk_probe.py
make -j4 diagnostics esp
python3 tests/openjdk_boot_test.py
python3 tests/openjdk_desktop_test.py
```

测试使用 512 MiB QEMU/NVMe。安装盘路径为 `build/openjdk-probe/data.img`，
不是用户原有的 `build/x86_64/nuvora-store.img`。在使用该盘的 Nuvora Terminal
中运行：

```text
compat --jit /home/jdk/bin/java @/home/java-check.args
compat --jit /home/jdk/bin/java @/home/java-jit.args
```

第一个程序验证 4 个 Java 工作线程、AtomicLong、CountDownLatch、join、
BigInteger、SHA-256 provider、NIO UTF-8 文件、时钟和 GC；第二个还实际编译并
执行 `JavaRuntimeCheck::hot`，20 万轮结果为 `dc9889355911c47f`。
`@参数文件` 由原版 Java 启动器解析，可承载超过原生 256 字节参数串的选项。
示例写入当前目录的 `jvm-proof.txt`。现有 `/apps/java` 命令仍是下述 bootstrap；
完整 JVM 使用 `/home/jdk/bin/java`，不会用 bootstrap 模拟 OpenJDK。

`--jit` 明确允许本进程及其线程创建可写、可执行用户页；默认进程仍拒绝 W+X。
示例固定使用 Serial GC、32 MiB Java 堆、一个 CPU，并关闭压缩指针与 perf data。
这是已验证配置，不应据此宣称任意 JVM 参数、桌面框架或游戏均可运行。

打包仅调整启动器 `PT_INTERP` 字符串及长度，使 `/lib/ld-musl-x86_64.so.1`
指向 `/home/ldso`；JVM、标准类库和其他本地库的代码不改动。原始 APK、重打包
启动器、类文件与 JAR 的摘要保存在盘旁 `.provenance.json`；常规许可证文件保留。
完整 JDK 和游戏资源不进入项目源码包。

| 语言 / 库 | 本轮真实验证 | 仍有限制 |
| --- | --- | --- |
| C / musl 1.2.5 | TLS、malloc/calloc/realloc/free、stdio、文件、seek/stat、内存映射、时钟 | 仅下表 OS 接口；不是完整 POSIX/libc 功能保证 |
| C++ | 类、模板、构造、musl I/O 与内存 | 示例未链接 libstdc++；STL、RTTI、C++ 异常尚未验证 |
| Lua 5.4.8 | 原版解释器、表、协程、GC、pcall 异常、浮点/libm、文件 | 此夹具未启用动态 Lua 模块和完整 POSIX 绑定 |
| JavaScript / QuickJS | 原版引擎、类、TypedArray、BigInt、Map、Promise、异常 | 小型嵌入提供 print/scriptArgs；无 Node.js、DOM、模块加载或 worker |
| Python / MicroPython 1.26.1 | 原版解释器、Python 模块导入、继承、生成器、UTF-8 字符串、任意精度整数、math、JSON、regex、struct/array、GC、文件与 stat | 不是 CPython；没有 pip、C 扩展、完整标准库、线程、信号或 socket；本轮只验证脚本与 `-c` |
| Java bootstrap | 原先的有界多类夹具继续通过 | 仅 [JAVA.md](JAVA.md) 子集；它不是 OpenJDK |
| OpenJDK 21.0.11 | 原版 HotSpot 启动、JAR/ZIP、Java 多线程与同步、标准库、JNI 文件/加密、GC、解释和 JIT 执行 | 仅固定 Alpine musl x86-64 配置；图形、socket、更多文件接口和大堆尚未验证 |
| CPython / Node / Go / Rust 标准运行时 | 未验证 | 需要分别补齐并测试各运行时依赖；不能由 JVM 成功推断兼容 |

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
- 装载静态 PIE 或带 `PT_INTERP` 的动态 PIE。主映像从 4 GiB 起，解释器从
  96 GiB 起；两份 ELF 分别检查、授权后才映射。成熟 musl linker 负责共享库、
  重定位和 TLS；内核传递 AT_BASE/AT_ENTRY、对齐栈和硬件 AT_RANDOM。
- Linux 进程栈 256 KiB；TLS 的 FS base 随进程切换保存/恢复，原生进程恢复为 0。
  pthread clone 共享地址空间、文件、工作目录和信号处理表，保留独立 TLS、
  内核栈、FP 状态、信号 mask/altstack；全系统仍只有 32 个任务槽和一个 CPU。
  主线程退出时保留等待状态至最后一个线程结束；clear-TID、exit_group 和原生
  quench 回收整个组，最后一个引用才释放页表。没有 fork、clone3 或线程组间 IPC。
- 私有文件映射是装载时快照，EOF 尾部清零；只读 MAP_SHARED 也只提供快照，
  适用于不可变模块归档，**没有共享写回或其他写入者的缓存一致性**，拒绝升级
  为可写。PROT_NONE 匿名预留不占实体页，mprotect 提交；已提交页改为不可访问
  后保留内容。MAP_FIXED/替换/拆分先准备资源再提交，失败保留旧内容和权限。
- 单次虚拟范围最大 16 GiB，Linux 映射区 128 GiB–64 TiB，128 个 VMA，
  用户实体叶页预算 256 MiB；实际 OOM 可更早发生。此预算不足以承诺 Minecraft
  大堆运行。原生 ABI 的资源规则继续适用，这不是额外全局配额或需求分页器。
  MADV_DONTNEED 清零匿名实体页，当前不释放其页框；没有 mremap 或完整 pager。
- 内存保护先检查整个范围，再改页属性；Linux errno 与 Nuvora 错误码显式转换。
  所有用户指针、长度与标量均检查，descriptor 高位不会悄悄别名成有效 fd。
- 文件和设备仍调用 Nuvora 原有授权接口，兼容模式没有提升用户权限。
  新增原生 `NV_COMPAT_SPAWN` / `NV_COMPAT_EXEC`，不改变旧操作编号或 ABI 1/2 布局。

| Linux 接口 | 实现范围 |
| --- | --- |
| read/write/readv/writev、open/openat、close、lseek、fstat | 使用现有文件/Terminal 路径；openat 仅 AT_FDCWD，最大 16 descriptors；短 I/O 保留 |
| stat/lstat/newfstatat、readlink/readlinkat | 路径元数据无需占用 fd；复用账户授权；at 接口仅 AT_FDCWD，fstatat 支持 0/AT_SYMLINK_NOFOLLOW；原生文件系统没有符号链接，readlink 对已存在的授权路径返回 EINVAL，缺失/私有路径保留相应错误 |
| pread64、fcntl、execve | 定位读不改 fd 偏移，取消人为 16 KiB 截断；CLOEXEC/APPEND/NONBLOCK；exec 先复制 argv/env，失败保留映像与 fd，成功关闭 CLOEXEC；环境最大 32 项/4096 字节；活动多线程组不支持 exec |
| ioctl | 无 TTY 模拟；有效 fd 返回 ENOTTY，非法 fd 返回 EBADF |
| mmap/brk/munmap/mprotect/madvise | 上述事务映射、预留/提交与保护；默认 W^X，可显式 --jit；匿名 discard 与部分可忽略 advisory |
| arch_prctl | SET_FS/GET_FS，拒绝 supervisor / 非法地址；没有 SET_GS |
| getpid/gettid、uid/gid、uname、getcwd | getpid 为组长、gettid 为线程；克隆保留账户授权和 Terminal 祖先；getcwd 返回 `/home/...` |
| clock_gettime/getres、gettimeofday、nanosleep、sched_yield | 100 Hz 单调/线程/进程 CPU 时钟；PC UTC RTC 提供实时时钟，无有效 RTC 时从 Unix epoch 继续计时，无网络校时 |
| sched_getaffinity/getcpu、sysinfo | 一个 CPU/node 0、实际内存/空闲页与任务计数；load averages 尚未统计 |
| getrandom | 硬件随机，每次最多 256 字节；检查 flags 和输出范围 |
| clone、futex、set_tid_address | 仅共享 VM/FS/FILES/SIGHAND 的线程 clone；WAIT/WAKE、BITSET、REQUEUE/CMP_REQUEUE、超时/EINTR、clear-TID join；无 PI/robust mutex/futex2 |
| rt_sigaction/procmask/return、sigaltstack、tkill/tgkill | 同一地址空间的线程信号与同步异常、可修改 user context、FP 恢复；返回选择子/RFLAGS/MXCSR 检查；最多 4 层处理，pending 用位集，不实现实时信号队列、job control 或跨进程发送 |
| prlimit64、exit/exit_group | 查询固定栈上限；单线程退出和整个线程组退出；其他资源限制接口仍未完成 |

未知功能返回 ENOSYS/其他明确错误。没有完整阻塞 I/O 信号重启、POSIX job control、
目录/路径调用全集或 Linux socket 层。文件系统仍受 31 字节组件、192 字节路径和
512 个节点等原生边界约束，Minecraft 安装与资源路径需要继续处理。
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

动态夹具可通过 `python3 scripts/build_dynamic_runtimes.py` 从固定 musl 源码重建，
`tests/dynamic_boot_test.py` 实测 DSO TLS、pthread、信号、exec、JIT 及精确内存回收。
新树形导入器保留两份 COW 根，预先拒绝冲突、越界路径和符号链接，写入失败不提交。
只读诊断工具也能检查真正的 JDK launcher / libjvm：

```sh
python3 scripts/runtime_probe.py /path/to/jdk/bin/java /path/to/jdk/lib/server/libjvm.so
```

它列出 PT_INTERP、DT_NEEDED 和 TLS 等要求，**不执行代码，不把格式符合当成
运行成功**。当前已运行的是上述 Alpine musl JDK；之前分析过的 glibc JDK
仍需相应 glibc 运行库及单独验证，不能混用。

## 接下来如何到 Minecraft

1. 已完成这一固定 JDK 的动态启动、线程/futex、信号、类库/JAR、GC 与 JIT 示例；
   接下来扩大文件/目录、网络/TLS、同步和资源预算的真实应用覆盖。
2. 运行原版服务端作为中间验证，并分别测试其他 JDK。
3. 为 LWJGL/JNI 接入 GLFW 的窗口/输入、OpenGL 与 OpenAL。先验证真实图形 API
   和材质渲染；当前 GOP 软件帧缓冲没有提供这些接口。
4. 验证原版客户端启动、进入世界、输入、保存/重载及长时间稳定性，再扩展 JDK
   与游戏版本矩阵。Python、Node、Rust/Go 等复用已经测试过的平台能力。

最终验收是实际玩到 Minecraft；本轮语言测试、依赖分析与截图都没有算作这个
最终目标已经完成。当前结果与限制见 [TESTING.md](TESTING.md)。
