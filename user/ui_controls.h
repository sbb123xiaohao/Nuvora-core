#ifndef NV_UI_CONTROLS_H
#define NV_UI_CONTROLS_H
#include "gui_text.h"

enum { UI_BLUE=0x1761f5, UI_BG=0xf2f4f8, UI_TEXT=0x172133,
       UI_MUTED=0x8993a4, UI_BORDER=0xe7eaf0, UI_WHITE=0xffffff,
       UI_BLUE_PALE=0xe9f0ff };
enum { UI_ICON_SEARCH, UI_ICON_HOME, UI_ICON_FOLDER, UI_ICON_DOCUMENT,
       UI_ICON_IMAGE, UI_ICON_VIDEO, UI_ICON_MUSIC, UI_ICON_DOWNLOAD,
       UI_ICON_APP, UI_ICON_STORAGE, UI_ICON_BACK, UI_ICON_PLUS,
       UI_ICON_CLOSE, UI_ICON_NETWORK, UI_ICON_SPEAKER, UI_ICON_SHIELD,
       UI_ICON_SETTINGS, UI_ICON_PLAY, UI_ICON_PAUSE, UI_ICON_PREVIOUS,
       UI_ICON_NEXT, UI_ICON_REPEAT, UI_ICON_SHUFFLE, UI_ICON_MORE,
       UI_ICON_CHECK, UI_ICON_TERMINAL, UI_ICON_COUNT };

/* Rounded strokes use signed coordinates so icons may be safely clipped at
 * window edges. Every tile visits the same coverage as a complete frame. */
static inline void ui_line(struct nv_canvas *c,struct desktop_clip clip,
    i32 x0,i32 y0,i32 x1,i32 y1,u32 stroke,u32 color) {
    i32 dx=x1>x0?x1-x0:x0-x1,dy=y1>y0?y1-y0:y0-y1;
    i32 sx=x0<x1?1:-1,sy=y0<y1?1:-1,error=dx-dy;
    for (;;) {
        desktop_round_alpha(c,clip,x0-(i32)(stroke/2),y0-(i32)(stroke/2),
            stroke,stroke,stroke/2,color,255);
        if (x0==x1 && y0==y1) break;
        i32 twice=error*2;
        if (twice>-dy) { error-=dy;x0+=sx; }
        if (twice<dx) { error+=dx;y0+=sy; }
    }
}
static inline i32 ui_icon_coordinate(i32 origin,u32 size,i32 point) {
    return origin+(i32)((i64)size*point/24);
}
static inline void ui_line_icon(struct nv_canvas *c,struct desktop_clip clip,
    i32 x,i32 y,u32 size,u32 id,u32 color) {
    if (!size || id>=UI_ICON_COUNT) return;
    u32 stroke=MAX(1u,size/18);
#define UI_SEG(a,b,d,e) ui_line(c,clip,ui_icon_coordinate(x,size,a),ui_icon_coordinate(y,size,b),ui_icon_coordinate(x,size,d),ui_icon_coordinate(y,size,e),stroke,color)
#define UI_DOT(a,b) desktop_round_alpha(c,clip,ui_icon_coordinate(x,size,a)-(i32)stroke,ui_icon_coordinate(y,size,b)-(i32)stroke,stroke*2+1,stroke*2+1,stroke,color,255)
    switch (id) {
    case UI_ICON_SEARCH:
        UI_SEG(5,3,11,3);UI_SEG(11,3,15,7);UI_SEG(15,7,15,12);
        UI_SEG(15,12,11,16);UI_SEG(11,16,5,16);UI_SEG(5,16,2,12);
        UI_SEG(2,12,2,7);UI_SEG(2,7,5,3);UI_SEG(14,15,21,22);break;
    case UI_ICON_HOME:
        UI_SEG(2,10,12,2);UI_SEG(12,2,22,10);UI_SEG(5,8,5,21);
        UI_SEG(5,21,19,21);UI_SEG(19,21,19,8);UI_SEG(10,21,10,14);
        UI_SEG(10,14,14,14);UI_SEG(14,14,14,21);break;
    case UI_ICON_FOLDER:
        UI_SEG(2,5,9,5);UI_SEG(9,5,12,8);UI_SEG(12,8,22,8);
        UI_SEG(22,8,22,20);UI_SEG(22,20,2,20);UI_SEG(2,20,2,5);
        UI_SEG(2,10,22,10);break;
    case UI_ICON_DOCUMENT:case UI_ICON_IMAGE:case UI_ICON_VIDEO:case UI_ICON_DOWNLOAD:
        UI_SEG(4,2,15,2);UI_SEG(15,2,21,8);UI_SEG(21,8,21,22);
        UI_SEG(21,22,4,22);UI_SEG(4,22,4,2);UI_SEG(15,2,15,8);UI_SEG(15,8,21,8);
        if (id==UI_ICON_DOCUMENT) {
            UI_SEG(8,11,16,11);UI_SEG(8,15,17,15);UI_SEG(8,19,14,19);
        } else if (id==UI_ICON_IMAGE) {
            UI_DOT(9,11);UI_SEG(7,19,12,13);UI_SEG(12,13,18,19);UI_SEG(7,19,18,19);
        } else if (id==UI_ICON_VIDEO) {
            UI_SEG(10,11,10,19);UI_SEG(10,19,17,15);UI_SEG(17,15,10,11);
        } else {
            UI_SEG(12,10,12,18);UI_SEG(8,14,12,18);UI_SEG(12,18,16,14);
        }break;
    case UI_ICON_MUSIC:
        UI_SEG(9,18,9,5);UI_SEG(9,5,20,3);UI_SEG(20,3,20,16);
        UI_SEG(9,8,20,6);UI_DOT(6,19);UI_DOT(17,17);break;
    case UI_ICON_APP:
        UI_SEG(3,3,9,3);UI_SEG(9,3,9,9);UI_SEG(9,9,3,9);UI_SEG(3,9,3,3);
        UI_SEG(15,3,21,3);UI_SEG(21,3,21,9);UI_SEG(21,9,15,9);UI_SEG(15,9,15,3);
        UI_SEG(3,15,9,15);UI_SEG(9,15,9,21);UI_SEG(9,21,3,21);UI_SEG(3,21,3,15);
        UI_SEG(15,15,21,15);UI_SEG(21,15,21,21);UI_SEG(21,21,15,21);UI_SEG(15,21,15,15);break;
    case UI_ICON_STORAGE:
        UI_SEG(5,4,19,4);UI_SEG(19,4,22,18);UI_SEG(22,18,2,18);
        UI_SEG(2,18,5,4);UI_SEG(2,18,2,22);UI_SEG(2,22,22,22);UI_SEG(22,22,22,18);UI_DOT(18,20);break;
    case UI_ICON_BACK:
        UI_SEG(14,4,6,12);UI_SEG(6,12,14,20);UI_SEG(6,12,22,12);break;
    case UI_ICON_PLUS:
        UI_SEG(12,3,12,21);UI_SEG(3,12,21,12);break;
    case UI_ICON_CLOSE:
        UI_SEG(5,5,19,19);UI_SEG(19,5,5,19);break;
    case UI_ICON_NETWORK:
        UI_SEG(2,7,7,3);UI_SEG(7,3,17,3);UI_SEG(17,3,22,7);
        UI_SEG(5,11,9,8);UI_SEG(9,8,15,8);UI_SEG(15,8,19,11);
        UI_SEG(8,15,12,12);UI_SEG(12,12,16,15);UI_DOT(12,20);break;
    case UI_ICON_SPEAKER:
        UI_SEG(3,9,7,9);UI_SEG(7,9,13,4);UI_SEG(13,4,13,20);
        UI_SEG(13,20,7,15);UI_SEG(7,15,3,15);UI_SEG(3,15,3,9);
        UI_SEG(17,8,19,12);UI_SEG(19,12,17,16);UI_SEG(20,4,23,12);UI_SEG(23,12,20,20);break;
    case UI_ICON_SHIELD:
        UI_SEG(3,5,12,2);UI_SEG(12,2,21,5);UI_SEG(21,5,20,15);
        UI_SEG(20,15,12,22);UI_SEG(12,22,4,15);UI_SEG(4,15,3,5);
        UI_SEG(8,11,11,14);UI_SEG(11,14,16,8);break;
    case UI_ICON_SETTINGS:
        UI_SEG(5,4,5,20);UI_SEG(12,4,12,20);UI_SEG(19,4,19,20);
        UI_SEG(2,8,8,8);UI_SEG(9,16,15,16);UI_SEG(16,10,22,10);break;
    case UI_ICON_TERMINAL:
        UI_SEG(4,6,10,12);UI_SEG(10,12,4,18);UI_SEG(13,18,21,18);break;
    case UI_ICON_PLAY:
        UI_SEG(7,3,7,21);UI_SEG(7,21,21,12);UI_SEG(21,12,7,3);break;
    case UI_ICON_PAUSE:
        UI_SEG(7,4,7,20);UI_SEG(17,4,17,20);break;
    case UI_ICON_PREVIOUS:case UI_ICON_NEXT:
        if (id==UI_ICON_PREVIOUS) {
            UI_SEG(5,4,5,20);UI_SEG(19,4,7,12);UI_SEG(7,12,19,20);UI_SEG(19,20,19,4);
        } else {
            UI_SEG(19,4,19,20);UI_SEG(5,4,17,12);UI_SEG(17,12,5,20);UI_SEG(5,20,5,4);
        }break;
    case UI_ICON_REPEAT:
        UI_SEG(4,8,20,8);UI_SEG(20,8,16,4);UI_SEG(20,8,16,12);
        UI_SEG(20,16,4,16);UI_SEG(4,16,8,12);UI_SEG(4,16,8,20);break;
    case UI_ICON_SHUFFLE:
        UI_SEG(3,5,7,5);UI_SEG(7,5,17,19);UI_SEG(17,19,21,19);
        UI_SEG(3,19,7,19);UI_SEG(7,19,17,5);UI_SEG(17,5,21,5);
        UI_SEG(18,2,21,5);UI_SEG(21,5,18,8);UI_SEG(18,16,21,19);UI_SEG(21,19,18,22);break;
    case UI_ICON_MORE:
        UI_DOT(4,12);UI_DOT(12,12);UI_DOT(20,12);break;
    case UI_ICON_CHECK:
        UI_SEG(4,12,9,18);UI_SEG(9,18,21,5);break;
    }
#undef UI_SEG
#undef UI_DOT
}
#endif
