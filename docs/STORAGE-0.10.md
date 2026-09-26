# NVSTORE3 大文件存储与媒体（0.10.0）

新建数据镜像默认使用 GPT + 一个 NVSTORE3 分区，逻辑大小 8 GiB。
这是稀疏镜像的默认容量，不是文件系统的几 GiB 上限；`--size` /
`--disk-size` 可选择更大的容量，现有镜像不会被覆盖。

## 使用

```sh
make -j4
python3 scripts/mkgptdisk.py build/x86_64/nuvora-store.img --size 65536 --if-missing
python3 scripts/import_media.py --disk build/x86_64/nuvora-store.img song.flac film.mpg backup.bin
```

在已关机的宿主机上操作专用数据镜像。`import_media.py` 接受任意扩展名、
空文件和大文件，按块复制；宿主文件系统支持 SEEK_DATA/SEEK_HOLE 时保留
稀疏空洞。最多 31 字节 ASCII 文件名仍是当前路径接口的约束。
同名文件默认拒绝，明确传 `--replace` 才替换。

旧 NVSTORE1/2 镜像继续按原格式读写，不原地改写分区或数据。
旧盘的 64 MiB 文件限制及 1/16/128 MiB 快照容量只有迁移后才解除：

```sh
python3 scripts/migrate_store.py old-store.img new-store.img --size 65536
```

迁移读取旧盘的最近有效提交，保留路径、目录和文件，另建 NVSTORE3 GPT
镜像，原文件不变；目标必须不存在。支持旧单卷整盘镜像或单 Nuvora
分区 GPT 镜像；多 Nuvora 卷会明确拒绝，避免漏掉其他盘。迁移后选择
新镜像作为数据盘；默认启动器使用 `build/x86_64/nuvora-store.img`。

## 磁盘格式

- 512 字节卷头沿用 NVSTORE2 的几何字段布局，magic=`NVSTORE3`、version=3。
  8..24 字节为 version、sector_size、slot0、slot1、slot_sectors（各 u32）；
  32 字节处为 u64 总扇区数，40 字节处为前 40 字节的 CRC32。
- 新建卷的两个元数据槽各为 4097 扇区：512 字节提交头 + 2 MiB 元数据。
  文件数据从第二槽之后对齐至 4 KiB 的位置开始，一直到卷尾完整块。
  元数据容量不再是所有文件内容的总容量。
- 提交头 magic=`NVSS0002`，其余字段沿用 NVSS0001：generation、length、
  metadata_crc、header_crc（各 u32），保留字节清零，头总长 512 字节。
- 元数据：u32 条目数；父目录在前。每条为 kind:u32、pathlen:u32、size:u64、
  extent_count:u32、reserved:u32，再接路径字节以及 extent_count 个
  `{logical_block:u64, physical_block:u64, block_count:u64}`。全部小端。
  块大小 4096。逻辑区间有序且不重叠；未映射区域读为零。目录长度和块数为零。
- 单文件逻辑长度最高为 `0x7ffffffffffff000`；实际内容受磁盘容量、
  空闲块、元数据空间及可用 RAM 约束。ATA 仍受 LBA48、NVMe 仍受所选
  namespace 的容量/扇区格式约束，不宣称无物理上限。

## 写入和恢复

`kernel/fs_extent.inc` 维护可合并的连续块区间。一次写入先准备新的块映射，
再写入新块；任何分配或 I/O 失败都保留原映射、文件长度和句柄偏移。
顺序写入相邻物理/逻辑块会合并，不为每个文件字节保留 RAM 副本。

`anchor` / `NV_CTL_SYNC`：准备新映射的保留引用 → 使备用槽头无效并 Flush →
写元数据并 Flush（此前文件数据也必须持久化）→ 写 CRC 提交头并 Flush →
切换活动根。两个已提交根以及当前工作树引用的块都不会被重用。
文件删除/覆盖后的旧块在两个根均不再引用它们后才可回收。最终提交写入或
Flush 失败的状态不确定，因此禁止继续写文件，直到重新挂载确定实际根。

启动验证两个元数据根的 CRC、规范路径、父子顺序、64 位边界以及物理块
重叠，再选择最新有效根。两根均损坏或恢复资源不足时禁止保存。
文件数据本身没有逐块校验和；元数据 CRC 不等于文件内容完整性校验。
各卷独立提交，跨卷/持久卷与 `/tmp` 之间的 rename/replace 被拒绝。
这也修复了把旧快照引用移出卷后被后续提交覆盖的问题。

## 接口与媒体

`NV_SEEK64`、`NV_STAT64`、`NV_LIST64` 追加调用号，保留 ABI 1 原编号。
描述符偏移和新文件长度使用 u64。旧 SEEK 遇到不能由正 int 表示的结果
返回 E2BIG 并保留原偏移；旧 LIST 对过大长度饱和到 UINT32_MAX。
新 LIST64、桌面、Loom 使用真实 64 位长度；桌面显示 KiB/GiB 等单位。
NV_VOLUME.snapshot_limit=0 表示磁盘块格式。

Media 以有界输入缓冲读取 MPEG-1 Program Stream，不再把整段视频装入
用户堆；去掉 640×480 的额外限制，画面按比例缩放到屏幕。MPEG-1 本身的
分辨率字段、软件解码速度和真实可用 RAM 仍有限制，已用 1280×720 样本验证。
MP3、独立 MP2、FLAC 按帧解码。WAV 支持 RIFF/RF64、整数 PCM 8/16/24/32-bit、
32-bit float、1–8 声道及 8–384 kHz；多声道混为立体声，统一重采样到 HDA
48 kHz。RF64 的 64 位 data 长度已覆盖 8 GiB 边界解析。

MP4/H.264/AAC、MKV/WebM/Vorbis **不是原生解码支持**。宿主安装 FFmpeg 后：

```sh
python3 scripts/prepare_media.py input.mp4 movie.mpg --disk build/x86_64/nuvora-store.img
python3 scripts/prepare_media.py input.m4a music.flac --audio-only --disk build/x86_64/nuvora-store.img
```

工具先用 ffprobe 识别流，再转换、完整解码检查输出，最后可选导入。
视频默认最高 1080 行（`--height` 可改），会重新编码为 MPEG-1/MP2；
音频输出立体声 48 kHz FLAC。源文件不改写，已有输出不覆盖。

## 已验证与仍存在的边界

19 组宿主回归（C 夹具启用 UBSan）通过，包含真实内核代码的 10 GiB
稀疏文件及超过 4 GiB 的物理位置、160 MiB 连续写入、随机覆盖、低内存/
磁盘满/写失败回滚、损坏根回退和不确定 Flush 后停止写入。
宿主导入器覆盖 6 GiB 稀疏文件、140 MiB 实数据、与内核双向读写、
旧盘迁移且源 SHA-256 不变、MP4 转换导入、24-bit FLAC、720p MPEG、
WAV/RF64 边界。没有本轮 QEMU 启动或实体断电/音频连续性验证。

当前仍是实验内核：512 个总节点、每进程 13 个普通文件句柄、每卷 2 MiB
元数据区、8 MiB 内核元数据堆、512 MiB 用户堆、64 GiB 物理管理窗口。
高度碎片化的块分配/恢复需要扫描区间，尚无 B 树、块缓存、按需分页、swap、
后台回写或扩容已有卷的工具。IDE PIO/NVMe 512B、HDA 等设备范围保持不变；
不支持所有现代硬件、NTFS/ext4 或任意音视频编码。

设计参考：
[Linux ext4 extent 描述](https://cdn.kernel.org/doc/html/latest/filesystems/ext4/ifork.html)、
[有序数据与提交](https://cdn.kernel.org/doc/html/latest/filesystems/ext4/journal.html)、
[pl_mpeg 流式接口](https://github.com/phoboslab/pl_mpeg)、
[dr_flac](https://github.com/mackron/dr_libs)、
[FFmpeg 文档](https://ffmpeg.org/ffmpeg.html)。实现使用 Nuvora 自有格式，不是 ext4。
