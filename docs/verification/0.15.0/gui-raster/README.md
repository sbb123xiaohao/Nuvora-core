# 圆角绘制优化

基线为完整 `aba81d4e4e1020e2e60d8ea1963baadb9ef30880`，2026-10-30。
Arch GCC 16.2.1 20260810、QEMU 11.1.1 TCG、OVMF；生产 UEFI/q35/
256 MiB/NVMe/USB，没有实体硬件测试。

旧圆角填充对矩形内每个像素调用覆盖率计算；不透明内部也执行坐标分支。
现在每行只在左右角落计算覆盖率，连续内部直接填充或按既有公式混色。
圆角半径、抗锯齿与通道舍入规则保留，空交集提前返回，无新增分配或缓存。
修改位于原生共享控件，未引入其他系统运行库。

## 性能范围

`tests/gui_bench.c` 通过公开绘制函数测量 1024×768 画布中 1000×750
卡片及其 8×10 局部重绘。两份二进制仅使用不同提交的控件头文件，编译选项
与源码 SHA-256 见 `benchmark.json`。选项包含 O2、freestanding、large model、
general-regs-only、禁用 SSE/MMX；计时与输出使用整数。每次绘制结果读取全部
画布计算校验和，防止消除绘制工作。

旧新二进制交替运行三对，每个数值先取七次样本中位数，再取三次运行中位数：

| 场景 | 旧版 ns/次 | 新版 ns/次 | 倍率 |
| --- | ---: | ---: | ---: |
| 不透明圆角卡片 | 1,074,560 | 208,419 | 5.16× |
| 半透明圆角卡片 | 2,900,406 | 1,851,508 | 1.57× |
| 卡片内部 8×10 重绘 | 132 | 42 | 3.14× |
| 无圆角卡片 | 445,927 | 204,244 | 2.18× |

这是共享宿主上的局部标量绘制微基准，有调度与计时噪声；微小区域尤为敏感。
不包括显示系统调用、窗口拷贝、整屏合成或客体调度，不能推算桌面 FPS。
四个场景的旧新完整画布校验和均一致。

可在各版本使用同一 `tests/gui_bench.c` 运行；旧版本需复制该新增基准源码，
使用其余原始头文件：

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
  -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables \
  -fno-unwind-tables -m64 -mcmodel=large -mno-red-zone -mgeneral-regs-only \
  -mno-sse -mno-sse2 -mno-mmx -msoft-float -no-pie -Iinclude \
  tests/gui_bench.c common/string.c -o /tmp/nuvora-gui-bench
/tmp/nuvora-gui-bench
```

## 正确性与启动验证

- `oracle-before.log`：新增独立像素参考测试在旧版上先通过；参考直接从圆形
  方程推导覆盖率，逐颜色通道混色，不使用被优化的三个函数计算预期像素。
- `sanitizers.log`：优化后共享控件及完整合成器的 ASan/UBSan；覆盖 8,192 组
  透明度、零/过大半径、零尺寸、负坐标、无交集/反向裁剪、非零画布起点、
  RGBX/BGRX 和缓冲区守卫，另保留现有缩放 1–4 与任意图块回归。
- `frame-comparison.json`：同一完整合成器测试，在基线头文件和新头文件上生成
  9 个场景；文件管理、设置、Apps、终端、编辑/删除确认、多窗口及音量画面的
  原始 PPM 长度、SHA-256 逐项相同。该记录是宿主合成器画面，不冒充客体截图。
- `build-default.log`、`build-native.log`、`host-default.log`、`host-native.log`：
  两种配置构建与各 55 组完整回归，C 夹具使用 UBSan。
- `desktop-guest.log`、`desktop-serial.log`：默认配置的真实生产登录、原生窗口、
  Terminal/Media/Folio、键盘、切换/总览、平铺/拖动/缩放、未保存确认和视频播放。
- `resolution-guest.log`、`resolution-manifest.json` 及两份生产串口：原生配置
  640×480、1920×1080，实际 GOP 模式/内容边界、最大化/半屏/还原与文本重排，
  USB 输入创建文件后暂停客体，读取提交根核对真实保存内容。
- `summary.json`：源码、生产产物与本目录证据的 SHA-256。

完整桌面测试使用宿主按原测试参数准备的六秒 MPEG，并通过 `--clip` 传入；
没有跳过视频断言。兼容配置仍可独立关闭，本轮没有 Minecraft 可玩性变更。

实际生产帧缓冲无损转换的截图：[多窗口桌面](desktop.png)、
[640×480 原生终端](native-640x480.png)、
[1920×1080 原生终端](native-1920x1080.png)。后两张的来源摘要见分辨率清单。
