# 窗口标题与系统入口身份

基线为完整 `aa3008665c895a7dff086acd06148c3b8fdd9c4b`，2026-10-25。
Arch GCC 16.2.1、QEMU 11.1.1 TCG、OVMF；生产 UEFI/q35/256 MiB/NVMe/USB。
未使用诊断内核跳过账户登录，未进行实体硬件测试。

旧 `client_poll` 根据窗口标题，把 Terminal/Media/Music 客户端放到专用槽。
启动器随后看到该槽已有窗口就直接聚焦，没有使用已有的登记应用身份校验。
现在槽位选择和固定使用同一启动身份：由桌面登记的子进程 PID 与应用标识。
未登记客户端进入普通槽；标题不再决定系统入口目标。

实际复现使用 `tests/fixtures/window_title_client.c`，不是系统终端：

1. 在真实 Terminal 中用 scatter 启动独立测试程序，关闭原终端。
2. 测试程序稍后建立标题为 Terminal 的 480×270 窗口，并提交 READY 记录。
3. 从 Apps 打开 Terminal，然后输入 weave/anchor 命令。
4. 旧桌面把命令交给该同名程序。其提交记录包含完整命令，真正终端没有启动。
5. 修复后的两种构建启动真正 Terminal，提交记录保持 READY；真实 shell
   执行命令并创建 identity-ok.txt，客体暂停后读取实际提交根确认内容。

像素检查使用独立程序的小窗口与真实 960×650 终端的面积差；二者可能使用相同
背景色，因此同时核对命令执行结果和实际输入记录，避免只凭标题或颜色判断。

证据：

- `baseline-artifacts.json`：旧 ESP、desktop ELF 与生产内核的摘要，与上一提交一致。
- `before.log`、`before-results.json`、`before-serial.log`：旧生产桌面的失败和真实输入去向。
- `default-results.json`、`native-results.json`、`guest.log`：修复后默认/原生构建的真实结果，
  两个独立同名客户端均未收到命令，真实 shell 完成文件保存。
- `default-serial.log`、`native-serial.log`、`launcher-serial.log`、
  `launcher-reboot-serial.log`：上述身份与固定/锁屏/重启测试的生产串口记录。
- `build.log`、`host-default.log`、`host-native.log`、`sanitizers.log`：
  两种构建各 55 组宿主回归，C 夹具使用 UBSan，桌面模型另通过 ASan/UBSan。
- `existing-desktop.log`、`existing-desktop-serial.log`：原有 Terminal/Media/Folio、
  多窗口、输入、未保存确认、MRU、总览、平铺和播放回归。
- `terminal-launched.png`：实际修复后帧缓冲；`summary.json` 保存源码、产物与证据摘要。

本次容器没有 FFmpeg，原有桌面回归使用宿主按原参数准备的六秒 MPEG，
通过 `--clip` 传入，不跳过播放断言。来源摘要保留在 summary.json。

```sh
make -j4 diagnostics esp
make test-host
python3 tests/window_identity_boot_test.py
make COMPAT=none -j4 diagnostics esp
make COMPAT=none test-host
NV_COMPAT=none python3 tests/window_identity_boot_test.py
python3 tests/launcher_boot_test.py --resolutions 1280x800
python3 tests/desktop_boot_test.py
```

修复限定入口身份选择。普通应用仍可设置同名标题；本轮没有新增第三方应用
注册、进程来源查询 ABI 或应用沙箱。原生与其他系统兼容模块的划分保持原样。
