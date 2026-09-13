#include "document.h"
void doc_init(struct document *d) {
    memset(d, 0, sizeof(*d));
    d->anchor = DOC_NONE;
}
void doc_bounds(const struct document *d, u32 *a, u32 *b) {
    *a = *b = d->cursor;
    if (d->anchor != DOC_NONE) {
        *a = MIN(d->cursor, d->anchor);
        *b = MAX(d->cursor, d->anchor);
    }
}
static u32 para_start(const struct document *d, u32 p) {
    while (p && d->cell[p - 1].ch != '\n')
        --p;
    return p;
}
void doc_normalize(struct document *d) {
    u8 para = d->cell[0].para;
    for (u32 i = 0; i <= d->len; ++i) {
        if (i && d->cell[i - 1].ch == '\n')
            para = d->cell[i].para;
        d->cell[i].para = para;
    }
    d->cell[d->len].ch = 0;
}
int doc_insert(struct document *d, const struct doc_cell *cells, u32 n) {
    u32 a, b;
    doc_bounds(d, &a, &b);
    if (n > DOC_CAP - (d->len - (b - a)))
        return -NV_E2BIG;
    memmove(d->cell + a + n, d->cell + b, (d->len - b + 1) * sizeof(*cells));
    if (n)
        memcpy(d->cell + a, cells, n * sizeof(*cells));
    d->len = d->len - (b - a) + n;
    d->cursor = a + n;
    d->anchor = DOC_NONE;
    doc_normalize(d);
    return 0;
}
int doc_type(struct document *d, u8 c) {
    if (c != '\n' && (c < 32 || c > 126))
        return -NV_EINVAL;
    u8 para = d->cell[d->cursor].para;
    struct doc_cell cell = {c, d->typing, para};
    int r = doc_insert(d, &cell, 1);
    if (!r && c == '\n') {
        /* A heading is followed by body text; manual page breaks never repeat. */
        u8 next = para & ~DOC_BREAK;
        if ((para & DOC_STYLE) == DOC_H1 || (para & DOC_STYLE) == DOC_H2)
            next &= ~DOC_STYLE;
        d->cell[d->cursor].para = next;
        d->typing = 0;
        doc_normalize(d);
    }
    return r;
}
void doc_delete(struct document *d, bool back) {
    u32 a, b;
    doc_bounds(d, &a, &b);
    if (a == b) {
        if (back && a)
            --a;
        else if (!back && b < d->len)
            ++b;
        else
            return;
        d->anchor = a;
        d->cursor = b;
    }
    doc_insert(d, NULL, 0);
}
void doc_font(struct document *d, u8 flag) {
    u32 a, b;
    doc_bounds(d, &a, &b);
    if (a == b) {
        d->typing ^= flag;
        return;
    }
    bool all = true;
    for (u32 i = a; i < b; ++i)
        if (!(d->cell[i].font & flag))
            all = false;
    for (u32 i = a; i < b; ++i) {
        if (all)
            d->cell[i].font &= ~flag;
        else
            d->cell[i].font |= flag;
    }
}
void doc_paragraph(struct document *d, u8 mask, u8 value) {
    u32 a, b;
    doc_bounds(d, &a, &b);
    if (a != b)
        --b;
    a = para_start(d, a);
    for (u32 p = a; p <= b && p <= d->len;) {
        d->cell[p].para = (d->cell[p].para & ~mask) | value;
        while (p < d->len && d->cell[p].ch != '\n')
            ++p;
        if (p == d->len)
            break;
        ++p;
    }
    doc_normalize(d);
}
static bool matches(const struct document *d, u32 at, const char *s, u32 n) {
    if (n > d->len - at)
        return false;
    for (u32 i = 0; i < n; ++i)
        if (d->cell[at + i].ch != (u8)s[i])
            return false;
    return true;
}
int doc_find(struct document *d, const char *s, bool next) {
    u32 n = strlen(s), a, b;
    doc_bounds(d, &a, &b);
    if (!n || n > d->len)
        return -NV_ENOENT;
    u32 start = next ? b : d->cursor;
    for (u32 i = start; i + n <= d->len; ++i)
        if (matches(d, i, s, n)) {
            d->anchor = i;
            d->cursor = i + n;
            return (int)i;
        }
    for (u32 i = 0; i < start && i + n <= d->len; ++i)
        if (matches(d, i, s, n)) {
            d->anchor = i;
            d->cursor = i + n;
            return (int)i;
        }
    return -NV_ENOENT;
}
int doc_replace_all(struct document *d, const char *from, const char *to) {
    u32 n = strlen(from), m = strlen(to), count = 0;
    if (!n || m > 191)
        return -NV_EINVAL;
    for (u32 i = 0; i + n <= d->len;) {
        if (matches(d, i, from, n)) {
            ++count;
            i += n;
        } else
            ++i;
    }
    if (m > n && count > (DOC_CAP - d->len) / (m - n))
        return -NV_E2BIG;
    struct doc_cell replacement[192];
    for (u32 i = 0; i + n <= d->len;) {
        if (!matches(d, i, from, n)) {
            ++i;
            continue;
        }
        for (u32 j = 0; j < m; ++j)
            replacement[j] = (struct doc_cell){(u8)to[j], d->cell[i].font, d->cell[i].para};
        d->anchor = i;
        d->cursor = i + n;
        doc_insert(d, replacement, m);
        i += m;
    }
    return (int)count;
}
u32 doc_hash(const struct document *d) {
    return crc32(d->cell, (d->len + 1) * sizeof(d->cell[0]));
}
bool doc_formatted(const struct document *d) {
    for (u32 i = 0; i <= d->len; ++i)
        if (d->cell[i].font || d->cell[i].para)
            return true;
    return false;
}
int doc_decode(struct document *d, const u8 *buf, u32 len, bool native) {
    /* Caller decodes into a spare document, preserving the open document on error. */
    doc_init(d);
    if (native) {
        u32 count, checksum;
        if (len < 19 || memcmp(buf, "NVFOLIO1", 8))
            return -NV_EIO;
        memcpy(&count, buf + 8, 4);
        memcpy(&checksum, buf + 12, 4);
        if (count > DOC_CAP || len != 16 + (count + 1) * 3 || crc32(buf + 16, len - 16) != checksum)
            return -NV_EIO;
        memcpy(d->cell, buf + 16, len - 16);
        d->len = count;
        for (u32 i = 0; i <= count; ++i) {
            struct doc_cell c = d->cell[i];
            if (c.font > 7 || c.para > 127 ||
                (i == count ? c.ch != 0 : (c.ch != '\n' && (c.ch < 32 || c.ch > 126))))
                return -NV_EIO;
            if (i && d->cell[i - 1].ch != '\n' && c.para != d->cell[i - 1].para)
                return -NV_EIO;
        }
        return 0;
    }
    for (u32 i = 0; i < len; ++i) {
        u8 c = buf[i];
        if (c == '\r') {
            if (i + 1 < len && buf[i + 1] == '\n')
                ++i;
            c = '\n';
        }
        if (c != '\n' && c != '\t' && (c < 32 || c > 126))
            return -NV_EINVAL;
        u32 count = c == '\t' ? 4 : 1;
        if (count > DOC_CAP - d->len)
            return -NV_E2BIG;
        while (count--)
            d->cell[d->len++].ch = c == '\t' ? ' ' : c;
    }
    return 0;
}
int doc_encode(const struct document *d, u8 *out, u32 cap, bool native) {
    if (!native) {
        if (d->len > cap)
            return -NV_E2BIG;
        for (u32 i = 0; i < d->len; ++i)
            out[i] = d->cell[i].ch;
        return (int)d->len;
    }
    u32 len = 16 + (d->len + 1) * 3, crc = doc_hash(d);
    if (len > cap)
        return -NV_E2BIG;
    memcpy(out, "NVFOLIO1", 8);
    memcpy(out + 8, &d->len, 4);
    memcpy(out + 12, &crc, 4);
    memcpy(out + 16, d->cell, len - 16);
    return (int)len;
}
struct writer {
    u8 *out;
    u32 len, cap;
    bool bad;
};
static void put(struct writer *w, const char *s) {
    u32 n = strlen(s);
    if (n > w->cap - w->len) {
        w->bad = true;
        return;
    }
    memcpy(w->out + w->len, s, n);
    w->len += n;
}
static void numeric(struct writer *w, u32 n) {
    char b[32];
    number(b, n, 10);
    put(w, b);
}
int doc_rtf(const struct document *d, u8 *out, u32 cap) {
    struct writer w = {out, 0, cap, false};
    put(&w, "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fmodern Courier New;}}\n"
            "{\\stylesheet{\\s0\\fs24 Normal;}{\\s1\\sb240\\sa120\\b\\fs40 Heading 1;}"
            "{\\s2\\sb180\\sa100\\b\\fs32 Heading 2;}{\\s3\\li360\\i\\fs24 Quote;}}\n"
            "\\paperw11906\\paperh16838\\margl1440\\margr1440\\margt1440\\margb1440\n"
            "{\\footer\\pard\\qc\\plain\\f0\\fs20 {\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}}"
            "\\widowctrl\n");
    bool paragraph = true;
    u32 last_font = 0xffffffff;
    for (u32 i = 0; i <= d->len; ++i) {
        u8 para = d->cell[i].para, font = d->cell[i].font, ch = d->cell[i].ch;
        if (paragraph) {
            u32 style = (para & DOC_STYLE) >> 2;
            put(&w, "\\pard\\plain\\f0\\s");
            numeric(&w, style);
            put(&w, style == 1   ? "\\fs40\\sb240\\sa120\\keepn"
                    : style == 2 ? "\\fs32\\sb180\\sa100\\keepn"
                                 : "\\fs24\\sa120");
            if (style == 3)
                put(&w, "\\li360\\ri360");
            static const char *const aligns[] = {"\\ql", "\\qc", "\\qr", "\\qj"};
            put(&w, aligns[para & DOC_ALIGN]);
            put(&w, (para & DOC_DOUBLE) ? "\\sl480\\slmult1" : "\\sl240\\slmult1");
            if ((para & DOC_BREAK) && i)
                put(&w, "\\pagebb");
            if (para & DOC_BULLET)
                put(&w, "\\li360\\fi-360\\tx360");
            put(&w, " ");
            if (para & DOC_BULLET)
                put(&w, "\\bullet\\tab ");
            paragraph = false;
            last_font = 0xffffffff;
        }
        if (i == d->len)
            break;
        u32 style = para & DOC_STYLE;
        if (style == DOC_H1 || style == DOC_H2)
            font |= DOC_BOLD;
        if (style == DOC_QUOTE)
            font |= DOC_ITALIC;
        if (font != last_font) {
            put(&w, (font & DOC_BOLD) ? "\\b" : "\\b0");
            put(&w, (font & DOC_ITALIC) ? "\\i" : "\\i0");
            put(&w, (font & DOC_UNDERLINE) ? "\\ul" : "\\ul0");
            put(&w, " ");
            last_font = font;
        }
        if (ch == '\n') {
            put(&w, "\\par\n");
            paragraph = true;
        } else {
            if (ch == '\\' || ch == '{' || ch == '}')
                put(&w, "\\");
            char c[2] = {(char)ch, 0};
            put(&w, c);
        }
    }
    put(&w, "}\n");
    return w.bad ? -NV_E2BIG : (int)w.len;
}
static u32 spaces(const struct document *d, const struct doc_line *l) {
    u32 n = 0;
    for (u32 i = l->first; i < l->end; ++i)
        if (d->cell[i].ch == ' ')
            ++n;
    return n;
}
u32 doc_column(const struct document *d, const struct doc_line *l, u32 pos) {
    u32 x = l->x, n = spaces(d, l), extra = l->width - (l->end - l->first), seen = 0;
    for (u32 i = l->first; i < MIN(pos, l->end); ++i) {
        ++x;
        if (l->justified && n && d->cell[i].ch == ' ') {
            x += extra / n + (seen < extra % n);
            ++seen;
        }
    }
    return x;
}
u32 doc_hit(const struct document *d, const struct doc_line *l, u32 x) {
    u32 best = l->first, distance = 0xffffffff;
    for (u32 i = l->first; i <= l->end; ++i) {
        u32 col = doc_column(d, l, i), dist = col > x ? col - x : x - col;
        if (dist < distance) {
            distance = dist;
            best = i;
        }
    }
    return best;
}
void doc_layout(const struct document *d, struct doc_layout *layout) {
    u32 p = 0, v = 0;
    layout->count = 0;
    for (;;) {
        u32 end = p;
        while (end < d->len && d->cell[end].ch != '\n')
            ++end;
        u8 para = d->cell[p].para;
        if ((para & DOC_BREAK) && p && v % DOC_PAGE_ROWS)
            v += DOC_PAGE_ROWS - v % DOC_PAGE_ROWS;
        u32 inset = (para & DOC_STYLE) == DOC_QUOTE ? 4 : 0;
        u32 width = DOC_WIDTH - inset * 2 - ((para & DOC_BULLET) ? 2 : 0);
        bool first = true;
        do {
            u32 q = MIN(end, p + width);
            if (q < end) {
                u32 word = q;
                while (word > p && d->cell[word - 1].ch != ' ')
                    --word;
                if (word > p)
                    q = word;
            }
            struct doc_line *l = &layout->line[layout->count++];
            *l = (struct doc_line){p,
                                   q,
                                   (v / DOC_PAGE_ROWS) * DOC_PAGE_SPAN + 1 + v % DOC_PAGE_ROWS,
                                   (u16)(inset + ((para & DOC_BULLET) ? 2 : 0)),
                                   (u16)width,
                                   para,
                                   !first,
                                   0};
            u32 used = q - p;
            if ((para & DOC_ALIGN) == DOC_CENTER)
                l->x += (width - used) / 2;
            if ((para & DOC_ALIGN) == DOC_RIGHT)
                l->x += width - used;
            if ((para & DOC_ALIGN) == DOC_JUSTIFY && q < end && spaces(d, l))
                l->justified = 1;
            if (!l->justified)
                l->width = used;
            ++v;
            if (para & DOC_DOUBLE)
                ++v;
            first = false;
            p = q;
        } while (p < end);
        if (end == d->len)
            break;
        ++v; /* paragraph spacing */
        p = end + 1;
    }
    layout->pages = (layout->line[layout->count - 1].visual / DOC_PAGE_SPAN) + 1;
}
u32 doc_line_at(const struct document *d, const struct doc_layout *layout, u32 pos) {
    (void)d;
    for (u32 i = 0; i < layout->count; ++i) {
        const struct doc_line *l = &layout->line[i];
        if (pos < l->end ||
            (pos == l->end && (i + 1 == layout->count || layout->line[i + 1].first != pos)))
            return i;
    }
    return layout->count - 1;
}
