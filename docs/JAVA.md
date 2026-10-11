# Java 21 与 Minecraft 兼容进度

当前交付的是 **Nuvora Java bootstrap 0.2**：在 x86-64 Ring 3 中解释真实
Java `.class` 字节码的实验性子集。它是本项目新写的 freestanding C 解释器，
**不是 OpenJDK 移植、不是 Java SE 21 实现，也还不能启动 Minecraft Java 版**。
类文件版本 65 可以被识别和执行，不代表 Java 21 标准库已经兼容。

目标优先面向 Java 21 和较新的 Minecraft；本阶段用于验证类加载、解释执行、
原生进程接入和错误边界。系统中没有附带 Minecraft 客户端、启动器、游戏资源
或账户凭据，也没有用仿制游戏代替兼容验证。

## 现在可以运行什么

在桌面 Terminal 或 Loom 中输入：

```text
java --version
java --demo "hello world"
java "/home/My Program.class" first "second argument"
java -cp /home MultiMain "two words"
java --class-path /home pkg.Entry "package argument"
```

`forge java ...` 同样可用。`java` 与 `forge /apps/java` 的参数会重新引用，
保留空参数、空格、双引号和反斜线。原生进程参数字符串仍受 ABI 的 256 字节
限制（包括结尾 NUL），重新引用后超限会拒绝启动。

示例源码为 [NuvoraJavaDemo.java](../examples/java/NuvoraJavaDemo.java)，编译
产物已随源码保存；正常构建系统无须安装 JDK。示例包含 20 项语义检查、对象
和方法调用、16×16×32 整数块网格及校验和计算；它不是 Minecraft 场景。

使用 **JDK 21** 编译自己的程序（须在下表实现范围内）：

```sh
javac --release 21 -g:none -encoding UTF-8 MyProgram.java
# 关闭对应虚拟机后，导入其已有 Nuvora 数据盘；不会写宿主物理盘。
python3 scripts/import_media.py --disk build/x86_64/nuvora-store.img MyProgram.class
```

导入工具把文件放到该卷的 `/home`。启动后执行 `java /home/MyProgram.class`。
入口须为 `public static void main(String[] args)`，用户参数是 UTF-8，进入
解释器后转换为 Java UTF-16 字符串。控制台最终输出 UTF-8；桌面字体目前仍
不完整，所以输出字节正确不等于所有 Unicode 字形都能显示。

## 多类与目录类路径

`-cp` / `--class-path` 当前接受**一个目录**，入口可写成 `pkg.Main`。
依赖 `pkg.Helper` 从该目录下的 `pkg/Helper.class` 按需读取；没有 JAR、通配符、
多目录列表或环境变量 CLASSPATH。直接指定 `.class` 时，以该文件所在目录为
依赖根目录；带 package 的程序应使用 `-cp` 明确指定包根。

可复现的多类示例在 [multiclass](../examples/java/multiclass)：

```sh
javac --release 21 -g:none -encoding UTF-8 -d out examples/java/multiclass/MultiMain.java
python3 scripts/import_media.py --disk build/x86_64/nuvora-store.img out/*.class
```

系统内运行 `java -cp /home MultiMain "two words"`，将实际加载 8 个类，验证
跨类调用、long 返回值、数组协变、循环初始化、构造时再次引用本类、字符串驻留，
以及 `new` / `getstatic` / `putstatic` / `invokestatic` 的首次初始化。
`MULTICLASS PASS` 是示例完成标记，输出还会与 OpenJDK 逐字节对照。

只解析类型、创建引用数组或做类型检查不会触发 `<clinit>`；静态初始化在同一
Java 线程上只执行一次，循环引用能看到字段默认值。失败的初始化终止该进程，
没有异常捕获或后续重试。依赖文件先查询长度，再读入 VM 自有 arena；查询与读取报告的长度不一致、
短读、文件声明类名与请求不一致均拒绝。内部类名拒绝点、反斜线、冒号、绝对路径、
重复目录分隔符、NUL 与 surrogate，避免依赖名越出所选目录；根目录由用户选择，
仍使用原生文件权限，不是额外的文件系统沙箱。类/成员有 public、包与 private
访问检查，final 字段仅允许声明类的初始化方法写入；未实现 nestmate 访问。

导入工具目前把每个文件平放在 `/home`，不会创建 Java package 目录。需要时在
系统中用 `nest /home/pkg` 创建目录，再用 `shift` 移入 `.class` 文件；之后执行
`java -cp /home pkg.Entry`。文件系统单个路径段仍受 31 字节限制。

## 实现范围

| 范围 | 当前实现 |
| --- | --- |
| 类格式 | major 52–65、minor 0；拒绝 preview；按需加载最多 16 个直接继承 Object 的应用类，拒绝应用覆盖 `java/` 类 |
| 类型 | `int`、`long`、boolean/byte/short/char 的整型表示、对象引用；有符号除余、移位与溢出按 Java 位宽计算 |
| 执行 | 各类独立静态与实例字段、首次使用时的 `<clinit>`、跨类构造器与静态/实例方法、分支、两种 switch、wide 局部变量、类型与栈宽度检查 |
| 数组 | 单维 int/long/byte/short/char/boolean，以及已加载应用类、Object、String 的引用数组；检查长度、下标与引用存储类型 |
| 字符串 | modified UTF-8 常量解析、UTF-16 存储、跨类常量驻留、`length`、`charAt`、`equals`；字符串、int/long/boolean/char/null 的 `makeConcatWithConstants` 拼接 |
| 基础输出 | `System.out`；PrintStream 的 `print`/`println` 支持 int、long、boolean、char、String，另有无参 `println()` |
| 接入方式 | `/apps/java` 是普通原生 ELF；通过现有文件和进程接口读类文件、分配堆、输出、让出 CPU；不扩大内核 ABI 或账户权限 |

没有实现 float/double、继承/接口分派、异常处理表/捕获、反射、
lambda/通用 invokedynamic、nestmate 私有访问、模块、JAR/ZIP、JNI、线程、GC、JIT 或 Java 标准库。
上述表格列出的宿主方法以外，库调用会明确停止并报告不支持。数值异常、空引用、
越界和错误转换会报告对应异常名后退出；并非可被 Java `catch` 捕获的异常对象。
这是带运行时检查的受限解释器，没有完整 JVMS 验证器，也未通过 Java TCK。

## 资源与输入边界

- 最多 16 个应用类（包含入口类）；每个类文件最多 256 KiB，每类常量池最多
  1,023 个可用索引、128 个方法、64 个字段。类名最多 159 字节。
- 每个方法最多 16 KiB 字节码，256 个局部槽与 256 个操作数槽；long 占两槽。
- 显式 Java 调用帧最多 32 层，避免用原生 C 栈递归执行 Java 方法。
- 每次进程运行使用 2 MiB arena（含解释器元数据、依赖类原始字节、帧、对象和数组），最多
  4,095 个对象、每个字符串 4,096 个 UTF-16 单元。数组内部每元素占 8 字节。
  没有 GC，分配耗尽会报错；进程结束后由内核回收整个 arena。
- 每次启动最多 2,000 万条字节码，类初始化和 main 共用预算；每 4,096 条
  调用宿主 poll，让出 CPU。无限循环会因预算耗尽退出。
- 文件/属性长度、常量池引用、指令长度、所有跳转边界、操作数/局部变量类型、
  引用、数组范围和分配长度都会检查。未知功能返回错误，Java 值不保存宿主地址。

错误输出包含原因、类索引、方法索引、字节码偏移及操作码。退出码 0 表示 main 完成；
核心错误 1–7 依次为格式、不支持、内存、验证、异常、配额、宿主 I/O；启动器
本身读取失败等情况也会返回非零。`--self-test` 是诊断入口，逐字节核对输出
后特意返回 74，供系统集成测试区分正常路径。

## Minecraft 仍然缺少的部分

| 下一阶段 | 完成判据 |
| --- | --- |
| 运行时路线 | 基于本阶段测试决定移植成熟 JVM 的平台层；本子集不能通过修改版本号变成完整 Java 21 |
| 类库与内存 | JAR、异常、浮点、完整对象模型、GC、线程/TLS/同步，以及 Java 21 类库测试通过 |
| 原生库 | JNI、共享库与相应 libc/线程/文件/套接字平台接口；能够加载所选 Minecraft 版本使用的 LWJGL |
| 图形与声音 | 实现相应 GLFW/OpenGL、窗口、输入和音频后端；通过 API 测试并实际渲染，现有 GOP 软件帧缓冲还不提供 OpenGL |
| 启动与游戏 | Java 网络/TLS、资源路径和授权登录链路可用，再验证真实客户端启动、进入世界、输入、保存与稳定性 |

当前没有做 Minecraft 启动成功、进入世界或帧率承诺。Java 21 是本轮用户选定
的优先目标；具体游戏版本及其依赖需要在完整运行时与图形阶段固定和验证。

## 复现验证

```sh
make -j4 diagnostics esp
make test-host
# 额外安装 JDK 21 后，与 OpenJDK 比较实际输出和错误类别：
python3 tests/java_compat_test.py
python3 tests/uefi_media_boot_test.py --memory 64
python3 tests/java_boot_test.py
python3 tests/java_desktop_test.py
```

宿主夹具直接编译与客体相同的 C 解释器；普通回归不依赖 Java 安装。
差分脚本重新编译源码并核对已保存的类文件，比较七组成功运行和十二类异常；
另有主类与依赖类的所有截断前缀、共 2,500 次确定性损坏输入、保护页及
ASan/UBSan 检查；依赖的缺失、冒名、读取长度变化与 16 类上限均有测试。
客体测试包括真实文件读取、引号参数、错误退出、进程页回收与后续文件操作。
范围与本轮实际结果见 [TESTING.md](TESTING.md)。
