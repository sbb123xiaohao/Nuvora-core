# 最终指针报告与缩放边界

基线为完整 `24b068529319c03fcd83ca8fa61c61cea484d440`；2026-10-25。
使用 Arch GCC 16.2.1、QEMU 11.1.1 TCG、OVMF，生产 UEFI/q35、256 MiB、
NVMe 私有数据盘、USB 键盘和 Tablet。无实体硬件测试。

`before.log` 和 `before-results.json` 保留旧生产桌面的四个失败：

- 松手报告中的最终位置没有移动窗口。
- 松手报告中的最终位置没有改变窗口尺寸。
- 已移离边缘，仍按旧预览提交半屏平铺。
- 640×480 右半屏 Files 小于首选最小宽度，缩放后右边框逃出帧缓冲。

旧 ESP、生产内核和 desktop ELF 的摘要与上一提交验证记录逐一核对，
见 `baseline-artifacts.json`。输入通过同一 QMP 调用提交坐标和释放状态，
没有额外补发保持按下的移动。绝对坐标由 15 位值映射到像素，缩放断言
保留 1 像素量化容差；它仍明确拒绝旧版本完全未应用的 40 像素位移。

修复将最终位置纳入拖动更新，再确认松手动作。缩放约束优先满足可用工作区，
不能为了应用首选最小尺寸越出屏幕；不移动的缩放点击保留平铺状态。

证据：

- `host-default.log`、`host-native.log`：两种配置各 55 组宿主回归，C 使用 UBSan。
- `sanitizers.log`：实际桌面模型 ASan/UBSan，包括 1,500 组缩放边界组合。
- `guest.log`：两种配置各一次 640×480 生产启动，四项指针/边界检查通过；
  1280×800 生产 Apps 测试继续通过固定/取消、精确几何和最大化恢复、锁屏与重启。
- `default-results.json`、`native-results.json`：每个指针检查的实际结果。
- `default-serial.log`、`native-serial.log`、`launcher-serial.log`、
  `launcher-reboot-serial.log`：上述四次生产启动的串口记录。
- `tile-bounded.png`：修复后实际帧缓冲，只进行无损 PNG 编码。
- `summary.json`：最终源码、测试证据、基线及构建产物摘要。

```sh
make -j4 diagnostics esp
make test-host
python3 tests/pointer_release_boot_test.py
make COMPAT=none -j4 diagnostics esp
make COMPAT=none test-host
NV_COMPAT=none python3 tests/pointer_release_boot_test.py
python3 tests/launcher_boot_test.py --resolutions 1280x800
```

本轮不新增应用固定方式或其他系统兼容能力；此前完整源码、截图与修复均保留。
