#ifndef NV_ABI_H
#define NV_ABI_H
#include <nv/types.h>
#define NV_VERSION "0.15.0"
#define NV_ABI_VERSION 2
#define NV_ABI_LEGACY 1u
/* ABI 2 selects native pointer-width arguments/results without changing any
 * ABI 1 call ID or record. Old applications continue to use int 0x81. */
#define NV_CALL_NATIVE (1ull << 32)
#define NV_NAME_MAX 31
#define NV_PATH_MAX 192
#define NV_ARG_MAX 256
#define NV_FILE_MAX (64u * 1024u * 1024u) /* legacy RAM/snapshot files */
#define NV_FILE_MAX64 0x7ffffffffffff000ull
#define NV_OPEN_MAX 16
#define NV_TASK_MAX 32
#define NV_VOLUME_MAX 4u
#define NV_PARTITION_MAX 32u
#define NV_PAGE 4096u
/* Nuvora x64 application ABI: int 0x81; eax=operation; ebx/ecx/edx=arguments.
 * Existing operation numbers and structure layouts are never renumbered. */
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
    NV_SEEK64, /* ebx=fd, ecx=nv_seek64*, edx=0; result through position */
    NV_STAT64, /* ebx=fd, ecx=nv_stat64*, edx=0 */
    NV_LIST64, /* same arguments as LIST; nv_dirent64 */
    NV_INFO64,
    NV_TASK64,
    NV_CLOCK64,
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
    NV_SUB_NET = 3,
    NV_SUB_DISPLAY = 4,
    NV_SUB_INPUT = 5,
    NV_SUB_AUDIO = 6,
    NV_SUB_WINDOW = 7,
    NV_SUB_ACCOUNT = 8
};
/* Local account service. Only the trusted desktop / initial console can own
 * this service. Hashes and salts never cross the ABI; status queries and
 * account enumeration return public profile data only. */
#define NV_ACCOUNT_API_VERSION 1u
#define NV_ACCOUNT_MAX 32u
#define NV_PASSWORD_MIN 15u
#define NV_PASSWORD_MAX 128u
#define NV_UID_NONE 0xffffffffu
enum { NV_ACCOUNT_INFO=1, NV_ACCOUNT_CLAIM, NV_ACCOUNT_LIST, NV_ACCOUNT_SETUP,
       NV_ACCOUNT_LOGIN, NV_ACCOUNT_ADD, NV_ACCOUNT_PASSWORD, NV_ACCOUNT_POLL,
       NV_ACCOUNT_CANCEL, NV_ACCOUNT_LOCK, NV_ACCOUNT_LOGOUT,
       NV_ACCOUNT_REMOVE, NV_ACCOUNT_UPDATE, NV_ACCOUNT_POLICY_GET,
       NV_ACCOUNT_POLICY_SET };
/* Idle locking is enforced by the kernel using genuine keyboard/pointer
 * activity. Existing account records and operation numbers stay unchanged. */
struct nv_account_policy { u32 idle_timeout_ticks, idle_ticks; };
#define NV_AUTH_IDLE_DEFAULT (5u * 60u * 100u)
#define NV_AUTH_IDLE_MIN (60u * 100u)
#define NV_AUTH_IDLE_MAX (15u * 60u * 100u)
enum { NV_ACCOUNT_ADMIN=1, NV_ACCOUNT_DISABLED=1 };
enum { NV_AUTH_SETUP=1, NV_AUTH_PERSISTENT=2, NV_AUTH_SIGNED_IN=4,
       NV_AUTH_LOCKED=8, NV_AUTH_ERROR=16, NV_AUTH_BUSY=32 };
struct nv_account_profile {
    u32 index, uid, role, flags;
    char name[32], display_name[64];
};
struct nv_account_info {
    u32 api_version, flags, count, uid, role, manager_pid, cooldown_ticks;
    char name[32], display_name[64], home[NV_PATH_MAX];
};
struct nv_account_request {
    u32 uid, role, flags;
    char name[32], display_name[64];
    char password[NV_PASSWORD_MAX+1], old_password[NV_PASSWORD_MAX+1];
};
struct nv_account_progress { u32 completed, total, uid; i32 result; };
/* Native application windows. The desktop owns the display and registers as
 * the window server; clients never receive another process's memory mapping.
 * BEGIN -> full-width UPLOAD tiles -> COMMIT publishes a complete frame.
 * READ is server-only and checks generation, so a changed snapshot is retried.
 * All pixels use the format reported by NV_DISPLAY_INFO. This is a native
 * protocol, not a Wayland or X11 wire implementation. */
#define NV_WINDOW_API_VERSION 1u
#define NV_WINDOW_MAX 8u
#define NV_WINDOW_EDGE_MAX 8192u
enum { NV_WINDOW_INFO = 1, NV_WINDOW_SERVER_ACQUIRE, NV_WINDOW_SERVER_RELEASE,
       NV_WINDOW_CREATE, NV_WINDOW_DESTROY, NV_WINDOW_BEGIN, NV_WINDOW_UPLOAD,
       NV_WINDOW_COMMIT, NV_WINDOW_POLL, NV_WINDOW_ENUM, NV_WINDOW_READ,
       NV_WINDOW_CONFIGURE, NV_WINDOW_SEND, NV_WINDOW_BIND_STDIO, NV_WINDOW_TEXT_READ,
       NV_WINDOW_UPLOAD64, NV_WINDOW_READ64 };
enum { NV_WINDOW_VISIBLE = 1, NV_WINDOW_FOCUSED = 2 };
enum { NV_WINDOW_TEXT = 4 };
enum { NV_WINDOW_EVENT_CONFIGURE = 1, NV_WINDOW_EVENT_FOCUS,
       NV_WINDOW_EVENT_KEY, NV_WINDOW_EVENT_POINTER, NV_WINDOW_EVENT_CLOSE };
struct nv_window_info { u32 api_version, server_pid, max_windows, max_copy_bytes; };
struct nv_window_create { u32 width, height, id, serial; char title[64]; };
struct nv_window_id { u32 id; };
struct nv_window_frame { u32 id, serial, width, height; };
struct nv_window_pixels32 { u32 id, generation, x, y, width, height, stride, pixels; };
struct nv_window_pixels { u32 id, generation, x, y, width, height, stride, reserved; u64 pixels; };
struct nv_window_entry {
    u32 index, id, pid, width, height, generation, serial, flags;
    u32 requested_width, requested_height;
    char title[64];
};
struct nv_window_configure { u32 id, width, height, flags; };
struct nv_window_event {
    u32 id, type, serial, width, height, key;
    i32 x, y, wheel;
    u32 buttons, flags;
};
_Static_assert(sizeof(struct nv_window_create) == 80, "window create ABI");
_Static_assert(sizeof(struct nv_window_pixels32) == 32, "legacy window pixels ABI");
_Static_assert(sizeof(struct nv_window_pixels) == 40, "native window pixels ABI");
_Static_assert(sizeof(struct nv_window_entry) == 104, "window entry ABI");
_Static_assert(sizeof(struct nv_window_event) == 44, "window event ABI");
/* One interleaved signed 16-bit little-endian stereo PCM output at 48 kHz.
 * WRITE copies exactly bytes from a user buffer and returns the byte count.
 * A single write is bounded so the synchronous DMA operation can be polled.
 * Unsupported hardware returns ENODEV; other formats return EINVAL. */
#define NV_AUDIO_API_VERSION 1u
#define NV_AUDIO_MAX_WRITE 3072u
enum { NV_AUDIO_INFO = 1, NV_AUDIO_WRITE = 2,
       NV_AUDIO_GET_VOLUME = 3, NV_AUDIO_SET_VOLUME = 4, NV_AUDIO_WRITE64 = 5 };
enum { NV_AUDIO_S16LE = 1 };
struct nv_audio_info {
    u32 api_version, outputs, sample_rate, channels, format, max_write_bytes;
};
struct nv_audio_write32 { u32 pixels, bytes; };
struct nv_audio_write { u64 pixels; u32 bytes, reserved; };
/* Global output attenuation, 0 (silent) through 100 (unchanged PCM). */
struct nv_audio_volume { u32 percent; };
_Static_assert(sizeof(struct nv_audio_info) == 24, "audio info ABI");
_Static_assert(sizeof(struct nv_audio_write32) == 8, "legacy audio write ABI");
_Static_assert(sizeof(struct nv_audio_write) == 16, "native audio write ABI");
_Static_assert(sizeof(struct nv_audio_volume) == 4, "audio volume ABI");
/* Pointer events normally carry signed relative dx/dy. If ABSOLUTE is set in
 * buttons, dx/dy instead carry unsigned coordinates in the 0..32767 HID
 * range; the consumer scales them to its display. Only the pixel-screen owner
 * may consume events; the queue is reset on acquire. */
#define NV_INPUT_API_VERSION 1u
enum { NV_INPUT_INFO = 1, NV_INPUT_POINTER_POLL = 2 };
enum { NV_POINTER_LEFT = 1, NV_POINTER_RIGHT = 2, NV_POINTER_MIDDLE = 4,
       NV_POINTER_ABSOLUTE = 0x80000000u };
struct nv_input_info { u32 api_version, pointer_devices, flags, reserved; };
struct nv_pointer_event { i32 dx, dy, wheel; u32 buttons; };
_Static_assert(sizeof(struct nv_input_info) == 16, "input info ABI");
_Static_assert(sizeof(struct nv_pointer_event) == 16, "pointer event ABI");
/* Display is an optional, unaccelerated firmware framebuffer. A successful
 * INFO reports the mode; applications must still acquire exclusive ownership
 * before presenting. Pixels are 32-bit little-endian words: BGRX8 accepts
 * 0x00RRGGBB, RGBX8 accepts 0x00BBGGRR. The X byte is ignored. */
#define NV_DISPLAY_API_VERSION 1u
#define NV_DISPLAY_MAX_COPY (1024u * 1024u)
enum { NV_DISPLAY_INFO = 1, NV_DISPLAY_ACQUIRE = 2,
       NV_DISPLAY_PRESENT = 3, NV_DISPLAY_RELEASE = 4, NV_DISPLAY_PRESENT64 = 5 };
enum { NV_DISPLAY_BGRX8 = 1, NV_DISPLAY_RGBX8 = 2 };
struct nv_display_info {
    u32 api_version, width, height, pitch, format, max_copy_bytes;
};
struct nv_display_present32 {
    u32 x, y, width, height, stride, pixels;
};
struct nv_display_present {
    u32 x, y, width, height, stride, reserved;
    u64 pixels;
};
_Static_assert(sizeof(struct nv_display_info) == 24, "display info ABI");
_Static_assert(sizeof(struct nv_display_present32) == 24, "legacy display present ABI");
_Static_assert(sizeof(struct nv_display_present) == 32, "native display present ABI");
/* Network buffers are fixed-size and copied across the user/kernel boundary.
 * Address fields hold four IPv4 octets in network order (e.g. 0xc0a80101). */
#define NV_NET_MAX 8u
#define NV_NET_DATA_MAX 1024u
enum { NV_NET_INFO = 1, NV_NET_DHCP = 2, NV_NET_STATIC = 3,
       NV_NET_UDP_SEND = 4, NV_NET_UDP_RECV = 5, NV_NET_SELECT = 6,
       NV_NET_WIFI_COMMAND = 7, NV_NET_WIFI_READ = 8,
       NV_NET_PING = 9, NV_NET_TCP_OPEN = 10, NV_NET_TCP_SEND = 11,
       NV_NET_TCP_RECV = 12, NV_NET_TCP_CLOSE = 13 };
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
/* PING sends on the first call, returns 0 while waiting and 1 with reply
 * details. TCP exposes one bounded, ordered IPv4 stream; OPEN similarly
 * returns 0 while connecting and 1 when established. RECV returns zero on
 * orderly EOF, EAGAIN while pending, or the number of bytes copied. */
struct nv_net_ping {
    u32 index, address, identifier, sequence, reply_ttl, reply_bytes;
};
struct nv_net_tcp {
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
       NV_USB_HUB = 4, NV_USB_ETHERNET = 5, NV_USB_MOUSE = 6,
       NV_USB_COMPOSITE_INPUT = 7 };
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
struct nv_seek64 { i64 offset; u64 position; u32 origin, reserved; };
struct nv_stat64 { u64 size, allocated; u32 kind, reserved; };
struct nv_dirent64 { char name[32]; u32 kind, reserved; u64 size; };
_Static_assert(sizeof(struct nv_seek64) == 24, "seek64 ABI");
_Static_assert(sizeof(struct nv_dirent64) == 48, "dirent64 ABI");
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
    NV_KEY_MODIFIERS = 320, /* Modifier state changed, including key release. */
    NV_KEY_SHIFT = 4096,
    NV_KEY_CTRL = 8192,
    NV_KEY_ALT = 16384,
    NV_KEY_DIRECT = 32768, /* Complete PS/2 or USB event, not an ANSI serial byte. */
    NV_KEY_META = 65536 /* Windows / Super / Command modifier. */
};
struct nv_surface {
    u16 cells[80 * 25];
    u32 cursor;
}; /* cursor == 2000 hides it */
struct nv_window_text { u32 id, generation; struct nv_surface surface; };
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
struct nv_info64 {
    u32 abi, hz;
    u64 ram_pages, free_pages, heap_used, heap_total, ticks;
    u32 tasks, nodes, disk_present, saved_generation;
};
struct nv_taskinfo64 {
    u32 pid, parent, state, abi;
    u64 cpu_ticks, pages, heap_end;
    char name[32];
};
_Static_assert(sizeof(struct nv_info64) == 64, "native system info ABI");
_Static_assert(sizeof(struct nv_taskinfo64) == 72, "native task info ABI");
struct nv_volume_info {
    u32 letter, partition_index, snapshot_limit, generation; /* limit=0: disk-backed NVSTORE3 */
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
