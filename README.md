# Nuvora Core 0.10.0

Nuvora Core 是自研实验操作系统。x86-64 版本提供 UEFI/BIOS 启动、Loom 命令环境、图形桌面、自有数据卷和基础驱动；ARM64 版本目前是独立的引导与 EL0 计算基线。项目尚未完成实体机兼容认证。

## 快速运行

Ubuntu 宿主安装 GCC、binutils、Python 3、QEMU、OVMF 和 mtools 后：

```sh
make -j4
make esp
python3 start.py --uefi --window --audio
```

`start.py` 默认创建 **8 GiB 稀疏 GPT 数据镜像**，保留已有镜像；`--disk-size` 以 MiB 指定新镜像容量，`--partitions 2` 可在首次建盘时生成 C:、D: 两卷。选择存储总线：

```sh
python3 start.py --uefi --window --disk-bus ahci
python3 start.py --uefi --window --disk-bus nvme
```

Windows 10/11 在 PowerShell 中运行 `./start.ps1 doctor`、`./start.ps1 build`、`./start.ps1 run -Uefi -Window`，默认使用 WSL2 工具链。详细依赖、ISO 与独立 UEFI 启动介质见 [Ubuntu 指南](docs/UBUNTU.md)、[Windows 指南](docs/WINDOWS.md)和[启动介质](docs/BOOT-MEDIA.md)。

## 当前功能

| 范围 | 状态 |
| --- | --- |
| 内存 | x64 最多管理 128 GiB 物理地址范围，支持最多 2048 个启动内存图条目；4 GiB 以下 DMA、分阶页分配和小对象 slab；ARM64 上限仍为 64 GiB |
| 数据盘 | IDE、SATA/AHCI 的 512B 与 512e 盘（LBA28/LBA48），以及 512B NVMe namespace；按活动命名空间列表识别稀疏编号，旧控制器回退顺序枚举 |
| 文件 | GPT 默认一个 Nuvora 卷；NVSTORE3 支持 64 位文件大小、稀疏块和双元数据根；旧 NVSTORE1/2 可读取，解除旧容量限制需迁移 |
| 桌面与媒体 | GOP 显示默认进入桌面；文件管理、纯文本编辑和终端可在独立窗口中切换，标题栏拖动与缩放、任务栏及开始菜单可用；Media 流式播放 MP3/MP2/FLAC、WAV/RF64 和 MPEG-1/MP2 视频 |
| 外设与网络 | xHCI Boot 键盘/鼠标（含数字小键盘）、USB CDC-ECM、部分 Intel 有线网卡；特定 ESP USB Dongle 固件提供 Wi-Fi 桥接；IPv4 `ping`、DNS、HTTP `wget` |

桌面按 F10 打开开始菜单；双击桌面图标或点击任务栏打开/恢复窗口，标题栏可拖动、双击最大化，右下角可调整窗口大小，Alt-Tab 切换。Files 可新建文件夹与文本、用 F2 改名、Delete 确认删除；`.txt`/`.md` 在窗口内编辑。终端输入 `exit` 关闭窗口，输入 `loom` 进入完整命令环境。Media 和 Folio 仍以全屏程序运行，退出后回桌面。在 Loom 中使用 `volumes`、`partitions` 查看数据卷，文件改动用 `anchor` 提交；桌面编辑器与 Files 的修改会尝试自动提交。操作细节见[命令手册](docs/COMMANDS.md)、[网络](docs/NETWORK.md)和[设备范围](docs/DEVICES.md)。

## 构建与验证

```sh
make -j4 all
make test-host
make iso-uefi   # 需要 mtools 和 xorriso
make media      # 生成 GPT/ESP 启动镜像，不写宿主磁盘
```

当前通过 28 组宿主回归，包括窗口桌面的分块绘制与控件命中、高地址内存、LBA28/LBA48 SATA、稀疏 NVMe namespace、GPT、文件提交、媒体解码及 ICMP/TCP 与 HTTP 解析。本轮环境缺少 QEMU、OVMF 和 mtools，尚未在客户机或实体机验证窗口交互与网络联机。首次上机请使用专用启动介质和测试数据盘。

目前只挂载 Nuvora 自有格式，**不会格式化或写入普通 Windows/Linux 分区**。4Kn 逻辑扇区、USB 存储挂载、笔记本内置 PCI Wi-Fi、VirtIO/SCSI、Secure Boot、多核和 Linux/POSIX 应用兼容尚未实现。完整边界与历史变更见[测试说明](docs/TESTING.md)、[架构](docs/ARCHITECTURE.md)、[路线图](docs/ROADMAP.md)和[更新记录](docs/CHANGELOG.md)。

## 许可

原创代码采用 MIT。打包的第三方组件及许可见 [third_party](third_party/README.md)；技术资料见[资料列表](docs/SOURCES.md)。
