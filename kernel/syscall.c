#include "kernel.h"
#define IO_MAX 16384u
void fill_info(struct nv_info *i) {
    *i = (struct nv_info){
        NV_ABI_VERSION, pages_total(),   pages_free(), heap_used(),       heap_total(), ticks, 100,
        task_count(),   fs_node_count(), disk_ready(), store_generation()};
}
struct frame *syscall_dispatch(struct frame *f) {
    if ((f->cs & 3) != 3 || !current)
        panic("syscall outside user mode");
    /* ABI 1 deliberately exposes a 32-bit user-address window on both CPUs. */
    if (f->eax > 0xffffffffu || f->ebx > 0xffffffffu || f->ecx > 0xffffffffu ||
        f->edx > 0xffffffffu) {
        f->eax = (u32)-NV_EINVAL;
        return f;
    }
    current->frame = f;
    task_reap();
    usb_poll();
    u32 a = f->ebx, b = f->ecx, c = f->edx;
    int result = -NV_ENOSYS;
    char path[NV_PATH_MAX], other[NV_ARG_MAX];
    bool reschedule = false;
    switch (f->eax) {
    case NV_EMIT:
    case NV_TAKE:
        if (c > IO_MAX) {
            result = -NV_E2BIG;
            break;
        }
        if (!user_range(current->pd, b, c, f->eax == NV_TAKE)) {
            result = -NV_EFAULT;
            break;
        }
        result = f->eax == NV_EMIT ? fs_write(current, (int)a, (void *)(uptr)b, c)
                                   : fs_read(current, (int)a, (void *)(uptr)b, c);
        break;
    case NV_OPEN:
        result = user_string(a, path, sizeof(path));
        if (result == 0)
            result = fs_open(current, path, b);
        break;
    case NV_CLOSE:
        result = fs_close(current, (int)a);
        break;
    case NV_SEEK:
        result = fs_seek(current, (int)a, (i32)b, c);
        break;
    case NV_LIST:
        result = user_string(a, path, sizeof(path));
        if (result < 0)
            break;
        if (!user_range(current->pd, c, sizeof(struct nv_dirent), true)) {
            result = -NV_EFAULT;
            break;
        }
        {
            struct nv_dirent ent;
            result = fs_list(current->cwd, path, b, &ent);
            if (result > 0)
                memcpy((void *)(uptr)c, &ent, sizeof(ent));
        }
        break;
    case NV_MKDIR:
    case NV_REMOVE:
    case NV_CHDIR:
        result = user_string(a, path, sizeof(path));
        if (result < 0)
            break;
        if (f->eax == NV_MKDIR)
            result = fs_mkdir(current->cwd, path);
        else if (f->eax == NV_REMOVE)
            result = fs_remove(current->cwd, path);
        else {
            result = fs_lookup(current->cwd, path);
            if (result >= 0) {
                if (fs_kind(result) != NV_DIR)
                    result = -NV_ENOTDIR;
                else {
                    current->cwd = result;
                    result = 0;
                }
            }
        }
        break;
    case NV_MOVE:
    case NV_REPLACE:
        result = user_string(a, path, sizeof(path));
        if (result < 0)
            break;
        result = user_string(b, other, NV_PATH_MAX);
        if (result == 0)
            result = f->eax == NV_MOVE ? fs_move(current->cwd, path, other)
                                       : fs_replace(current->cwd, path, other);
        break;
    case NV_GETCWD:
        if (!b || b > NV_PATH_MAX) {
            result = -NV_EINVAL;
            break;
        }
        if (!user_range(current->pd, a, b, true)) {
            result = -NV_EFAULT;
            break;
        }
        result = fs_path(current->cwd, path, sizeof(path));
        if (result == 0) {
            if (strlen(path) + 1 > b)
                result = -NV_E2BIG;
            else
                memcpy((void *)(uptr)a, path, strlen(path) + 1);
        }
        break;
    case NV_SPAWN:
    case NV_EXEC:
        result = user_string(a, path, sizeof(path));
        if (result < 0)
            break;
        result = user_string(b, other, sizeof(other));
        if (result < 0)
            break;
        /* Resolve relative executables to the caller's actual current directory. */
        {
            int node = fs_lookup(current->cwd, path);
            if (node < 0) {
                result = node;
                break;
            }
            result = fs_path(node, path, sizeof(path));
            if (result == 0)
                result =
                    f->eax == NV_EXEC ? task_exec(path, other) : task_spawn(path, other, current);
        }
        break;
    case NV_WAIT:
        result = task_wait(a);
        if (result == -4096) {
            result = 0;
            reschedule = true;
        }
        break;
    case NV_EXIT:
        task_exit((int)(a & 255));
        result = 0;
        reschedule = true;
        break;
    case NV_SLEEP:
        if (a > 86400000u) {
            result = -NV_EINVAL;
            break;
        }
        if (a) {
            current->wake = ticks + (a + 9) / 10;
            current->state = NV_SLEEPING;
        }
        result = 0;
        reschedule = true;
        break;
    case NV_YIELD:
        result = 0;
        reschedule = true;
        break;
    case NV_GROW:
        result = task_grow((i32)a);
        break;
    case NV_INFO:
        if (!user_range(current->pd, a, sizeof(struct nv_info), true)) {
            result = -NV_EFAULT;
            break;
        }
        {
            struct nv_info info;
            fill_info(&info);
            memcpy((void *)(uptr)a, &info, sizeof(info));
            result = 0;
        }
        break;
    case NV_TASK:
        if (!user_range(current->pd, b, sizeof(struct nv_taskinfo), true)) {
            result = -NV_EFAULT;
            break;
        }
        {
            struct nv_taskinfo info;
            result = task_info(a, &info);
            if (result > 0)
                memcpy((void *)(uptr)b, &info, sizeof(info));
        }
        break;
    case NV_STOP:
        result = task_stop(a);
        break;
    case NV_CLOCK:
        result = (int)ticks;
        break;
    case NV_KEY:
        result = (a || b || c) ? -NV_EINVAL : console_key(current->pid);
        break;
    case NV_SURFACE:
        if (c || (a != NV_SCREEN_PRESENT && b)) {
            result = -NV_EINVAL;
            break;
        }
        if (a == NV_SCREEN_PRESENT &&
            !user_range(current->pd, b, sizeof(struct nv_surface), false)) {
            result = -NV_EFAULT;
            break;
        }
        result = console_surface(current->pid, a, (const void *)(uptr)b);
        break;
    case NV_USB:
        if (a == NV_USB_RESCAN) {
            result = (b || c) ? -NV_EINVAL : usb_rescan();
            break;
        }
        if (a != NV_USB_CONTROLLERS && a != NV_USB_DEVICES) {
            result = -NV_EINVAL;
            break;
        }
        if (b >= (a == NV_USB_CONTROLLERS ? NV_USB_CONTROLLER_MAX : NV_USB_DEVICE_MAX)) {
            result = -NV_EINVAL;
            break;
        }
        if (!user_range(current->pd, c,
                        a == NV_USB_CONTROLLERS ? sizeof(struct nv_usb_controller)
                                                : sizeof(struct nv_usb_device),
                        true)) {
            result = -NV_EFAULT;
            break;
        }
        if (a == NV_USB_CONTROLLERS) {
            struct nv_usb_controller item;
            result = usb_controller_info(b, &item);
            if (result > 0)
                memcpy((void *)(uptr)c, &item, sizeof(item));
        } else {
            struct nv_usb_device item;
            result = usb_device_info(b, &item);
            if (result > 0)
                memcpy((void *)(uptr)c, &item, sizeof(item));
        }
        break;
    case NV_HARDWARE:
        if ((a != NV_HW_CPU && a != NV_HW_GPU && a != NV_HW_PLATFORM) ||
            ((a == NV_HW_CPU || a == NV_HW_PLATFORM) ? b != 0 : b >= NV_GPU_MAX)) {
            result = -NV_EINVAL;
            break;
        }
        u32 hardware_size = a == NV_HW_CPU       ? sizeof(struct nv_cpu_info)
                            : a == NV_HW_GPU     ? sizeof(struct nv_gpu_info)
                                                : sizeof(struct nv_platform_info);
        if (!user_range(current->pd, c, hardware_size, true)) {
            result = -NV_EFAULT;
            break;
        }
        if (a == NV_HW_CPU) {
            struct nv_cpu_info item;
            cpu_get_info(&item);
            memcpy((void *)(uptr)c, &item, sizeof(item));
            result = 1;
        } else if (a == NV_HW_GPU) {
            struct nv_gpu_info item;
            result = gpu_get_info(b, &item);
            if (result > 0)
                memcpy((void *)(uptr)c, &item, sizeof(item));
        } else {
            struct nv_platform_info item;
            acpi_get_info(&item);
            memcpy((void *)(uptr)c, &item, sizeof(item));
            result = 1;
        }
        break;
    case NV_DEVCTL:
        switch (a) {
        case NV_SUB_CPU:
            result = cpu_ioctl(b, c);
            break;
        case NV_SUB_GPU:
            result = gpu_ioctl(b, c);
            break;
        case NV_SUB_AI:
            result = ai_ioctl(b, c);
            break;
        case NV_SUB_NET:
            result = net_ioctl(b, c);
            break;
        default:
            result = -NV_EINVAL;
            break;
        }
        break;
    case NV_CONTROL:
        if (a == NV_CTL_SYNC && console_owned(current->pid)) {
            result = store_sync();
            break;
        }
        if (a == NV_CTL_CLEAR) {
            if (console_owned(0) == false) {
                result = -NV_EBUSY;
                break;
            }
            console_clear();
            result = 0;
            break;
        }
        if (current->pid != 1) {
            result = -NV_EACCESS;
            break;
        }
        if (a == NV_CTL_SYNC)
            result = store_sync();
        else if (a == NV_CTL_POWEROFF)
            machine_poweroff();
        else if (a == NV_CTL_REBOOT)
            machine_reboot();
        else if (a == NV_CTL_TEST_EXIT && test_mode) {
            outl(0xf4, b ? 0x11 : 0x10);
            machine_poweroff();
        } else
            result = -NV_EINVAL;
        break;
    default:
        break;
    }
    f->eax = (u32)result;
    return reschedule ? schedule(f) : f;
}
