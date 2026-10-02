# Arch Linux 上构建和运行

在 x86-64 Arch Linux 的终端中安装构建、图形虚拟机和启动介质依赖。
进入本项目根目录，先完整更新系统，再安装软件包：

```sh
sudo pacman -Syu --needed gcc binutils make python git qemu-desktop edk2-ovmf mtools xorriso
make -j"$(nproc)" diagnostics
make test-host
make esp
python3 start.py --uefi --window --audio --network --disk-bus ahci --memory 1024
```

使用下载的完整源码包时，解压后进入项目目录，从 `make` 开始执行。
`qemu-desktop` 包含图形显示和音频模块；图形登录与桌面需要 `--window`。
首次启动在图形界面创建管理员账户，进入桌面后从 Terminal 使用 Loom
命令。纯构建与 `make test-host` 可在没有图形会话的环境运行。

## 固件与数据盘

脚本自动识别 `edk2-ovmf` 提供的
`/usr/share/edk2/x64/OVMF_CODE.4m.fd` 和同目录的
`OVMF_VARS.4m.fd`。它将变量模板复制到
`build/x86_64/ovmf-vars.fd`，后续启动保留副本中的设置。
旧 Arch 包的 `/usr/share/edk2-ovmf/x64` 目录也继续支持。
自定义固件路径可以这样指定：

```sh
NV_OVMF=/usr/share/edk2/x64/OVMF_CODE.4m.fd python3 start.py --uefi --window
```

固件代码文件须有匹配的 VARS 模板。换用不同容量的固件时，先备份并
移走 `build/x86_64/ovmf-vars.fd`，再运行；脚本会拒绝容量不匹配的副本。
Nuvora 尚不支持 Secure Boot，使用普通 `OVMF_CODE.4m.fd`。

`start.py` 首次创建 8 GiB 稀疏 GPT 数据镜像，已有镜像会保留。
首次创建可加 `--disk-size 16384 --partitions 2`；更改这些参数不会重新
分区已有镜像。数据总线可选 `--disk-bus ide`、`ahci` 或 `nvme`。
在 Terminal 中用 `net`、`net dhcp` 查看网络，用 `anchor` 提交数据。

修改内核后，先重新执行 `make -j"$(nproc)" all && make esp`，
再启动 UEFI 虚拟机；固件读取的是 ESP 内的内核副本。

## 启动镜像与验证

```sh
make iso-uefi media
```

UEFI ISO 位于 `build/x86_64/nuvora-core-0.15.0-x86_64-uefi.iso`，
GPT/ESP 启动镜像位于 `build/x86_64/nuvora-uefi-media.img`。
构建 BIOS/GRUB ISO 还需 `sudo pacman -Syu --needed grub`，然后运行
`make iso`。制作启动 U 盘的步骤见 [启动介质指南](BOOT-MEDIA.md)。

`make test` 使用独立诊断内核执行 QEMU 客户机回归；生产镜像保留认证
和桌面会话。具体通过的检查与未验证范围见 [测试说明](TESTING.md)。
Arch 为滚动发行版，依赖使用官方仓库中的完整升级版本；固件文件布局
可参阅 [edk2-ovmf 软件包](https://archlinux.org/packages/extra/any/edk2-ovmf/)。
