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
| `net [wifi ... | dhcp ... | static ... | send ... | recv ...]` | 查看网卡、连接特定 USB Wi-Fi 桥并配置 IPv4/UDP | `net wifi scan` |
| `ping HOST [COUNT]` | 对 IPv4 地址或经 DNS 解析的主机发送 echo | `ping 10.0.2.2 4` |
| `wget [-O FILE] http://HOST[:PORT]/PATH` | 经网卡下载 HTTP 文件，完成后尝试提交数据卷 | `wget -O /home/a.txt http://10.0.2.2:8000/a.txt` |
| `exit` | 关闭桌面终端窗口；恢复控制台保持登录要求 | `exit` |
| `where` | 查看当前目录 | `where` |
| `step PATH` | 切换目录 | `step /home` |
| `glance [PATH]` | 列出目录 | `glance /apps` |
| `nest PATH` | 创建一级目录 | `nest /home/notes` |
| `weave FILE TEXT` | 创建文件或覆盖文本，末尾加换行 | `weave /home/note "hello"` |
| `stitch FILE TEXT` | 追加一行文本 | `stitch /home/note "second"` |
| `unfold FILE` | 输出文件内容 | `unfold /home/note` |
| `folio [FILE]` | 打开全文编辑器和 Word 可读 RTF 排版编辑器 | `folio /home/report.nvd` |
| `desktop` | 打开 UEFI 窗口桌面 | `desktop` |
| `media [FILE]` | Music 图形播放器，支持 MP3、FLAC、PCM WAV、MP2 和 MPEG-1 视频 | `media /home/clip.mpg` |
| `wave FILE.wav \| wave --test` | HDA 模拟输出的 PCM WAV 播放与测试音 | `wave --test` |
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
| `rest` | 请求 ACPI 关机，不自动保存；固件不支持时停机 | `rest` |
| `renew` | 请求固件/PC 重启，不自动保存 | `renew` |

`forge` 和 `scatter` 接收不带 `/` 的名称时，在 `/apps/` 查找。内置程序同样支持 `forge APP --help`：`loom` 是完整命令行，`desktop` 管理图形窗口，`folio` 是全文与格式编辑器，`media` 播放 MP3、FLAC、PCM WAV、MP2 音频和 MPEG-1 视频，`wave` 播放 PCM 音频，`pulse` 输出五次时间，`spin` 是抢占测试死循环，`fault` 故意触发 CPU 异常，`probe` 执行集成检查，`relay` 是内核回归辅助程序。

`ports` 会列出 PCI 控制器的总线地址、厂商/产品 ID、xHCI/UHCI/OHCI/EHCI 状态，以及真实 USB 设备的速度、VID/PID、USB 类、Hub 父子关系、厂商、产品和序列号。xHCI 设备枚举读取标准描述符；Hub 会递归扫描，USB Boot Protocol 键盘输入进入 Loom 和 Folio，CDC-ECM 网卡会显示 `Ethernet=active`。Boot 鼠标和 QEMU USB Tablet 会显示 `input=active`；U 盘等尚未启用的设备标注为“identification only”，本版本仍没有 USB 大容量存储块读写或文件系统挂载。

`net`、`ping`、`wget` 的网卡支持、DNS、HTTP 限制及 Wi-Fi 口令输入见 [NETWORK.md](NETWORK.md)。有 GOP 的 UEFI 机器启动后默认进入图形登录与桌面；F10 打开 Apps 面板，选 Terminal 打开窗口终端。窗口终端运行完整 Loom 命令集；`exit` 关闭窗口。网络配置与电源命令需要已解锁的管理员账户。BIOS 文本模式和显式 `nv.recovery=1` 进入需要本地登录的恢复控制台。

## 桌面窗口

登录后显示桌面，不自动打开 Files。顶部 Apps 或 F10 打开居中的应用网格；左右键移动一项，上下键跨行，Enter 启动。空桌面按 Enter 也可打开 Apps。底部应用栏单击启动或切换窗口，Alt-Tab 在已打开的窗口间切换。拖动标题栏移动窗口，双击标题栏或点方框按钮最大化/还原；右下角可调整大小，标题栏的横线最小化、叉号关闭。Settings 提供外观、声音、网络、安全与系统五页设置。操作通知在应用栏上方显示六秒；桌面没有返回裸控制台的菜单入口。

Settings 有外观、声音、网络、安全、系统五页，支持壁纸和文件视图偏好、实际输出音量/静音、网卡与 IPv4 状态、管理员 DHCP 操作和 1/5/15 分钟自动锁屏。System 显示实时内存、任务数、运行时长和数据盘状态。保存数据可供当前用户使用；重启/关机只向管理员开放，先显示默认选中 Cancel 的确认框，再处理未保存文档并提交数据卷。键盘用左右键切换页、Tab 选择操作、Enter 执行、Esc 取消；外观和锁屏选项也可用数字 1–3。偏好保存到用户目录，兼容旧版记录。

Files 默认显示 Cards 文件卡片，宽窗口显示选中项的真实名称、类型和大小等详情。点击 Cards 或 List 明确选择卡片或紧凑列表，重复点击当前视图保持该视图；Ctrl-1/2 同样分别选择两种视图。侧栏搜索框或 Ctrl-F 激活即时搜索，不区分大小写；Enter 返回文件导航，Esc 清空搜索。Sort 按钮或 F8 循环名称、类型、大小排序，文件夹始终排在前面；Hidden 按钮或 Ctrl-H 显示/隐藏点文件。

方向键按可见网格移动，PageUp/Down 翻页。双击或 Enter 进入目录/打开文件，Backspace 或侧栏 Up 按钮返回上级，数字 `1`–`7` 切换卷与常用目录。上方 `+ Folder` 打开新建文件夹输入框，`+ Note` 打开 Text Editor 的新文档，随后用 Ctrl-S 保存；键盘对应为 Ctrl-N 和 Ctrl-T。窄窗口会隐藏详情及放不下的工具按钮，并将 Folder/Note 缩为图标；Ctrl-1/2、F8、Ctrl-H、Ctrl-N、Ctrl-T 仍可操作对应功能。F2 改名，Delete 打开确认框。删除框显示文件名和不可撤销提示，按 D 或点击 Delete 才执行，Enter 不会删除。F5 刷新，F6 保存数据卷。文件管理器的修改会尝试提交到数据卷；`/tmp` 只在内存中。

双击 `.txt` 或 `.md` 会在 Text Editor 窗口打开。Ctrl-S 保存、Ctrl-Shift-S 另存、Ctrl-N 新建；关闭或切换文档时如有未保存内容会询问。窗口编辑器每份文档最多读取 128 KiB 的纯文本，不限制数据卷里其他文件的大小；格式排版使用 `folio` 和 `.nvd`。`.nvd`、Media 音视频和完整终端在各自的进程窗口中运行；播放、下载时可以切换其他窗口。Alt-F4 请求关闭，Alt-F9 最小化，Alt-F10 最大化/恢复。

Apps 或底部应用栏的 Music 打开播放器；从 Files 打开音视频文件时，播放列表来自该文件所在目录中的真实媒体文件和目录。在文件浏览状态用上下键选择、Enter 或双击打开、Backspace 返回上级、F5 刷新。播放时点击列表中的另一媒体文件，或点击上一首/下一首按钮可切换播放；左右键同样切换相邻媒体文件，跳过目录，没有相邻文件时对应按钮不可用。Space 或 Enter 暂停/继续，Esc 停止并返回列表；暂停保留当前位置，最小化后仍可继续播放。

Music 的音量条可点击或拖动，`+`/`-` 每次调整 5%，与 Settings 声音页及 Dock 共用实际 HDA 输出音量，暂停时也会同步。时间显示来自实际播放数据；PCM WAV 和带总帧数的 FLAC 显示计算出的真实总时长，无法获得总时长时显示 `--:--`。音频默认封面由程序绘制，当前不读取文件内嵌的专辑封面；视频区域显示实际解码画面。

`ports --scan` 立即重试端口并处理拔插；后台还会定期扫描。xHCI 控制器发生不可恢复错误时会停止并显示 `failed`，不会把损坏 DMA 页重新交给用户进程。UHCI/OHCI/EHCI 控制器目前只报告 `unsupported`，不会伪造设备列表。

路径按当前目录解析；输入行最多 511 个字符、24 个参数；完整路径最多 191 字节。引号不闭合或参数数量错误时显示对应命令的 usage。`C:/` 指向 `/home`，`D:/` 等映射到 `/drives/字母`，`anchor` 保存所有已挂载数据盘；`/tmp` 在重启后清空。详见 [GPT 分区说明](PARTITIONS.md)。

## Folio 全文编辑器

在桌面终端或 Loom 中输入 `folio` 打开新文档，`folio /home/report.nvd` 打开已有文档。Folio 使用 80×25 文档模型，有桌面时绘制在独立窗口中，无桌面时使用字符屏，带工具行、文档区、行号和状态栏；`.nvd` 保存格式信息，`.txt` 和 `.md` 保存纯文本。

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

`prism` 只读输出显卡启动快照，并在 ECAM 可用时解析 AER、ACS、ATS、SR-IOV、Resizable BAR、PASID 与 DPC 标记；这些标记不表示对应功能已经启用，也不会加载 NVIDIA 驱动。命令和内置程序支持 `--help`。硬件边界见 [CPU-GPU.md](CPU-GPU.md)。

`forge probe devctl` 定向验证设备控制接口，`forge probe --help` 查看说明；`trial` 继续执行包含它在内的完整用户态回归。DEVCTL 本身是程序接口；当前新加的 `ping`、`wget` 和桌面终端专用 `exit` 也在统一帮助目录中。
