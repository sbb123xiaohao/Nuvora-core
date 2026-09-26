# Ubuntu 上构建和验证

在 Ubuntu 原生终端，或已安装 WSLg 的 Ubuntu WSL2 中，进入本项目根目录。
先安装 x64 构建和 UEFI/QEMU 所需工具：

```sh
sudo apt update
sudo apt install build-essential binutils python3 qemu-system-x86 ovmf mtools xorriso
make -j"$(nproc)" all
make test-host
make esp
python3 start.py --uefi --window --audio --network --disk-bus ahci --memory 1024
```

没有图形窗口时去掉 `--window`，在终端串口输入 Loom 命令。启动后可输入
`net`、`net dhcp`，检查 e1000e 的链路及 DHCP；输入 `partitions` 和
`volumes` 查看数据盘。第一次创建数据盘可以加 `--disk-size 16384`
（MiB）和 `--partitions 2`；已有镜像不会因参数变化而重分区。

启动脚本优先选择发行版提供的 `OVMF_CODE_4M.fd` 与
`OVMF_VARS_4M.fd`。它将 VARS 模板复制到 `build/x86_64/ovmf-vars.fd`；
固件写入只发生在此副本。换用不同容量的 OVMF 时先备份并移走这个
副本，脚本不会默默覆盖已有 UEFI 启动变量。

构建 BIOS/GRUB ISO 还需 `grub-pc-bin grub-common`；生成 VMware VMDK
还需 `qemu-utils`，提供 `qemu-img`。`make test` 是更长的 QEMU 客户机回归，
需要额外时间；成功时结果写入 `build/x86_64/test-results/`。

当前代码的硬件范围见 [设备说明](DEVICES.md)。Ubuntu 宿主的 QEMU 启动
通过，也不能代替 VMware 17 或实体机器的实测。
