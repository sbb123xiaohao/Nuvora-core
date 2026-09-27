/* Check that a VMware/QEMU e1000e PCI identity selects the compatible wired
 * backend before the network stack starts DHCP/IPv4 work. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
struct task { u32 pid; void *pd; };
static struct task task_object;
static struct task *current = &task_object;
static u32 ticks;
static bool link_up = true;
static u8 sent[1514];
static void pci_visit(void (*cb)(u32, u32, u32)) {
    cb(0x00003000, (0x10d3u << 16) | 0x8086, 0x02000000);
}
static bool net_igc_start(u32 address, u8 mac[6]) {
    (void)address; (void)mac; return false;
}
static bool net_igc_link(void) { return false; }
static int net_igc_send(const void *p, u32 size) { (void)p; (void)size; return -NV_ENODEV; }
static void net_igc_poll(void (*receive)(const void *, u32)) { (void)receive; }
static bool net_e1000_start(u32 address, u8 mac[6]) {
    assert(address == 0x3000);
    const u8 assigned[6] = {2, 10, 20, 30, 40, 50};
    memcpy(mac, assigned, sizeof(assigned));
    return true;
}
static bool net_e1000_link(void) { return link_up; }
static int net_e1000_send(const void *p, u32 size) {
    assert(size <= sizeof(sent)); memcpy(sent, p, size); return 0;
}
static void net_e1000_poll(void (*receive)(const void *, u32)) { (void)receive; }
static bool usb_ecm_link(void) { return false; }
static int usb_ecm_send(const void *p, u32 size) { (void)p; (void)size; return -NV_ENODEV; }
static int usb_wifi_command(const char *p, u32 n) { (void)p; (void)n; return -NV_ENODEV; }
static int usb_wifi_read(char *p, u32 n) { (void)p; (void)n; return -NV_ENODEV; }
static bool user_range(void *pd, u32 address, u32 size, bool write) {
    (void)pd; (void)address; (void)size; (void)write; return false;
}
static void kprintf(const char *format, ...) { (void)format; }
#include "../kernel/net.c"

int main(void) {
    net_init();
    assert(count == 1 && active == 0 && wired == 0);
    assert(adapters[0].vendor == 0x8086 && adapters[0].product == 0x10d3);
    assert(!memcmp(adapters[0].mac, "\x02\x0a\x14\x1e\x28\x32", 6));
    net_poll();
    assert(adapters[0].state == NV_NET_LINK);
    link_up = false; net_poll();
    assert(adapters[0].state == NV_NET_DOWN && adapters[0].ip == 0);
    puts("PASS network e1000: VMware/QEMU e1000e identity selects wired backend and link loss");
}
