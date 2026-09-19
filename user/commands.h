/* One catalog drives both the complete command list and per-command help. */
struct command_help {
    const char *name, *syntax, *purpose, *example, *notes;
};
static const struct command_help commands[] = {
    {"help", "help [COMMAND]", "List all commands, or explain one command.", "help ports",
     "Every listed command accepts --help. atlas is an alias for help."},
    {"atlas", "atlas [COMMAND]", "List all commands, or explain one command.", "atlas folio",
     "Alias for help. Use COMMAND --help for syntax, purpose and an example."},
    {"origin", "origin", "Show the kernel version, architecture and ABI.", "origin", ""},
    {"silicon", "silicon", "Show CPU identity, enabled instruction state and compatibility limits.",
     "silicon",
     "One CPU is online. x87/MMX/SSE state is isolated between tasks.\nAVX/XSAVE are disabled; "
     "hardware features alone do not mean OS support."},
    {"firmament", "firmament",
     "Show ACPI root tables, PCIe ECAM coverage and the active PCI fallback.", "firmament",
     "Reports checksum-validated RSDT/XSDT and MCFG data.\n"
     "AML, sleep states, battery data and ACPI device methods are not implemented."},
    {"prism", "prism",
     "Show PCI/PCIe display adapters, NVIDIA identification and assigned BAR addresses.", "prism",
     "Read-only boot snapshot. No BAR sizing, GPU register writes or firmware loading.\nNVIDIA "
     "discovery and extended capability reporting are implemented; modesetting, 3D and CUDA are "
     "not implemented."},
    {"horizon", "horizon", "Show memory, processes, uptime and data-disk status.", "horizon", ""},
    {"ports", "ports [--scan]", "List USB controllers and connected USB devices.", "ports --scan",
     "--scan retries connected ports; automatic hotplug scans also run.\n"
     "xHCI: device descriptors, hubs and boot-keyboard input.\n"
     "Other classes are identified; USB disk files and mouse input are not implemented.\n"
     "UHCI/OHCI/EHCI controllers are listed as unsupported."},
    {"where", "where", "Show the current directory.", "where", ""},
    {"step", "step PATH", "Change the current directory.", "step /home",
     "Paths accept /, . and .. ."},
    {"glance", "glance [PATH]", "List directory entries and file sizes.", "glance /apps",
     "The default path is the current directory."},
    {"nest", "nest PATH", "Create a directory.", "nest /home/notes",
     "The parent must already exist."},
    {"weave", "weave FILE TEXT", "Create or replace a text file with one line.",
     "weave /home/note.txt \"Hello Nuvora\"",
     "Replaces existing contents. Use anchor to save /home to disk."},
    {"stitch", "stitch FILE TEXT", "Append a line to a text file.",
     "stitch /home/note.txt \"Second line\"",
     "Creates the file if needed. Use anchor to save /home to disk."},
    {"unfold", "unfold FILE", "Print a file's contents.", "unfold /home/note.txt", ""},
    {"folio", "folio [FILE]", "Open the full-screen text and formatted-document editor.",
     "folio /home/report.nvd",
     "Ctrl-S saves, Ctrl-Q closes, F1 shows shortcuts.\n"
     "Use .nvd for formatted documents or .txt for plain text.\n"
     "Folio saves /home to the Nuvora data disk when it is available."},
    {"mirror", "mirror FROM TO", "Copy a file to a new destination.",
     "mirror /home/note.txt /home/copy.txt", "The destination must not exist."},
    {"shift", "shift FROM TO", "Move or rename a file or directory.",
     "shift /home/copy.txt /home/draft.txt", "The destination must not exist."},
    {"prune", "prune PATH", "Delete a file or empty directory.", "prune /tmp/draft.txt",
     "Deletion has no undo. Use anchor to commit /home changes."},
    {"sparks", "sparks", "List processes, states, CPU ticks and page counts.", "sparks", ""},
    {"forge", "forge APP [ARGUMENTS]", "Run a program and wait for its exit status.",
     "forge pulse --help",
     "Names without / are resolved under /apps. Arguments are joined with spaces."},
    {"scatter", "scatter APP [ARGUMENTS]", "Start a program in the background and print its PID.",
     "scatter pulse", "Use gather PID to wait, or quench PID to terminate it."},
    {"gather", "gather PID", "Wait for one child process and show its exit status.", "gather 2",
     "The PID must identify an unwaited child of this shell."},
    {"quench", "quench PID", "Terminate a process by PID.", "quench 2",
     "PID 1 cannot be terminated."},
    {"tempo", "tempo", "Show monotonic timer ticks since boot.", "tempo",
     "There are 100 ticks per second."},
    {"doze", "doze MILLISECONDS", "Pause the shell for a specified duration.", "doze 500",
     "Valid range: 0 to 86400000 milliseconds; timer resolution is 10 ms."},
    {"anchor", "anchor", "Commit /home to the Nuvora data disk.", "anchor",
     "Requires the IDE Nuvora data image. /tmp is never persisted.\nUSB mass-storage devices are "
     "not mounted by this command."},
    {"trial", "trial", "Run the kernel's integration tests in a child process.", "trial",
     "Creates temporary files and test processes, including intentional faults."},
    {"scrub", "scrub", "Clear the VGA command screen.", "scrub", ""},
    {"rest", "rest", "Power off the virtual machine.", "rest",
     "Run anchor first to commit pending /home changes."},
    {"renew", "renew", "Reboot the virtual machine.", "renew",
     "Run anchor first to commit pending /home changes."}};
static const struct command_help *find_command(const char *name) {
    for (u32 i = 0; i < ARRAY_LEN(commands); ++i)
        if (!strcmp(name, commands[i].name))
            return &commands[i];
    return NULL;
}
static void explain_command(const struct command_help *entry) {
    print(entry->name);
    print(": ");
    println(entry->purpose);
    print("Usage: ");
    println(entry->syntax);
    print("Example: ");
    println(entry->example);
    if (*entry->notes)
        println(entry->notes);
    print("Help: ");
    print(entry->name);
    println(" --help");
}
static void help(void) {
    println("Loom commands (use NAME --help for usage and examples)");
    for (u32 i = 0; i < ARRAY_LEN(commands); ++i) {
        print("  ");
        print(commands[i].name);
        for (u32 n = strlen(commands[i].name); n < 8; ++n)
            print(" ");
        print(" --help  ");
        println(commands[i].purpose);
    }
    println("Paths accept /, . and ..; quote text containing spaces.");
    println("Only /home is saved by anchor. /tmp is discarded on reboot.");
}
