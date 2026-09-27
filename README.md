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
| 桌面与媒体 | UEFI GOP 桌面、文件管理、Folio；Media 流式播放 MP3/MP2/FLAC、WAV/RF64 和 MPEG-1/MP2 视频；HDA 模拟音频输出 |
| 外设与网络 | xHCI Boot 键盘/鼠标、QEMU USB Tablet、USB CDC-ECM、部分 Intel 有线网卡；特定 ESP USB Dongle 固件提供 Wi-Fi 桥接 |

在 Loom 中运行 `desktop` 打开桌面；使用 `volumes`、`partitions` 查看数据卷，修改文件后运行 `anchor` 才会提交到数据镜像。可在虚拟机关机后用 `scripts/import_media.py` 导入文件。操作细节见[命令手册](docs/COMMANDS.md)、[设备范围](docs/DEVICES.md)和[存储与迁移](docs/STORAGE-0.10.md)。

## 构建与验证

```sh
make -j4 all
make test-host
make iso-uefi   # 需要 mtools 和 xorriso
make media      # 生成 GPT/ESP 启动镜像，不写宿主磁盘
```

当前通过 27 组宿主回归，包括高地址内存、碎片化 UEFI 内存图、LBA28/LBA48 SATA、稀疏 NVMe namespace、GPT、文件提交与媒体解码。宿主测试不能替代 QEMU 或实体机启动；本轮没有实体硬件验证。首次上机请使用专用启动介质和测试数据盘。

目前只挂载 Nuvora 自有格式，**不会格式化或写入普通 Windows/Linux 分区**。4Kn 逻辑扇区、USB 存储挂载、笔记本内置 PCI Wi-Fi、VirtIO/SCSI、Secure Boot、多核和 Linux/POSIX 应用兼容尚未实现。完整边界与历史变更见[测试说明](docs/TESTING.md)、[架构](docs/ARCHITECTURE.md)、[路线图](docs/ROADMAP.md)和[更新记录](docs/CHANGELOG.md)。

## 许可

原创代码采用 MIT。打包的第三方组件及许可见 [third_party](third_party/README.md)；技术资料见[资料列表](docs/SOURCES.md)。
