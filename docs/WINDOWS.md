# Windows 主机构建与通用启动介质

Windows 10/11 在 PowerShell 中使用同一源码。默认通过 WSL2 调用 GNU
构建工具；如已安装 MSYS2 工具链也可使用 `-Native`：

```powershell
.\start.ps1 doctor
.\start.ps1 build
.\start.ps1 iso -Uefi
.\start.ps1 media
.\start.ps1 run -Uefi -Window -Audio -Network
```

`iso` 产出可挂载到 UEFI 虚拟机光驱的 ISO；`media` 产出可用于 GPT 虚拟磁盘
或实体 U 盘的原始镜像。所有虚拟机和实体机使用相同的启动文件，项目不生成
VMX 或其他特定软件的配置。启动介质的路径和设备选项见
[通用 UEFI 启动介质](BOOT-MEDIA.md)。

首次使用可安装 Ubuntu WSL2：

```powershell
wsl --install -d Ubuntu
```

重新打开 PowerShell 后，脚本会把工作区转换为 WSL 路径并在同一份源码上
运行 `make` 和 `start.py`。已有 MSYS2 make、GCC、binutils 与 Python 时，
可显式选择本机构建：

```powershell
.\start.ps1 build -Native
.\start.ps1 media -Native
```

`-Window` 需要 WSLg 或 Windows QEMU 的图形后端；没有图形后端时去掉该
参数使用串口控制台。`doctor` 分别检查工具是否存在，不会配置某款虚拟机。

UEFI 启动器未签名，需要关闭 Secure Boot。已有内核路径包括 GOP 显示、
AHCI SATA、标准 512B NVMe、USB xHCI Boot 键盘/鼠标与部分 Intel 有线网卡；
VirtIO、VMXNET3、SCSI、笔记本内置 Wi-Fi、USB 存储挂载和复杂图形驱动仍未
实现。首次测试请保留启动屏幕照片和设备型号；宿主编译不能证明实体机支持。
