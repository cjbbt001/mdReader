#pragma once
#include "common.h"

struct Theme {
    bool dark = false;
    D2D1_COLOR_F bg, text, muted, codeBg, codeText, link, rule, quoteBar, tableHead, tableLine, thumb,
        panel, selection, highlight, kw, str, com, num;
};

inline COLORREF themeBgColorRef(bool dark) { return dark ? RGB(0x1E, 0x1E, 0x1E) : RGB(0xFF, 0xFF, 0xFF); }

inline Theme makeTheme(bool dark) {
    Theme t;
    t.dark = dark;
    if (dark) {
        t.bg = rgb(0x1E1E1E);
        t.text = rgb(0xE6E6E6);
        t.muted = rgb(0x9AA0A6);
        t.codeBg = rgb(0x2A2A2A);
        t.codeText = rgb(0xE6E6E6);
        t.link = rgb(0x6CB6FF);
        t.rule = rgb(0x3A3A3A);
        t.quoteBar = rgb(0x4A4A4A);
        t.tableHead = rgb(0x2A2A2A);
        t.tableLine = rgb(0x3A3A3A);
        t.thumb = rgb(0x5A5A5A);
        t.panel = rgb(0x232323);
        t.selection = rgb(0x2F4F7F);
        t.highlight = rgb(0x6B5A1E);
        t.kw = rgb(0xFF7B72);
        t.str = rgb(0xA5D6FF);
        t.com = rgb(0x8B949E);
        t.num = rgb(0x79C0FF);
    } else {
        t.bg = rgb(0xFFFFFF);
        t.text = rgb(0x24292F);
        t.muted = rgb(0x6E7781);
        t.codeBg = rgb(0xF4F6F8);
        t.codeText = rgb(0x24292F);
        t.link = rgb(0x0969DA);
        t.rule = rgb(0xD8DEE4);
        t.quoteBar = rgb(0xD0D7DE);
        t.tableHead = rgb(0xF4F6F8);
        t.tableLine = rgb(0xD8DEE4);
        t.thumb = rgb(0xC4C9CE);
        t.panel = rgb(0xFAFBFC);
        t.selection = rgb(0xCDE3FF);
        t.highlight = rgb(0xFFE08A);
        t.kw = rgb(0xCF222E);
        t.str = rgb(0x0A3069);
        t.com = rgb(0x6E7781);
        t.num = rgb(0x0550AE);
    }
    return t;
}
