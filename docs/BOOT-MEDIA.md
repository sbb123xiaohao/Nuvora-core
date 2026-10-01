# 通用 UEFI 启动介质与首次上机

在 Ubuntu 中进入项目根目录后构建：

```sh
sudo apt install build-essential binutils python3 mtools xorriso
make -j"$(nproc)" all
make test-host
make iso-uefi media
```

生成的 `build/x86_64/nuvora-core-0.15.0-x86_64-uefi.iso` 是 UEFI 光驱镜像，
可作为虚拟机的光驱介质；`build/x86_64/nuvora-uefi-media.img` 是单个 GPT/ESP
启动磁盘镜像，内含 64 MiB FAT32 ESP，可写入专用 U 盘启动实体电脑。两者使用同一
`BOOTX64.EFI` 和内核，不需要某种虚拟机的配置生成器。虚拟机固件选 x64 UEFI、
关闭 Secure Boot；如添加数据盘，用 SATA AHCI 或标准 NVMe，键鼠可用 xHCI。
当前没有 VirtIO、VMXNET3 或 SCSI 存储驱动；没有匹配的外设会在系统中显示
为未支持，而不是自动加载通用驱动。

## 写入测试 U 盘

先用 `lsblk` 对照容量、型号和连接类型确认**专用 U 盘**的整盘设备路径。
下列命令会覆盖所选设备的全部内容，不能填正在使用的 Ubuntu 系统盘：

```sh
lsblk -o NAME,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS
sudo dd if=build/x86_64/nuvora-uefi-media.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

把 `/dev/sdX` 换成核实后的整只 U 盘，勿用 `/dev/sdX1` 分区路径。
生成器只写工作目录中的普通文件，不会自动触碰实体盘。随后在电脑的 UEFI
启动菜单选择该 U 盘，关闭 Secure Boot。镜像内只有 FAT32 ESP，没有自动安装器
或 Nuvora 数据分区；系统首次启动可在内存中操作桌面，**不会自动分区**。
USB 大容量存储设备目前只能识别，不能在系统中挂载这个 U 盘作为 C:。

首次进入图形设置并创建账户；以后输入密码登录。在 Start → Terminal 输入
`firmament`、`silicon`、`ports --scan`、`partitions`、`volumes`、`net` 和
`wave --test`，记录每一步结果。没有声卡或网卡对应驱动时，
相关命令可能报告未连接；笔记本内置 Wi-Fi 目前不能直接连接。若卡住，记录
机器型号、UEFI 设置、最后的界面和固件/设备信息。正常桌面隐藏内核屏幕日志，
UART 串口若存在仍输出诊断；明确需要文本恢复时，在 ESP 的
`EFI/NUVORA/CMDLINE` 写入 `nv.recovery=1`，恢复模式同样要求账户登录。

发布 ESP、ISO 与 GPT 镜像使用生产内核；`nv.test=1` 对它们没有认证绕过作用。
`make diagnostics` 单独生成 `boot-test.elf` 和 `nuvora-uefi-test.elf`，只供私有
测试镜像使用，不能替换发布介质的 `EFI/NUVORA/NUVORA.ELF`。

当前内核只自动选择签名、GPT 和卷头均通过校验的 Nuvora 数据卷；请先在
专用测试盘或镜像中验证存储，不要将重要数据盘改造成测试卷。实体机尚未完成
认证；UEFI 实现、Intel VMD/RST、4Kn NVMe、内置 Wi-Fi、复杂 HID、GPU
驱动与现代音频路由仍可能限制功能。详见 [设备范围](DEVICES.md)。
