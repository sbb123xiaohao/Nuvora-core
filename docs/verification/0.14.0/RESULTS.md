# 0.14.0 验证记录

日期：2026-10-01。构建工具：GCC 14.2.0 / GNU binutils 2.44；启动回归：
QEMU 10.0.11 TCG、SeaBIOS、OVMF 4M。所有磁盘均为独立测试镜像。
输入基线为用户提供的完整 0.13.1/e6c66b1；工作开始时 GitHub main 为
`625464112618a7a0b2412452edd86205e699c82f`（0.10.0），没有以它覆盖较新基线。

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| x86-64 BIOS/UEFI 构建、ELF/未定义符号检查 | PASS | build-fingerprint.json |
| 宿主回归 | 37 组 PASS，C fixture 使用 UBSan | host-regressions.log |
| 32、64、256 MiB Ring 3 | 每轮 144 PASS，退出 33 | probe-32/64/256MiB.log |
| 5120 MiB Ring 3 | 144 PASS，跨 4 GiB 物理 RAM，退出 33 | probe-5120MiB.log |
| 碎片化 32 MiB | 144 PASS，分散内核堆与失败回滚 | fragmented-heap.log |
| ABI 1 实际执行 | INFO=1、旧堆、负页数和 EXIT 通过 | 各 probe 日志 |
| OVMF q35 桌面 | USB 键鼠、窗口、MPEG 解码与切换 PASS | uefi-desktop-summary/serial.log |
| USB 可移动 GPT/FAT32 固件启动 | BOOTX64.EFI、xHCI、144 Ring 3 PASS | uefi-removable-summary/serial.log |
| 正常 BIOS 账户/电源 | 持久登录、OS renew 重启、rest ACPI 关机 PASS | accounts-power-summary/serial.log |
| 外部 SDK 示例 | large code model ELF64，入口 0x100000000 | examples/pixel_demo.c 与 SDK.md 的构建命令 |
| 交付镜像 | FAT32 64 MiB ESP、GPT 双表 CRC 与 ESP 字节一致 | build-fingerprint.json |

每轮 SSE #XM 递交有 1 项显式 SKIP，未认定对应异常已验证。HDA 使用
无声后端；本轮没有实体电脑/主板/控制器/网卡/声卡接入，未验证实体音频、
真实断电语义和所有硬件组合。ARM64 保留原 bringup，此轮未构建或启动它。

宿主测试的 MMIO/固件/DMA 输入为合成夹具。QEMU 回归验证真实 guest
代码与固件启动路径，其结果不等于实体机认证。实体机要求见
[NATIVE64-HARDWARE](../../NATIVE64-HARDWARE.md)。

源码交付由 scripts/package.py 从干净 HEAD 读取全部跟踪文件，并逐一核对
ZIP 路径、数量、CRC 和内容 SHA256；构建物及启动镜像另包交付。
