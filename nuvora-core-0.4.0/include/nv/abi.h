#ifndef NV_ABI_H
#define NV_ABI_H
#include <nv/types.h>
#define NV_VERSION "0.4.0"
#define NV_ABI_VERSION 1
#define NV_NAME_MAX 31
#define NV_PATH_MAX 192
#define NV_ARG_MAX 256
#define NV_FILE_MAX (128u * 1024u)
#define NV_OPEN_MAX 16
#define NV_TASK_MAX 32
#define NV_PAGE 4096u
/* Private ABI: int 0x81; eax=operation; ebx/ecx/edx/esi/edi=arguments. */
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
    NV_CALL_COUNT
};
enum { NV_USB_CONTROLLERS = 1, NV_USB_DEVICES = 2, NV_USB_RESCAN = 3 };
#define NV_USB_CONTROLLER_MAX 8u
#define NV_USB_DEVICE_MAX 32u
enum { NV_USB_UNSUPPORTED = 1, NV_USB_RUNNING = 2, NV_USB_FAILED = 3 };
enum { NV_USB_IDENTIFIED = 1, NV_USB_CONFIGURED = 2, NV_USB_KEYBOARD = 3, NV_USB_HUB = 4 };
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
_Static_assert(sizeof(struct nv_dirent) == 40, "dirent ABI");
#endif
