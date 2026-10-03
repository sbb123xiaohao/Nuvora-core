/* The real HDA driver against a register/codec/DMA model. No audio device or
 * QEMU is needed to check the codec route, BDL geometry and ABI boundaries. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
struct task { void *pd; };
static struct task task_object;
static struct task *current = &task_object;
static u8 regs[PAGE], dma[3][PAGE];
static u32 next_page, pci_command, submitted, played, pin_control, amp_control;
static u32 input_amp_control, pin_amp_control;
static u32 output_caps = (1u << 31) | (1u << 16) | (63u << 8) | 53u;
static u32 input_caps = (1u << 31) | (1u << 16) | (31u << 8) | 17u;
static u32 pin_output_caps = (1u << 31) | (2u << 16) | (31u << 8) | 29u;
static u32 group_output_caps = (1u << 31) | (1u << 16) | (63u << 8) | 42u;
static u32 group_input_caps = (1u << 31) | (2u << 16) | (31u << 8) | 11u;
static bool amp_override = true, amps_present = true;
static bool format_override = true, multiple_groups, multiple_codecs;
static u32 group_pcm = (1u << 6) | (1u << 17), group_formats = 1;
static u32 widget_format_queries, group_format_queries;
static u32 dac_channel_count = 1; /* WCAP encoding: channel count minus one. */
static u8 failed_param;
static bool wide_connections;
static u32 widget_amp_queries, group_amp_queries;
static bool codec_present = true, bad_format, stalled;
static volatile u64 ticks;
static u64 hardware_us, reset_entered_us, reset_released_us;
static u32 idle_calls, reset_ack_failure, keyboard_wakes;
static bool require_reset_hold = true, require_codec_delay = true;
static bool reset_short, reset_released, interrupts_enabled;
static bool pending_pit;
static u8 *user_page;
static u32 read_reg(u32 offset, u32 bytes) {
    if (offset == 0x0e && bytes == 2 &&
        ((!reset_released || (require_reset_hold && reset_short)) ||
         (require_codec_delay && hardware_us - reset_released_us < 540))) return 0;
    u32 value = 0;
    memcpy(&value, regs + offset, bytes);
    return value;
}
static u32 answer(u32 word) {
    u8 codec = (u8)(word >> 28);
    u8 node = (u8)((word >> 20) & 0x7f);
    u32 verb = word & 0xfffff;
    if (multiple_groups && node == 0 && verb == 0xf0004) return 0x00010004;
    u8 group = node >= 4 ? 4 : 1;
    bool unsupported_route = (multiple_codecs && codec != 1) ||
                             (multiple_groups && group != 4);
    if ((node == 1 || (multiple_groups && node == 4)) &&
        (verb == 0xf000a || verb == 0xf000b)) {
        ++group_format_queries;
        return unsupported_route ? 0 : verb == 0xf000a ? group_pcm : group_formats;
    }
    /* The second function group uses nodes 5/6 for the same DAC/pin model. */
    bool second_group = multiple_groups && node >= 4;
    if (second_group) node = (u8)(node - 3);
    if (node == 0 && verb == 0xf0004) return 0x00010001;
    if (node == 1 && verb == 0xf0005) return 1;
    if (node == 1 && verb == 0xf0004) return second_group ? 0x00050002 : 0x00020002;
    if (node == 1 && verb == 0xf000d) { ++group_amp_queries; return group_input_caps; }
    if (node == 1 && verb == 0xf0012) { ++group_amp_queries; return group_output_caps; }
    if (node == 2 && verb == 0xf0009) return (dac_channel_count & 1u) |
        (dac_channel_count >> 1) << 13 | (amps_present ? 4u : 0u) |
        (amp_override ? 1u << 3 : 0u) | (format_override ? 1u << 4 : 0u) | 1u << 10;
    if (node == 2 && (verb == 0xf000a || verb == 0xf000b)) {
        ++widget_format_queries;
        return !format_override || unsupported_route || bad_format ? 0 :
               verb == 0xf000a ? (1u << 6) | (1u << 17) : 1u;
    }
    if (node == 2 && verb == 0xf0012) { ++widget_amp_queries; return amp_override ? output_caps : 0; }
    if (node == 3 && verb == 0xf0009) return 4u << 20 | (amps_present ? 2u | 4u : 0u) |
        (amp_override ? 1u << 3 : 0u) | 1u << 8 | 1u << 10;
    if (node == 3 && verb == 0xf000d) { ++widget_amp_queries; return amp_override ? input_caps : 0; }
    if (node == 3 && verb == 0xf0012) { ++widget_amp_queries; return amp_override ? pin_output_caps : 0; }
    if (node == 3 && verb == 0xf000c) return 1u << 4 | 1u << 16;
    if (node == 3 && verb == 0xf1c00) return 0x01014010;
    if (node == 3 && verb == 0xf000e) return wide_connections ? 17 : 1;
    if (node == 3 && (verb & 0xfff00) == 0xf0200)
        return wide_connections ? ((verb & 255) == 16 ? 2 : 0) : second_group ? 5 : 2;
    if (node == 3 && (verb & 0xfff00) == 0x70700) pin_control = verb & 255;
    if (node == 2 && (verb & 0xf0000) == 0x30000) amp_control = verb;
    if (node == 3 && (verb & 0xf0000) == 0x30000) {
        if (verb & 0x8000) pin_amp_control = verb;
        if (verb & 0x4000) input_amp_control = verb;
    }
    ++submitted;
    return 0;
}
static void write_reg(u32 offset, u32 value, u32 bytes) {
    if (offset == 0x08 && bytes == 4) {
        if ((!(value & 1) && reset_ack_failure == 1) ||
            ((value & 1) && reset_ack_failure == 2)) return;
        if (!(value & 1)) {
            reset_entered_us = hardware_us;
            reset_released = false;
        } else {
            reset_short = hardware_us - reset_entered_us < 100;
            reset_released_us = hardware_us;
            reset_released = true;
        }
    }
    memcpy(regs + offset, &value, bytes);
    if (offset == 0x68 && bytes == 2) {
        if (value == 1) {
            u32 cmd = read_reg(0x60, 4);
            if (failed_param && (cmd & 0xfffff) == (0xf0000u | failed_param)) {
                regs[0x68] = 0; /* Immediate command receives no response. */
                return;
            }
            u32 result = answer(cmd);
            memcpy(regs + 0x64, &result, 4);
            regs[0x68] = 2;
        } else regs[0x68] = 0;
    }
    if (offset == 0xc0 && value == 2 && !stalled) {
        regs[0xc3] = 4;
        u32 size = read_reg(0xc8, 4) - 1024;
        memcpy(regs + 0xc4, &size, 4);
        ++played;
    }
    if (offset == 0xc3 && value == 0x1c) regs[0xc3] = 0;
}
#define HDA_READ8(o) ((u8)read_reg((o), 1))
#define HDA_READ16(o) ((u16)read_reg((o), 2))
#define HDA_READ32(o) read_reg((o), 4)
#define HDA_WRITE8(o, x) write_reg((o), (x), 1)
#define HDA_WRITE16(o, x) write_reg((o), (x), 2)
#define HDA_WRITE32(o, x) write_reg((o), (x), 4)
static void pci_visit(void (*visit)(u32, u32, u32)) {
    visit(0x1000, 0x80862668, 0x04030000);
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x1000);
    return offset == 0x10 ? 0x40000000 : offset == 4 ? pci_command : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert(address == 0x1000 && offset == 4);
    pci_command = value;
}
static void *vm_mmio_map(u64 physical, u32 length) {
    assert(physical == 0x40000000 && length == PAGE);
    return regs;
}
static uptr page_alloc_below(u64 limit) {
    assert(limit == 0x100000000ull && ++next_page <= 2);
    memset(dma[next_page], 0, PAGE);
    return (uptr)next_page * PAGE;
}
static void *phys_ptr(uptr physical) {
    assert(physical && physical % PAGE == 0 && physical / PAGE <= next_page);
    return dma[physical / PAGE];
}
static bool user_range(void *pd, uptr address, usize size, bool write) {
    (void)pd; (void)write;
    uptr base = (uptr)user_page;
    return address >= base && size <= PAGE && address - base <= PAGE - size;
}
static void kprintf(const char *format, ...) { (void)format; }
static void irq_enable(void) { interrupts_enabled = true; }
static void irq_disable(void) { interrupts_enabled = false; }
static uptr irq_save(void) {
    uptr flags = interrupts_enabled ? 0x200u : 0;
    interrupts_enabled = false;
    return flags;
}
static void irq_restore(uptr flags) { interrupts_enabled = !!(flags & 0x200u); }
static void idle_once(void) {
    assert(!interrupts_enabled);
    if (keyboard_wakes) {
        --keyboard_wakes;
        ++hardware_us;
        return; /* A keyboard IRQ wakes HLT but is not a PIT tick. */
    }
    if (pending_pit) {
        pending_pit = false;
        ++ticks; /* IF=0 may have left an older IRQ waiting in the PIC. */
        return;
    }
    /* The next PIT IRQ may be only a microsecond away. One tick therefore
     * cannot provide even the minimum 100 us codec reset hold time. */
    hardware_us += ++idle_calls == 1 ? 1 : 10000;
    ++ticks;
}
#include "../kernel/audio.c"

static void reset_device(void) {
    memset(regs, 0, sizeof(regs));
    regs[0x08] = 1; /* firmware left the controller out of reset */
    regs[1] = 0x22; /* GCAP: two input and two output streams */
    regs[0x0e] = codec_present ? (multiple_codecs ? 3 : 1) : 0;
    next_page = submitted = played = pin_control = amp_control = pci_command = 0;
    input_amp_control = pin_amp_control = 0;
    widget_amp_queries = group_amp_queries = 0;
    widget_format_queries = group_format_queries = 0;
    ticks = hardware_us = reset_entered_us = reset_released_us = 0;
    idle_calls = 0;
    reset_short = reset_released = false;
    keyboard_wakes = 0;
    pending_pit = true;
}
int main(int argc, char **argv) {
    if (argc == 2) {
        assert(!strcmp(argv[1], "reset-hold") || !strcmp(argv[1], "reset-detect"));
        require_reset_hold = !strcmp(argv[1], "reset-hold");
        require_codec_delay = !strcmp(argv[1], "reset-detect");
    } else assert(argc == 1);
    user_page = mmap(NULL, PAGE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(user_page != MAP_FAILED && (uptr)user_page > 0xffffffffu);
    reset_device();
    audio_init();
    assert(hda.ready && hda.codec == 0 && hda.pin == 3 && hda.dac == 2);
    assert(!interrupts_enabled);
    assert((pci_command & 6) == 6 && submitted && pin_control == 0x40 &&
           (amp_control & 0x8000) && hda.stream == 0xc0);
    assert((amp_control & 0xffffu) == (0xb000u | 53u));
    assert((input_amp_control & 0xffffu) == (0x7000u | 17u));
    assert((pin_amp_control & 0xffffu) == (0xb000u | 29u));
    assert(widget_amp_queries == 3 && !group_amp_queries);
    assert(widget_format_queries == 2 && !group_format_queries);
    assert(!reset_short && hardware_us - reset_released_us >= 540);
    /* Even at PIT counter rollover and with unrelated IRQ wakeups, both
     * codec timing limits hold and an initially enabled IF stays enabled. */
    reset_device(); ticks = ~0ull - 1; keyboard_wakes = 3;
    interrupts_enabled = true;
    audio_init();
    assert(hda.ready && interrupts_enabled && ticks == 4 && !keyboard_wakes &&
           !reset_short && hardware_us - reset_released_us >= 540);
    /* A stuck reset acknowledgement must not issue codec commands or
     * allocate DMA pages; retain IF on both entry and exit failures. */
    for (reset_ack_failure = 1; reset_ack_failure <= 2; ++reset_ack_failure) {
        reset_device(); audio_init();
        assert(!hda.ready && !next_page && !submitted && interrupts_enabled);
    }
    reset_ack_failure = 0;
    interrupts_enabled = false;
    reset_device(); audio_init(); assert(hda.ready && !interrupts_enabled);
    struct nv_audio_info *info = (void *)user_page;
    assert(audio_ioctl(NV_AUDIO_INFO, (uptr)info) == 0 &&
           info->outputs == 1 && info->sample_rate == 48000 &&
           info->max_write_bytes == NV_AUDIO_MAX_WRITE);
    assert(audio_ioctl(0, 0) == -NV_EINVAL && audio_ioctl(NV_AUDIO_INFO, 0) == -NV_EFAULT);
    struct nv_audio_write *request = (void *)(user_page + 32);
    request->pixels = (uptr)(user_page + 64);
    request->bytes = 3000;
    for (u32 i = 0; i < request->bytes; ++i) user_page[64 + i] = (u8)i;
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == 3000 && played == 1);
    struct hda_bdl *bdl = phys_ptr(hda.bdl_page);
    assert(bdl[0].address == hda.data_page && bdl[0].length == 3000 && !bdl[0].flags);
    assert(bdl[1].address == hda.data_page + 3000 && bdl[1].length == HDA_SILENCE && bdl[1].flags == 1);
    assert(!memcmp(phys_ptr(hda.data_page), user_page + 64, 3000));
    struct nv_audio_volume *volume = (void *)(user_page + 48);
    assert(audio_ioctl(NV_AUDIO_GET_VOLUME, (uptr)volume) == 0 &&
           volume->percent == 100);
    volume->percent = 101;
    assert(audio_ioctl(NV_AUDIO_SET_VOLUME, (uptr)volume) == -NV_EINVAL);
    assert(audio_ioctl(NV_AUDIO_SET_VOLUME, 0) == -NV_EFAULT);
    volume->percent = 50;
    assert(audio_ioctl(NV_AUDIO_SET_VOLUME, (uptr)volume) == 0);
    short pcm[] = {32767, -32768, 100, -101};
    memcpy(user_page + 64, pcm, sizeof(pcm));
    request->bytes = sizeof(pcm);
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == (int)sizeof(pcm));
    short expected[] = {16383, -16384, 50, -50};
    assert(!memcmp(phys_ptr(hda.data_page), expected, sizeof(expected)));
    assert(!memcmp(user_page + 64, pcm, sizeof(pcm)));
    volume->percent = 0;
    assert(audio_ioctl(NV_AUDIO_SET_VOLUME, (uptr)volume) == 0);
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == (int)sizeof(pcm));
    short silence[4] = {0};
    assert(!memcmp(phys_ptr(hda.data_page), silence, sizeof(silence)));
    assert(audio_ioctl(NV_AUDIO_GET_VOLUME, (uptr)volume) == 0 &&
           volume->percent == 0);
    request->bytes = 3;
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == -NV_EINVAL);
    request->bytes = NV_AUDIO_MAX_WRITE + 4;
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == -NV_EINVAL);
    request->bytes = 4; request->pixels = 0;
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == -NV_EFAULT);
    request->pixels = (uptr)(user_page + 64);
    stalled = true;
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == -NV_EIO && hda.ready);
    stalled = false;
    assert(audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == 4);
    /* FMT 0x11 requires two channels. A mono DAC cannot advertise that
     * output; multichannel DACs support it, including three with Stereo=0. */
    dac_channel_count = 0;
    reset_device(); audio_init(); assert(!hda.ready && !next_page);
    for (u32 encoded = 2; encoded <= 15; encoded += 13) {
        dac_channel_count = encoded;
        reset_device(); audio_init(); assert(hda.ready);
    }
    dac_channel_count = 1;
    /* A DAC without Format Override inherits both PCM rates/bit depths and
     * stream formats from its own audio function group. Widget parameters
     * are zero in this model and must not be queried in that case. */
    format_override = false;
    reset_device(); audio_init();
    assert(hda.ready && !widget_format_queries && group_format_queries == 2);
    group_pcm &= ~(1u << 6);
    reset_device(); audio_init(); assert(!hda.ready && !next_page);
    group_pcm |= 1u << 6;
    group_pcm &= ~(1u << 17);
    reset_device(); audio_init(); assert(!hda.ready && !next_page);
    group_pcm |= 1u << 17;
    group_formats = 0;
    reset_device(); audio_init(); assert(!hda.ready && !next_page);
    /* Format Override set continues to honor the DAC rather than the AFG. */
    format_override = true;
    reset_device(); audio_init();
    assert(hda.ready && widget_format_queries == 2 && !group_format_queries);
    bad_format = true; group_formats = 1;
    reset_device(); audio_init(); assert(!hda.ready && !next_page);
    bad_format = false;
    for (u32 override = 0; override < 2; ++override)
        for (u32 parameter = 0x0a; parameter <= 0x0b; ++parameter) {
            format_override = override; failed_param = (u8)parameter;
            reset_device(); audio_init();
            assert(!hda.ready && !next_page);
        }
    failed_param = 0;
    /* Earlier groups and codecs advertise incompatible formats. Inherited
     * parameters must remain scoped to the eventual codec 1 / AFG 4 route. */
    format_override = false; multiple_groups = multiple_codecs = true;
    reset_device(); audio_init();
    assert(hda.ready && hda.codec == 1 && hda.group == 4 &&
           hda.pin == 6 && hda.dac == 5 && !widget_format_queries);
    multiple_groups = multiple_codecs = false; format_override = true;
    /* AFG amp parameters are inherited when the widget override bit is clear.
     * Widget parameter queries return zero in this model and must not occur. */
    amp_override = false;
    reset_device(); audio_init();
    assert(hda.ready && !widget_amp_queries && group_amp_queries == 3);
    assert((amp_control & 0xffffu) == (0xb000u | 42u));
    assert((pin_amp_control & 0xffffu) == (0xb000u | 42u));
    assert((input_amp_control & 0xffffu) == (0x7000u | 11u));
    /* StepSize zero is legal, including one fixed gain and the maximum
     * seven-bit offset. Direction/channels stay set; mute stays clear. */
    for (u32 override = 0; override < 2; ++override)
        for (u32 maximum = 0; maximum <= 127; maximum += 127) {
            amp_override = override;
            u32 caps = (1u << 31) | maximum << 8 | maximum;
            output_caps = input_caps = pin_output_caps = caps;
            group_output_caps = group_input_caps = caps;
            reset_device(); audio_init();
            assert(hda.ready);
            assert((amp_control & 0xffffu) == (0xb000u | maximum));
            assert((pin_amp_control & 0xffffu) == (0xb000u | maximum));
            assert((input_amp_control & 0xffffu) == (0x7000u | maximum));
        }
    amp_override = true;
    output_caps = (1u << 31) | (1u << 16) | (63u << 8) | 53u;
    input_caps = (1u << 31) | (1u << 16) | (31u << 8) | 17u;
    pin_output_caps = (1u << 31) | (2u << 16) | (31u << 8) | 29u;
    /* An offset outside NumSteps cannot describe 0 dB. Do not emit an
     * out-of-range gain or publish an unavailable route as ready. */
    output_caps += 11;
    reset_device(); audio_init();
    assert(!hda.ready && !next_page && !amp_control);
    output_caps -= 11;
    input_caps += 15;
    reset_device(); audio_init();
    assert(!hda.ready && !next_page && !input_amp_control && !pin_amp_control && !amp_control);
    input_caps -= 15;
    pin_output_caps += 3;
    reset_device(); audio_init();
    assert(!hda.ready && !next_page && !pin_amp_control && !amp_control);
    pin_output_caps -= 3;
    amp_override = false;
    group_output_caps = (63u << 8) | 64u;
    reset_device(); audio_init();
    assert(!hda.ready && !next_page && !widget_amp_queries && !pin_amp_control);
    /* The input amplifier index is four bits even for a longer connection
     * list. Unsupported index 16 must not accidentally program index zero. */
    amp_override = true; wide_connections = true;
    reset_device(); audio_init();
    assert(!hda.ready && !next_page && !input_amp_control && !pin_amp_control && !amp_control);
    wide_connections = false; amps_present = false;
    reset_device(); audio_init();
    assert(hda.ready && !widget_amp_queries && !group_amp_queries &&
           !amp_control && !input_amp_control && !pin_amp_control);
    amps_present = true;
    codec_present = false;
    reset_device(); audio_init();
    assert(!hda.ready && !next_page && audio_ioctl(NV_AUDIO_INFO, (uptr)info) == 0 &&
           !info->outputs && audio_ioctl(NV_AUDIO_WRITE64, (uptr)request) == -NV_ENODEV);
    codec_present = true; bad_format = true;
    reset_device(); audio_init();
    assert(!hda.ready && !next_page);
    assert(munmap(user_page, PAGE) == 0);
    puts("PASS audio: codec reset timing/IF, HDA route/channel count, DAC/AFG format inheritance, 0 dB amp offsets/bounds, immediate verbs, two DMA descriptors and absent device");
    return 0;
}
