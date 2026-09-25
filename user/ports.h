static void usb_hex16(u32 value) {
    const char *digits = "0123456789abcdef";
    char text[5];
    for (u32 n = 0; n < 4; ++n)
        text[n] = digits[(value >> (12 - n * 4)) & 15];
    text[4] = 0;
    print(text);
}
static const char *usb_kind(const struct nv_usb_device *d) {
    if (d->state == NV_USB_KEYBOARD)
        return "keyboard";
    if (d->class_code == 3)
        return d->subclass == 1 && d->protocol == 2 ? "mouse" : "HID";
    switch (d->class_code) {
    case 1: return "audio";
    case 2: return "communications";
    case 6: return "imaging";
    case 7: return "printer";
    case 8: return "mass-storage";
    case 9: return "hub";
    case 14: return "video";
    case 224: return "wireless";
    case 255: return "vendor-specific";
    default: return "device";
    }
}
static int show_ports(void) {
    u32 controllers = 0, count = 0;
    println("USB controllers");
    for (u32 i = 0; i < NV_USB_CONTROLLER_MAX; ++i) {
        struct nv_usb_controller c;
        int r = usb_controller(i, &c);
        if (r < 0)
            return r;
        if (!r)
            continue;
        ++controllers;
        print("  host "); print_u32(i);
        print(" PCI "); print_u32(c.bus); print(":"); print_u32(c.device);
        print("."); print_u32(c.function); print(" ");
        usb_hex16(c.vendor); print(":"); usb_hex16(c.product); print(" ");
        print(c.interface == 0x30 ? "xHCI" : c.interface == 0x20 ? "EHCI" :
              c.interface == 0x10 ? "OHCI" : c.interface == 0 ? "UHCI" : "unknown");
        print(" - ");
        println(c.state == NV_USB_RUNNING ? "running" :
                c.state == NV_USB_FAILED ? "failed" : "unsupported");
    }
    if (!controllers)
        println("  No PCI USB controller detected.");
    println("USB devices");
    static const char *const speed[] = {"unknown", "12 Mb/s", "1.5 Mb/s", "480 Mb/s",
                                        "5 Gb/s", "10 Gb/s"};
    for (u32 i = 0; i < NV_USB_DEVICE_MAX; ++i) {
        struct nv_usb_device d;
        int r = usb_device(i, &d);
        if (r < 0)
            return r;
        if (!r)
            continue;
        ++count;
        print("  device "); print_u32(i + 1); print(" host="); print_u32(d.controller);
        print(" parent="); print_u32(d.parent); print(" port="); print_u32(d.port);
        print(" address="); print_u32(d.address); print(" ");
        usb_hex16(d.vendor); print(":"); usb_hex16(d.product); print(" ");
        println(usb_kind(&d));
        print("    ");
        print(d.product_name[0] ? d.product_name : "No product string");
        print(" | "); println(d.speed < ARRAY_LEN(speed) ? speed[d.speed] : speed[0]);
        if (d.manufacturer[0]) { print("    maker: "); println(d.manufacturer); }
        if (d.serial[0]) { print("    serial: "); println(d.serial); }
        print("    class="); usb_hex16(d.class_code); print(" interfaces="); print_u32(d.interfaces);
        if (d.state == NV_USB_KEYBOARD) {
            print(" input=active reports="); print_u32(d.reports);
        } else if (d.state == NV_USB_ETHERNET)
            print(" Ethernet=active");
        else if (d.state == NV_USB_HUB)
            print(" hub=active");
        else
            print(" identification only");
        print("\n");
    }
    print_u32(controllers); print(" controller(s), "); print_u32(count); println(" device(s).");
    println("Use ports --scan to retry. Use ports --help for supported functions.");
    return 0;
}
