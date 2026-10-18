# 离线存储导入与恢复保护

基线为完整 `0e0d93a08c7f5600bf115d65994d1179113f14b2`，2026-10-18。
Arch GCC 16.2.1、Python 3.14.7、QEMU 11.1.1 TCG、OVMF；无实体硬件验证。
原生存储工具与测试位于 `scripts/`、`tests/`，内核和兼容后端没有修改。

`before-tests.log` 是把本轮 `tests/offline_storage_test.py` 放入基线脚本树后
运行的实际结果：7 个测试方法，19 个失败。`before-mutations.log` 独立展示
旧实现对重叠分区、截断镜像以及重复路径树仍继续写入，整盘摘要发生变化。
前两者修复后直接拒绝；第三者有较旧有效根时应当恢复后继续导入，不能把
整个镜像发生变化本身当作错误。新测试逐字节核对较旧恢复槽保留、新提交
使用另一槽且目录/内容有效。两槽均损坏则拒绝且整盘摘要不变。

原始记录：

- `after-tests.log`：结构损坏、代次回绕、迁移文件内容、拒绝时源盘不变与目标不存在；合法扩大镜像、单扇区其他类型分区及已有中文目录不被误拒绝。
- `build-host-default.log`、`host-native.log`：两种构建及各 55 组宿主回归，C 夹具使用 UBSan。
- `guest-default.log`、`guest-native.log`：真实导入/迁移 CLI、恢复槽/源盘不变、BIOS/UEFI 读写保存重启和双盘隔离断言结果。
- `default-offline-*.log`、`native-offline-*.log`：每次 QEMU 启动和关机的完整串口记录，各 7 次。
- `summary.json`：源码、内核产物和上述证据的 SHA-256。10 个内核/loader 可执行产物均与基线记录相同；ESP 因重新打包的 FAT 时间等信息不要求逐字节相同。

复现：

```sh
python3 tests/offline_storage_test.py
make -j4 diagnostics esp
make test-host
python3 tests/offline_storage_boot_test.py
make COMPAT=none -j4 diagnostics esp
make COMPAT=none test-host
NV_COMPAT=none python3 tests/offline_storage_boot_test.py
```

写入前检查整个主 GPT，其他类型分区也参与边界和重叠判定。无效主表不使用
备份表继续写入，也不自动修复。已扩大的镜像允许备份 GPT 留在原位置，但
不扩容卷。离线操作须关闭使用该镜像的虚拟机；本轮没有并发写入支持。
不涉及新的 Java/Minecraft 能力，未重测游戏可玩性。
