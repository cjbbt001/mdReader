#include "doc.h"
#include <md4c.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace {

struct Ctx {
    Doc* doc = nullptr;
    size_t cur = (size_t)-1;      // block index that receives text
    int quote = 0;
    int list = 0;
    std::vector<bool> orderedStack;
    std::vector<int> nextNumberStack;
    bool bold = false, italic = false, code = false, strike = false;
    std::wstring href;

    // table
    bool inCell = false;
    bool inHead = false;
    bool rowHeader = false;
    std::wstring cell;
    std::vector<std::wstring> row;
    std::vector<std::vector<std::wstring>> rows;
    int headerRows = 0;
};

Block& pushBlock(Ctx& c, BKind k) {
    Block b;
    b.kind = k;
    b.listDepth = c.list > 0 ? c.list - 1 : 0;
    b.quoteDepth = c.quote;
    c.doc->blocks.push_back(std::move(b));
    c.cur = c.doc->blocks.size() - 1;
    return c.doc->blocks.back();
}

bool isCjk(wchar_t ch) { return ch >= 0x2E80 && ch <= 0xFFEF; }

void addText(Ctx& c, const wchar_t* s, size_t n) {
    if (!n) return;
    if (c.inCell) { c.cell.append(s, n); return; }
    if (c.cur >= c.doc->blocks.size()) return;
    Block& b = c.doc->blocks[c.cur];
    if (b.kind == BKind::Code) { b.code.append(s, n); return; }

    bool link = !c.href.empty();
    if (!b.runs.empty()) {
        Run& last = b.runs.back();
        if (last.bold == c.bold && last.italic == c.italic && last.code == c.code &&
            last.strike == c.strike && last.link == link) {
            last.text.append(s, n);
            return;
        }
    }
    Run r;
    r.text.assign(s, n);
    r.bold = c.bold; r.italic = c.italic; r.code = c.code;
    r.strike = c.strike; r.link = link;
    b.runs.push_back(std::move(r));
}

Block* curBlock(Ctx& c) {
    return c.cur < c.doc->blocks.size() ? &c.doc->blocks[c.cur] : nullptr;
}

void finishBlock(Ctx& c) {
    if (Block* b = curBlock(c)) {
        bool textual = b->kind == BKind::Para || b->kind == BKind::Heading ||
                       b->kind == BKind::Bullet || b->kind == BKind::Ordered;
        if (textual && b->runs.empty()) c.doc->blocks.pop_back();
    }
    c.cur = (size_t)-1;
}

int onEnterBlock(MD_BLOCKTYPE t, void* d, void* ud) {
    Ctx& c = *(Ctx*)ud;
    switch (t) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_QUOTE:
        c.quote++;
        break;
    case MD_BLOCK_UL:
        c.list++;
        c.orderedStack.push_back(false);
        c.nextNumberStack.push_back(0);
        break;
    case MD_BLOCK_OL: {
        auto* ol = (MD_BLOCK_OL_DETAIL*)d;
        c.list++;
        c.orderedStack.push_back(true);
        c.nextNumberStack.push_back(ol ? (int)ol->start : 1);
        break;
    }
    case MD_BLOCK_LI: {
        bool ord = !c.orderedStack.empty() && c.orderedStack.back();
        Block& b = pushBlock(c, ord ? BKind::Ordered : BKind::Bullet);
        if (ord && !c.nextNumberStack.empty()) b.number = c.nextNumberStack.back()++;
        break;
    }
    case MD_BLOCK_H: {
        auto* h = (MD_BLOCK_H_DETAIL*)d;
        Block& b = pushBlock(c, BKind::Heading);
        b.level = h ? (int)h->level : 1;
        break;
    }
    case MD_BLOCK_P:
        if (c.cur != (size_t)-1) {
            Block& b = c.doc->blocks[c.cur];
            if ((b.kind == BKind::Bullet || b.kind == BKind::Ordered) && b.runs.empty()) break;
        }
        pushBlock(c, BKind::Para);
        break;
    case MD_BLOCK_CODE: {
        auto* cd = (MD_BLOCK_CODE_DETAIL*)d;
        Block& b = pushBlock(c, BKind::Code);
        if (cd && cd->lang.text) b.lang = widen(cd->lang.text, cd->lang.size);
        break;
    }
    case MD_BLOCK_HR:
        pushBlock(c, BKind::Rule);
        break;
    case MD_BLOCK_TABLE:
        c.rows.clear();
        c.headerRows = 0;
        pushBlock(c, BKind::Table);
        break;
    case MD_BLOCK_THEAD:
        c.inHead = true;
        break;
    case MD_BLOCK_TR:
        c.row.clear();
        c.rowHeader = false;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        c.inCell = true;
        c.cell.clear();
        if (t == MD_BLOCK_TH) c.rowHeader = true;
        break;
    default:
        break;
    }
    return 0;
}

int onLeaveBlock(MD_BLOCKTYPE t, void* d, void* ud) {
    (void)d;
    Ctx& c = *(Ctx*)ud;
    switch (t) {
    case MD_BLOCK_QUOTE:
        if (c.quote) c.quote--;
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (c.list) c.list--;
        if (!c.orderedStack.empty()) c.orderedStack.pop_back();
        if (!c.nextNumberStack.empty()) c.nextNumberStack.pop_back();
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        c.row.push_back(c.cell);
        c.cell.clear();
        c.inCell = false;
        break;
    case MD_BLOCK_TR:
        c.rows.push_back(c.row);
        c.row.clear();
        if (c.rowHeader) c.headerRows = (int)c.rows.size();
        break;
    case MD_BLOCK_TABLE:
        if (Block* b = curBlock(c)) {
            b->cells = c.rows;
            b->headerRows = c.headerRows;
        }
        c.rows.clear();
        c.headerRows = 0;
        c.cur = (size_t)-1;
        break;
    case MD_BLOCK_P:
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_LI:
    case MD_BLOCK_HR:
        finishBlock(c);
        break;
    default:
        break;
    }
    return 0;
}

int onEnterSpan(MD_SPANTYPE t, void* d, void* ud) {
    Ctx& c = *(Ctx*)ud;
    switch (t) {
    case MD_SPAN_EM: c.italic = true; break;
    case MD_SPAN_STRONG: c.bold = true; break;
    case MD_SPAN_CODE: c.code = true; break;
    case MD_SPAN_DEL: c.strike = true; break;
    case MD_SPAN_A: {
        auto* a = (MD_SPAN_A_DETAIL*)d;
        if (a && a->href.text) c.href = widen(a->href.text, a->href.size);
        break;
    }
    default: break;
    }
    return 0;
}

int onLeaveSpan(MD_SPANTYPE t, void* d, void* ud) {
    (void)d;
    Ctx& c = *(Ctx*)ud;
    switch (t) {
    case MD_SPAN_EM: c.italic = false; break;
    case MD_SPAN_STRONG: c.bold = false; break;
    case MD_SPAN_CODE: c.code = false; break;
    case MD_SPAN_DEL: c.strike = false; break;
    case MD_SPAN_A: c.href.clear(); break;
    default: break;
    }
    return 0;
}

int onText(MD_TEXTTYPE t, const MD_CHAR* s, MD_SIZE n, void* ud) {
    Ctx& c = *(Ctx*)ud;
    if (!s || !n) return 0;
    switch (t) {
    case MD_TEXT_NULLCHAR: {
        const wchar_t rep = 0xFFFD;
        addText(c, &rep, 1);
        break;
    }
    case MD_TEXT_BR: {
        const wchar_t nl = L'\n';
        addText(c, &nl, 1);
        break;
    }
    case MD_TEXT_SOFTBR: {
        // No space when a CJK line break joined: Chinese text has no spaces.
        bool cjkBefore = false;
        if (c.cur != (size_t)-1) {
            Block& b = c.doc->blocks[c.cur];
            if (!b.runs.empty() && !b.runs.back().text.empty())
                cjkBefore = isCjk(b.runs.back().text.back());
        }
        if (!cjkBefore) {
            const wchar_t sp = L' ';
            addText(c, &sp, 1);
        }
        break;
    }
    case MD_TEXT_HTML:
        break;
    default: {
        std::wstring w = widen(s, (size_t)n);
        addText(c, w.c_str(), w.size());
        break;
    }
    }
    return 0;
}

}  // namespace

bool loadDoc(const std::wstring& path, Doc& doc, std::wstring& err) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) { err = L"无法打开文件"; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();
    if (src.size() >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF)
        src.erase(0, 3);

    doc.blocks.clear();
    doc.path = path;
    size_t slash = path.find_last_of(L"\\/");
    doc.name = slash == std::wstring::npos ? path : path.substr(slash + 1);

    Ctx c;
    c.doc = &doc;

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
                   MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_NOHTML;
    parser.enter_block = onEnterBlock;
    parser.leave_block = onLeaveBlock;
    parser.enter_span = onEnterSpan;
    parser.leave_span = onLeaveSpan;
    parser.text = onText;
    parser.debug_log = nullptr;
    parser.syntax = nullptr;

    if (md_parse(src.c_str(), (MD_SIZE)src.size(), &parser, &c) != 0) {
        err = L"Markdown 解析失败";
        return false;
    }
    return true;
}

void buildToc(const Doc& doc, std::vector<TocEntry>& out) {
    out.clear();
    for (size_t i = 0; i < doc.blocks.size(); ++i) {
        const Block& b = doc.blocks[i];
        if (b.kind != BKind::Heading) continue;
        std::wstring text;
        for (const auto& r : b.runs) text += r.text;
        size_t a = text.find_first_not_of(L" \t\r\n");
        size_t z = text.find_last_not_of(L" \t\r\n");
        if (a == std::wstring::npos) continue;
        TocEntry e;
        e.level = b.level < 1 ? 1 : (b.level > 6 ? 6 : b.level);
        e.text = text.substr(a, z - a + 1);
        e.blockIndex = (int)i;
        out.push_back(std::move(e));
    }
}
