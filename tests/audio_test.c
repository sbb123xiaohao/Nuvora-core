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
static bool wide_connections;
static u32 widget_amp_queries, group_amp_queries;
static bool codec_present = true, bad_format, stalled;
static u8 *user_page;
static u32 read_reg(u32 offset, u32 bytes) {
    u32 value = 0;
    memcpy(&value, regs + offset, bytes);
    return value;
}
static u32 answer(u32 word) {
    u8 node = (u8)((word >> 20) & 0x7f);
    u32 verb = word & 0xfffff;
    if (node == 0 && verb == 0xf0004) return 0x00010001;
    if (node == 1 && verb == 0xf0005) return 1;
    if (node == 1 && verb == 0xf0004) return 0x00020002;
    if (node == 1 && verb == 0xf000d) { ++group_amp_queries; return group_input_caps; }
    if (node == 1 && verb == 0xf0012) { ++group_amp_queries; return group_output_caps; }
    if (node == 2 && verb == 0xf0009) return 1u | (amps_present ? 4u : 0u) |
        (amp_override ? 1u << 3 : 0u) | 1u << 4 | 1u << 10;
    if (node == 2 && verb == 0xf000a) return bad_format ? 0 : 1u << 6 | 1u << 17;
    if (node == 2 && verb == 0xf000b) return 1;
    if (node == 2 && verb == 0xf0012) { ++widget_amp_queries; return amp_override ? output_caps : 0; }
    if (node == 3 && verb == 0xf0009) return 4u << 20 | (amps_present ? 2u | 4u : 0u) |
        (amp_override ? 1u << 3 : 0u) | 1u << 8 | 1u << 10;
    if (node == 3 && verb == 0xf000d) { ++widget_amp_queries; return amp_override ? input_caps : 0; }
    if (node == 3 && verb == 0xf0012) { ++widget_amp_queries; return amp_override ? pin_output_caps : 0; }
    if (node == 3 && verb == 0xf000c) return 1u << 4 | 1u << 16;
    if (node == 3 && verb == 0xf1c00) return 0x01014010;
    if (node == 3 && verb == 0xf000e) return wide_connections ? 17 : 1;
    if (node == 3 && (verb & 0xfff00) == 0xf0200)
        return wide_connections ? ((verb & 255) == 16 ? 2 : 0) : 2;
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
    memcpy(regs + offset, &value, bytes);
    if (offset == 0x68 && bytes == 2) {
        if (value == 1) {
            u32 cmd = read_reg(0x60, 4), result = answer(cmd);
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
static void irq_enable(void) {}
static void irq_disable(void) {}
#include "../kernel/audio.c"

static void reset_device(void) {
    memset(regs, 0, sizeof(regs));
    regs[1] = 0x22; /* GCAP: two input and two output streams */
    regs[0x0e] = codec_present ? 1 : 0;
    next_page = submitted = played = pin_control = amp_control = pci_command = 0;
    input_amp_control = pin_amp_control = 0;
    widget_amp_queries = group_amp_queries = 0;
}
int main(void) {
    user_page = mmap(NULL, PAGE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(user_page != MAP_FAILED && (uptr)user_page > 0xffffffffu);
    reset_device();
    audio_init();
    assert(hda.ready && hda.codec == 0 && hda.pin == 3 && hda.dac == 2);
    assert((pci_command & 6) == 6 && submitted && pin_control == 0x40 &&
           (amp_control & 0x8000) && hda.stream == 0xc0);
    assert((amp_control & 0xffffu) == (0xb000u | 53u));
    assert((input_amp_control & 0xffffu) == (0x7000u | 17u));
    assert((pin_amp_control & 0xffffu) == (0xb000u | 29u));
    assert(widget_amp_queries == 3 && !group_amp_queries);
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
    puts("PASS audio: HDA codec route, 0 dB amp offsets/AFG inheritance/bounds, immediate verbs, two DMA descriptors and absent device");
    return 0;
}
