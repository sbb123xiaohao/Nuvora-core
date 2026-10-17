#ifndef NV_TERMINAL_H
#define NV_TERMINAL_H
#include <nv/types.h>

/* Bounded native text grids; independent of the legacy 80x25 surface ABI. */
#define NV_TERMINAL_MAX_CELLS 65536u
#define NV_TERMINAL_MAX_COLUMNS 1024u
#define NV_TERMINAL_MAX_ROWS 512u
struct nv_terminal_layout { u32 columns, rows, scale, padding, cell_w, cell_h; };
struct nv_terminal_view { const u16 *cells; u32 columns, rows, cursor, scale; };
static inline u32 nv_terminal_scale(u32 width, u32 height) {
    return width >= 3840 && height >= 2000 ? 4 :
           width >= 2880 && height >= 1620 ? 3 :
           width >= 1000 && height >= 700 ? 2 : 1;
}
static inline struct nv_terminal_layout nv_terminal_layout(u32 width, u32 height, u32 scale) {
    scale = MAX(1u, MIN(scale, 4u));
    u32 padding = MIN(6 * scale, MIN(width, height) / 4);
    u32 cell_w = 6 * scale, cell_h = scale == 1 ? 13 : 12 * scale;
    u32 columns = MAX(1u, MIN(NV_TERMINAL_MAX_COLUMNS, (width - 2 * padding) / cell_w));
    u32 rows = MAX(1u, MIN(NV_TERMINAL_MAX_ROWS, (height - 2 * padding) / cell_h));
    rows = MIN(rows, NV_TERMINAL_MAX_CELLS / columns);
    return (struct nv_terminal_layout){columns, rows, scale, padding, cell_w, cell_h};
}
#endif
