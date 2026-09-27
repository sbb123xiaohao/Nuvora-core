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
/* IDENTIFY word 83 bit 13 advertises FLUSH CACHE EXT. LBA48 alone does
 * not imply support for opcode EA; bit 12 is the non-EXT FLUSH command. */
static inline bool nv_ata_flush_ext(const u16 *id) {
    return (id[83] & 0xc000u) == 0x4000u && (id[83] & (1u << 13));
}
#endif
