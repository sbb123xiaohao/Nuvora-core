# 测试与复现

## 未发布：PC 硬件模拟修复的验证

在包含此前全部修复的 `a6ac234` 上，用 Arch QEMU **11.1.1 / TCG**、
OVMF **202608-1** 实际启动 BIOS/UEFI 客户机。本轮复现并修复：

- 未被固件初始化的第二个 AHCI 控制器保持 `PxSIG=0xffffffff`；在接收
  初始 D2H FIS 前检查签名导致有效盘漏检。现在先启用私有 FIS 缓冲，
  有界等待任务文件就绪，再检查签名；延迟 FIS 的宿主回归也先红后绿。
- 64 MiB UEFI USB 启动中，ELF 暂存 pool 落入内核固定目标范围，加载器
  因重叠拒绝启动。完整验证 ELF 后安全搬移暂存文件，保留目标/stub
  重叠检查、分配失败处理和退出固件服务前的内存归属检查。
- 交互 `trial` / `forge probe` 无条件关闭诊断客户机。测试退出控制只对
  初始 PID 1 生效；子进程返回 shell，直接启动的 probe 仍退出 33。
- 拔掉主 USB 鼠标后，已枚举的备用鼠标没有被激活。扫描处理旧设备后，
  无活动指针时选择健康备用设备，仍只保留一个活动指针。
- USB 网卡卸载后残留网关与 DNS；断连清理现在与其他 IPv4 状态一致。

另修复文件增长诊断的误判：`NV_APPEND` 会忽略写入前的 seek，不能用于
测试 64 MiB 旧卷上限；改用显式定位的读写句柄，未改变文件系统限制。
Folio 串口测试改为等待文档标题，不依赖旧版标题栏的空格数。

生产/诊断 BIOS 与 UEFI 构建和全部 **38 组**宿主回归通过，C 夹具使用 UBSan。

| 实际运行配置 | 检查 |
| --- | --- |
| BIOS：core2duo / Nehalem / phenom / max | 每个模型 144 项断言；8 种必要 CPU 特性缺失时稳定拒绝 |
| 32 / 64 / 128 / 256 / 1024 / 5120 MiB | 每轮 144 项断言；碎片化 32 MiB、OOM 回收、预期内核故障 |
| Q35、默认/无/多功能 VGA | 交互诊断返回 shell；ECAM 与 CF8 回退；硬件阶段 18 组检查 |
| OVMF、USB GPT/FAT32、64 / 128 / 256 MiB | 每轮 144 项断言，退出码 33；64 MiB 原版本启动失败 |
| AHCI 端口 0 / 5、第二控制器 | 保存、关机后重启恢复、交互 144 项断言；外来盘整盘哈希不变 |
| NVMe 单控制器 / 第二 namespace / 第二控制器 / PCIe root port | 同样验证保存恢复、交互诊断和外来盘不变，共 7 个存储拓扑 |
| xHCI、无 PS/2、键鼠和两级 Hub | 534 个 HID 报告、66 次热插拔、24 次重扫、备用鼠标接管；8 组通过，未泄漏页 |
| e1000 / e1000e / USB RNDIS | DHCP、3 次 ping、断连重连、256 KiB HTTP 下载及保存内容逐字节核对 |
| 生产 UEFI 图形会话 | 实际空闲锁屏、错误密码拒绝、解锁、重启持久化与 ACPI 关机 |

网络下载的 SHA-256 为
`2312394bd99545d9de131c24efb781e765ac1aec243f2ed9347597a793a415e9`。
SSE #XM 递交仍有明确 SKIP；不将它计作已验证的硬件异常递交。

```sh
make -j4 diagnostics esp
make test-host
python3 scripts/test.py --phase hardware
python3 scripts/test.py --phase memory
python3 scripts/test.py --phase storage
python3 scripts/test.py --phase usb
python3 tests/pc_storage_boot_test.py
python3 tests/pc_network_boot_test.py
python3 tests/uefi_media_boot_test.py --memory 64
python3 tests/uefi_media_boot_test.py --memory 128
python3 tests/uefi_media_boot_test.py --memory 256
python3 tests/session_boot_test.py --memory 64
```

新增存储/网络专项只使用临时数据盘、ESP 和私有 VARS，失败返回非零；
日志与结果分别输出到 `build/x86_64/pc-storage`、`pc-network-results`
和已有专项目录。UEFI 专项会重建 ESP，避免使用旧 `BOOTX64.EFI`。
这里验证的是 QEMU 模拟 PC；实体主板、真实磁盘突然断电、硬件音频输出、
KVM、多核和未列出的设备组合仍需单独验证。

## 未发布 Arch Linux 支持与 USB 网卡修复的验证

在包含此前全部修复的 `2d7efc8` 上，官方 Arch `edk2-ovmf` 已安装时，
原 `find_uefi_firmware()` 仍返回 `None`；修复后自动选择
`/usr/share/edk2/x64/OVMF_CODE.4m.fd`。新增夹具验证 Arch 点分文件名、
CODE/VARS 配对、变量副本保留和模板缺失时的完整固件回退，Ubuntu
旧布局回归继续通过。

新增 USB 网卡注册夹具在原代码上因全零 MAC 被发布而失败；修复后
全零、组播和广播地址都被拒绝，合法含 `ff` 的单播地址正常注册。
接口卸载后再收到无效地址时，原槽位的厂商信息和 MAC 不变；有效
重插复用同一槽位。CDC-ECM 描述符也在配置网络端点前拒绝全零地址。

实际验证环境为 Arch 官方 OCI 容器，安装官方仓库依赖并完整升级：
GCC **16.2.1**、binutils **2.47**、Python **3.14.7**、QEMU **11.1.1**、
edk2-ovmf **202608-1**、mtools **4.0.49**、libisoburn **1.5.8.2**。
源代码独立复制，使用普通 UID 1000 构建和运行；未复用宿主的编译对象。

```sh
make -j4 diagnostics
make test-host
make esp media iso-uefi
python3 tests/uefi_media_boot_test.py
python3 tests/session_boot_test.py --iso build/x86_64/nuvora-core-0.15.0-x86_64-uefi.iso --boot-only
```

生产/诊断 BIOS 与 UEFI 构建、全部 **38 组**宿主回归（C 夹具使用
UBSan）、ESP/GPT 启动镜像和 UEFI ISO 构建通过。Arch OVMF / q35 / TCG
从可移动 GPT/FAT32 介质启动，**144 项**原生/旧 ABI Ring 3 断言通过，
QEMU 退出码 33；SSE #XM 递交继续由夹具明确标记 SKIP。
生产 ISO 实际通过图形 OOBE、桌面、Terminal 和
**7 项**原生客户端权限断言。

本轮验证的是 Arch 容器中的 QEMU 客户机，未验证 Arch 宿主的 GTK
窗口显示、实体 PC 或实体 USB 网卡；未重跑完整锁屏、重启/关机和
历史 ARM64 矩阵。磁盘格式和应用 ABI 保持不变。

## 未发布 TCP 修复的验证

基于包含此前网络、存储与 HTTP 修复的 `41766e8`，六个新增子场景
分别复现无 ACK 的 RST 中断握手、窗口外 ACK 清掉重传队列、超前 ACK
携带的数据与 FIN 被接收、窗口边界处 FIN 提前关闭连接、EOF 后再次
返回数据，以及序列号回绕后的窗口外 ACK 清队列。相同最终夹具针对
旧版本全部失败，修复后全部通过。

夹具构造带真实 IPv4/TCP 校验和的以太网帧，经 `receive` 进入内核，
使用真实 `net_ioctl` 完成配置、连接、发送和接收；只替换物理网卡与
系统调用的用户地址检查。覆盖无效/有效握手 RST、SYN+ACK+RST、
已连接状态下的 SYN、窗口内乱序段与部分 ACK、重复旧段、实际超时
重传、超前和旧 ACK、零窗口 ACK/数据/FIN、读取后窗口恢复、EOF
保持、跨 32 位序列号回绕和重叠数据去重。

`make -j4 diagnostics` 与全部 **38 组** `make test-host` 通过；六个
新增 TCP 子场景纳入已有网络组，C 夹具使用 UBSan。磁盘格式和应用
ABI 不变。当前环境未安装 QEMU/OVMF，本轮未执行虚拟机或实体机测试。

## 未发布存储与 HTTP 修复的验证

在包含上一轮网络修复的 `a91c331` 上，新增快照回归失败：将懒加载的
旧格式文件从原 EOF 扩容到第 4 页后，未写入区域混入快照中相邻文件的
数据。修复为分别记录逻辑大小和已保存数据大小；这只改变内存中的节点
状态，NVSTORE1/2/3 磁盘格式与应用 ABI 不变。

修复后，真实 `fs.c` / `store.c` 夹具验证原长度为 0、513、4095、4096、
4109 字节的文件，覆盖部分页读取、空洞内写入、原 EOF 所在页填充、
失败提交后继续读取、成功保存后的引用切换和重启恢复；邻接文件保持原值。
原有分页文件 OOM 与 backing I/O 失败回滚仍通过。

HTTP 新增用例在旧代码上分别复现制表符/无空格/组合 `Transfer-Encoding`
漏检、`identity` 前缀误匹配、合法空白与 u64 最大长度误拒绝、冲突长度
以及错误状态码/版本/不完整头误接受。字段名空白、缺少冒号、折叠行与
非法控制字符也被拒绝，合法未知字段继续接受。修复后这些用例与内嵌 NUL、
缓冲区容量边界检查全部通过；未新增分块传输或压缩解码能力。

`make -j4 diagnostics` 和全部 **38 组** `make test-host` 通过，C 夹具使用
UBSan。最后追加的原 EOF 页和失败提交用例另通过存储、分页文件与 HTTP
专项。当前环境没有 QEMU/OVMF，本轮未运行虚拟机或实体机测试。

## 未发布网络修复的验证

基于 `e28270d`（0.15.0）新增的回归在修复前分别复现：e1000 和
I225/I226 驱动拒绝合法的 `02:01:ff:03:04:05` / `02:01:ff:03:04:ff`
地址；未连接 USB 网卡时，选择 `NV_NET_MAX` 误返回成功并丢失当前
接口；重新配置静态 IPv4 后，旧 UDP 数据和待完成的 ping 没有失效。

修复后，生产与诊断 BIOS/UEFI 内核构建通过，**38 组宿主回归**通过。
新增 IGC 夹具覆盖合法含 `ff` 地址、全零/组播/广播/未编程地址拒绝、
高于 4 GiB 的 DMA、TX 环满与 RX 回收。网络夹具通过真实 `net_ioctl`
验证不存在/不支持的接口编号、静态配置及 DHCP 重置、旧包拒收，以及
非法配置保留已有流量。全部 C 夹具继续使用 UBSan。

```sh
make -j4 diagnostics
make test-host
```

本轮使用宿主 MMIO/DMA 与网卡收发夹具；运行环境未安装 QEMU/OVMF，
未执行本轮虚拟机启动或实体网卡测试。下方启动矩阵是历史结果。

## 0.15.0 当前结果

生产/诊断 BIOS 与 UEFI 构建、GPT/FAT32 启动镜像和 UEFI ISO 已构建。
**37 组**宿主回归通过，包括真实账户服务的空闲计时、有效输入重置、
时间回绕、管理进程丢失、可信入口和权限检查；C 夹具使用 UBSan。
UEFI 夹具新增规范 ACPI GUID、版本优先级、空表与高地址来源验证。

QEMU **10.0.11 / TCG** 下，32、256、5120 MiB、碎片化 32 MiB 以及
64 MiB `core2duo` 每轮 **144 项 Ring 3 断言**通过；每轮仍有 1 项
SSE #XM 递交明确跳过。高地址原生 ABI 和旧 ABI 1 实际执行均保留。
`max` 启用 SMEP；无 SMEP 的 `core2duo` 使用原有页保护继续执行。

OVMF / q35、256 MiB、USB 键鼠下，完整桌面操作与账户专项通过；新增
普通应用分别在管理员/普通会话执行 7/10 项真实内核权限断言。生产 USB
介质故意加入 `nv.test=1` 仍显示认证流程；实际等待 1 分钟验证内核自动
锁屏、错误密码拒绝和恢复。图形重启/关机验证默认 Cancel、账户/偏好
持久保存及真实 ACPI 断电；生产 BIOS 恢复登录、重启与 ACPI 关机也通过。
独立诊断内核通过可移动 GPT/FAT32 固件启动并完成 144 项自测。

```sh
make -j4 diagnostics esp media iso-uefi
make test-host
python3 tests/desktop_boot_test.py --machine q35 --keyboard usb
python3 tests/account_boot_test.py --machine q35 --keyboard usb
python3 tests/session_boot_test.py
python3 tests/session_boot_test.py --iso build/x86_64/nuvora-core-0.15.0-x86_64-uefi.iso --boot-only
python3 tests/uefi_media_boot_test.py
python3 tests/account_console_boot_test.py
```

`make test` 显式构建并使用独立诊断内核；生产镜像不会因引导参数关闭认证。
生产 ISO 回归使用真实账户/图形流程，不期待未登录的裸 shell。
当前证据见 [verification/0.15.0](verification/0.15.0/RESULTS.md)。没有实体
电脑接入，也未重跑所有历史网络、断电、BIOS ISO 或 ARM64 矩阵。

## 0.14.0 历史结果

`make -j6 all`、`make media iso-uefi` 和 **37 组** `make test-host` 通过。
新增原生页表/ELF/用户复制/堆夹具，将真实物理页放在 4 GiB 以上，覆盖
跨 4 GiB、PDPT/PML4、最高用户地址、只读/NX/共享 supervisor 隔离及
分配回滚。xHCI/e1000 检查高地址环与 DMA 完成指针；UEFI 夹具覆盖
10-bit GOP 掩码、非整页高地址、保留当前模式和 SetMode 失败回退。
ACPI 电源夹具检查高地址 FADT/DSDT/GAS、校验和及静态 S5 的有界解析。

QEMU **10.0.11 / TCG**：32、64、256、5120 MiB 以及碎片化 32 MiB
配置下，每次 **144 项 Ring 3 断言**通过，退出码 33。新断言实际运行
4 GiB 代码、高地址栈/堆、原生 syscall 和旧 ABI 1 ELF64 fixture。
每轮 SSE #XM 递交仍明确 SKIP 1 项；不将它计作异常递交已验证。

OVMF / q35，256 MiB、xHCI USB 键盘/Tablet、HDA 无声后端：完整桌面
专项通过，包括 OOBE、数字键盘、原生终端/Folio/Media、未保存文档、
窗口拖动/缩放/平铺、搜索/MRU/总览及实际 MPEG 解码和切换窗口。
这是回归模拟环境；HDA 无声后端没有验证实体扬声器或耳机路由。

GPT/FAT32 镜像作为可移动 USB 介质由 OVMF 启动，并完成 144 项原生/旧
ABI 断言；未通过 `-kernel` 绕过固件加载。正常 BIOS 账户专项完成用户
命令 `renew` 重启及 `rest` ACPI 关机，退出码 0，没有使用测试模式的
固定虚拟关机端口。生产启动镜像不携带 `nv.test=1`。

```sh
make -j4 all media iso-uefi
make test-host
python3 tests/desktop_boot_test.py --machine q35 --keyboard usb
python3 tests/uefi_media_boot_test.py
python3 tests/account_console_boot_test.py
```

本轮证据保存在 [verification/0.14.0](verification/0.14.0/RESULTS.md)。
没有实体机器接入，也未重跑所有历史网络、断电和 ARM64 矩阵。
物理兼容范围与上机步骤见 [NATIVE64-HARDWARE](NATIVE64-HARDWARE.md)。

## 0.13.1 历史结果

`make -j4 all esp` 和 **35 组** `make test-host` 回归通过。
新增账户回归在修复前的真实服务上失败：连续五次输错旧密码后，改密码
仍接受新验证；空闲服务在 `0x80000001` tick 误报等待。修复后检查登录
与改密共同限速、等待期间不启动作业、正确验证清除失败计数、计时器
回绕、剩余 1 tick/到期边界，以及长时间未查询后的过期状态。

QEMU 8.2.2 / SeaBIOS，64 MiB：掩码 OOBE、错误密码及保存后重启登录
通过。OVMF 的 q35 + USB，256 MiB：OOBE、登录/锁屏、用户管理、未保存
文本退出确认、普通账户、错误旧密码后重新输入、成功改密及重启拒绝
旧密码/接受新密码，以及 Loom/桌面管理权交接通过。

```sh
make -j4 all esp
make test-host
python3 tests/account_boot_test.py --machine q35 --keyboard usb
python3 tests/account_console_boot_test.py
```

本轮重跑账户专项与全部宿主回归，未重新执行上一版完整桌面、Ring 3
及实体硬件矩阵。账户和数据卷格式保持兼容。

## 0.13.0 历史结果

`make -j4 all`、`make esp` 和 **35 组** `make test-host` 回归通过。
新增 SHA-256 / RFC 4231 HMAC / PBKDF2 向量；账户夹具使用与 Python
`hashlib` 独立核对的 600,000 轮结果，检查持久重载、密码修改、禁用、
限速、最后管理员、别名路径/私有目录、锁屏交互权限及存储失败。账户
界面在 640×480、1024×768、1280×800、1600×900 和 1920×1080 的 RGBX/
BGRX 输出验证分块等价、缓冲边界、比例字宽、秘密清理及安全删除焦点；
既有桌面夹具继续覆盖更大的分辨率。

QEMU 8.2.2 / SeaBIOS，64 MiB：**140 项 Ring 3 断言**通过，退出码 33；
TCG 不触发 SSE #XM 的一项检查明确 SKIP。账户在专用 `nv.test=1`
回归启动中关闭，正常 BIOS/UEFI 启动启用。

QEMU 8.2.2 / OVMF，256 MiB：pc + PS/2、q35 + USB 两条实际桌面路径
通过原生窗口、键盘数字/运算符、未保存文档、搜索输入隔离、MRU/总览、
鼠标拖动/缩放、键盘和拖动平铺、实际 MPEG 解码/暂停/窗口切换。
测试从真实 OOBE 建立账户，再检查桌面。异步客户关闭后显式恢复终端
焦点，拖动坐标从实际标题栏提取，避免使用旧字号下的固定坐标。

q35 + USB 的账户专项实际完成 OOBE、错误密码、锁屏时禁止打开 Start、
添加/禁用/启用用户、未保存文本取消/丢弃后退出、干净的新会话、普通
账户修改密码、重启保留账户/拒绝旧密码/接受新密码，以及 Loom 返回
桌面的管理权交接。正常 BIOS 文本模式另用 64 MiB 完成掩码密码设置、
错误密码和重启登录，串口中没有测试密码明文。

```sh
make -j4 all esp
make test-host
python3 tests/account_boot_test.py --machine q35 --keyboard usb
python3 tests/account_console_boot_test.py
python3 tests/desktop_boot_test.py --machine pc --keyboard ps2 --output build/desktop-pc
python3 tests/desktop_boot_test.py --machine q35 --keyboard usb --output build/desktop-q35
```

测试均使用独立数据镜像；实际截图已人工检查 OOBE、登录、账户管理、
开始菜单、编辑器、拖动和视频。音频仍是无声虚拟后端，没有证明实体
扬声器输出。图形仍是 GOP 软件合成，未实现 GPU 加速；字体仍为 ASCII，
这轮没有新增实体机认证，也未重跑所有历史硬件矩阵。

## 0.12.1 历史结果

`make -j4 all`、`make esp` 和 **32 组** `make test-host` 回归通过。
新增回归在修复前的真实 `kernel/console.c` 上失败：PS/2 小键盘 `+`、`-`
没有产生输入事件。修复后检查 PS/2 与 USB 的 `-+*/`，覆盖 Num Lock
开/关、Shift 按下/松开，确认按键释放不会重复输入，数字/导航切换仍正确。

QEMU 8.2.2 / OVMF 实际启动 pc + PS/2、q35 + USB 两条 UEFI 桌面路径，
均使用 256 MiB 和独立临时数据盘。在 Text Editor 中将主键区的 `-+*/`
与小键盘输入逐像素对比：旧 0.12.0 的 PS/2 路径只显示 `*/`，对比失败；
修复后两条路径均完全一致，串口无 panic/trap。复现命令：

```sh
make -j4 all
make esp
make test-host
python3 tests/desktop_boot_test.py --machine pc --keyboard ps2 --keypad-only --output build/keypad-pc
python3 tests/desktop_boot_test.py --machine q35 --keyboard usb --keypad-only --output build/keypad-q35
```

本轮修复内核输入解码，未重新运行下方完整桌面交互与历史 Ring 3 启动组合。

## 0.12.0 历史结果

`make -j4 all`、`make esp` 和 **32 组** `make test-host` 回归通过。
新增搜索多词/空结果、稳定 MRU 与客户退出、平铺/恢复、预览布局夹具；
实际 PS/2/USB 解码代码验证 Super、修饰键松开、多键盘合并和满队列释放。
xHCI 的真实 Boot 报告处理另检查短包、按键重复、rollover 中的修饰键释放、
Caps/Num Lock 不自动重复。合成器检查 RGBX 红蓝转换、预览/切换器的整屏与
分块一致性和输入命中边界；C 夹具使用 UBSan。

QEMU 8.2.2 / SeaBIOS 的 64 MiB 客户机完成 **140 项 Ring 3 断言**，
退出码 33；TCG 不触发 SSE #XM 的一项检查在日志中明确 SKIP。

OVMF 的 q35 + USB Boot 键盘、pc + PS/2 键盘两条实际 UEFI 桌面路径，
均使用 256 MiB、USB Tablet、模拟 HDA 与临时 128 MiB 数据盘，检查：

- 原生 Terminal/Media/Folio 并行、数字输入、未保存取消/丢弃、连续开关；
- Super 打开 Start、应用多词搜索、空结果 Enter、全选替换，以及查询
  输入没有写入终端；Escape 清空查询，再关闭菜单；
- 按住 Alt 连续选择，松开提交、Esc 取消、正向/反向 MRU；
- 窗口总览恢复最小化窗口、七个窗口翻页、鼠标选择已有窗口；
- Win 方向键平铺/最大化/恢复，标题拖动与边缘缩放，从最大化拖回自由
  窗口，贴边预览在释放后提交，恢复拖动前的位置和大小；
- 等待真实 MPEG 彩色图像，检查暂停及切换到终端和 Files。

测试的 QMP 使用 stdin/stdout 管道；各实例复制自己的 ESP 和 OVMF 变量，
不会打开用户的数据镜像，也可同时运行。音频采用无声虚拟后端，不能证明
扬声器可听见声音。截图是实际软件合成输出，已人工检查菜单、总览、
切换器和平铺；没有用示意图片替代运行画面。

Ubuntu 装好依赖、QEMU、OVMF、mtools 和 FFmpeg 后：

```sh
make -j4 all
make esp
make test-host
python3 tests/desktop_boot_test.py --machine q35 --keyboard usb --output build/desktop-q35
python3 tests/desktop_boot_test.py --machine pc --keyboard ps2 --output build/desktop-pc
```

PPM 截图和串口日志保存到输出目录。FFmpeg 只在宿主延长已提交的短视频
测试片段。未验证实体机、Windows 宿主、长时间影音同步、GPU 加速、屏幕
阅读器或显示器帧率；桌面仍为 ASCII 界面，本轮未重跑历史多 GiB 组合和
全部故障注入。下方记录是历史结果。

## 0.11.0 历史结果

`make -j4 all`、`make esp` 与 `make test-host` 的 **30 组**通过。
窗口夹具执行真实 `kernel/window.c`，检查整帧提交、跨页复制、配置序号、
旧快照拒绝、分配失败回滚、所有者权限、按键/鼠标顺序、终端继承输入输出
和进程清理。新增原生画面与字符窗口的整屏/分块绘制、裁剪和光标修复检查；
C 夹具使用 UBSan。普通文件的程序读取另验证临时缓冲的内容、OOM 和 I/O 回滚。

QEMU 8.2.2 / SeaBIOS 的 64 MiB 客户机完成 **140 项 Ring 3 断言**，
包含成功/失败 exec、12 次替换回收、进程和内存隔离；TCG 不触发 SSE #XM
的检查在日志中明确 SKIP。内核栈上下保护页与初始化异常三种故障注入均
达到预期诊断和退出码。

q35 与 pc 两种机型的 UEFI 桌面交互检查均通过，使用 OVMF、256 MiB、
USB 键盘/Tablet、模拟 HDA 和独立
128 MiB 数据盘，检查终端命令输入、Media/Folio 窗口、数字输入、未保存
取消/丢弃、连续开关窗口、最小化、最大化、恢复、鼠标拖动和边缘缩放。
视频检查等待实际 MPEG 彩色图像，再验证暂停及切换到终端、Files；
不是仅检查播放器界面是否出现。测试使用无输出的虚拟音频后端，不能证明
扬声器可听见声音。启动盘与数据盘均为临时副本，不打开用户现有数据镜像。

Ubuntu 装好构建依赖、QEMU、OVMF、mtools 和 FFmpeg 后可复现：

```sh
make -j4 all
make esp
make test-host
python3 tests/desktop_boot_test.py --machine q35 --output build/desktop-q35
python3 tests/desktop_boot_test.py --machine pc --output build/desktop-pc
```

PPM 截图与串口日志保存在指定输出目录，供人工检查。FFmpeg 只在宿主延长
已提交的半秒测试片段，测试素材不进入内核。尚未验证实体机、Windows
宿主、长时间视频同步、GPU 加速或实际显示器帧率；本轮未重跑所有历史
多 GiB 客户机组合。下方版本数字和旧环境限制为历史记录。

## 0.10.0 历史结果

本次窗口桌面增量：`make all` 和 `make test-host` 的 28 组宿主回归通过。
桌面栅格夹具覆盖空桌面与窗口的 640×480、1024×768、1280×800、
1600×900、1920×1080、
2560×720、2560×1000、2560×1440 的整屏与分块一致性、任务栏/标题栏/文件
操作/编辑器弹窗命中与边界；人工查看 640×480、1024×768、1280×800、
1920×1080 的桌面预览，以及 Files、开始菜单、编辑器未保存提示与
终端窗口的 1280×800 实际绘制画面。字体生成脚本单独运行，构建使用
已提交的四档灰度字形。网络夹具运行
内核实际 IPv4 代码与模拟网卡，验证 DHCP、ARP、UDP、主动 ICMP 校验和与
回包、TCP SYN/SYN-ACK/ACK、数据收发、重复片段、FIN、进程清理和失链；
另有 HTTP URL、状态行与长度溢出解析夹具。
构建环境缺少 QEMU、OVMF 与 mtools，未执行本次 UEFI 客户机窗口交互或
实体机验证。请在 Ubuntu 装齐依赖后使用 `make esp`、
`python3 start.py --uefi --window --network --audio`，在桌面中验证双击文件、
窗口拖动/缩放、任务栏、编辑器保存/取消、文件改名/删除与终端命令。
终端可试 `net`、`ping 10.0.2.2` 和
`wget -O /home/test.txt http://可访问的HTTP主机/文件`，再检查下载文件。

本轮增加 NVSTORE3 最终 Flush 失败后的文件树检查：新建、截断、删除、
改名和替换都返回原 I/O 错误，已有文件内容与长度保持不变；临时目录可继续
使用。ATA/AHCI 夹具分别验证 LBA48 盘有无 FLUSH CACHE EXT 能力时的命令。

本轮补测扩容后主表指向旧备表位置、带有效 CRC 但分区项数组落入数据区、
错误主备头部指针、无效保护 MBR 与备表恢复；AHCI 夹具注入短 DMA 和
taskfile 错误。异常表被拒绝，
不会先读取其指向的数据区；短读写不会返回成功。

`make -j4 all` 及 `make test-host` 的 **27 组**通过（所有 C 夹具 UBSan）。
本轮新增 64 GiB 以上的页分配与 768 项 UEFI 内存图样本；AHCI 夹具覆盖
LBA28/LBA48 命令 FIS、读写和 Flush；NVMe 夹具覆盖稀疏活动 namespace
列表及列表不可用时的顺序回退。x64 管理上限现为 128 GiB，尚未在
128 GiB 实机上进行压力测试。
新增 xHCI 能力解析与暂存页分配夹具，覆盖 64/255 个端口、512 项跨页边界、
最多 1023 个暂存页、分配失败回收及 4 GiB 以下连续页约束。
AHCI 模拟增加后续控制器上的有效卷、前控制器外来盘隔离和非法 BAR5；
PCIe 模拟增加 MCFG 区域首总线为空、后续设备匹配及配置空间不匹配回退。
新增 GPT/ESP 可移动启动镜像的主备 GPT CRC、分区位置和 FAT 镜像字节比对；
NVMe 夹具新增同控制器后续 namespace、第二控制器的外来盘隔离。
新增双 SATA 端口的真实 AHCI/卷识别代码夹具：前盘是外来格式、
不支持 LBA48 或 4Kn 时，后盘仍可挂载，写入不会落到前盘。
新增 OVMF 成对固件与 QEMU IDE/AHCI/NVMe 参数的宿主回归；在 Ubuntu
上仍需运行 QEMU 客户机测试，不能把参数检查当成 UEFI 实际启动。
本轮新增 AHCI 命令 FIS/高位 LBA、DMA 读写/Flush/错误停用夹具，以及
e1000e 网卡选择和真实驱动收发环夹具。通用 Windows PowerShell 入口未在
Windows 上执行；当前环境没有 QEMU、mtools、xorriso 或 PowerShell。
新增真实 fs/store 的 10 GiB 稀疏长度和 >4 GiB 物理地址读写、160 MiB
连续数据且索引内存不随内容增长、180 次碎片覆盖/OOM 回滚、磁盘满、
不确定最终 Flush 阻止继续写入；ATA/NVMe 各跑旧格式与 NVSTORE3 GPT。
新 Python/内核互操作测试验证宿主导入的 6 GiB 文件能由内核挂载、修改，
再由宿主解析新根；140 MiB 实数据、旧盘只读迁移和源 SHA-256 不变通过。
媒体覆盖真实 24-bit FLAC、720p MPEG 文件回调流、64 位回调位置、PCM
8/16/24/32-bit、float 样本边界和 8 GiB RF64 头。FFmpeg 安装时另验证
MP4/H.264/AAC 转换及导入；缺少宿主 FFmpeg 时该外部转换子项明确跳过。
桌面 64 位大小单位显示通过多分辨率绘制检查，1280×800 图像已人工复核。

本轮未运行 QEMU/UEFI/实体硬件；宿主控制器夹具和重挂载测试不能替代
真实启动、断电写缓存、长视频同步或实体声卡验证。细节见 STORAGE-0.10.md。
下方 0.9.0 及更早数字保留为历史记录，不代表这轮虚拟机运行结果。

## 0.9.0 文件存储改动

`make test-host` 的 16 组 UBSan 宿主回归使用真实 `kernel/fs.c` 与
`kernel/store.c`：64 MiB 稀疏寻址、写入中途内存不足的页回滚、
8 MiB 稀疏文件的流式保存和重启读取、已提交文件的写时复制、
双卷独立提交、提交头失败、较新快照 CRC 回退、两个非空槽均无效时
禁止覆盖，以及旧格式内容导入。关机导入器还验证普通/空文件与
超过旧 4 MiB 上限的数据文件。当前环境未安装 QEMU，修改后的
客户机磁盘路径与实体断电语义仍待运行验证。

## 0.9.0 源码扩展：NVMe、USB 鼠标与 HDA 音频

当前环境实际执行 x64 `make -j4` 以及 `make test-host`，通过 14 组
UBSan 宿主源码回归。新增 NVMe 夹具模拟控制器寄存器、PCI、队列和 DMA，
使用真实双分区 GPT 数据镜像，验证扇区格式、读/写/Flush、非快照槽拒绝、
队列回绕及故障停用。USB 鼠标夹具验证 Boot 报告有符号位移、按键释放、
事件队列回绕和清空；桌面夹具检查选中命中和渲染分块一致性。
HDA 夹具验证真实驱动在模拟 MMIO、codec verb、DMA 上的路由选择、
双描述符播放和用户缓冲校验；实际绘制 1280×800、640×480 界面截图
并人工检查。`forge probe devctl` 在之前 137 项基础上增加 2 项音频断言，
x64 guest 运行器预期 **139 项**。当前环境没有 QEMU、OVMF、mtools
或实体硬件，因此没有本轮 guest/UEFI/实体 SSD、鼠标和音频的运行通过记录。下方较早的
131 项 guest 与 8–11 组宿主数字均属以前源码的历史验证，不适用于当前扩展。
设备范围和进一步验证要求见 [DEVICES.md](DEVICES.md)。

本轮 Media 扩展的 `make test-host` 共 16 组，使用合成的真实 MP3
（44.1 kHz）和 MPEG-1 Program Stream（160×120、25 fps、MP2 音轨）
验证解码输出，比较图形完整/逐块绘制结果，并检查 GPT 镜像导入的
CRC、双槽提交、重复文件拒绝和显式替换。FFmpeg 仅用于生成已提交的
测试样本，不进入目标系统。还需在 QEMU UEFI/HDA 上验证播音连续性、
长文件以及实体声卡路径。

**像素桌面扩展的验证边界：**x64 `make -j4` 已通过；宿主截图夹具
以实际 `user/desktop_ui.h` 绘制 1280×800、640×480 的布局并人工检查；
`make test-host` 的桌面组对四种分辨率、两种像素格式比较整屏与分块绘制，
并用 UBSan 检查边界与盘符选中状态。
`forge probe devctl` 新增 4 个显示相关断言，故 QEMU 运行器将预期数从
131 改为 135；当前环境没有 QEMU/OVMF，不能将历史的 131 项运行成绩
当成本轮 135 项的通过结果。真实 UEFI GOP、显示复制和按键启动流程
仍需 QEMU 与实体设备运行验证。

**实体网络扩展的验证边界：**本轮 `make -j4` 通过，`make test-host`
现为 11 组 UBSan 宿主测试。新增 `tests/network_test.c` 将实际
`kernel/net.c` 接入模拟的物理 NIC，覆盖 PCI 型号识别、DHCP
Discover/Offer/Request/Ack、ARP 邻居缓存、UDP 收包、IPv4 校验和错误与
链路断开清理。USB CDC-ECM/CDC 控制和 I225/I226 DMA 寄存器路径仅通过
编译和代码检查；没有实体网卡或 QEMU，因此尚无 Wi-Fi 入网或实体有线
收发的运行记录。之前记录的 QEMU 结果不能替代本轮网络验证。

**GPT 分区源码扩展的验证边界：**本轮 `make -j4` 与 `make test-host`
已在当前宿主执行，新增第 9 组测试使用真实 `mkgptdisk.py` 生成的双分区
GPT 镜像和实际 `kernel/disk.c` 的 ATA 端口模拟器，检查扫描、几何及写入
范围；RAM 文件夹具另外覆盖 C:/D:/ 路径和快照隔离。下方原版 0.9.0
的 QEMU、OVMF、ARM64 运行数字是以前版本的记录，**不能当成本扩展的
虚拟机启动结果**；当前环境缺少 QEMU/ARM64 交叉编译器，需复验。

x64 与 ARM64 在 QEMU 8.2.2 TCG 模拟器中实际执行；编译器为 GCC 13.3.0，链接器为 GNU ld 2.42。虚拟硬件覆盖 PC 与 q35、单 CPU、VGA、COM1、PS/2、PIT/PIC，存储测试连接 IDE primary master 数据镜像；q35 额外挂载 ISA IDE 桥，以保持内核传统 ATA PIO 端口与 q35 的 AHCI 默认设备隔离。本轮 UEFI 检查使用 Ubuntu OVMF 的 `OVMF_CODE_4M.fd` 与配套 VARS，通过 pflash 加载（可用 `NV_OVMF` 指定固件），数据镜像与 ESP 分别挂在 IDE primary master 和 slave。

## 结果范围

| 架构 | 用户态断言 / 每次启动 | 内存配置 | 宿主集成检查 |
| --- | --- | --- | --- |
| x86-64 | 131 项 | 32 / 64 / 128 / 256 MiB，另有 1 GiB 与 5 GiB（跨 4 GiB 边界）回归 | 完整回归 53 组通过（含 BIOS/UEFI ISO） |
| ARM64 | 15 项 / 单个 EL0 工作负载 | 64 / 256 / 1024 / 5120 MiB | 4 组（设备树、分阶页与 slab、页表、NEON、SVC 与隔离） |

执行记录分别位于 `build/x86_64/test-results/` 和 `build/aarch64/test-results/`。`RESULTS.md` 与 `results.json` 由测试程序根据成功执行结果生成。每次 probe 的完整日志含逐项 PASS 记录；宿主必须同时收到 QEMU 正确的退出码并找到零失败汇总，才把该轮判为通过。

## CPU / 平台 / 显卡矩阵

| 架构 | 成功运行完整 probe 的 CPU 模型 | 启动前拒绝的 CPU 配置 |
| --- | --- | --- |
| x64 | 默认 qemu64、core2duo、Nehalem、phenom、max | qemu64 分别关闭 nx、pae、fxsr、sse2、lm、msr、fpu、cmov |
| ARM64 | QEMU virt 上的 cortex-a57 | 尚无 ARM64 CPU 特征拒绝矩阵 |

这是 QEMU TCG CPU 模型验证，不表示对应实体处理器已经认证。

每次 probe 有 1 项硬件异常递交检查明确跳过：有 SSE 的 QEMU 8.2.2 TCG 记录了未屏蔽除零的 MXCSR 位，但没有递交 #XM；无 SSE 的模型不执行 SSE。日志以 `SKIP` 单列，成功断言只校验其结果分类，不能解读为 #XM 递交已通过。物理 CPU 上若出现相同问题，测试不会将其当作 TCG 跳过。

两个 `vector` 工作进程验证全部 x87 / MMX 数据和 XMM0–7（x64 为 XMM0–15）、控制字与 MXCSR 在定时器抢占、yield、sleep、SPAWN 与 EXEC 中的隔离。父进程保持另一组浮点数据；新进程检查清零状态。另有真实 x87 #MF 和 AVX #UD 进程故障隔离。

显卡分别测试默认 QEMU VGA、`-vga none` 和同一设备号上 0/1 两个 function 的 VGA。PCI 解析器另在宿主执行 25 项合成配置检查，含 NVIDIA 显示/音频区分、64 位 BAR 超过 4 GiB、BAR 高半合并、无效末尾 BAR、legacy/extended 能力链环、越界与未对齐，以及 AER、Resizable BAR、ACS、ATS、SR-IOV、PASID、DPC 标记。

ACPI 解析器执行 11 项合成固件检查，覆盖 RSDP 双校验和、EBDA 优先、XSDT 到 RSDT 回退、坏 MCFG 校验和、部分记录、未对齐、重叠、容量边界和不可读地址。q35 guest 必须从 MCFG 启用 `0xb0000000` 的 0–255 总线 ECAM、通过完整 probe，再以 `nv.no-ecam=1` 启动并通过 CF8 回退读取同一显卡。没有实体 NVIDIA GPU 或实体主板 ACPI/ECAM 测试；宿主解析器检查不属于 QEMU guest 检查。`--phase hardware` 单独执行此阶段。

## DEVCTL 整合回归

x64 包含 14 项设备控制断言：操作路由、保留接口的 ENOSYS、完整 16 字节输出边界、只读/跨页/溢出与高位指针、索引错误时响应清零、600 次失败请求后仍能映射、600 次成功请求复用窗口、MMIO 保持 supervisor-only。无显卡/单显卡/多功能显卡三种场景还分别运行两个独立 `forge probe devctl` 子进程，核对发现快照保持一致；无显卡时明确跳过实际映射检查。

## 检查内容

- 系统调用：未知调用号、越界和溢出指针、内核地址、只读输出、跨页输出的完整检查、过长字符串和有界 I/O。
- 平台：固定平台 ABI、ACPI/MCFG/ECAM 状态一致性、q35 4 KiB PCIe 配置访问、显式禁用后的 CF8/CFC 回退，以及合成坏表隔离。
- 内存：物理分配器自检、堆块合并、用户页零初始化、分配/收缩、错误返回在 64 位指针中的保留、页表回收；x64 另在 1 GiB 与 5 GiB（跨 4 GiB 边界）配置下运行完整用户态断言，覆盖 2 MiB 大页、独立 supervisor 物理别名、64 位页地址与 DMA 掩码约束。
- 文件：目录与相对路径、排他创建、读写/seek、稀疏区清零、追加、截断、复制和重命名、目录环、忙状态、句柄耗尽、虚拟设备与状态文件；规范路径恰好 191 字节时可用，再增加节点会被拒绝，文件重命名拒绝目录后缀。
- 加载：无效文件、越界 ELF 程序头、内核地址入口，以及失败路径上已分配页的回收。
- 进程：启动、等待、只领取一次状态、40 次重复创建/回收、进程槽耗尽与恢复、未领取退出状态时内存释放、孤儿进程退出回收。
- EXEC：内核地址参数拒绝、缺失文件、20 次映射后失败的完整回滚、12 次原地替换；验证 PID、父子关系、cwd、文件偏移保留，旧映像与屏幕租约释放。
- 内核保护：主动触发内核栈上下保护页；下界溢出必须经独立应急栈报告 double fault，上界写入必须报告 page fault；PID 1 异常必须给出明确停止诊断。故障注入仅在 `nv.test=1` 下启用，这三项预期 QEMU 退出码为 35，不能与普通测试失败混为一谈。
- 真实内存压力：在 32 MiB 虚拟机中用多个用户进程占满物理页，连续 8 轮 EXEC/SPAWN/GROW 都返回内存不足，调用者保持有效；清理后核对物理页、内核堆、文件节点和进程计数，并重新启动应用。
- 抢占：两个不调用任何系统调用的忙循环都获得 CPU tick；测试进程能够恢复并终止它们。
- 故障：空指针、访问内核内存、写代码段、特权指令、除零、无效操作码、用户栈保护页、x87 未屏蔽除零 #MF 和未启用的 AVX #UD。
- x64 专项：64 位代码、高位参数拒绝、r8–r15 等寄存器的高 32 位跨调度保存、NX 阻止从堆和栈执行。
- 存储：两次提交后恢复最新代次、未提交修改丢弃、`/tmp` 清空、实际重启、四类快照损坏回退、非本项目磁盘全盘 SHA-256 保持不变；4 TiB NVSTORE2 稀疏镜像的 anchor/重启恢复往返覆盖 u64 总扇区数与 LBA48 命令模式，但快照槽位于低 LBA，不能据此声称做过高 LBA 实盘传输。高 LBA 的寄存器字节序列由下述端口夹具验证。
- UEFI（x64，需 OVMF/edk2 固件与 mtools 生成的 ESP）：stub 完成启动（携带 .reloc 基址重定位表、多卷回退）、内核到达 Ring 3，`horizon`/`origin` 可用；anchor 后重启从数据盘恢复 `/home`，并运行完整 `trial`；UEFI El Torito ISO 光驱启动同样进入用户态。
- 使用流程：在 Loom 中执行 `trial` 并回到提示符；模拟 PS/2 键盘输入成功；保存真实 VGA 截图。
- USB：PCI 控制器分类、xHCI 描述符与字符串、键盘 Boot Protocol、鼠标/存储只读识别、两级 Hub、66 次根端口热插拔、拔除后的 DMA 页回收和事件/命令环回绕。`--phase usb` 单独执行这组检查。
- 帮助：当前脚本准备逐一查询 35 个命令的 `--help`、`help`/`atlas` 别名、错误参数和字面量 `--help` 文件内容；10 个用户程序以 `forge APP --help` 正常退出。`--phase help` 单独执行这组检查，尚待本轮 QEMU 运行。
- Folio：创建 `.nvd`，输入并选择文本，应用粗体和一级标题，关闭后重新打开校验，导出 `.rtf` 并检查标题字号和粗体控制字。
- 启动介质：从 GRUB BIOS ISO 的虚拟光驱进入用户态，成功运行一个独立 ELF 子进程；x64 另从 UEFI ISO（OVMF）进入用户态。

每个内核还在进入用户态前运行物理页、堆和页权限的内部自检；测试启动还会耗尽全部可分配物理页，逐一验证仅有 0–4 页可用时，页表与内核栈的部分分配回滚。内部自检没有额外计入上述用户态断言数字。

## 复现命令

```sh
make -j4
make iso
make esp        # UEFI：需要 mtools
make iso-uefi
python3 scripts/test.py --iso

make ARCH=aarch64 CROSS=aarch64-linux-gnu- -j4
make ARCH=aarch64 test
```

缺少制作 ISO 的工具时，`make test` 仍可运行直接启动及存储测试，只跳过 ISO 光驱检查；缺少 OVMF 固件时 UEFI 组打印跳过原因；x64 测试前必须用 mtools 构建 ESP，缺失产物会报错。发布打包要求实际完成 UEFI 组，不能用跳过结果通过。也可通过 `--phase storage` 或 `--phase iso` 定向诊断；这些定向执行会生成仅包含相应阶段的结果表，不能作为完整测试矩阵。

测试使用 `build/ARCH/test-results/` 下的专用临时镜像，不使用 `start.py` 的 `nuvora-store.img`。完整回归会写入 `build-fingerprint.json`，记录已测试内核、用户程序、全部 C/头文件/汇编/链接脚本、构建及测试脚本、EFI/ESP 与 ISO 的 SHA-256；`execution.json` 另记录完整阶段、ISO 检查和成功组数；打包时逐项核对，防止把测试后改变的二进制当作已验证版本。发布包保留执行日志和截图，不附带故意损坏的测试磁盘；重新运行会自动生成它们。

## 尚未验证

没有验证真实主板的 ACPI/ECAM 固件差异、真实 PCIe/NVIDIA 硬件、实体主板 UEFI 固件（含 Secure Boot 与带重定位的加载场景）、KVM、其他虚拟机品牌、多核、网络设备、NVMe、长时间运行、所有低内存组合，以及真实磁盘突然断电的全部时序；USB 验证使用 QEMU xHCI/UHCI、键盘、鼠标、Hub 和虚拟存储设备。这些结果证明本次实现覆盖的功能路径实际运行过，不是生产级完整内核认证。

## 源码边界夹具

构建 x64 内核后执行 `make test-host`，或 `python3 scripts/test_regressions.py`。0.9.0 共 8 组通过，采用 GCC UBSan，直接包含被修复的 Nuvora C 源码；固件与 I/O 回调是明确的模拟输入，并非实体硬件结果。日志由测试程序输出。

| 组 | 覆盖 |
| --- | --- |
| 地址 | 跨 4 GiB 的对齐、NX 地址掩码、物理地址与内核指针往返 |
| 分阶索引 | 4000 轮随机碎片分配、保留空洞、对齐块与分阶合并，逐次对照朴素空闲扫描 |
| 分配器 | 连续缓冲跨 1 GiB 时用户页不变、保留区、DMA 上界、碎片化分配失败和恢复、12000 轮堆操作及 slab 空页归还 |
| 堆页表 | 分散的物理页、0/1/17/1024/2047 页后注入 OOM 的完整回滚、固定页、NX/supervisor 权限、共享映射不随进程销毁 |
| RAM 文件内存 | 4 种快照恢复长度，在充足/临界/不足堆预算下扩容；容量封顶、数据与稀疏区、失败原子性、完整回收 |
| ATA | `0x123456789abc` 的 LBA48 端口序列、LBA28 边界、flush 命令、签名/版本/几何错误 |
| 快照 | 低 RAM 无法恢复时禁止覆盖、极小槽位缓冲边界、扇区尾部清零 |
| UEFI | 分段读取与 rewind、退出服务重试内存图更新、恶意 ELF、内存所有权、段复制和 BSS 清零 |

本轮 OVMF 的 8 MiB 地址附近存在 ACPI NVS，固定 BSS 中的 8 MiB 堆使旧 ELF 跨入该保留区。修复后堆从可用 RAM 分配，最终 UEFI 直接启动、数据恢复和纯光驱 ISO 启动均实际通过。测试期间没有另外附加 ESP 来代替 UEFI 光驱启动。

## 0.7.2 内存专项复现

`python3 scripts/test.py --phase memory` 执行内存矩阵、内核保护页、低内存压力和 x64 碎片化启动。定向运行不代替发布所需的 `--iso` 完整矩阵。

x64 的 `nv.test=1 nv.memory-test=fragmented` 在启动内存表中加入 6 个页大小的保留空洞（8–28 MiB，每隔 4 MiB），自动用 32 MiB QEMU 执行。0.7.1 在该配置出现 `cannot reserve kernel heap`；0.7.2 通过完整用户态检查。夹具只在两个测试参数同时存在时启用，普通启动不会加入空洞。

x64 在 0.8.0 执行 12 轮 1/511/512/513/1023/2048 页的用户堆扩缩容，核对再分配清零、释放后地址拒绝访问、空闲页计数恢复；x64 另验证直接访问物理映射别名和 1 TiB 内核堆只能终止子进程，不能读取内核内容。根因、失败证据和变更范围见 [MEMORY-0.7.2.md](MEMORY-0.7.2.md)。

## 0.7.3 文件扩容复现

`--phase memory` 与 `--phase storage` 均包含 32 MiB 的恢复文件扩容检查。运行器写入带有效校验和、含 131071 字节文件的专用快照；启动后以 `forge probe file-growth` 执行 7 项检查，要求追加后的堆占用维持 128 KiB、数据完整、超限拒绝、删除全部回收。x64 日志为 `restored-file-growth.log`，独立于常规 probe 的 131 项；ARM64 尚无此文件系统。

宿主夹具使用真实 ramfs 与堆分配器，旧 0.7.2 在恰好 128 KiB 可用时返回 ENOMEM 的日志也随包保留。详细根因见 [MEMORY-0.7.3.md](MEMORY-0.7.3.md)。
