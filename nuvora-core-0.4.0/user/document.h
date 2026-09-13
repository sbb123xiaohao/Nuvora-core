#ifndef NV_DOCUMENT_H
#define NV_DOCUMENT_H
#include <nv/abi.h>
#include <nv/string.h>
#define DOC_CAP 32768u
#define DOC_NONE 0xffffffffu
#define DOC_WIDTH 64u
#define DOC_PAGE_ROWS 48u
#define DOC_PAGE_SPAN (DOC_PAGE_ROWS + 3u)
enum { DOC_BOLD = 1, DOC_ITALIC = 2, DOC_UNDERLINE = 4 };
enum {
    DOC_LEFT = 0,
    DOC_CENTER = 1,
    DOC_RIGHT = 2,
    DOC_JUSTIFY = 3,
    DOC_ALIGN = 3,
    DOC_BODY = 0,
    DOC_H1 = 4,
    DOC_H2 = 8,
    DOC_QUOTE = 12,
    DOC_STYLE = 12,
    DOC_BULLET = 16,
    DOC_BREAK = 32,
    DOC_DOUBLE = 64
};
struct doc_cell {
    u8 ch, font, para;
};
_Static_assert(sizeof(struct doc_cell) == 3, "document format");
struct document {
    u32 len, cursor, anchor;
    u8 typing;
    struct doc_cell cell[DOC_CAP + 1];
};
struct doc_line {
    u32 first, end, visual;
    u16 x, width;
    u8 para, continued, justified;
};
struct doc_layout {
    u32 count, pages;
    struct doc_line line[DOC_CAP + 1];
};
void doc_init(struct document *);
void doc_bounds(const struct document *, u32 *, u32 *);
void doc_normalize(struct document *);
int doc_insert(struct document *, const struct doc_cell *, u32);
int doc_type(struct document *, u8);
void doc_delete(struct document *, bool);
void doc_font(struct document *, u8);
void doc_paragraph(struct document *, u8, u8);
int doc_find(struct document *, const char *, bool);
int doc_replace_all(struct document *, const char *, const char *);
u32 doc_hash(const struct document *);
bool doc_formatted(const struct document *);
int doc_decode(struct document *, const u8 *, u32, bool);
int doc_encode(const struct document *, u8 *, u32, bool);
int doc_rtf(const struct document *, u8 *, u32);
void doc_layout(const struct document *, struct doc_layout *);
u32 doc_line_at(const struct document *, const struct doc_layout *, u32);
u32 doc_column(const struct document *, const struct doc_line *, u32);
u32 doc_hit(const struct document *, const struct doc_line *, u32);
#endif
