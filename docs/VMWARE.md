# VMware Workstation 17 配置说明

`scripts/vmware.ps1` 生成一个不依赖 Linux shell 的 VMware 配置。它不会
修改原始 raw 镜像；只在 `vmware/<名称>/` 生成 VMDK、VMX 和后续的 NVRAM。

```powershell
.\start.ps1 vmware -VmName NuvoraCore -DiskController ide
```

配置采用：

| 项目 | 设置 | 原因 |
| --- | --- | --- |
| 固件 | UEFI，Secure Boot 关闭 | 使用 `BOOTX64.EFI`；loader 尚未签名 |
| 数据盘 | IDE（可选 NVMe） | IDE/AHCI/NVMe 均有内核路径，IDE 最保守 |
| 网卡 | `e1000e`，默认 NAT | VMware 常见虚拟网卡；内核使用标准 Intel 描述符 |
| USB | xHCI / USB 3.x | 接入 USB Boot 键盘、鼠标 |
| 音频 | HDAudio | 落到现有 Intel HDA 播放路径 |
| 显示 | SVGA，3D 关闭 | UEFI GOP 像素桌面，不依赖 VMware 3D 驱动 |

Workstation 17 中可以在虚拟机硬件设置里把网络改为 Bridged，把 USB
控制器保留为 USB 3.x；若宿主不需要音频可移除 Sound Card。不要改成
VMXNET3、SCSI 或 Secure Boot 后再据此判断系统坏了，这些路径当前没有
对应的 Nuvora 驱动或签名 loader。

生成器需要 PATH 或 WSL2 中的 `qemu-img`。如果 `NuvoraCore.vmx` 已存在，
必须显式传 `-Force` 才会覆盖配置；已有 VMDK 始终保留。当前内核仅运行
一个 CPU，模板也只给一个 vCPU。
