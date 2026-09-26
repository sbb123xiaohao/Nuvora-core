# Windows 主机与 VMware 使用

Windows 10/11 可以直接从 PowerShell 管理构建和启动。项目的编译链仍是
POSIX 风格，默认后端是 **WSL2**；这不是把虚拟网络当成实体网卡，而是让
Windows 主机稳定提供 GNU make、binutils、QEMU 和 UEFI 工具。

## PowerShell 入口

在项目根目录运行：

```powershell
.\start.ps1 doctor
.\start.ps1 build
.\start.ps1 iso -Uefi
.\start.ps1 run -Uefi -Window -Audio -Network
```

`doctor` 会分别报告 Python、make/GCC/binutils、QEMU、mtools、xorriso、
qemu-img 和 WSL2；构建命令会在缺少后端时给出明确错误。

首次使用可安装 Ubuntu WSL2：

```powershell
wsl --install -d Ubuntu
```

然后重新打开 PowerShell；脚本会把 Windows 工作区转换为 WSL 路径并在同一
份源码上运行 `make`、`start.py`，不会复制一份旧工程。只有已经通过 MSYS2
提供 make、GCC、binutils 和 Python 时才使用：

```powershell
.\start.ps1 build -Native
.\start.ps1 run -Native -Uefi
```

`-Window` 需要 WSLg 或 Windows QEMU 的图形后端；没有图形后端时去掉它，
使用串口控制台即可。

## VMware Workstation 17

一条命令会构建 UEFI ISO、创建数据镜像，再生成可直接打开的配置：

```powershell
.\start.ps1 vmware -DiskController ide
```

输出在 `vmware\NuvoraCore\`：脚本用本机或 WSL2 的 `qemu-img` 将稀疏 raw 数据盘转成
VMDK，并写好 UEFI、USB 3.x xHCI、HDAudio、e1000e 和 GOP 图形配置。默认
IDE 数据盘适合当前启动路径；也可传 `-DiskController ahci` 使用 SATA，
或传 `-DiskController nvme` 使用标准 NVMe
控制器。打开生成的 `.vmx` 后，网络可保持 NAT 或改成 Bridged。
已有 VMDK 保留；`-Force` 只重新生成 VMX 配置，不覆盖虚拟机里的文件。

Nuvora 的 UEFI loader 没有签名，因此模板明确关闭 Secure Boot；不要在
VMware 中重新打开它。VMware Workstation Pro 17 支持为虚拟机选择 UEFI，
也支持 USB 2.0/3.0 控制器；Nuvora 选择 UEFI、USB 3.x 和 e1000e 是为了让
键盘、鼠标、网卡和 GOP 在现代主机上落到已有标准路径。

## 已覆盖与仍需实机验证

- x64 UEFI/GOP、IDE、AHCI/SATA、标准 NVMe、Intel I225/I226、Intel
  e1000/e1000e、USB CDC-ECM、USB Boot 键盘/鼠标和 Intel HDA 有内核路径。
- 网络、磁盘和音频采用轮询；VMware 的 VMXNET3、SCSI、USB 存储、内置
  Wi-Fi（AX/BE 系列）、Secure Boot、GPU 3D 加速和多核调度仍未承诺支持。
- 本机没有 VMware/实体机器时，构建和宿主回归不能替代实际 Workstation
  启动。首次上机请保留串口/日志窗口，并先用复制出的数据盘测试。
