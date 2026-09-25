# Loom 命令手册

Loom 是运行在 Ring 3 的用户态命令环境。命令名区分大小写；输入 `help` 查看完整命令表，`atlas` 是兼容旧版本的别名。每个命令都接受精确形式的 `命令 --help`，只显示用途、语法和示例，不执行该命令；也可以使用 `help COMMAND`。文件名有空格时使用引号。

| 命令 | 作用 | 示例 |
| --- | --- | --- |
| `help [COMMAND]` | 列出命令或说明一个命令 | `help ports` |
| `atlas [COMMAND]` | `help` 的兼容别名 | `atlas folio` |
| `origin` | 显示内核版本、架构和 ABI | `origin` |
| `silicon` | 查看 CPU 身份、启用的指令状态和兼容限制 | `silicon --help` |
| `firmament` | 查看 ACPI 根表、MCFG、PCIe ECAM 范围和 CF8 回退 | `firmament --help` |
| `prism` | 查看 PCI/PCIe 显卡、NVIDIA 标识、BAR 和扩展能力 | `prism --help` |
| `horizon` | 显示内存、进程和数据盘状态 | `horizon` |
| `volumes` | 查看已挂载 Nuvora 分区、盘符和快照容量 | `volumes` |
| `partitions` | 查看检测到的 GPT 分区（包括未挂载的格式） | `partitions` |
| `ports [--scan]` | 查看 PCI USB 控制器、设备描述符、Hub 和键盘状态 | `ports --scan` |
| `net [wifi ... | dhcp ... | static ... | send ... | recv ...]` | 查看实体网卡、用 USB 桥接设备连接 Wi-Fi 并配置 IPv4/UDP | `net wifi scan` |
| `where` | 查看当前目录 | `where` |
| `step PATH` | 切换目录 | `step /home` |
| `glance [PATH]` | 列出目录 | `glance /apps` |
| `nest PATH` | 创建一级目录 | `nest /home/notes` |
| `weave FILE TEXT` | 创建文件或覆盖文本，末尾加换行 | `weave /home/note "hello"` |
| `stitch FILE TEXT` | 追加一行文本 | `stitch /home/note "second"` |
| `unfold FILE` | 输出文件内容 | `unfold /home/note` |
| `folio [FILE]` | 打开全文编辑器和 Word 可读 RTF 排版编辑器 | `folio /home/report.nvd` |
| `mirror FROM TO` | 复制到新文件，拒绝覆盖已有目标 | `mirror /home/note /home/copy` |
| `shift FROM TO` | 移动或重命名，拒绝覆盖目标 | `shift /home/copy /home/draft` |
| `prune PATH` | 删除一个文件或空目录 | `prune /tmp/draft` |
| `sparks` | 列出进程、状态、计时和用户页数 | `sparks` |
| `forge APP [ARGS]` | 启动程序并等待退出 | `forge pulse --help` |
| `scatter APP [ARGS]` | 启动后台程序并返回 PID | `scatter spin` |
| `gather PID` | 等待并领取子进程退出状态 | `gather 3` |
| `quench PID` | 终止进程 | `quench 3` |
| `tempo` | 查看启动以来的定时器 tick | `tempo` |
| `doze MS` | 休眠若干毫秒 | `doze 1000` |
| `anchor` | 提交 C: 及其他已挂载数据分区的快照 | `anchor` |
| `trial` | 在当前系统启动用户态测试程序 | `trial` |
| `scrub` | 清空 VGA 屏幕 | `scrub` |
| `rest` | 关闭虚拟机，不自动保存 | `rest` |
| `renew` | 重启虚拟机，不自动保存 | `renew` |

`forge` 和 `scatter` 接收不带 `/` 的名称时，在 `/apps/` 查找。内置程序同样支持 `forge APP --help`：`loom` 是命令行，`folio` 是全文与格式编辑器，`pulse` 输出五次时间，`spin` 是抢占测试死循环，`fault` 故意触发 CPU 异常，`probe` 执行集成检查，`relay` 是内核回归辅助程序。

`ports` 会列出 PCI 控制器的总线地址、厂商/产品 ID、xHCI/UHCI/OHCI/EHCI 状态，以及真实 USB 设备的速度、VID/PID、USB 类、Hub 父子关系、厂商、产品和序列号。xHCI 设备枚举读取标准描述符；Hub 会递归扫描，USB Boot Protocol 键盘输入进入 Loom 和 Folio，CDC-ECM 网卡会显示 `Ethernet=active`。USB 鼠标、U 盘等设备会被识别并标注为“identification only”，本版本没有鼠标指针、USB 大容量存储块读写或文件系统挂载。

`net` 的具体网卡支持清单、Wi-Fi 命令、密码输入和限制见 [NETWORK.md](NETWORK.md)。

`ports --scan` 立即重试端口并处理拔插；后台还会定期扫描。xHCI 控制器发生不可恢复错误时会停止并显示 `failed`，不会把损坏 DMA 页重新交给用户进程。UHCI/OHCI/EHCI 控制器目前只报告 `unsupported`，不会伪造设备列表。

路径按当前目录解析；输入行最多 511 个字符、24 个参数；完整路径最多 191 字节。引号不闭合或参数数量错误时显示对应命令的 usage。`C:/` 指向 `/home`，`D:/` 等映射到 `/drives/字母`，`anchor` 保存所有已挂载数据盘；`/tmp` 在重启后清空。详见 [GPT 分区说明](PARTITIONS.md)。

## Folio 全文编辑器

在 Loom 中输入 `folio` 打开新文档，或输入 `folio /home/report.nvd` 打开已有文档。Folio 使用 80×25 的全屏 VGA/串口界面；编辑内容按字符保存，`.nvd` 保存格式信息，`.txt` 和 `.md` 保存纯文本。

| 快捷键 | 作用 |
| --- | --- |
| 方向键、Home/End | 移动光标；按住 Shift 扩展选区 |
| Ctrl-A / Ctrl-C / Ctrl-X / Ctrl-V | 全选、复制、剪切、粘贴；保留字符格式 |
| Ctrl-Z / Ctrl-Y | 撤销 / 重做，保留最近 12 组编辑 |
| Ctrl-F / F7 | 查找 / 查找下一个 |
| Ctrl-H | 替换全部匹配项 |
| Ctrl-B / Ctrl-I / Ctrl-U | 粗体、斜体、下划线；有选区时应用到选区 |
| F8、Ctrl-1..4 | 正文、一级标题、二级标题、引用 |
| F9、Ctrl-L/E/R/J | 左对齐、居中、右对齐、两端对齐 |
| F10 / F11 / F12 | 项目符号、单倍/双倍行距、段前分页 |
| F2 / F3 / F4 | 保存、打开、另存为 |
| F5 / F6 | 页面预览 / 导出 RTF |
| Ctrl-Q | 关闭；有未保存内容时必须选择保存或放弃 |

保存采用临时文件完整写入后再替换目标文件；F2 / `Ctrl-S` 保存后自动提交 `/home` 快照。导出的 `.rtf` 使用 Word 可读取的 RTF 控制字，包含标题字号、字体样式、段落对齐、行距、项目符号、分页和页码字段。Folio 当前读取纯文本和本地 `.nvd`，暂不解析外部 `.docx` 或 `.rtf`。

## CPU / 平台 / 显卡诊断

`forge vector` 运行 x87/MMX/SSE 进程隔离与 EXEC 测试；成功显示 `VECTOR RESULT: PASS`。`silicon` 区分 CPU 原始功能与内核已启用状态。`firmament` 显示经过校验的 ACPI 根表、MCFG 项和当前 PCI 配置方式；在 q35 上可看到 4096 字节 ECAM，在传统 pc 机型上保留 256 字节 CF8/CFC。启动参数 `nv.no-ecam=1` 或 `python3 start.py --machine q35 --no-ecam` 强制使用回退路径。

`prism` 只读输出显卡启动快照，并在 ECAM 可用时解析 AER、ACS、ATS、SR-IOV、Resizable BAR、PASID 与 DPC 标记；这些标记不表示对应功能已经启用，也不会加载 NVIDIA 驱动。所有 33 个命令支持 `--help`，8 个内置程序支持 `forge APP --help`。硬件边界见 [CPU-GPU.md](CPU-GPU.md)。

`forge probe devctl` 定向验证设备控制接口，`forge probe --help` 查看说明；`trial` 继续执行包含它在内的完整用户态回归。DEVCTL 本身是程序接口；0.6.0 新增的 Loom 命令只有 `firmament`，当时的命令总数为 31；本开发版加入 volumes、partitions 和 net 后为 34。
