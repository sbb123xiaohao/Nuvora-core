#include "runtime.h"
#include "document.h"
#define UNDO_DEPTH 12u
#define VIEW_TOP 4u
#define VIEW_ROWS 18u
static struct document doc, spare, history[UNDO_DEPTH];
static struct doc_cell clipboard[DOC_CAP];
static struct doc_layout layout;
static struct nv_surface screen;
static u8 io_buffer[NV_FILE_MAX];
static u32 clip_len, hist_at, hist_count, saved_hash, top, goal = DOC_NONE;
static bool grouped, preview, new_file, disk_pending;
static char path[NV_PATH_MAX], message[160], query[192];
static const char *const styles[] = {"Body", "Heading 1", "Heading 2", "Quote"};
static const char *const aligns[] = {"Left", "Center", "Right", "Justify"};
static bool dirty(void) {
    return doc_hash(&doc) != saved_hash;
}
static void note(const char *s) {
    strlcpy(message, s, sizeof(message));
}
static void failure(const char *s, int r) {
    strlcpy(message, s, sizeof(message));
    u32 n = strlen(message);
    strlcpy(message + n, ": ", sizeof(message) - n);
    n = strlen(message);
    strlcpy(message + n, error_name(r), sizeof(message) - n);
}
static void text_at(u32 y, u32 x, const char *s, u8 color) {
    while (*s && x < 80 && y < 25)
        screen.cells[y * 80 + x++] = ((u16)color << 8) | (u8)*s++;
}
static u32 number_at(u32 y, u32 x, u32 n, u8 color) {
    char b[32];
    u32 count = number(b, n, 10);
    text_at(y, x, b, color);
    return x + count;
}
static void fill(u32 y, u32 x, u32 h, u32 w, u8 color) {
    for (u32 r = y; r < MIN(y + h, 25u); ++r)
        for (u32 c = x; c < MIN(x + w, 80u); ++c)
            screen.cells[r * 80 + c] = ((u16)color << 8) | ' ';
}
static void show(void) {
    surface(NV_SCREEN_PRESENT, &screen);
}
static void redraw(void) {
    doc_layout(&doc, &layout);
    u32 caret = doc_line_at(&doc, &layout, doc.cursor), visual = layout.line[caret].visual;
    if (!preview) {
        if (visual < top)
            top = visual;
        if (visual >= top + VIEW_ROWS)
            top = visual - VIEW_ROWS + 1;
    }
    fill(0, 0, 25, 80, 0x17);
    fill(0, 0, 1, 80, 0x1f);
    text_at(0, 1, "Folio", 0x1f);
    text_at(0, 8, *path ? path : "Untitled.nvd", 0x1f);
    if (dirty())
        text_at(0, 70, "Modified", 0x1e);
    else if (disk_pending)
        text_at(0, 71, "RAM only", 0x1e);
    text_at(1, 1, "F2 Save  F3 Open  F4 Save as  F5 Preview  F6 Export RTF  F1 Help", 0x1f);
    u8 para = doc.cell[doc.cursor].para;
    text_at(2, 1, styles[(para & DOC_STYLE) >> 2], 0x1f);
    text_at(2, 12, aligns[para & DOC_ALIGN], 0x1f);
    u32 a, b;
    doc_bounds(&doc, &a, &b);
    u8 font = a != b ? doc.cell[a].font : doc.typing;
    text_at(2, 21, (font & DOC_BOLD) ? "B:on " : "B:off", 0x1f);
    text_at(2, 28, (font & DOC_ITALIC) ? "I:on " : "I:off", 0x1f);
    text_at(2, 35, (font & DOC_UNDERLINE) ? "U:on " : "U:off", 0x1f);
    text_at(2, 43, (para & DOC_BULLET) ? "List:on" : "List:off", 0x1f);
    text_at(2, 53, (para & DOC_DOUBLE) ? "Space:2" : "Space:1", 0x1f);
    text_at(2, 63, preview ? "PREVIEW" : "EDIT", 0x1e);
    text_at(3, 8, "1       9       17      25      33      41      49      57", 0x17);
    for (u32 y = VIEW_TOP; y < VIEW_TOP + VIEW_ROWS; ++y) {
        u32 v = top + y - VIEW_TOP, offset = v % DOC_PAGE_SPAN, page = v / DOC_PAGE_SPAN;
        if (page >= layout.pages)
            continue;
        if (offset <= DOC_PAGE_ROWS)
            fill(y, 5, 1, 70, 0x70);
        if (!offset) {
            text_at(y, 8, "Page ", 0x78);
            u32 x = number_at(y, 13, page + 1, 0x78);
            text_at(y, x, " / ", 0x78);
            number_at(y, x + 3, layout.pages, 0x78);
        }
    }
    for (u32 i = 0; i < layout.count; ++i) {
        const struct doc_line *l = &layout.line[i];
        if (l->visual < top || l->visual >= top + VIEW_ROWS)
            continue;
        u32 y = VIEW_TOP + l->visual - top;
        if ((l->para & DOC_BULLET) && !l->continued)
            text_at(y, 8 + l->x - 2, "*", 0x70);
        for (u32 p = l->first; p < l->end; ++p) {
            u8 color = 0x70, f = doc.cell[p].font;
            if ((l->para & DOC_STYLE) == DOC_H1 || (l->para & DOC_STYLE) == DOC_H2)
                f |= DOC_BOLD;
            if ((l->para & DOC_STYLE) == DOC_QUOTE)
                f |= DOC_ITALIC;
            if (f & DOC_ITALIC)
                color = 0x75;
            if (f & DOC_UNDERLINE)
                color = 0x74;
            if (f & DOC_BOLD)
                color = 0x71;
            if (p >= a && p < b)
                color = 0x1f;
            u32 x = 8 + doc_column(&doc, l, p);
            if (x < 72)
                screen.cells[y * 80 + x] = ((u16)color << 8) | doc.cell[p].ch;
        }
        if (l->end >= a && l->end < b && doc.cell[l->end].ch == '\n') {
            u32 x = 8 + doc_column(&doc, l, l->end);
            if (x < 73)
                text_at(y, x, " ", 0x1f);
        }
    }
    fill(22, 0, 1, 80, 0x70);
    text_at(22, 1, "Page ", 0x70);
    u32 x = number_at(22, 6, visual / DOC_PAGE_SPAN + 1, 0x70);
    text_at(22, x, "/", 0x70);
    x = number_at(22, x + 1, layout.pages, 0x70);
    text_at(22, x + 2, "Chars ", 0x70);
    x = number_at(22, x + 8, doc.len, 0x70);
    text_at(22, x + 2, "Selected ", 0x70);
    number_at(22, x + 11, b - a, 0x70);
    text_at(23, 1, message, 0x1f);
    text_at(24, 1,
            preview ? "PgUp/PgDn Page   Up/Down Scroll   F5 Return to editing"
                    : "Ctrl-S Save  Ctrl-Q Close  Ctrl-F Find  Ctrl-Z Undo  Shift+arrows Select",
            0x1f);
    screen.cursor = 2000;
    if (!preview && visual >= top && visual < top + VIEW_ROWS) {
        u32 cx = MIN(8 + doc_column(&doc, &layout.line[caret], doc.cursor), 72u);
        screen.cursor = (VIEW_TOP + visual - top) * 80 + cx;
    }
    show();
}
static int wait_raw(void) {
    for (;;) {
        int k = key_event();
        if (k != -NV_EAGAIN)
            return k;
        nap(10);
    }
}
static int get_key(void) {
    int k = wait_raw();
    if (k < 0)
        return k;
    if (k & NV_KEY_DIRECT)
        return k & ~NV_KEY_DIRECT;
    if (k > 255)
        return k;
    if (k > 0 && k < 27 && k != '\n' && k != '\t' && k != '\b')
        return NV_KEY_CTRL + 'a' + k - 1;
    if (k == 127)
        return '\b';
    if (k != 27)
        return k;
    /* ANSI serial terminals, including modified cursor and function keys. */
    u32 deadline = clock_ticks() + 15;
    int c;
    do {
        c = key_event();
        if (c != -NV_EAGAIN)
            break;
        nap(10);
    } while ((i32)(clock_ticks() - deadline) < 0);
    if (c < 0)
        return 27;
    if (c != '[' && c != 'O')
        return (c & 4095) | NV_KEY_ALT;
    int prefix = c;
    u32 param = 0, mod = 0;
    bool second = false;
    for (u32 n = 0; n < 16; ++n) {
        c = key_event();
        if (c == -NV_EAGAIN) {
            if ((i32)(clock_ticks() - deadline) >= 0)
                return 27;
            nap(10);
            --n;
            continue;
        }
        if (c < 0 || c > 255)
            return 27;
        if (c >= '0' && c <= '9') {
            if (second)
                mod = mod * 10 + c - '0';
            else
                param = param * 10 + c - '0';
            continue;
        }
        if (c == ';') {
            second = true;
            continue;
        }
        u32 key = 0, flags = 0;
        if (mod >= 2 && mod <= 8) {
            u32 m = mod - 1;
            if (m & 1)
                flags |= NV_KEY_SHIFT;
            if (m & 2)
                flags |= NV_KEY_ALT;
            if (m & 4)
                flags |= NV_KEY_CTRL;
        }
        if (c == 'A')
            key = NV_KEY_UP;
        else if (c == 'B')
            key = NV_KEY_DOWN;
        else if (c == 'C')
            key = NV_KEY_RIGHT;
        else if (c == 'D')
            key = NV_KEY_LEFT;
        else if (c == 'H')
            key = NV_KEY_HOME;
        else if (c == 'F')
            key = NV_KEY_END;
        else if (prefix == 'O' && c >= 'P' && c <= 'S')
            key = NV_KEY_F1 + c - 'P';
        else if (c == '~') {
            switch (param) {
            case 1:
            case 7:
                key = NV_KEY_HOME;
                break;
            case 4:
            case 8:
                key = NV_KEY_END;
                break;
            case 2:
                key = NV_KEY_INSERT;
                break;
            case 3:
                key = NV_KEY_DELETE;
                break;
            case 5:
                key = NV_KEY_PGUP;
                break;
            case 6:
                key = NV_KEY_PGDN;
                break;
            case 11:
                key = NV_KEY_F1;
                break;
            case 12:
                key = NV_KEY_F2;
                break;
            case 13:
                key = NV_KEY_F3;
                break;
            case 14:
                key = NV_KEY_F4;
                break;
            case 15:
                key = NV_KEY_F5;
                break;
            case 17:
                key = NV_KEY_F6;
                break;
            case 18:
                key = NV_KEY_F7;
                break;
            case 19:
                key = NV_KEY_F8;
                break;
            case 20:
                key = NV_KEY_F9;
                break;
            case 21:
                key = NV_KEY_F10;
                break;
            case 23:
                key = NV_KEY_F11;
                break;
            case 24:
                key = NV_KEY_F12;
                break;
            }
        }
        return key ? (int)(key | flags) : 0;
    }
    return 0;
}
static int prompt(const char *label, char *out, u32 cap, bool empty) {
    u32 len = strlen(out), pos = len;
    bool selected = len != 0;
    for (;;) {
        redraw();
        fill(8, 4, 7, 72, 0x70);
        text_at(9, 6, label, 0x71);
        fill(11, 6, 1, 68, selected ? 0x1f : 0x07);
        u32 off = pos > 65 ? pos - 65 : 0;
        text_at(11, 6, out + off, selected ? 0x1f : 0x07);
        text_at(13, 6, "Enter: accept   Esc: cancel", 0x70);
        screen.cursor = 11 * 80 + 6 + pos - off;
        show();
        int k = get_key();
        u32 c = (u32)k & 4095;
        if (k == 27)
            return 0;
        if (c == '\n') {
            if (len || empty)
                return 1;
            continue;
        }
        if (c == NV_KEY_LEFT || c == NV_KEY_RIGHT || c == NV_KEY_HOME || c == NV_KEY_END) {
            selected = false;
            if (c == NV_KEY_LEFT && pos)
                --pos;
            if (c == NV_KEY_RIGHT && pos < len)
                ++pos;
            if (c == NV_KEY_HOME)
                pos = 0;
            if (c == NV_KEY_END)
                pos = len;
            continue;
        }
        if (k & NV_KEY_CTRL) {
            if (c == 'a')
                selected = true;
            continue;
        }
        if (c == '\b' || c == NV_KEY_DELETE) {
            if (selected) {
                len = pos = 0;
                out[0] = 0;
                selected = false;
            } else if (c == '\b' && pos) {
                memmove(out + pos - 1, out + pos, len - pos + 1);
                --pos;
                --len;
            } else if (c == NV_KEY_DELETE && pos < len) {
                memmove(out + pos, out + pos + 1, len - pos);
                --len;
            }
        } else if (c >= 32 && c < 127 && !(k & NV_KEY_ALT)) {
            if (selected) {
                len = pos = 0;
                out[0] = 0;
                selected = false;
            }
            if (len + 1 < cap) {
                memmove(out + pos + 1, out + pos, len - pos + 1);
                out[pos++] = (char)c;
                ++len;
            }
        }
    }
}
static bool confirm_overwrite(const char *name) {
    redraw();
    fill(8, 3, 8, 74, 0x70);
    text_at(9, 5, "Replace the existing file?", 0x71);
    text_at(10, 5, name, 0x70);
    text_at(12, 5, "Its previous contents will be replaced after a complete write.", 0x70);
    text_at(14, 5, "R: replace file   Esc: cancel", 0x70);
    screen.cursor = 2000;
    show();
    for (;;) {
        int k = get_key();
        if (k == 'r' || k == 'R')
            return true;
        if (k == 27)
            return false;
    }
}
static void checkpoint(bool typing) {
    if (typing && grouped)
        return;
    if (hist_at == UNDO_DEPTH) {
        memmove(history, history + 1, (UNDO_DEPTH - 1) * sizeof(doc));
        --hist_at;
    }
    memcpy(&history[hist_at++], &doc, sizeof(doc));
    hist_count = hist_at;
    grouped = typing;
}
static void undo(bool redo) {
    grouped = false;
    if ((redo && hist_at == hist_count) || (!redo && !hist_at)) {
        note(redo ? "Nothing to redo." : "Nothing to undo.");
        return;
    }
    u32 slot = redo ? hist_at++ : --hist_at;
    memcpy(&spare, &doc, sizeof(doc));
    memcpy(&doc, &history[slot], sizeof(doc));
    memcpy(&history[slot], &spare, sizeof(doc));
    goal = DOC_NONE;
    note(redo ? "Redone." : "Undone.");
}
static bool suffix(const char *name, const char *ext) {
    u32 n = strlen(name), m = strlen(ext);
    return n >= m && !strcmp(name + n - m, ext);
}
static void with_extension(char *dest, const char *source, const char *ext) {
    strlcpy(dest, *source ? source : "/home/Untitled.nvd", NV_PATH_MAX);
    u32 n = strlen(dest), dot = n;
    while (dot && dest[dot - 1] != '/' && dest[dot - 1] != '.')
        --dot;
    if (dot && dest[dot - 1] == '.')
        n = dot - 1;
    if (n + strlen(ext) < NV_PATH_MAX)
        strlcpy(dest + n, ext, NV_PATH_MAX - n);
}
static int read_document(const char *name) {
    int fd = open_file(name, NV_READ);
    if (fd < 0)
        return fd;
    int size = seek_file(fd, 0, 2);
    seek_file(fd, 0, 0);
    int result = 0;
    if (size < 0 || (u32)size > sizeof(io_buffer))
        result = -NV_E2BIG;
    else {
        u32 pos = 0;
        while (pos < (u32)size) {
            int n = take(fd, io_buffer + pos, MIN(16384u, (u32)size - pos));
            if (n <= 0) {
                result = n < 0 ? n : -NV_EIO;
                break;
            }
            pos += (u32)n;
        }
    }
    close_file(fd);
    if (result < 0)
        return result;
    if (size >= 5 && !memcmp(io_buffer, "{\\rtf", 5))
        return -NV_EINVAL;
    bool native = suffix(name, ".nvd") || (size >= 8 && !memcmp(io_buffer, "NVFOLIO1", 8));
    result = doc_decode(&spare, io_buffer, (u32)size, native);
    if (result < 0)
        return result;
    memcpy(&doc, &spare, sizeof(doc));
    strlcpy(path, name, sizeof(path));
    saved_hash = doc_hash(&doc);
    hist_at = hist_count = 0;
    top = 0;
    goal = DOC_NONE;
    grouped = preview = new_file = disk_pending = false;
    note("Opened. F1 lists editing and formatting shortcuts.");
    return 0;
}
static int atomic_write(const char *name, const u8 *data, u32 len) {
    char temp[NV_PATH_MAX], digits[32];
    u32 p = strlen(name);
    while (p && name[p - 1] != '/')
        --p;
    if (p + 29 >= sizeof(temp))
        return -NV_E2BIG;
    memcpy(temp, name, p);
    strlcpy(temp + p, ".folio-", sizeof(temp) - p);
    u32 end = p + 7;
    number(digits, clock_ticks(), 16);
    strlcpy(temp + end, digits, sizeof(temp) - end);
    end = strlen(temp);
    temp[end++] = '-';
    int fd = -NV_EEXIST;
    for (u32 i = 0; i < 32 && fd == -NV_EEXIST; ++i) {
        number(temp + end, i, 10);
        fd = open_file(temp, NV_WRITE | NV_CREATE | NV_EXCL);
    }
    if (fd < 0)
        return fd;
    u32 pos = 0;
    int result = 0;
    while (pos < len) {
        int n = emit(fd, data + pos, MIN(16384u, len - pos));
        if (n <= 0) {
            result = n < 0 ? n : -NV_EIO;
            break;
        }
        pos += (u32)n;
    }
    close_file(fd);
    if (!result)
        result = replace_file(temp, name);
    if (result < 0)
        remove_path(temp);
    return result;
}
static bool persistent_path(const char *name) {
    /* Resolve . and .. in user space for an honest /home persistence message. */
    char full[NV_PATH_MAX * 2], part[NV_PATH_MAX];
    u32 n = 0;
    if (*name != '/') {
        if (getcwd_path(full, sizeof(part)) < 0)
            return false;
        n = strlen(full);
        full[n++] = '/';
    }
    strlcpy(full + n, name, sizeof(full) - n);
    u32 len = 0, p = 0;
    while (full[p]) {
        while (full[p] == '/')
            ++p;
        u32 start = p;
        while (full[p] && full[p] != '/')
            ++p;
        u32 count = p - start;
        if (!count)
            break;
        if (count == 1 && full[start] == '.')
            continue;
        if (count == 2 && full[start] == '.' && full[start + 1] == '.') {
            while (len && part[--len] != '/') {
            }
            continue;
        }
        if (len + count + 1 >= sizeof(part))
            return false;
        part[len++] = '/';
        memcpy(part + len, full + start, count);
        len += count;
    }
    part[len] = 0;
    return !strncmp(part, "/home/", 6);
}
static bool save(bool as) {
    char dest[NV_PATH_MAX];
    strlcpy(dest, *path ? path : "/home/Untitled.nvd", sizeof(dest));
    bool need_name = as || new_file;
    if (doc_formatted(&doc) && !suffix(dest, ".nvd")) {
        with_extension(dest, dest, ".nvd");
        need_name = true;
    }
    if (need_name && !prompt("Save document (.nvd keeps formatting; .txt / .md are plain)", dest,
                             sizeof(dest), false))
        return false;
    bool native = suffix(dest, ".nvd");
    if (!native && !suffix(dest, ".txt") && !suffix(dest, ".md")) {
        note("Use .nvd for documents, or .txt / .md for plain text.");
        return false;
    }
    if (!native && doc_formatted(&doc)) {
        note("Formatting needs .nvd. Use F4 and a .nvd filename.");
        return false;
    }
    if (strcmp(dest, path) || new_file) {
        int fd = open_file(dest, NV_READ);
        if (fd >= 0) {
            close_file(fd);
            if (!confirm_overwrite(dest))
                return false;
        }
    }
    int n = doc_encode(&doc, io_buffer, sizeof(io_buffer), native);
    int r = n < 0 ? n : atomic_write(dest, io_buffer, (u32)n);
    if (r < 0) {
        failure("Save failed; document retained", r);
        return false;
    }
    strlcpy(path, dest, sizeof(path));
    new_file = false;
    saved_hash = doc_hash(&doc);
    r = control(NV_CTL_SYNC, 0);
    disk_pending = r < 0 || !persistent_path(path);
    if (!disk_pending)
        note("Saved to disk. Text and formatting will survive reboot.");
    else if (!persistent_path(path))
        note("Saved in RAM only. Use F4 to save under /home for persistence.");
    else
        failure("Saved in RAM; disk save failed. Ctrl-S retries", r);
    return !disk_pending;
}
static bool can_leave(void) {
    if (!dirty() && !disk_pending)
        return true;
    redraw();
    fill(8, 3, 9, 74, 0x70);
    text_at(9, 5,
            dirty() ? "Document has unsaved changes."
                    : "Document is saved in RAM, but has not been saved to disk.",
            0x71);
    text_at(11, 5, *path ? path : "Untitled.nvd", 0x70);
    text_at(13, 5,
            dirty() ? "D discards edits since the last save. This cannot be undone."
                    : "D closes the editor. RAM-only files are lost on reboot.",
            0x70);
    text_at(15, 5, "S: save and continue   D: discard / close   Esc: keep editing", 0x70);
    screen.cursor = 2000;
    show();
    for (;;) {
        int k = get_key();
        if (k == 27)
            return false;
        if (k == 'd' || k == 'D')
            return true;
        if (k == 's' || k == 'S')
            return save(false);
    }
}
static void export_rtf(void) {
    char dest[NV_PATH_MAX];
    with_extension(dest, path, ".rtf");
    if (!prompt("Export RTF (open the exported file in Word or LibreOffice)", dest, sizeof(dest),
                false))
        return;
    if (!suffix(dest, ".rtf")) {
        note("Use a .rtf filename for export.");
        return;
    }
    int fd = open_file(dest, NV_READ);
    if (fd >= 0) {
        close_file(fd);
        if (!confirm_overwrite(dest))
            return;
    }
    int n = doc_rtf(&doc, io_buffer, sizeof(io_buffer));
    int r = n < 0 ? n : atomic_write(dest, io_buffer, (u32)n);
    if (r < 0) {
        failure("Export failed; original file retained", r);
        return;
    }
    r = control(NV_CTL_SYNC, 0);
    if (r < 0)
        failure("RTF exported in RAM; disk save failed", r);
    else if (!persistent_path(dest))
        note("RTF exported in RAM only. Export under /home to retain it.");
    else
        note("RTF exported to disk. Ctrl-S saves the editable source document.");
}
static void help(void) {
    static const char *const lines[] = {
        "FOLIO / EDITING AND DOCUMENT FORMAT",
        "Arrow keys, Home/End: move   Ctrl-Home/End: start/end of document",
        "Shift + navigation: select   Ctrl-A: select all   Ctrl-G: go to line",
        "Ctrl-C/X/V: copy/cut/paste   Ctrl-Z/Y: undo/redo (12 edit groups)",
        "Ctrl-F: find   F7: next match   Ctrl-H: replace all (one undo)",
        "Ctrl-S / F2: save   Ctrl-Shift-S / F4: save as   Ctrl-O / F3: open",
        "Ctrl-N: new document   Ctrl-Q: close (asks about unsaved changes)",
        "",
        "Ctrl-B/I/U: bold/italic/underline (selection or next typed text)",
        "Ctrl-L/E/R/J: left/center/right/justify selected paragraphs",
        "F8: body/heading 1/heading 2/quote   Ctrl-1/2/3/4: same styles",
        "F9: cycle alignment   F10: bullets   F11: single/double spacing",
        "Ctrl-Enter: insert page break   F12: toggle break before paragraph",
        "F5 / Ctrl-P: page preview   F6: export RTF   Esc: dismiss dialog",
        "",
        "Save .nvd to keep all formats. Open/edit/save .txt and .md as text.",
        "Ctrl-S commits /home to disk. A failed commit is shown as RAM only.",
        "VGA uses fixed-size ASCII: blue=bold, purple=italic, red=underline.",
        "RTF exports real fonts, heading sizes, alignment, spacing and pages.",
        "Preview uses 64-column text pages; Word may wrap/page differently.",
        "This version does not import .docx/.rtf, display Chinese or images."};
    fill(0, 0, 25, 80, 0x70);
    for (u32 i = 0; i < ARRAY_LEN(lines); ++i)
        text_at(i + 1, 2, lines[i], i == 0 ? 0x71 : 0x70);
    text_at(24, 2, "Press any key to return to the document.", 0x71);
    screen.cursor = 2000;
    show();
    get_key();
}
static void move_cursor(u32 target, bool shift) {
    if (shift) {
        if (doc.anchor == DOC_NONE)
            doc.anchor = doc.cursor;
    } else
        doc.anchor = DOC_NONE;
    doc.cursor = MIN(target, doc.len);
    doc.typing =
        doc.cell[doc.cursor < doc.len ? doc.cursor : (doc.cursor ? doc.cursor - 1 : 0)].font;
}
static bool space(u8 ch) {
    return ch == ' ' || ch == '\n';
}
static bool handle(int key) {
    if (key < 0) {
        failure("Input", key);
        return true;
    }
    u32 c = (u32)key & 4095;
    bool ctrl = key & NV_KEY_CTRL, shift = key & NV_KEY_SHIFT;
    if (c >= 'A' && c <= 'Z' && ctrl)
        c += 'a' - 'A';
    if (c == NV_KEY_F1) {
        help();
        grouped = false;
        return true;
    }
    if ((ctrl && c == 'q')) {
        grouped = false;
        return !can_leave();
    }
    if (c == NV_KEY_F5 || (ctrl && c == 'p')) {
        preview = !preview;
        grouped = false;
        note(preview ? "Page preview. F5 returns to editing." : "Editing.");
        return true;
    }
    if (preview) {
        if (c == 27)
            preview = false;
        if (c == NV_KEY_UP && top)
            --top;
        if (c == NV_KEY_DOWN && top + 1 < layout.pages * DOC_PAGE_SPAN)
            ++top;
        if (c == NV_KEY_PGUP)
            top = top >= DOC_PAGE_SPAN ? top - DOC_PAGE_SPAN : 0;
        if (c == NV_KEY_PGDN)
            top = MIN(top + DOC_PAGE_SPAN, (layout.pages - 1) * DOC_PAGE_SPAN);
        return true;
    }
    if (c == NV_KEY_F2 || c == NV_KEY_F4 || (ctrl && c == 's')) {
        grouped = false;
        save(c == NV_KEY_F4 || shift);
        return true;
    }
    if (c == NV_KEY_F6) {
        grouped = false;
        export_rtf();
        return true;
    }
    if (c == NV_KEY_F3 || (ctrl && c == 'o')) {
        grouped = false;
        if (!can_leave())
            return true;
        char name[NV_PATH_MAX];
        strlcpy(name, path, sizeof(name));
        if (prompt("Open .nvd, .txt or .md (existing document stays on failure)", name,
                   sizeof(name), false)) {
            int r = read_document(name);
            if (r < 0)
                failure("Open failed; current document retained", r);
        }
        return true;
    }
    if (ctrl && c == 'n') {
        grouped = false;
        if (!can_leave())
            return true;
        doc_init(&doc);
        path[0] = 0;
        new_file = true;
        disk_pending = false;
        saved_hash = doc_hash(&doc);
        top = hist_at = hist_count = 0;
        note("New document. Ctrl-S chooses a filename.");
        return true;
    }
    if (ctrl && (c == 'z' || c == 'y')) {
        undo(c == 'y' || shift);
        return true;
    }
    if (ctrl && c == 'a') {
        grouped = false;
        doc.anchor = 0;
        doc.cursor = doc.len;
        return true;
    }
    if (ctrl && (c == 'c' || c == 'x' || c == 'v')) {
        grouped = false;
        u32 a, b;
        doc_bounds(&doc, &a, &b);
        if (c == 'v') {
            if (clip_len) {
                checkpoint(false);
                int r = doc_insert(&doc, clipboard, clip_len);
                if (r < 0)
                    failure("Paste", r);
                else
                    note("Pasted with formatting.");
            }
        } else if (a != b) {
            clip_len = b - a;
            memcpy(clipboard, doc.cell + a, clip_len * sizeof(*clipboard));
            if (c == 'x') {
                checkpoint(false);
                doc_delete(&doc, false);
            }
            note(c == 'x' ? "Cut selection." : "Copied selection with formatting.");
        } else
            note("Select text first with Shift+arrows or Ctrl-A.");
        return true;
    }
    if ((ctrl && c == 'f') || c == NV_KEY_F7) {
        grouped = false;
        if ((c == NV_KEY_F7 && *query) ||
            prompt("Find text (case sensitive, wraps at end)", query, sizeof(query), false)) {
            if (doc_find(&doc, query, true) < 0)
                note("Text not found.");
            else
                note("Match selected. F7 finds the next match.");
        }
        return true;
    }
    if (ctrl && c == 'h') {
        grouped = false;
        char replacement[192] = {0};
        if (prompt("Replace all: find text", query, sizeof(query), false) &&
            prompt("Replace all: replacement (may be empty)", replacement, sizeof(replacement),
                   true)) {
            checkpoint(false);
            int r = doc_replace_all(&doc, query, replacement);
            if (r < 0)
                failure("Replace all", r);
            else {
                char b[32];
                number(b, (u32)r, 10);
                note(b);
                strlcpy(message + strlen(message), " replacements. Ctrl-Z undoes this operation.",
                        sizeof(message) - strlen(message));
            }
        }
        return true;
    }
    if (ctrl && c == 'g') {
        grouped = false;
        char line[16] = {0};
        u32 n;
        if (prompt("Go to source line (starting at 1)", line, sizeof(line), false) &&
            !parse_u32(line, &n) && n) {
            u32 p = 0;
            while (p < doc.len && n > 1)
                if (doc.cell[p++].ch == '\n')
                    --n;
            move_cursor(p, false);
        }
        return true;
    }
    if (ctrl && (c == 'b' || c == 'i' || c == 'u')) {
        grouped = false;
        checkpoint(false);
        doc_font(&doc, c == 'b' ? DOC_BOLD : c == 'i' ? DOC_ITALIC : DOC_UNDERLINE);
        note("Format applied. The toolbar shows the current text format.");
        return true;
    }
    if (c == NV_KEY_F8 || (ctrl && c >= '1' && c <= '4')) {
        grouped = false;
        checkpoint(false);
        u8 style = c == NV_KEY_F8 ? ((doc.cell[doc.cursor].para + 4) & DOC_STYLE) : (c - '1') * 4;
        doc_paragraph(&doc, DOC_STYLE, style);
        note("Paragraph style applied.");
        return true;
    }
    if (c == NV_KEY_F9 || (ctrl && (c == 'l' || c == 'e' || c == 'r' || c == 'j'))) {
        grouped = false;
        checkpoint(false);
        u8 align = c == NV_KEY_F9 ? ((doc.cell[doc.cursor].para + 1) & DOC_ALIGN)
                   : c == 'l'     ? DOC_LEFT
                   : c == 'e'     ? DOC_CENTER
                   : c == 'r'     ? DOC_RIGHT
                                  : DOC_JUSTIFY;
        doc_paragraph(&doc, DOC_ALIGN, align);
        note("Paragraph alignment applied.");
        return true;
    }
    if (c == NV_KEY_F10 || c == NV_KEY_F11 || c == NV_KEY_F12) {
        grouped = false;
        checkpoint(false);
        u8 mask = c == NV_KEY_F10 ? DOC_BULLET : c == NV_KEY_F11 ? DOC_DOUBLE : DOC_BREAK;
        doc_paragraph(&doc, mask, (doc.cell[doc.cursor].para ^ mask) & mask);
        note("Paragraph format updated.");
        return true;
    }
    if (c >= NV_KEY_LEFT && c <= NV_KEY_PGDN) {
        grouped = false;
        u32 p = doc.cursor, line = doc_line_at(&doc, &layout, p);
        bool vertical = false;
        if (c == NV_KEY_LEFT && p) {
            --p;
            if (ctrl) {
                while (p && space(doc.cell[p].ch))
                    --p;
                while (p && !space(doc.cell[p - 1].ch))
                    --p;
            }
        }
        if (c == NV_KEY_RIGHT && p < doc.len) {
            ++p;
            if (ctrl) {
                while (p < doc.len && !space(doc.cell[p].ch))
                    ++p;
                while (p < doc.len && space(doc.cell[p].ch))
                    ++p;
            }
        }
        if (c == NV_KEY_HOME)
            p = ctrl ? 0 : layout.line[line].first;
        if (c == NV_KEY_END)
            p = ctrl ? doc.len : layout.line[line].end;
        if (c == NV_KEY_UP || c == NV_KEY_DOWN || c == NV_KEY_PGUP || c == NV_KEY_PGDN) {
            vertical = true;
            if (goal == DOC_NONE)
                goal = doc_column(&doc, &layout.line[line], p);
            u32 delta = (c == NV_KEY_UP || c == NV_KEY_DOWN) ? 1 : VIEW_ROWS - 1;
            if (c == NV_KEY_UP || c == NV_KEY_PGUP)
                line = line >= delta ? line - delta : 0;
            else
                line = MIN(line + delta, layout.count - 1);
            p = doc_hit(&doc, &layout.line[line], goal);
        }
        if (!vertical)
            goal = DOC_NONE;
        move_cursor(p, shift);
        return true;
    }
    if (c == '\b' || c == NV_KEY_DELETE) {
        checkpoint(true);
        doc_delete(&doc, c == '\b');
        goal = DOC_NONE;
        return true;
    }
    if (ctrl && c == '\n') {
        grouped = false;
        checkpoint(false);
        int r = doc_type(&doc, '\n');
        if (r < 0)
            failure("Page break", r);
        else
            doc_paragraph(&doc, DOC_BREAK, DOC_BREAK);
        return true;
    }
    if (ctrl || (key & NV_KEY_ALT))
        return true;
    if (c == '\t' || c == '\n' || (c >= 32 && c < 127)) {
        checkpoint(true);
        int r = 0;
        if (c == '\t') {
            struct doc_cell tab[4];
            for (u32 i = 0; i < 4; ++i)
                tab[i] = (struct doc_cell){' ', doc.typing, doc.cell[doc.cursor].para};
            r = doc_insert(&doc, tab, 4);
        } else
            r = doc_type(&doc, (u8)c);
        if (r < 0)
            failure("Document limit (32 KiB)", r);
        else
            message[0] = 0;
        if (c == ' ' || c == '\n' || c == '\t')
            grouped = false;
        goal = DOC_NONE;
    }
    return true;
}
int user_main(const char *args) {
    if (app_help("folio", args))
        return 0;
    doc_init(&doc);
    saved_hash = doc_hash(&doc);
    new_file = true;
    if (*args) {
        int r = read_document(args);
        if (r == -NV_ENOENT) {
            strlcpy(path, args, sizeof(path));
            new_file = true;
            note("New file. Ctrl-S saves; F1 shows all shortcuts.");
        } else if (r < 0) {
            report_error("Folio: cannot open document", r);
            return 1;
        }
    } else
        note("New document. Ctrl-S saves; F1 shows all shortcuts.");
    int r = surface(NV_SCREEN_ACQUIRE, NULL);
    if (r < 0) {
        report_error("Folio: screen unavailable", r);
        return 1;
    }
    do {
        redraw();
    } while (handle(get_key()));
    surface(NV_SCREEN_RELEASE, NULL);
    println("Folio closed.");
    return 0;
}
