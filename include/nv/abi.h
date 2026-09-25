#ifndef NV_ABI_H
#define NV_ABI_H
#include <nv/types.h>
#define NV_VERSION "0.9.0"
#define NV_ABI_VERSION 1
#define NV_NAME_MAX 31
#define NV_PATH_MAX 192
#define NV_ARG_MAX 256
#define NV_FILE_MAX (128u * 1024u)
#define NV_OPEN_MAX 16
#define NV_TASK_MAX 32
#define NV_VOLUME_MAX 4u
#define NV_PARTITION_MAX 32u
#define NV_PAGE 4096u
/* Private ABI: int 0x81; eax=operation; ebx/ecx/edx=arguments. */
enum nv_call {
    NV_EMIT,
    NV_TAKE,
    NV_OPEN,
    NV_CLOSE,
    NV_SEEK,
    NV_LIST,
    NV_MKDIR,
    NV_REMOVE,
    NV_MOVE,
    NV_CHDIR,
    NV_GETCWD,
    NV_SPAWN,
    NV_WAIT,
    NV_EXIT,
    NV_SLEEP,
    NV_YIELD,
    NV_GROW,
    NV_INFO,
    NV_TASK,
    NV_STOP,
    NV_CLOCK,
    NV_CONTROL,
    NV_SURFACE,
    NV_KEY,
    NV_REPLACE,
    NV_EXEC,
    NV_USB,
    NV_HARDWARE,
    NV_DEVCTL, /* generic extensible device control; see enum nv_subsystem */
    NV_VOLUME, /* enumerate mounted Nuvora data partitions */
    NV_PARTITION, /* enumerate all validated GPT entries, including unmounted */
    NV_CALL_COUNT
};
enum { NV_HW_CPU = 1, NV_HW_GPU = 2, NV_HW_PLATFORM = 3 };
/* NV_DEVCTL convention: eax=NV_DEVCTL, ebx=subsystem (enum nv_subsystem),
 * ecx=subsystem-local op code, edx=user pointer to a fixed-size request/
 * response struct whose exact type and size are implied by (subsystem, op) —
 * same convention NV_HARDWARE/NV_USB already use. Adding a new op never
 * changes NV_DEVCTL itself or bumps NV_ABI_VERSION; only the subsystem's own
 * op enum grows. USB keeps its existing NV_USB entry point. */
enum nv_subsystem {
    NV_SUB_CPU = 1,
    NV_SUB_GPU = 2,
    NV_SUB_NET = 3
};
/* Network buffers are fixed-size and copied across the user/kernel boundary.
 * Address fields hold four IPv4 octets in network order (e.g. 0xc0a80101). */
#define NV_NET_MAX 8u
#define NV_NET_DATA_MAX 1024u
enum { NV_NET_INFO = 1, NV_NET_DHCP = 2, NV_NET_STATIC = 3,
       NV_NET_UDP_SEND = 4, NV_NET_UDP_RECV = 5, NV_NET_SELECT = 6,
       NV_NET_WIFI_COMMAND = 7, NV_NET_WIFI_READ = 8 };
enum { NV_NET_WIRED = 1, NV_NET_WIFI = 2, NV_NET_USB_BRIDGE = 3 };
enum { NV_NET_UNSUPPORTED = 1, NV_NET_DOWN = 2, NV_NET_LINK = 3,
       NV_NET_CONFIGURING = 4, NV_NET_ONLINE = 5 };
struct nv_net_info {
    u32 index, type, state, vendor, product, bus, device, function;
    u32 ip, mask, gateway, dns, rx_packets, tx_packets;
    u8 mac[6], reserved[2];
};
struct nv_net_static {
    u32 index, ip, mask, gateway, dns;
};
struct nv_net_udp {
    u32 index, address, port, local_port, length;
    u8 data[NV_NET_DATA_MAX];
};
struct nv_net_wifi_command {
    u32 length;
    char text[252]; /* command or partial response; password never persisted */
};
_Static_assert(sizeof(struct nv_net_info) == 64, "network info ABI");
enum nv_gpu_op {
    NV_GPU_OP_MAP_BAR = 1,
    NV_GPU_OP_SET_MODE, /* not implemented: needs a display mode-setting path first */
    NV_GPU_OP_PRESENT,  /* not implemented: needs NV_GPU_OP_SET_MODE first */
    NV_GPU_OP_SUBMIT    /* not implemented: needs command submission machinery first */
};
struct nv_gpu_map_bar_req {
    u32 index, bar;
};
struct nv_gpu_map_bar_res {
    u32 ok, reserved;
    u64 length; /* Kernel mapping window, not BAR size or VRAM capacity. */
};
/* MAP_BAR uses one in/out buffer: allocate the whole union, not just the
 * 8-byte request. Kernel-only, one page per BAR; no user pointer is returned.
 * A writable buffer receives a zero response on errors; EFAULT writes nothing. */
union nv_gpu_map_bar_io {
    struct nv_gpu_map_bar_req request;
    struct nv_gpu_map_bar_res response;
};
_Static_assert(sizeof(struct nv_gpu_map_bar_req) == 8, "GPU map-BAR request ABI");
_Static_assert(sizeof(struct nv_gpu_map_bar_res) == 16, "GPU map-BAR response ABI");
_Static_assert(sizeof(union nv_gpu_map_bar_io) == 16, "GPU map-BAR in/out ABI");
enum { NV_FP_X87 = 1, NV_FP_FXSAVE = 2 };
enum { NV_CPU_X87 = 1, NV_CPU_MMX = 2, NV_CPU_SSE = 4, NV_CPU_SSE2 = 8 };
struct nv_cpu_info {
    char vendor[16], brand[64];
    u32 version, bits, family, model, stepping, max_basic, max_extended;
    u32 features_edx, features_ecx, extended_edx, leaf7_ebx, physical_bits;
    u32 fp_mode, usable, online_cpus;
};
#define NV_GPU_MAX 16u
enum { NV_GPU_DISCOVERED = 1 };
enum {
    NV_GPU_NVIDIA = 1,
    NV_GPU_BAD_CAPS = 2,
    NV_GPU_BAD_HEADER = 4,
    NV_GPU_BAD_EXT_CAPS = 8
};
enum { NV_PCI_MSI = 1, NV_PCI_MSIX = 2, NV_PCI_EXPRESS = 4 };
enum {
    NV_PCIE_AER = 1,
    NV_PCIE_ACS = 2,
    NV_PCIE_ATS = 4,
    NV_PCIE_SRIOV = 8,
    NV_PCIE_RESIZABLE_BAR = 16,
    NV_PCIE_PASID = 32,
    NV_PCIE_DPC = 64
};
enum {
    NV_BAR_MEMORY = 1,
    NV_BAR_IO = 2,
    NV_BAR_PREFETCH = 4,
    NV_BAR_64 = 8,
    NV_BAR_UNASSIGNED = 16,
    NV_BAR_INVALID = 32,
    NV_BAR_UPPER = 64
};
struct nv_pci_bar {
    u32 low, high, flags, reserved;
};
struct nv_gpu_info {
    u32 bus, device, function, vendor, product, revision, subvendor, subproduct;
    u32 class_code, subclass, interface, command, status, irq_line, irq_pin;
    u32 capabilities, pcie_link, state, flags, ext_capabilities;
    struct nv_pci_bar bars[6];
};
_Static_assert(sizeof(struct nv_cpu_info) == 140, "CPU info ABI");
_Static_assert(sizeof(struct nv_gpu_info) == 176, "GPU info ABI");
enum {
    NV_PLATFORM_ACPI = 1,
    NV_PLATFORM_XSDT = 2,
    NV_PLATFORM_MCFG = 4,
    NV_PLATFORM_ECAM = 8,
    NV_PLATFORM_CF8 = 16,
    NV_PLATFORM_ECAM_DISABLED = 32
};
struct nv_platform_info {
    u32 flags, acpi_revision, mcfg_entries, ecam_regions, rejected_entries;
    u32 segment, start_bus, end_bus, base_low, base_high, config_bytes, reserved;
    char oem_id[8], oem_table_id[8];
};
_Static_assert(sizeof(struct nv_platform_info) == 64, "platform info ABI");
enum { NV_USB_CONTROLLERS = 1, NV_USB_DEVICES = 2, NV_USB_RESCAN = 3 };
#define NV_USB_CONTROLLER_MAX 8u
#define NV_USB_DEVICE_MAX 32u
enum { NV_USB_UNSUPPORTED = 1, NV_USB_RUNNING = 2, NV_USB_FAILED = 3 };
enum { NV_USB_IDENTIFIED = 1, NV_USB_CONFIGURED = 2, NV_USB_KEYBOARD = 3,
       NV_USB_HUB = 4, NV_USB_ETHERNET = 5 };
struct nv_usb_controller {
    u32 bus, device, function, vendor, product, interface, ports, state;
};
struct nv_usb_device {
    u32 controller, port, parent, address, speed, vendor, product, usb_version;
    u32 class_code, subclass, protocol, interfaces, state, reports;
    char manufacturer[48], product_name[64], serial[48];
};
_Static_assert(sizeof(struct nv_usb_controller) == 32, "USB controller ABI");
_Static_assert(sizeof(struct nv_usb_device) == 216, "USB device ABI");
enum nv_error {
    NV_EINVAL = 1,
    NV_ENOENT,
    NV_ENOMEM,
    NV_EFAULT,
    NV_EACCESS,
    NV_EEXIST,
    NV_ENOTDIR,
    NV_EISDIR,
    NV_ENOSPC,
    NV_EBUSY,
    NV_EBADF,
    NV_ENOTEMPTY,
    NV_ENOEXEC,
    NV_ECHILD,
    NV_ENOSYS,
    NV_EIO,
    NV_ENODEV,
    NV_E2BIG,
    NV_EAGAIN
};
enum { NV_READ = 1, NV_WRITE = 2, NV_CREATE = 4, NV_TRUNC = 8, NV_APPEND = 16, NV_EXCL = 32 };
enum { NV_DIR = 1, NV_FILE = 2, NV_DEVICE = 3, NV_PROC = 4 };
enum { NV_READY = 1, NV_SLEEPING = 2, NV_WAITING = 3, NV_ZOMBIE = 4 };
enum {
    NV_CTL_SYNC = 1,
    NV_CTL_POWEROFF = 2,
    NV_CTL_REBOOT = 3,
    NV_CTL_CLEAR = 4,
    NV_CTL_TEST_EXIT = 5
};
enum { NV_SCREEN_ACQUIRE = 1, NV_SCREEN_PRESENT = 2, NV_SCREEN_RELEASE = 3 };
enum {
    NV_KEY_LEFT = 256,
    NV_KEY_RIGHT,
    NV_KEY_UP,
    NV_KEY_DOWN,
    NV_KEY_HOME,
    NV_KEY_END,
    NV_KEY_PGUP,
    NV_KEY_PGDN,
    NV_KEY_DELETE,
    NV_KEY_INSERT,
    NV_KEY_F1 = 288,
    NV_KEY_F2,
    NV_KEY_F3,
    NV_KEY_F4,
    NV_KEY_F5,
    NV_KEY_F6,
    NV_KEY_F7,
    NV_KEY_F8,
    NV_KEY_F9,
    NV_KEY_F10,
    NV_KEY_F11,
    NV_KEY_F12,
    NV_KEY_SHIFT = 4096,
    NV_KEY_CTRL = 8192,
    NV_KEY_ALT = 16384,
    NV_KEY_DIRECT = 32768 /* Complete PS/2 or USB event, not an ANSI serial byte. */
};
struct nv_surface {
    u16 cells[80 * 25];
    u32 cursor;
}; /* cursor == 2000 hides it */
_Static_assert(sizeof(struct nv_surface) == 4004, "surface ABI");
struct nv_dirent {
    char name[32];
    u32 kind;
    u32 size;
};
struct nv_info {
    u32 abi, ram_pages, free_pages, heap_used, heap_total;
    u32 ticks, hz, tasks, nodes, disk_present, saved_generation;
};
struct nv_taskinfo {
    u32 pid, parent, state, cpu_ticks, pages;
    char name[32];
};
struct nv_volume_info {
    u32 letter, partition_index, snapshot_limit, generation;
    u32 sectors_low, sectors_high;
};
_Static_assert(sizeof(struct nv_volume_info) == 24, "volume info ABI");
enum { NV_PART_NUVORA = 1, NV_PART_MOUNTED = 2, NV_PART_LEGACY = 4 };
struct nv_partition_info {
    u32 number, flags, letter, reserved;
    u32 start_low, start_high, sectors_low, sectors_high;
};
_Static_assert(sizeof(struct nv_partition_info) == 32, "partition info ABI");
_Static_assert(sizeof(struct nv_dirent) == 40, "dirent ABI");
#endif
