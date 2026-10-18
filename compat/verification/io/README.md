# I/O 边界和启动盘配置隔离

基线为完整 `9a6dd1f56c4d74556e16c5175a7850c12b715d86`，2026-10-18。
Arch GCC 16.2.1、QEMU 11.1.1 TCG、OVMF；没有实体硬件认证。

`before-io.log` 保留新断言在旧实现上的失败及旧客体的真实写入结果。
旧客体的 boot-test.elf SHA256 与上一轮记录一致。同一固定 SHA256 的 musl ELF
在旧内核把非法 writev 执行为 16,385 字节写入，在修复后的内核通过全部 I/O 检查。
`linux-reference.log` 是宿主 Linux 的独立原始系统调用对照。修复保留合法用户页
在传输时失效的部分写入，没有把所有分页错误变成预先拒绝。

`before-profile.log` 保存打包缺陷的新回归断言和实际生产 ESP 载荷不一致。
此前诊断测试用测试内核覆盖 ESP 文件，掩盖了原始内核误取默认目录的问题。
新检查在覆盖前先提取实际 FAT 文件，核对生产内核、loader 和所选命令行。
主机配置测试只替代 mtools，运行真实打包 CLI；它验证配置选择与失败保留，
实际 FAT/UEFI 启动由另外的客体测试验证。

记录包含：

- `host-default.log`、`host-native.log`：两种配置各 54 组回归；新增打包用例在后者执行。
- `sanitizers.log`：Linux ABI ASan/UBSan，GCC 14.2.0。
- `guest-dynamic.log`、`guest-openjdk.log`：实际客体串口输出，回收精确相等。
- `guest-boot.log`、`guest-native.log`：BIOS/UEFI 与原生隔离/载荷检查。
- `native-desktop.log`、`native-desktop-manifest.json`：原生生产桌面测试和 GOP 图像摘要。
- `media-proof.json`：原生 USB 内嵌 ESP 与所选 ESP 的完整内容摘要相同。
- `summary.json`：源码、固定夹具、构建与日志摘要。

截图原始 PPM/PNG 留在 `build/x86_64-native/resolution-desktop/`。数据盘、运行时
安装、启动镜像和编译缓存不是源码包。动态夹具的来源、许可证和摘要位于
[`compat/fixtures/dynamic`](../../fixtures/dynamic/manifest.json)，可用固定源包重建：

```sh
python3 compat/tools/build_dynamic_runtimes.py --output compat/fixtures/dynamic
python3 compat/tests/dynamic_tools_test.py
make -j4 diagnostics esp
make test-host
python3 compat/tests/dynamic_boot_test.py
make COMPAT=none -j4 diagnostics esp media
make COMPAT=none test-host
python3 compat/tests/native_independence_test.py
NV_COMPAT=none python3 tests/resolution_desktop_test.py --resolutions 1280x800
```

兼容接口仍有明确上限：最多 16 项 iovec，没有完整 Linux ABI。未新增 socketpair、
OpenGL 或 GLFW 平台；本轮未重跑 Minecraft，没有达到游戏可玩验收。
