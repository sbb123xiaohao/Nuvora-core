# IDE、AHCI/NVMe、USB 鼠标与 HDA 音频（开发范围）

## 数据盘

x86-64 按 IDE primary master、所有可见 AHCI 控制器的 SATA 端口顺序、所有
可见 PCIe NVMe 控制器的顺序寻找有效 Nuvora 数据卷。AHCI 跳过外来格式或
不支持 512 字节逻辑扇区的盘，继续检查后续端口和控制器，使用一个轮询命令槽；
支持 LBA28 与 LBA48 SATA 盘。NVMe 优先读取活动 namespace 列表，处理稀疏
编号；旧控制器未实现列表时按 IDENTIFY 的 NN 字段顺序探测（一次最多 4096 个）。
IDE/AHCI 仅在设备声明支持 FLUSH CACHE EXT 时使用扩展刷盘命令；
否则使用普通 FLUSH CACHE，命令出错仍向提交方报告失败。
仅选择 512 字节扇区、无 metadata 和保护信息的 NVM namespace。控制器
使用轮询管理队列和一个 I/O 队列；
IDENTIFY、读、写和 Flush 使用带 64 位 PRP 的独立 DMA 页。
当前一次只处理一个 512 字节扇区，命令超时或出错后停用该 namespace。
AHCI 在命令完成后还检查任务文件错误和 DMA 的实际字节数；不足 512
字节的传输会报告 I/O 错误并停用该端口，不交付部分读取的数据。
磁盘总容量、GPT 校验、分区与快照写入边界由现有 `kernel/disk.c` 和
`kernel/store.c` 管理：**普通 GPT、NTFS、EFI、非 Nuvora 分区不会被写入**。
GPT 头部需位于磁盘两端；原盘扩容后的有效主表可指向原备表位置。
分区项数组只能位于可用分区范围之外；
保护 MBR 必须包含起始 LBA 为 1 的 GPT 项。主表损坏时校验备表后恢复读取，
不会依照仅有 CRC、但把 GPT 表放在可写卷里的伪造头部写盘。
打开的 Nuvora 旧卷只允许写入自身两个快照槽；NVSTORE3 还可写入本卷的
数据区；不会自动格式化硬盘。

目前只选择一个数据盘；IDE 上有有效 Nuvora 卷时，不同时挂载 NVMe。
ATA/AHCI 驱动拒绝 4Kn 逻辑扇区盘，允许 512e；当前没有 4Kn 写入支持。
不支持 4K 原生 namespace、NVMe 多队列、中断、
热插拔、设备休眠恢复或断电恢复验证。盘符表示 Nuvora 自有卷，
不是 Windows 文件系统兼容承诺。可用 `partitions`、`volumes` 查看已识别卷，
`anchor` 把各卷内存状态提交到其快照槽。
在配有 OVMF、mtools 和 QEMU 的宿主上，`python3 start.py --uefi --window
--disk-bus nvme` 可将已有的 GPT 数据镜像挂为模拟 NVMe 设备；
`--disk-bus ahci` 使用 SATA/AHCI，省略参数使用 IDE。QEMU/OVMF 已验证
SATA 端口 0/5、第二 AHCI 控制器，以及单/多 namespace、多控制器与 PCIe
root port 后的 NVMe 保存和重启恢复；复验入口为
`python3 tests/pc_storage_boot_test.py`，范围见 [TESTING](TESTING.md)。

## 鼠标

xHCI 按控制器能力读取最多 255 个根端口与 1023 个暂存缓冲区，超过 512 个
暂存指针时使用连续两页 DMA 表；AC64 控制器可使用高地址，否则限制在 4 GiB 以下。不能完整分配则不启动该控制器。
支持协议扩展能力中的端口区间按实际端口数检查。控制器停止失败后隔离已交给
设备的页，避免作为普通内存重新分配。

xHCI 枚举 HID Boot Mouse 接口（class 3、subclass 1、protocol 2）及
Interrupt IN 端点，切换到 Boot Protocol，接收三个字节的按键与 X/Y
相对位移。`NV_SUB_INPUT` 向像素屏租约的应用提供事件；`desktop` 使用
单击选中和双击打开。拔出鼠标后清除按键状态和设备计数。
QEMU 图形窗口使用 USB Tablet 的六字节绝对坐标报告，避免宿主鼠标与
虚拟机指针偏移；普通 Boot 鼠标仍使用相对坐标。一次只激活一只鼠标；
主设备拔除后，扫描会选择已枚举且健康的备用指针。
滚轮、多点触控、其他非 Boot HID 报告和 PS/2 鼠标尚不支持。
没有有效 UEFI 像素帧缓冲时，图形桌面不可用。

## 音频输出

x86-64 枚举 PCI class 04:03 的 Intel High Definition Audio 控制器，从
`STATESTS` 找到 codec，沿模拟输出 pin 的连接列表寻找支持 48 kHz
双声道 16-bit PCM 的 DAC。驱动设置 pin、转换器、必要的功放/电源状态，
用两个 HDA BDL 项播放交错 PCM；GCAP 声明 64 位 DMA 时可用高地址，否则使用 4 GiB 以下页。数据先从用户地址
复制进内核 DMA 缓冲，应用不能访问 MMIO 或 DMA 页。

当前版本用 HDA 的可选 immediate-command 通道，固件/控制器不支持时
明确报告无输出；不解析长格式或范围编码的 codec 连接列表。只选第一个
符合条件的模拟 line-out、speaker 或 headphone 路径，没有插拔自动切换、
HDMI/DisplayPort 数字音频、USB 声卡、蓝牙、录音和多音源混合。
全局 0–100 音量在 DMA 拷贝后对 S16LE 样本做软件衰减；桌面任务栏
点击扬声器可拖动滑块，Media 播放页可点滑条或按 `+` / `-` 调整。
它不改动 codec 的模拟放大器，也不控制宿主系统音量。
每次写入同步播放最多 3072 字节，分块重新启动 DMA 会在块间产生短暂间隙
（静音尾部缩为 128 字节）。`wave FILE.wav` 仍是原有短 PCM 测试工具；
图形 `media` 流式解码 MP3/MP2/FLAC、WAV/RF64 和 MPEG-1/MP2 视频，
新 NVSTORE3 数据盘支持大文件，详见 STORAGE-0.10.md。仍不是连续低延迟音乐播放栈，
没有 MP4/H.264/AAC 解码。

`python3 start.py --uefi --audio --window` 将 QEMU `intel-hda` 与 `hda-duplex`
接给 guest，随后在 Loom 输入 `wave --test`。QEMU 自动选择宿主音频后端；
没有声音时应检查宿主 QEMU 的后端设置。音频也可在 BIOS 文本模式使用，
只有像素桌面依赖 UEFI GOP。公开 API 见 [SDK.md](SDK.md)。

驱动参照 [Intel HD Audio 规范](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf)
的 codec verb、流描述符和 BDL 定义；虚拟设备参数见
[QEMU 音频设备说明](https://github.com/qemu/qemu/blob/master/docs/qdev-device-use.txt)。

## 验证边界

`make test-host` 的 NVMe 夹具将实际控制器队列代码接到模拟 PCI/MMIO、
DMA 和 GPT 镜像，检查前置外来 namespace/控制器、格式拒绝、队列回绕、分区边界、写入、Flush 和
I/O 故障。USB 输入夹具验证实际 Boot 报告解码、按键释放与队列溢出；
桌面栅格夹具验证坐标命中和画面一致性；HDA 夹具把实际驱动接到模拟
寄存器、codec verb 和 DMA，检查路由、格式、BDL、错误与用户缓冲边界。
AHCI 夹具还覆盖前控制器仅有外来盘、后控制器有有效卷，以及 BAR5 非法时跳过。
PCIe ECAM 夹具覆盖空总线、后续总线有效设备和 MCFG 不匹配回退。
xHCI 夹具检查 64/255 个端口、跨页暂存指针、4 GiB 以下连续 DMA 分配
和初始化分配失败时的回收。
这些夹具是宿主模拟。另已实际运行 QEMU/OVMF 的 AHCI/NVMe 保存恢复、
xHCI 键鼠热插拔和网络设备专项，详细矩阵见 [TESTING](TESTING.md)。
HDA 实体音频输出和实体设备尚未验证；驱动部署前还需
不同控制器和固件的实机测试、长时间 I/O、突然断电及热插拔测试。

实现依据：NVM Express Base Specification 1.0e 的寄存器、队列与 NVM
命令，以及 USB-IF HID Boot Protocol 的鼠标报告；实现代码未复制其他
操作系统驱动。
