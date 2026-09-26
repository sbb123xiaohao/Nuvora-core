#ifndef NV_ATA_H
#define NV_ATA_H
#include <nv/types.h>

/* IDENTIFY word 106 bits 15:14 validate the sector-size report. Bit 12 then
 * means that each logical sector is larger than the 256 words supported by
 * Nuvora's current GPT/store and 512-byte DMA/PIO transfers. A 512e disk may
 * set bit 13 (multiple logical sectors per physical sector) and is safe. */
static inline bool nv_ata_sector_512(const u16 *id) {
    return (id[106] & 0xd000u) != 0x5000u;
}
#endif
