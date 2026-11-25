#include "doc.h"
#include <md4c.h>
#include <algorithm>
#include <string_view>

namespace {

bool emptyParagraphTag(std::string_view line) {
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string_view::npos) return false;
    const size_t last = line.find_last_not_of(" \t\r\n");
    if (last == std::string_view::npos || last < first) return false;
    line = line.substr(first, last - first + 1);
    if (line.size() < 4 || line.front() != '<' || line.back() != '>' ||
        (line[1] != 'b' && line[1] != 'B') || (line[2] != 'r' && line[2] != 'R')) return false;
    line = line.substr(3, line.size() - 4);
    if (!line.empty() && line.back() == '/') line.remove_suffix(1);
    return line.find_first_not_of(" \t") == std::string_view::npos;
}

struct Ctx {
    Doc* doc = nullptr;
    size_t cur = (size_t)-1;      // block index that receives text
    int quote = 0;
    int list = 0;
    std::vector<bool> orderedStack;
    std::vector<int> nextNumberStack;
    bool bold = false, italic = false, code = false, strike = false;
    int inImage = 0;
    bool imageStarted = false;
    int link = -1;
    std::wstring imgSrc, imgAlt;
    std::vector<MD_SIZE> lineStarts{0};
    int firstLine = -1, lastLine = -1;
    const char* utf8 = nullptr;
    size_t utf8Size = 0;
    std::vector<UINT32> sourceOffsets;
    std::vector<SourceStyle>* styles = nullptr;

    // table
    bool inCell = false;
    bool rowHeader = false;
    std::vector<Run> cell;
    std::vector<std::vector<Run>> row;
    std::vector<std::vector<std::vector<Run>>> rows;
    int headerRows = 0;
};

Block& pushBlock(Ctx& c, BKind k) {
    // A list item can start with a heading, code block, or nested list.
    // In that case its placeholder has no text or layout of its own.
    if (c.cur < c.doc->blocks.size()) {
        const Block& current = c.doc->blocks[c.cur];
        if ((current.kind == BKind::Bullet || current.kind == BKind::Ordered) &&
            current.firstLine < 0)
            c.doc->blocks.pop_back();
    }
    Block b;
    b.kind = k;
    b.firstLine = c.firstLine;
    b.lastLine = c.lastLine;
    b.listDepth = c.list > 0 ? c.list - 1 : 0;
    b.quoteDepth = c.quote;
    c.doc->blocks.push_back(std::move(b));
    c.cur = c.doc->blocks.size() - 1;
    return c.doc->blocks.back();
}

void addText(Ctx& c, const wchar_t* s, size_t n) {
    if (!n) return;
    if (c.inImage) { c.imgAlt.append(s, n); return; }
    if (c.cur >= c.doc->blocks.size()) return;
    Block& b = c.doc->blocks[c.cur];
    if (b.kind == BKind::Code) { b.code.append(s, n); return; }
    auto& runs = c.inCell ? c.cell : b.runs;
    if (!runs.empty()) {
        Run& last = runs.back();
        if (last.bold == c.bold && last.italic == c.italic && last.code == c.code &&
            last.strike == c.strike && last.link == c.link) {
            last.text.append(s, n);
            return;
        }
    }
    Run r;
    r.text.assign(s, n);
    r.bold = c.bold; r.italic = c.italic; r.code = c.code;
    r.strike = c.strike; r.link = c.link;
    runs.push_back(std::move(r));
}

Block* curBlock(Ctx& c) {
    return c.cur < c.doc->blocks.size() ? &c.doc->blocks[c.cur] : nullptr;
}

void finishBlock(Ctx& c) {
    if (Block* b = curBlock(c); b && b->firstLine < 0) c.doc->blocks.pop_back();
    c.cur = (size_t)-1;
}

int lineAt(const Ctx& c, MD_SIZE offset) {
    return (int)(std::upper_bound(c.lineStarts.begin(), c.lineStarts.end(), offset) -
                 c.lineStarts.begin()) - 1;
}

void onLeafRange(MD_SIZE begin, MD_SIZE end, void* userdata) {
    auto& c = *static_cast<Ctx*>(userdata);
    c.firstLine = lineAt(c, begin);
    c.lastLine = lineAt(c, end);
}

void setListRange(Block& block, const Ctx& c) {
    if (block.firstLine < 0) {
        block.firstLine = c.firstLine;
        block.lastLine = c.lastLine;
    }
}

void advanceInlineLine(Ctx& c, MD_SIZE offset) {
    Block* block = curBlock(c);
    if (!block || c.inCell) return;
    const bool list = block->kind == BKind::Bullet || block->kind == BKind::Ordered;
    if (list) setListRange(*block, c);
    if (!list && block->kind != BKind::Para && block->kind != BKind::Image) return;
    if (block->lastLine <= block->firstLine || offset < c.lineStarts[(size_t)block->firstLine + 1]) return;

    Block next;
    next.kind = list ? block->kind : BKind::Para;
    next.firstLine = std::min(lineAt(c, offset), block->lastLine);
    next.lastLine = block->lastLine;
    next.listDepth = block->listDepth;
    next.quoteDepth = block->quoteDepth;
    next.number = block->number;
    next.task = block->task;
    next.continuation = list;
    block->lastLine = block->firstLine;
    c.cur = c.doc->blocks.size();
    c.doc->blocks.push_back(std::move(next));
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
        c.firstLine = c.lastLine = -1;
        bool ord = !c.orderedStack.empty() && c.orderedStack.back();
        Block& b = pushBlock(c, ord ? BKind::Ordered : BKind::Bullet);
        if (ord && !c.nextNumberStack.empty()) b.number = c.nextNumberStack.back()++;
        auto* li = (MD_BLOCK_LI_DETAIL*)d;
        if (li && li->is_task) {
            b.task = true;
            b.checked = (li->task_mark == 'x' || li->task_mark == 'X');
        }
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
            if ((b.kind == BKind::Bullet || b.kind == BKind::Ordered) && b.firstLine < 0) {
                setListRange(b, c);
                break;
            }
        }
        pushBlock(c, BKind::Para);
        break;
    case MD_BLOCK_CODE: {
        auto* cd = (MD_BLOCK_CODE_DETAIL*)d;
        Block& b = pushBlock(c, BKind::Code);
        b.fenced = cd && cd->fence_char;
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
        c.row.push_back(std::move(c.cell));
        c.cell.clear();
        c.inCell = false;
        break;
    case MD_BLOCK_TR:
        c.rows.push_back(std::move(c.row));
        c.row.clear();
        if (c.rowHeader) c.headerRows = (int)c.rows.size();
        break;
    case MD_BLOCK_TABLE:
        if (Block* b = curBlock(c)) {
            b->cells = std::move(c.rows);
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
        if (a && a->href.size) {
            c.link = (int)c.doc->links.size();
            c.doc->links.push_back(widen(a->href.text, a->href.size));
        }
        break;
    }
    case MD_SPAN_IMG: {
        auto* im = (MD_SPAN_IMG_DETAIL*)d;
        c.inImage++;
        c.imageStarted = false;
        c.imgSrc.clear();
        c.imgAlt.clear();
        if (im && im->src.text) c.imgSrc = widen(im->src.text, im->src.size);
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
    case MD_SPAN_A: c.link = -1; break;
    case MD_SPAN_IMG: {
        if (c.inImage) c.inImage--;
        Block* b = curBlock(c);
        if (!b) break;
        bool alone = c.link < 0 && !c.inCell && b->kind == BKind::Para && b->runs.empty() && b->code.empty();
        if (alone) {
            b->kind = BKind::Image;
            b->imgSrc = c.imgSrc;
            b->imgAlt = c.imgAlt;
        } else {
            Run r;
            r.text = c.imgAlt.empty() ? L"[图片]" : c.imgAlt;
            r.italic = true;
            r.link = c.link;
            (c.inCell ? c.cell : b->runs).push_back(std::move(r));
        }
        break;
    }
    default: break;
    }
    return 0;
}

int onText(MD_TEXTTYPE t, const MD_CHAR* s, MD_SIZE n, void* ud) {
    Ctx& c = *(Ctx*)ud;
    if (!s || !n) return 0;
    const auto begin = reinterpret_cast<uintptr_t>(s);
    const auto base = reinterpret_cast<uintptr_t>(c.utf8);
    const bool fromSource = begin >= base && begin - base < c.utf8Size;
    if (fromSource && (!c.inImage || !c.imageStarted)) {
        advanceInlineLine(c, (MD_SIZE)(begin - base));
        c.imageStarted = c.inImage > 0;
    }
    if (c.styles && (t == MD_TEXT_NORMAL || t == MD_TEXT_ENTITY || t == MD_TEXT_CODE)) {
        if (fromSource && n <= c.utf8Size - (begin - base)) {
            const size_t offset = begin - base;
            const UINT32 start = c.sourceOffsets[offset];
            c.styles->push_back({start, c.sourceOffsets[offset + n] - start,
                                 c.bold, c.italic, c.code, c.strike, c.link >= 0});
        }
    }
    switch (t) {
    case MD_TEXT_NULLCHAR: {
        const wchar_t rep = 0xFFFD;
        addText(c, &rep, 1);
        break;
    }
    case MD_TEXT_BR:
    case MD_TEXT_SOFTBR: {
        if (const Block* b = curBlock(c); b && b->kind == BKind::Heading) {
            const wchar_t newline = L'\n';
            addText(c, &newline, 1);
        } else if (c.inImage) {
            const wchar_t space = L' ';
            addText(c, &space, 1);
        }
        break;
    }
    case MD_TEXT_HTML:
        break;
    default: {
        // Inline code emits a synthetic space at a line boundary. Source
        // offsets already move the next text to its actual line.
        if (!fromSource && c.code && !c.inCell && !c.inImage &&
            curBlock(c) && curBlock(c)->kind != BKind::Heading) break;
        std::wstring w = widen(s, (size_t)n);
        addText(c, w.c_str(), w.size());
        break;
    }
    }
    return 0;
}

}  // namespace

bool parseText(const std::wstring& text, Doc& doc, std::vector<SourceStyle>* styles) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr,
                                   nullptr);
    std::string utf8((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], need, nullptr, nullptr);

    Ctx c;
    c.doc = &doc;
    c.styles = styles;
    c.utf8 = utf8.data();
    c.utf8Size = utf8.size();
    if (styles) {
        styles->clear();
        c.sourceOffsets.resize(utf8.size() + 1);
        UINT32 units = 0;
        for (size_t i = 0; i < utf8.size(); ++i) {
            const auto byte = (unsigned char)utf8[i];
            c.sourceOffsets[i] = units;
            if (byte < 0x80 || byte >= 0xC0) units += byte >= 0xF0 ? 2 : 1;
        }
        c.sourceOffsets.back() = units;
    }
    doc.blocks.clear();
    doc.links.clear();
    for (size_t i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\n' || (utf8[i] == '\r' && (i + 1 == utf8.size() || utf8[i + 1] != '\n')))
            c.lineStarts.push_back((MD_SIZE)i + 1);
    }

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
                   MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_NOHTML;
    parser.enter_block = onEnterBlock;
    parser.leave_block = onLeaveBlock;
    parser.enter_span = onEnterSpan;
    parser.leave_span = onLeaveSpan;
    parser.text = onText;
    parser.leaf_range = onLeafRange;
    if (md_parse(utf8.c_str(), (MD_SIZE)utf8.size(), &parser, &c) != 0) return false;

    // Markdown omits blank lines and some syntax-only lines. The editor needs
    // a place for the caret on each of those source lines as well.
    std::vector<Block> parsed = std::move(doc.blocks);
    doc.blocks.reserve(std::max(c.lineStarts.size(), parsed.size()));
    int line = 0;
    const int lineCount = (int)c.lineStarts.size();
    auto fillUntil = [&](int end) {
        for (; line < end; ++line) {
            const size_t start = c.lineStarts[(size_t)line];
            size_t stop = line + 1 < lineCount ? c.lineStarts[(size_t)line + 1] : utf8.size();
            while (stop > start && (utf8[stop - 1] == '\n' || utf8[stop - 1] == '\r')) --stop;
            Block block;
            block.firstLine = block.lastLine = line;
            if (stop > start) {
                Run run;
                run.text = widen(utf8.data() + start, stop - start);
                block.runs.push_back(std::move(run));
            }
            doc.blocks.push_back(std::move(block));
        }
    };
    for (auto& block : parsed) {
        if (block.firstLine < line) continue;
        fillUntil(block.firstLine);
        if (block.kind == BKind::Para || block.kind == BKind::Bullet ||
            block.kind == BKind::Ordered || block.kind == BKind::Image)
            block.lastLine = block.firstLine;
        if (block.kind == BKind::Para && !block.runs.empty()) {
            const size_t start = c.lineStarts[(size_t)block.firstLine];
            const size_t end = block.firstLine + 1 < lineCount ?
                c.lineStarts[(size_t)block.firstLine + 1] : utf8.size();
            if (emptyParagraphTag(std::string_view(utf8).substr(start, end - start))) block.runs.clear();
        }
        line = block.lastLine + 1;
        doc.blocks.push_back(std::move(block));
    }
    fillUntil(lineCount);
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
