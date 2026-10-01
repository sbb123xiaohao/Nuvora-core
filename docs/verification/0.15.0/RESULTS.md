# 0.15.0 验证记录

日期：2026-10-01。完整工作基线为 0.14.0 提交
`5e5c509182822f1b750e3b3f5c39b7aa4d203ddd`，保留该提交全部原生 64 位改动。
工作前与提交前远端 main 均为 `625464112618a7a0b2412452edd86205e699c82f`，
未用较旧远端覆盖用户完整版本。工具：GCC 14.2.0、binutils 2.44、
QEMU 10.0.11 TCG、SeaBIOS 与 OVMF 4M，磁盘均为独立测试镜像。

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| 生产/诊断 BIOS/UEFI 构建、链接与 ELF 检查 | PASS，UEFI 装载结束低于 32 MiB | build-fingerprint.json |
| 宿主回归 | 37 组 PASS，C 夹具使用 UBSan | host-regressions.log |
| 32、256、5120 MiB 原生/旧 ABI | 每轮 144 PASS，退出 33 | probe-32/256/5120MiB.log |
| 碎片化 32 MiB 内核堆 | 144 PASS，分散页与 OOM 回滚 | fragmented-heap.log |
| 无 SMEP 的 core2duo / 64 MiB | 144 PASS，SMEP unavailable | cpu-core2duo.log |
| OVMF q35 桌面交互 | USB 键鼠、数字键盘、原生应用、未保存取消/放弃、窗口拖动/缩放/平铺、搜索/MRU/总览、真实 MPEG 解码 PASS | uefi-desktop-summary/serial.log |
| OOBE/账户/标准权限 | 错误密码、锁屏输入隔离、添加/禁用、改密、持久登录、10 项标准客户端权限断言 PASS | uefi-accounts-summary/serial.log |
| 生产 USB 与负引导参数 | 临时生产 ESP 加入 nv.test=1 仍要求认证，7 项管理员会话客户端权限断言 PASS | session-security-summary/serial.log |
| 实际自动锁屏 | 选择 1 分钟、无输入到内核锁定、F10 输入隔离、错误密码、正确解锁 PASS | session-security-summary.log、lock.png |
| 图形电源与偏好 | 默认 Cancel、真实重启、账户/壁纸/超时重载、ACPI 关机退出 0，无 firmware shutdown unavailable | session-security-summary/serial.log |
| 生产 UEFI ISO | 原始生产 El Torito 介质：OOBE、桌面、窗口终端和 7 项客户端权限断言 PASS | production-iso-summary/serial.log |
| USB 可移动固件自测 | 私有诊断 ESP/GPT：BOOTX64.EFI、xHCI、144 原生/旧 ABI 断言 PASS | uefi-removable-summary/serial.log |
| 正常 BIOS 恢复/电源 | 掩码 OOBE、持久登录、错误密码、renew 重启、ACPI rest 关机 PASS | bios-accounts-power-summary/serial.log |

每轮完整 probe 有 1 项 SSE #XM 递交明确 SKIP，没有把它认作已验证的异常。
权限专项的 7/10 项全部执行；标准会话额外验证关机、重启和 DHCP 被拒绝。
宿主账户夹具验证可信程序来源、无身份 PID 1、策略边界、真实输入重置、
计时回绕和管理进程丢失；指针夹具验证重复静止绝对报告不延长空闲时间。

截图由生产系统实际运行后通过 QMP 获取： [桌面](desktop.png)、
[安全设置](settings.png)、[自动锁屏](lock.png)，不是设计效果图。
`build-fingerprint.json` 绑定本轮最终源代码和生产/诊断 ELF、ESP、GPT、ISO；
构建产物不进入源码 ZIP，完整源码打包从干净提交逐路径、数量和内容核对。

本轮没有实体电脑或外设接入，未验证实体 GPU 加速、真实扬声器/耳机路由、
所有控制器组合和实体断电；未重跑全部历史网络/断电矩阵、BIOS ISO 或
ARM64 bringup。正常 UEFI 软件帧缓冲及标准设备路径面向实体 PC，模拟
回归结果不等于实体认证。没有 Secure Boot、磁盘加密、SMAP 或 IOMMU。

复现命令见 [TESTING](../../TESTING.md)，安全边界见
[DESKTOP-SECURITY](../../DESKTOP-SECURITY.md)，实体上机步骤见
[BOOT-MEDIA](../../BOOT-MEDIA.md)。
