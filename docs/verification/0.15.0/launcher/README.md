# Apps 拖放固定与窗口交互

基线为完整 `0bb5a2e9fe0ec9e9cb7051f67e9f7e7d4e318c3f`，2026-10-18。
Arch GCC 16.2.1、Python 3.14.7、QEMU 11.1.1 TCG、OVMF；无实体硬件测试。
本轮仅改动原生用户态桌面和测试，不新增外部内核/系统依赖。

旧版本在 Apps 卡片上按下鼠标就启动应用，无法拖离取消。
`before-click.log` 保留真实旧生产桌面的 QMP 复现和失败断言，
`after-click.log` 是相同操作在修复后的结果。完整回归中还实际按住、拖离、
移回再释放，确认不会把已取消的拖动当作点击。

行为和边界：

- 把已注册应用窗口的标题栏拖到顶栏 Apps，出现「Pin to Apps」后松手。
  固定项优先显示；原窗口恢复原有位置、大小、最大化/平铺状态。
- 右键或 Ctrl-P 切换固定。窗口身份由桌面启动的路径和 PID 关联，未知客户
  端不因为标题相同就成为可固定应用。没有任意第三方程序注册或向桌面拖出图标。
- 固定记录写入当前用户 `.apps`，验证 magic 和全部位；临时文件原子替换，
  有数据盘时同步。短写、关闭和替换失败不发布新列表；同步失败报告错误，
  相同固定请求也重试同步。无数据盘只保留 RAM 状态，注销清除当前列表。
- Esc 取消窗口移动/缩放；锁屏和模态切换丢弃待处理点击。初始窗口适配
  工作区，小屏编辑器不再进入 Dock 区域。应用卡片圆角外不触发启动。

证据：

- `build.log`、`host-default.log`、`host-native.log`：两种构建及各 55 组宿主回归。
- `sanitizers.log`：桌面模型/真实 I/O 失败路径和分块渲染的 ASan/UBSan。
- `production-launcher.log`：1280×800 默认构建、640×480 和 1920×1080 原生独立
  构建的生产 UEFI/q35/NVMe/USB 测试，各含一次重启。暂停客体读取已提交根，
  校验 `.apps` 的实际 8 字节内容；重启后启动第一项，确认真正 Terminal 打开。
- `default-1280x800-*.log`、`native-640x480-*.log`、`native-1920x1080-*.log`：
  生产登录、实际应用与重启串口记录，共 6 次启动。
- `existing-desktop.log`、`existing-desktop-serial.log`：原有客户端、MRU、总览、
  最小化/最大化、拖动/平铺、保存和权限回归。
- `summary.json`：最终源码、构建产物、日志和截图摘要。

README 的 `apps-pinned.png` 和 `apps-drop.png` 来自实际生产 GOP 帧缓冲，
只作无损 PNG 编码；校验及原始路径见 `docs/screenshots/apps-manifest.json`。
本次容器没有 FFmpeg，六秒 MPEG 由宿主使用相同参数生成，通过 `--clip` 交给
原有桌面测试；不跳过播放断言，也不修改客体。准备文件摘要保留在 `summary.json`。

```sh
make -j4 diagnostics esp
make test-host
python3 tests/launcher_boot_test.py --resolutions 1280x800
make COMPAT=none -j4 diagnostics esp
make COMPAT=none test-host
NV_COMPAT=none python3 tests/launcher_boot_test.py --resolutions 640x480 1920x1080
python3 tests/desktop_boot_test.py
```
