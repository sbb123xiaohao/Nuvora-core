#ifndef NV_PCI_H
#define NV_PCI_H
#include <nv/abi.h>
typedef u32 (*nv_pci_read)(u32 address, u32 offset);
/* Pure read-only decoder, also used by synthetic configuration-space tests. */
int pci_decode_display(u32 address, nv_pci_read read, struct nv_gpu_info *out);
#endif
