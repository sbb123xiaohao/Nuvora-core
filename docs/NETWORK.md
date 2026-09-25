# 实体网络开发版（x86-64）

本轮加入 PCIe 有线和 USB 网卡的真实收发路径。支持范围取决于设备的准确类型，PCI 枚举到无线网卡并不代表能连接 Wi-Fi。ARM64 目标目前没有这一网络模块。代码已通过编译与合成网络帧的宿主测试；尚未在对应实体网卡或 USB 设备上验证，不能据此认定任意现代 PC 已可联网。

| 硬件 | 路径 | 当前状态 |
| --- | --- | --- |
| Intel I225-V / I225-LM、I226-V / I226-LM | 真实 PCIe BAR、物理 DMA RX/TX 环、轮询 | 驱动已接入；需实机链路、DHCP 和收发验证 |
| 标准 USB CDC-ECM 网卡 | xHCI bulk IN/OUT、MAC 描述符、热插拔 | 驱动已接入；需实机验证；RNDIS/NCM 不支持 |
| Espressif ESP32-S2/S3 USB Dongle 示例固件（USB ECM + CDC） | ECM 以太网帧；CDC 上的 `scan` / `sta` 命令配置设备的 Wi-Fi 射频 | 有专用 `net wifi` 控制入口；用户需自行准备支持 USB OTG 的开发板并烧录固件；需实机验证 |
| Intel AX200/AX210/AX211/BE200 等 PCI Wi-Fi | 仅 PCI 识别 | 无原生固件加载、802.11 驱动和 WPA 认证，显示 `no driver` |
| 其他有线 PCIe 网卡、USB RNDIS/NCM、手机 USB 共享 | 仅识别或未匹配 | 暂不可收发 |

支持的协议：以太网、ARP、IPv4、DHCP（申请地址）、ICMP echo 应答、UDP 发送和单包接收。程序经 `NV_DEVCTL` 的 `NV_SUB_NET` 访问这些功能；不是 POSIX socket API。当前没有 TCP、DNS 名称查询、TLS、IPv6、VLAN、DHCP 租约续期、多个并发 UDP 接收队列，也没有通用网络管理服务。

## 操作

```text
net                                  # PCI 与 USB 网络接口及状态
net dhcp                             # 对当前接口申请 IPv4 地址
net dhcp 1                           # 选择接口 1 后申请地址
net static 0 192.168.1.42 255.255.255.0 192.168.1.1 8.8.8.8
net use 0                            # 切回指定接口
net send 192.168.1.10 9000 hello     # UDP 源端口固定为 40000
net recv 40000                       # 等待一个 UDP 包，最多 5 秒
```

USB Dongle 固件需同时启用 USB ECM 和 CDC 命令口，并使用 Espressif USB 厂商 ID `303a`；网络接口必须在 `ports` 出现 `Ethernet=active`，在 `net` 出现 USB Wi-Fi bridge。官方示例：[ESP USB Dongle README](https://github.com/espressif/esp-iot-solution/blob/master/examples/usb/device/usb_dongle/README.md)，包含开发板、固件编译/烧录和 `sta -s ... -p ...` 命令。串口密码输入仅针对这个设备/固件组合，普通 USB Wi-Fi 棒并不兼容。

```text
net wifi scan                        # 用设备 CDC 控制口扫描热点
net wifi join MySSID                 # 隐藏输入 WPA2 密码；成功后启动 DHCP
net wifi join OpenSSID --open        # 开放网络
net wifi status                      # 查询设备的 station 状态
```

SSID 和密码当前限定为不含空格的 ASCII；密码长度 8–63。口令不写入文件或命令历史，输入时不回显；外接 Wi-Fi 模块及无线侧安全由其固件处理。`net wifi join` 等待最长 15 秒，连接结果需以设备 CDC 回复和 DHCP 获址为准；如果返回“still pending”，可运行 `net wifi status` 和 `net` 复查。没有外接设备时 Wi-Fi 命令返回设备不可用。

## 设计边界与实机验证

- x64 内核仅有 PIC/PIT，没有 ACPI AML 中断路由/APIC 和 IOMMU 映射，因而采用轮询和低于 4 GiB 的 DMA 页。实机网卡、电源管理和固件配置差异仍需测试；异常时驱动会停止或隔离 DMA 页。
- 只有一块受支持的 PCIe I225/I226 有线卡和一块 ECM 网卡可同时活动；`net use` 选择其一。其他 PCIe 网卡仍会列出但不会接管设备。
- Wi-Fi 通过独立 ESP 射频芯片和官方固件，不代表内置无线卡可用。要覆盖现代笔记本内置 Wi-Fi，还需具体芯片驱动、固件分发/加载、802.11 与 WPA2/WPA3 链路管理；适配器支持列表必须逐型号实测。
- UEFI 启动、xHCI 和 PCIe 资源、AHCI/NVMe 安装介质等同样决定整台现代电脑能否使用；本补丁未完成通用实机安装和日用系统支持。
