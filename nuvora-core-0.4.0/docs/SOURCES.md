# 参考规范与来源

处理器和标准格式相关实现参考下列公开文档。项目自己的代码、命令名称、ABI 编排和快照格式在本次工作中编写，没有将 Linux 内核源码改名包装。

- [Intel 64 and IA-32 Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)：保护模式、长模式、描述符、分页、权限与异常的处理器规范入口。0.3.0/0.4.0 的内核栈保护、x64 IST 和 i686 双重故障 task gate 对应 Volume 3A 的分页、异常与任务管理章节。
- [GNU Multiboot header fields](https://www.gnu.org/software/grub/manual/multiboot/html_node/Header-magic-fields.html)：启动头标识和校验关系。实现使用 Multiboot v1，进入后由本内核自行初始化。
- [System V ABI — ELF Header](https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.eheader.html)：ELF32/ELF64 头部布局和机器类别。
- [System V ABI — Program Header](https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.pheader.html)：程序段字段，用于本项目的受限静态加载器。
- [QEMU system invocation](https://www.qemu.org/docs/master/system/invocation.html)：虚拟机启动、设备和串口配置参考。本次实际使用的是 QEMU 8.2.2，未把网页最新版本的结果当成本包测试结果。
- [T13 ATA drafts](https://www.t13.org/)：ATA 规范组织入口。本项目仅实现传统 PIO 子集，不声称完整符合 ATA 标准。
- [Arch Linux qemu-system-x86 package](https://archlinux.org/packages/extra/x86_64/qemu-system-x86/)：Arch 宿主依赖名称。
- [Rich Text Format 1.5 specification](https://www.biblioscape.com/rtf15_spec.htm)：RTF 控制字、分组、字体和段落格式的公开规范页面；Folio 只写入其中的 ASCII 子集。

GRUB 是唯一随 ISO 提供的第三方引导组件，其来源、许可证及对应源码位置见 [third_party/README.md](../third_party/README.md)。QEMU 和编译器只用于构建、运行与验证，没有放进发布包。
