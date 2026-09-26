# GPT 分区与盘符（0.9.0 源码扩展）

这份源码没有系统安装器。`start.py` 首次创建数据盘镜像时充当初始化入口，
默认 GPT + 一个 Nuvora C: 数据分区。`--partitions 2` 至 `4` 可在新镜像中
预先分出 D:、E:、F:。新 NVSTORE3 分区各保留两个 2 MiB 元数据槽，
其余完整 4 KiB 块存放文件；默认镜像逻辑容量 8 GiB，可设置更大容量。
旧 NVSTORE1/2 镜像不会被覆盖或原地改写；`--legacy` 仅用于旧格式测试。
现有建盘工具仍要求至少 64 MiB 镜像，多盘每分区约 34 MiB 的建盘下限。
详见 [大文件存储与迁移](STORAGE-0.10.md)。

```
python3 scripts/mkgptdisk.py /tmp/new-disk.img --size 128 --partitions 2
python3 start.py --disk-size 128 --partitions 2
```

启动优先扫描 IDE primary master 上的有效 Nuvora 数据卷；找不到时扫描
首个 PCIe NVMe 控制器的首个受支持 namespace。读取 protective MBR、
GPT 主头/备份头和全部
128 条分区项，核验 CRC、LBA 边界与重叠。`partitions` 最多显示前 32 条有效
记录（含不支持的类型）；匹配 Nuvora 类型 GUID 且分区首扇区具有有效
NVSTORE1/2/3 签名和几何信息的分区，按 GPT 顺序最多挂载 4 个盘符。其他 GPT
分区只报告，不读取文件或写入。没有 GPT 的NVSTORE1/2/3 整盘镜像按
C: 使用，不进行原地转换。类型 GUID 为
`9b431778-b821-4748-a3f8-2d17c37a5601`。

用户路径 `C:/x` 映射为 `/home/x`，`D:/x` 映射为 `/drives/D/x`；系统路径
`/apps`、`/tmp`、`/dev`、`/sys` 仍存在。切换到分区目录后 `where`
显示盘符。每个分区保留各自的两个快照槽及提交代次；`anchor` 按盘符
顺序提交所有已挂载分区。单次磁盘故障可令不同盘的代次不同，不能把
一次 `anchor` 当作跨盘原子事务。跨盘重命名会被拒绝；需复制文件并分别
确认保存。断电安全仍受现有双槽
快照实现限制。

GPT 是物理分区表，盘符和快照格式属于 Nuvora。它不包含 Windows 的
NTFS、卷管理器或在线分区管理；没有新盘的图形磁盘管理器，也不扫描
USB 存储或第二块 IDE 磁盘。NVMe 当前仅识别 512 字节扇区且无额外
metadata/protection 的 NVM namespace；4K 原生盘不挂载。NVSTORE3 文件
采用 64 位大小/偏移，容量取决于数据区和 COW 空闲块，不再受旧单文件
64 MiB/整卷 128 MiB 快照限制。所有卷仍共用 512 个文件节点；旧盘保持
原有上限，需要另存迁移。扩大镜像本身不会更改卷头几何。
当前仅在源码夹具中验证过 ATA/NVMe GPT 扫描、分区边界和卷快照隔离；
虚拟机/实体硬件验证需另行执行。
