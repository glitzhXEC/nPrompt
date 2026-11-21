// ============================================================================
//  Prompt Pad - a tiny dark scratchpad for writing AI prompts on Windows
// ----------------------------------------------------------------------------
//  * No Save button. Text is auto-restored the next time you open the app.
//  * Ctrl+V an image or files -> shown as a compact chip (icon + file name),
//    but copying always yields the real full path.
//  * Code blocks with VS Code style syntax colours, moved/deleted with a click.
//  * Chips can be dragged around; the floating copy is translucent.
//
//  Build with MSVC:   build.bat
//  Build with MinGW:  g++ -std=c++17 -O2 -municode -mwindows nPrompt.cpp
//                        -o nPrompt.exe -lgdiplus -lcomctl32 -luxtheme
//                        -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -lmsimg32
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cwctype>

#include "icons_data.h"   // generated sprite sheet + suffix rules

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#endif

using std::wstring;

// ---------------------------------------------------------------- constants
static const wchar_t* kAppName   = L"nPrompt";
static const wchar_t* kRegKey    = L"Software\\nPrompt";
static const wchar_t* kRegKeyOld = L"Software\\PromptPad";
static const wchar_t* kWndClass  = L"nPromptWindow";
static const wchar_t* kViewClass = L"nPromptView";
static const wchar_t* kMenuClass = L"nPromptMenu";

static const UINT_PTR ID_TIMER  = 1;
static const int      ID_HOTKEY = 1;
static const int      IDI_APPICON = 1;

static const wchar_t CHIP_OPEN  = L'\xE000';
static const wchar_t CHIP_CLOSE = L'\xE001';

// colours
static const COLORREF C_BG      = RGB(0x10, 0x13, 0x18);
static const COLORREF C_EDITBG  = RGB(0x0B, 0x0E, 0x12);
static const COLORREF C_STATUS  = RGB(0x14, 0x18, 0x1E);
static const COLORREF C_TEXT    = RGB(0xE6, 0xED, 0xF3);
static const COLORREF C_DIM     = RGB(0x7A, 0x84, 0x90);
static const COLORREF C_GHOST   = RGB(0x3E, 0x46, 0x50);
static const COLORREF C_ACCENT  = RGB(0x7E, 0xE7, 0x87);
static const COLORREF C_LINE    = RGB(0x24, 0x2B, 0x33);
static const COLORREF C_THUMB   = RGB(0x3A, 0x44, 0x50);
static const COLORREF C_CARET   = RGB(0x7E, 0xE7, 0x87);
static const COLORREF C_SEL     = RGB(0x1E, 0x3C, 0x60);
static const COLORREF C_CHIPBG  = RGB(0x17, 0x1E, 0x27);
static const COLORREF C_CHIPLINE= RGB(0x2A, 0x35, 0x42);
static const COLORREF C_CODEBG  = RGB(0x0E, 0x15, 0x1D);
static const COLORREF C_CODEBAR = RGB(0x2B, 0x5A, 0x86);

// dark context menu
static const COLORREF C_MENUBG   = RGB(0x17, 0x1C, 0x24);
static const COLORREF C_MENUSEL  = RGB(0x25, 0x2F, 0x3C);
static const COLORREF C_MENUTEXT = RGB(0xE6, 0xED, 0xF3);
static const COLORREF C_MENUDIM  = RGB(0x7A, 0x84, 0x90);
static const COLORREF C_MENUDIS  = RGB(0x4B, 0x53, 0x5E);

// syntax colours (VS Code Dark+ family)
static const COLORREF HL_KEYWORD = RGB(0x56, 0x9C, 0xD6);
static const COLORREF HL_CONTROL = RGB(0xC5, 0x86, 0xC0);
static const COLORREF HL_STRING  = RGB(0xCE, 0x91, 0x78);
static const COLORREF HL_NUMBER  = RGB(0xB5, 0xCE, 0xA8);
static const COLORREF HL_COMMENT = RGB(0x6A, 0x99, 0x55);
static const COLORREF HL_FUNC    = RGB(0xDC, 0xDC, 0xAA);
static const COLORREF HL_TYPE    = RGB(0x4E, 0xC9, 0xB0);
static const COLORREF HL_VAR     = RGB(0x9C, 0xDC, 0xFE);
static const COLORREF HL_MACRO   = RGB(0xC5, 0x86, 0xC0);

static const int kDefaultFontPt = 12;
static const int kUndoLimit     = 120;

// ------------------------------------------------------------------ globals
static HINSTANCE g_hInst   = nullptr;
static HWND      g_hwnd    = nullptr;
static HWND      g_view    = nullptr;

static HFONT     g_hFont       = nullptr;
static HFONT     g_hStatusFont = nullptr;
static Gdiplus::Bitmap* g_iconSheet = nullptr;
static wstring   g_face        = L"Consolas";

static int       g_fontPt  = kDefaultFontPt;
static int       g_fontGen = 0;
static UINT      g_dpi     = 96;
static bool      g_topMost = false;
static bool      g_dirty   = false;
static ULONGLONG g_lastEdit = 0;

static wstring   g_toast;
static ULONGLONG g_toastUntil = 0;
static wstring   g_statusL, g_statusR;

static bool      g_hasRect = false;
static RECT      g_savedRect{};

static HBRUSH    g_brBg     = nullptr;
static HBRUSH    g_brEdit   = nullptr;
static HBRUSH    g_brStatus = nullptr;

static BOOL (WINAPI *g_allowDarkForWindow)(HWND, BOOL) = nullptr;

// ----------------------------------------------------------- document model
enum ChipKind {
    CK_FILE = 0,
    CK_IMAGE = 1,
    CK_FOLDER = 2,
    CK_URL = 3
};

struct Chip {
    wstring  path;
    wstring  label;
    int      kind = CK_FILE;
    int      icon = 0;
    int      width = 0;
    int      widthGen = -1;
};

struct Unit {
    wchar_t ch   = 0;
    int     chip = -1;
};

// a code block: a range of units [start, end) painted as a panel
enum Lang { LANG_C = 0, LANG_JS, LANG_CS, LANG_RUST, LANG_PY, LANG_LUA, LANG_SH,
            LANG_JSON, LANG_HTML, LANG_SQL };
static inline bool IsCFamily(int lang) { return lang <= LANG_RUST; }

struct Block { size_t start = 0, end = 0; int lang = LANG_C; };

struct LoadedBlock { int start = 0, end = 0, lang = LANG_C; };

static std::vector<Unit> g_units;
static std::vector<Chip> g_chips;
static std::vector<Block> g_blocks;
static std::vector<LoadedBlock> g_loadedBlocks;

static size_t g_caret  = 0;
static size_t g_anchor = 0;
static int    g_scrollY = 0;
static bool   g_dragging = false;
static bool   g_swallowChar = false;
static bool   g_blinkOn = true;

static int    g_lineH = 18;

// layout
struct Glyph { int line, x, w; };
static std::vector<Glyph> g_gl;
static std::vector<int>   g_lineStarts;
static std::vector<int>   g_lineY;
static int    g_layoutW = 0;
static bool   g_layoutDirty = true;

// cached counters for the status bar
static int    g_charCount = 0;
static int    g_lineCount = 1;
static int    g_fileCount = 0;
static int    g_linkCount = 0;

// chip drag & drop
static bool   g_chipPress = false;
static size_t g_chipPressPos = 0;
static POINT  g_chipPressPt{};
static bool   g_chipDrag = false;
static Chip   g_chipDragChip;
static size_t g_chipDragOrigin = 0;
static size_t g_chipDrop = 0;
static POINT  g_chipMouse{};

// hover state for the code block delete button
static int    g_hoverBlockLine = -1;
static bool   g_hoverDelete = false;

// owner drawn context menu
struct MenuRow {
    const wchar_t* label = nullptr;
    const wchar_t* accel = nullptr;
    UINT id = 0;
    bool separator = false;
    bool enabled = true;
};
static std::vector<MenuRow> g_menuRows;
static std::vector<RECT>    g_menuRects;
static HWND      g_menuHwnd = nullptr;
static int       g_menuResult = 0;
static int       g_menuHover = -1;
static int       g_menuW = 0, g_menuH = 0;
static bool      g_menuClosing = false;
static size_t    g_ctxChip = (size_t)-1;

// undo
struct Snapshot {
    std::vector<Unit> units;
    std::vector<Chip> chips;
    std::vector<Block> blocks;
    size_t caret = 0;
    size_t anchor = 0;
};
static std::vector<Snapshot> g_undo;
static ULONGLONG g_lastUndoTick = 0;
static int       g_lastUndoKind = 0;

static wstring   g_loadedDoc;

// ------------------------------------------------------------------ helpers
static inline int Dpi(int v) { return MulDiv(v, (int)g_dpi, 96); }
static inline int IconSize()    { return Dpi(16); }
static inline int ChipIconGap() { return Dpi(6); }
static inline int ChipPillPad() { return Dpi(7); }
static inline int ChipTail()    { return Dpi(3); }

static UINT GetDpiForWindowSafe(HWND hwnd) {
    typedef UINT (WINAPI *PFN)(HWND);
    static PFN fn = (PFN)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn) {
        UINT d = fn(hwnd);
        if (d) return d;
    }
    return 96;
}

static std::string ToUtf8(const wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static wstring FromUtf8(const char* p, int n) {
    if (!p || n <= 0) return wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, p, n, nullptr, 0);
    if (len <= 0) return wstring();
    wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p, n, &w[0], len);
    return w;
}

static wstring Commas(long long v) {
    wstring s = std::to_wstring(v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, L",");
    return s;
}

static bool IsImagePath(const wstring& p) {
    size_t dot = p.find_last_of(L'.');
    if (dot == wstring::npos) return false;
    wstring ext = p.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    static const wchar_t* exts[] = {
        L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp", L"tif", L"tiff",
        L"ico", L"svg", L"heic", L"avif", L"psd", L"dds", L"raw", L"cr2", L"nef"
    };
    for (const wchar_t* e : exts)
        if (ext == e) return true;
    return false;
}

static wstring ExtensionOf(const wstring& p) {
    size_t slash = p.find_last_of(L"\\/");
    size_t dot = p.find_last_of(L'.');
    if (dot == wstring::npos) return wstring();
    if (slash != wstring::npos && dot < slash) return wstring();
    wstring ext = p.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

static bool StartsWithNoCase(const wstring& s, const wchar_t* prefix) {
    size_t n = wcslen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (towlower(s[i]) != towlower(prefix[i])) return false;
    return true;
}

static wstring TrimText(const wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

static bool IsUrlText(const wstring& raw) {
    wstring s = TrimText(raw);
    if (s.empty()) return false;
    if (s.find(L'\n') != wstring::npos) return false;
    if (s.find(L' ') != wstring::npos) return false;
    return StartsWithNoCase(s, L"http://") || StartsWithNoCase(s, L"https://") ||
           StartsWithNoCase(s, L"ftp://")  || StartsWithNoCase(s, L"file://") ||
           StartsWithNoCase(s, L"mailto:") || StartsWithNoCase(s, L"www.");
}

static wstring UrlLabel(const wstring& raw) {
    wstring s = TrimText(raw);
    if (StartsWithNoCase(s, L"http://"))  s = s.substr(7);
    if (StartsWithNoCase(s, L"https://")) s = s.substr(8);
    if (StartsWithNoCase(s, L"ftp://"))   s = s.substr(6);
    const size_t kMax = 44;
    if (s.size() > kMax)
        s = s.substr(0, 24) + L"\u2026" + s.substr(s.size() - (kMax - 25));
    return s;
}

static bool IsDirectoryPath(const wstring& p) {
    DWORD attr = GetFileAttributesW(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// pick an icon from the generated rules
static int IconForFileName(const wstring& path) {
    wstring name = path;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != wstring::npos) name = name.substr(slash + 1);
    for (auto& ch : name) ch = (wchar_t)towlower(ch);

    for (const IconRule& r : kIconStems)
        if (name == r.key) return r.icon;

    size_t n = name.size();
    for (const IconRule& r : kIconSuffixes) {
        size_t kl = wcslen(r.key);
        if (kl == 0 || kl >= n) continue;
        if (name[n - kl - 1] == L'.' && name.compare(n - kl, kl, r.key) == 0)
            return r.icon;
    }
    return kIconFile;
}

static wstring FileLabel(const wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    wstring name = (slash == wstring::npos) ? path : path.substr(slash + 1);
    if (name.empty()) name = path;

    const size_t kMax = 44;
    if (name.size() > kMax)
        name = name.substr(0, 22) + L"\u2026" + name.substr(name.size() - (kMax - 23));
    return name;
}

static wstring StripQuotes(const wstring& p) {
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"')
        return p.substr(1, p.size() - 2);
    return p;
}

// -------------------------------------------------------------------- fonts
static int CALLBACK FontEnumProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
    *(bool*)lp = true;
    return 0;
}

static bool FontExists(const wchar_t* name) {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, name, LF_FACESIZE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, FontEnumProc, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}

static wstring PickMonoFont() {
    static const wchar_t* candidates[] = {
        L"Cascadia Mono", L"Cascadia Code", L"JetBrains Mono", L"Fira Code",
        L"Consolas", L"Lucida Console", L"Courier New"
    };
    for (const wchar_t* c : candidates)
        if (FontExists(c)) return wstring(c);
    return wstring(L"Consolas");
}

static void InvalidateLayout();
static void ClampScroll();
static void EnsureLayout();
static void ToggleTopMost();
static void SetFontPt(int pt);
static int  DetectLanguage(const wstring& code);
static const wchar_t* LangName(int lang);

static void ApplyFonts() {
    if (g_hFont)       { DeleteObject(g_hFont);       g_hFont = nullptr; }
    if (g_hStatusFont) { DeleteObject(g_hStatusFont); g_hStatusFont = nullptr; }

    int h = -MulDiv(g_fontPt, (int)g_dpi, 72);
    g_hFont = CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, g_face.c_str());
    if (!g_hFont) g_hFont = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);

    int hs = -MulDiv(9, (int)g_dpi, 72);
    g_hStatusFont = CreateFontW(hs, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!g_hStatusFont) g_hStatusFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HDC dc = GetDC(g_view ? g_view : nullptr);
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    g_lineH = tm.tmHeight + tm.tmExternalLeading;
    SelectObject(dc, old);
    ReleaseDC(g_view, dc);
    if (g_lineH < 8) g_lineH = 16;

    g_fontGen++;
    InvalidateLayout();
    ClampScroll();
    if (g_view) InvalidateRect(g_view, nullptr, FALSE);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

// --------------------------------------------------------------- doc basics
static bool IsChipUnit(size_t i) { return i < g_units.size() && g_units[i].chip >= 0; }

static bool IsNewlineUnit(size_t i) {
    return i < g_units.size() && g_units[i].chip < 0 && g_units[i].ch == L'\n';
}

static bool IsSpaceUnit(size_t i) {
    return i < g_units.size() && g_units[i].chip < 0 &&
           (g_units[i].ch == L' ' || g_units[i].ch == L'\t' || g_units[i].ch == L'\n');
}

static Chip MakeChip(const wstring& rawPath, int forceKind = -1) {
    Chip c;
    wstring p = StripQuotes(rawPath);

    int kind = forceKind;
    if (kind < 0) {
        if (IsUrlText(p))            kind = CK_URL;
        else if (IsDirectoryPath(p)) kind = CK_FOLDER;
        else if (IsImagePath(p))     kind = CK_IMAGE;
        else                         kind = CK_FILE;
    }
    c.path = p;
    c.kind = kind;

    if (kind == CK_URL) {
        c.label = UrlLabel(p);
        c.icon  = kIconUrl;
    } else {
        c.label = FileLabel(p);
        if (kind == CK_FOLDER)     c.icon = kIconFolder;
        else if (kind == CK_IMAGE) c.icon = kIconImage;
        else                       c.icon = IconForFileName(p);
    }
    return c;
}

static int AddChip(const Chip& c) {
    g_chips.push_back(c);
    return (int)g_chips.size() - 1;
}

static void BlocksOnInsert(size_t at, size_t count) {
    if (!count || g_blocks.empty()) return;
    for (Block& b : g_blocks) {
        if (at <= b.start) { b.start += count; b.end += count; }
        else if (at < b.end) { b.end += count; }
    }
}

static void BlocksOnDelete(size_t a, size_t b) {
    if (a >= b || g_blocks.empty()) return;
    size_t len = b - a;
    for (Block& blk : g_blocks) {
        if (b <= blk.start) {
            blk.start -= len;
            blk.end   -= len;
        } else if (a >= blk.end) {
            // untouched
        } else {
            size_t newStart = (blk.start < a) ? blk.start : a;
            size_t newEnd   = (blk.end > b) ? blk.end - len : a;
            blk.start = newStart;
            blk.end   = (newEnd < newStart) ? newStart : newEnd;
        }
    }
    g_blocks.erase(std::remove_if(g_blocks.begin(), g_blocks.end(),
                                  [](const Block& b) { return b.end <= b.start; }),
                   g_blocks.end());
}

static size_t InsertChipAt(size_t at, const Chip& c) {
    int idx = AddChip(c);
    Unit u;
    u.ch = 0;
    u.chip = idx;
    g_units.insert(g_units.begin() + at, u);
    BlocksOnInsert(at, 1);
    return at + 1;
}

static void InsertTextAt(size_t at, const wstring& text) {
    std::vector<Unit> add;
    add.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\t') {
            for (int k = 0; k < 4; ++k) { Unit u; u.ch = L' '; add.push_back(u); }
            continue;
        }
        if (c == CHIP_OPEN || c == CHIP_CLOSE) continue;
        Unit u;
        u.ch = c;
        add.push_back(u);
    }
    if (add.empty()) return;
    g_units.insert(g_units.begin() + at, add.begin(), add.end());
    BlocksOnInsert(at, add.size());
}

static void DeleteUnits(size_t a, size_t b) {
    if (a >= b || b > g_units.size()) return;
    g_units.erase(g_units.begin() + a, g_units.begin() + b);
    BlocksOnDelete(a, b);
}

static void Selection(size_t& a, size_t& b) {
    a = (g_caret < g_anchor) ? g_caret : g_anchor;
    b = (g_caret < g_anchor) ? g_anchor : g_caret;
    if (b > g_units.size()) b = g_units.size();
    if (a > b) a = b;
}

static wstring DocText(bool crlf = false) {
    wstring s;
    s.reserve(g_units.size());
    for (const Unit& u : g_units) {
        if (u.chip >= 0) {
            s += g_chips[(size_t)u.chip].path;
        } else if (crlf && u.ch == L'\n') {
            s += L"\r\n";
        } else {
            s += u.ch;
        }
    }
    return s;
}

static void RecountDoc() {
    int chars = 0;
    int lines = 1;
    int files = 0;
    int links = 0;
    for (const Unit& u : g_units) {
        if (u.chip >= 0) {
            const Chip& c = g_chips[(size_t)u.chip];
            chars += (int)c.path.size();
            if (c.kind == CK_URL) links++;
            else files++;
        } else {
            chars++;
            if (u.ch == L'\n') lines++;
        }
    }
    g_charCount = chars;
    g_lineCount = lines;
    g_fileCount = files;
    g_linkCount = links;
}

// ------------------------------------------------------------- serialisation
static wchar_t KindMarker(int kind) {
    switch (kind) {
    case CK_IMAGE:  return L'I';
    case CK_FOLDER: return L'D';
    case CK_URL:    return L'U';
    default:        return L'F';
    }
}

static int MarkerKind(wchar_t c) {
    switch (c) {
    case L'I': return CK_IMAGE;
    case L'D': return CK_FOLDER;
    case L'U': return CK_URL;
    default:   return CK_FILE;
    }
}

static wstring SerializeDoc() {
    wstring s;
    for (const Unit& u : g_units) {
        if (u.chip >= 0) {
            const Chip& c = g_chips[(size_t)u.chip];
            s += CHIP_OPEN;
            s += KindMarker(c.kind);
            s += c.path;
            s += CHIP_CLOSE;
        } else {
            s += u.ch;
        }
    }
    return s;
}

static void DeserializeDoc(const wstring& s) {
    g_units.clear();
    g_chips.clear();
    g_caret = g_anchor = 0;
    g_scrollY = 0;

    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == CHIP_OPEN) {
            size_t end = s.find(CHIP_CLOSE, i + 2);
            if (end != wstring::npos && end > i + 2) {
                int kind = MarkerKind(s[i + 1]);
                wstring path = s.substr(i + 2, end - i - 2);
                InsertChipAt(g_units.size(), MakeChip(path, kind));
                i = end + 1;
                continue;
            }
        }
        wchar_t c = s[i];
        if (c == L'\r') { ++i; continue; }
        if (c == L'\t') { InsertTextAt(g_units.size(), L"    "); ++i; continue; }
        Unit u;
        u.ch = c;
        g_units.push_back(u);
        ++i;
    }
    g_caret = g_anchor = g_units.size();
    RecountDoc();

    g_blocks.clear();
    for (const LoadedBlock& pr : g_loadedBlocks) {
        size_t a = pr.start < 0 ? 0 : (size_t)pr.start;
        size_t b = pr.end < 0 ? 0 : (size_t)pr.end;
        if (b > g_units.size()) b = g_units.size();
        if (b > a) {
            Block blk;
            blk.start = a;
            blk.end = b;
            blk.lang = (pr.lang >= 0 && pr.lang <= LANG_SQL) ? pr.lang : LANG_C;
            g_blocks.push_back(blk);
        }
    }
    g_loadedBlocks.clear();

    InvalidateLayout();
}

// ------------------------------------------------------------------- undo
static void PushUndo(int kind) {
    ULONGLONG now = GetTickCount64();
    bool coalesce = (kind != 0) && (kind == g_lastUndoKind) &&
                    (now - g_lastUndoTick < 700) && !g_undo.empty();
    g_lastUndoTick = now;
    g_lastUndoKind = kind;
    if (coalesce) return;
    if (g_undo.size() >= (size_t)kUndoLimit) g_undo.erase(g_undo.begin());
    Snapshot s;
    s.units  = g_units;
    s.chips  = g_chips;
    s.blocks = g_blocks;
    s.caret  = g_caret;
    s.anchor = g_anchor;
    g_undo.push_back(s);
}

static bool UndoOnce() {
    if (g_undo.empty()) return false;
    Snapshot s = g_undo.back();
    g_undo.pop_back();
    g_units.swap(s.units);
    g_chips.swap(s.chips);
    g_blocks.swap(s.blocks);
    g_caret  = s.caret;
    g_anchor = s.caret;
    if (g_caret > g_units.size()) g_caret = g_units.size();
    if (g_anchor > g_units.size()) g_anchor = g_units.size();
    g_lastUndoKind = 0;
    if (g_scrollY < 0) g_scrollY = 0;
    return true;
}

// ------------------------------------------------------------------ layout
static void TextArea(RECT* r) {
    RECT rc{};
    if (g_view) GetClientRect(g_view, &rc);
    r->left   = rc.left + Dpi(12);
    r->top    = rc.top + Dpi(8);
    r->right  = rc.right - Dpi(12) - Dpi(10);
    r->bottom = rc.bottom - Dpi(8);
    if (r->right  < r->left + 40) r->right  = r->left + 40;
    if (r->bottom < r->top + 20)  r->bottom = r->top + 20;
}

static HDC MeasureDC() {
    static HDC dc = nullptr;
    if (!dc) dc = CreateCompatibleDC(nullptr);
    return dc;
}

static int TextWidth(HDC dc, const wstring& s) {
    SIZE sz{};
    if (!s.empty()) GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    return sz.cx;
}

// Combining marks (Thai tone marks, vowels above/below, ...) have no advance of
// their own - they ride on the preceding base character.
static bool IsMarkChar(wchar_t c) {
    if ((unsigned)c < 128) return false;
    static wchar_t seen[64];
    static unsigned char seenVal[64];
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (seen[i] == c) return seenVal[i] != 0;

    WORD t = 0;
    unsigned char v = 0;
    // C3_VOWELMARK is NOT a good test: Thai leading vowels (เ แ โ ใ ไ) and SARA AA
    // are flagged as vowel marks but they are spacing characters with their own
    // advance. Only truly non-spacing marks (tone marks, above/below vowels) count.
    if (GetStringTypeW(CT_CTYPE3, &c, 1, &t))
        v = (t & (C3_NONSPACING | C3_DIACRITIC)) ? 1 : 0;
    if (count < 64) {
        seen[count] = c;
        seenVal[count] = v;
        ++count;
    }
    return v != 0;
}

static bool IsMarkUnit(size_t i) {
    return i < g_units.size() && g_units[i].chip < 0 && IsMarkChar(g_units[i].ch);
}

static size_t SnapPastMarks(size_t i) {
    while (i < g_units.size() && IsMarkUnit(i)) ++i;
    return i;
}

static int CharWidth(HDC dc, wchar_t c) {
    static int ascii[128] = {0};
    static int gen = -1;
    if (gen != g_fontGen) {
        for (int i = 0; i < 128; ++i) ascii[i] = 0;
        gen = g_fontGen;
    }
    if ((unsigned)c < 128) {
        if (ascii[(unsigned)c] == 0) {
            int w = 0;
            GetCharWidth32W(dc, c, c, &w);
            ascii[(unsigned)c] = (w > 0) ? w : 1;
        }
        return ascii[(unsigned)c];
    }
    int w = 0;
    GetCharWidth32W(dc, c, c, &w);
    return (w > 0) ? w : 0;
}

static int UnitWidth(HDC dc, size_t i) {
    const Unit& u = g_units[i];
    if (u.chip >= 0) {
        Chip& c = g_chips[(size_t)u.chip];
        if (c.widthGen != g_fontGen || c.width == 0) {
            c.width = IconSize() + ChipIconGap() + ChipPillPad() * 2 +
                      TextWidth(dc, c.label) + ChipTail();
            if (c.width < Dpi(60)) c.width = Dpi(60);
            c.widthGen = g_fontGen;
        }
        return c.width;
    }
    if (u.ch == L'\n') return 0;
    if ((unsigned)u.ch < 128) return CharWidth(dc, u.ch);
    if (IsMarkChar(u.ch)) return 0;

    // measure the whole cluster (base + its marks) exactly like GDI draws it
    wstring cluster(1, u.ch);
    size_t j = i + 1;
    while (j < g_units.size() && g_units[j].chip < 0 && g_units[j].ch != L'\n' &&
           IsMarkChar(g_units[j].ch)) {
        cluster += g_units[j].ch;
        ++j;
    }
    return TextWidth(dc, cluster);
}

static int LineCount() { return (int)g_lineStarts.size(); }

static int  LineHeight(int line);
static bool BlockFirstLine(int line);
static bool BlockLastLine(int line);

static bool LineIsInBlock(int line) {
    if (g_blocks.empty() || line < 0 || line >= LineCount()) return false;
    size_t s = (size_t)g_lineStarts[line];
    for (const Block& b : g_blocks)
        if (s >= b.start && s < b.end) return true;
    return false;
}

static void InvalidateLayout() {
    g_layoutDirty = true;
}

static void ComputeLayout(HDC dc, int width) {
    size_t n = g_units.size();
    g_gl.assign(n, Glyph{0, 0, 0});
    g_lineStarts.clear();

    int line = 0;
    size_t i = 0;

    for (;;) {
        g_lineStarts.push_back((int)i);
        if (i >= n) break;

        int x = 0;
        size_t lastSpace = (size_t)-1;

        while (i < n) {
            const Unit& u = g_units[i];

            if (u.chip < 0 && u.ch == L'\n') {
                g_gl[i].line = line;
                g_gl[i].x = x;
                g_gl[i].w = 0;
                ++i;
                break;
            }

            int w = UnitWidth(dc, i);
            if (x + w > width && x > 0) {
                if (lastSpace != (size_t)-1 && lastSpace >= (size_t)g_lineStarts.back()) {
                    i = lastSpace + 1;
                }
                break;
            }

            g_gl[i].line = line;
            g_gl[i].x = x;
            g_gl[i].w = w;
            if (u.chip < 0 && u.ch == L' ') {
                lastSpace = i;
            }
            x += w;
            ++i;
        }

        ++line;

        if (i >= n) {
            if (n > 0 && g_units[n - 1].chip < 0 && g_units[n - 1].ch == L'\n')
                g_lineStarts.push_back((int)n);
            break;
        }
    }

    g_layoutW = width;
    g_layoutDirty = false;

    int y = 0;
    g_lineY.assign(g_lineStarts.size(), 0);
    for (int l = 0; l < LineCount(); ++l) {
        g_lineY[l] = y;
        y += LineHeight(l);
    }
}

static bool BlockFirstLine(int line) {
    return LineIsInBlock(line) && (line == 0 || !LineIsInBlock(line - 1));
}

static bool BlockLastLine(int line) {
    return LineIsInBlock(line) && (line + 1 >= LineCount() || !LineIsInBlock(line + 1));
}

static int BlockPad() { return Dpi(7); }

static int LineHeight(int line) {
    int h = g_lineH;
    if (BlockFirstLine(line)) h += BlockPad();
    if (BlockLastLine(line))  h += BlockPad();
    return h;
}

static int LineTop(int line) {
    if (g_lineY.empty()) return 0;
    if (line < 0) line = 0;
    if (line >= (int)g_lineY.size()) line = (int)g_lineY.size() - 1;
    return g_lineY[line];
}

static int LineTextTop(int line) {
    return LineTop(line) + (BlockFirstLine(line) ? BlockPad() : 0);
}

static int LineAtY(int y) {
    if (g_lineY.empty()) return 0;
    int lo = 0, hi = (int)g_lineY.size() - 1, best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_lineY[mid] <= y) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return best;
}

static int LineOfUnit(size_t unit) {
    if (g_lineStarts.empty()) return 0;
    int lo = 0, hi = LineCount() - 1, best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((size_t)g_lineStarts[mid] <= unit) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return best;
}

static const Block* BlockOfLine(int line) {
    if (g_blocks.empty() || line < 0 || line >= LineCount()) return nullptr;
    size_t s = (size_t)g_lineStarts[line];
    for (const Block& b : g_blocks)
        if (s >= b.start && s < b.end) return &b;
    return nullptr;
}

static wstring LineTextAndMap(int line, std::vector<size_t>& map, bool& hasChip) {
    map.clear();
    hasChip = false;
    wstring s;
    size_t start = (size_t)g_lineStarts[line];
    size_t end = (line + 1 < LineCount()) ? (size_t)g_lineStarts[line + 1] : g_units.size();
    for (size_t i = start; i < end; ++i) {
        const Unit& u = g_units[i];
        if (u.chip >= 0) {
            hasChip = true;
            s += L' ';
            map.push_back(i);
        } else if (u.ch == L'\n') {
            break;
        } else {
            s += u.ch;
            map.push_back(i);
        }
    }
    return s;
}

static wstring DocRangeText(size_t a, size_t b) {
    wstring s;
    if (b > g_units.size()) b = g_units.size();
    for (size_t i = a; i < b; ++i) {
        const Unit& u = g_units[i];
        if (u.chip >= 0) s += g_chips[(size_t)u.chip].path;
        else s += u.ch;
    }
    return s;
}

static void EnsureLayout() {
    HDC dc = MeasureDC();
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    RECT ta{};
    TextArea(&ta);
    int w = ta.right - ta.left;
    if (g_layoutDirty || w != g_layoutW) ComputeLayout(dc, w);
    SelectObject(dc, old);
}

static void CaretXY(size_t idx, int& cx, int& cy) {
    size_t n = g_units.size();
    if (n == 0) { cx = 0; cy = 0; return; }
    if (idx >= n) {
        if (g_units[n - 1].chip < 0 && g_units[n - 1].ch == L'\n') {
            cx = 0;
            cy = LineTextTop(g_gl[n - 1].line + 1);
        } else {
            cx = g_gl[n - 1].x + g_gl[n - 1].w;
            cy = LineTextTop(g_gl[n - 1].line);
        }
    } else {
        cx = g_gl[idx].x;
        cy = LineTextTop(g_gl[idx].line);
    }
}

static void CaretLineCol(int& line, int& col) {
    EnsureLayout();
    size_t n = g_units.size();
    if (n == 0) { line = 0; col = 1; return; }
    if (g_caret >= n) {
        line = LineCount() - 1;
        size_t start = (size_t)g_lineStarts[line];
        col = (int)(n - start) + 1;
        return;
    }
    line = g_gl[g_caret].line;
    col = (int)(g_caret - (size_t)g_lineStarts[line]) + 1;
}

static int ContentHeight() {
    if (g_lineY.empty()) return 0;
    int last = LineCount() - 1;
    return g_lineY[last] + LineHeight(last);
}

static size_t LineEndIndex(int line) {
    size_t n = g_units.size();
    if (line < 0) line = 0;
    if (line >= LineCount()) line = LineCount() - 1;
    size_t e = (line + 1 < LineCount()) ? (size_t)g_lineStarts[line + 1] : n;
    if (e > 0 && e <= n && g_units[e - 1].chip < 0 && g_units[e - 1].ch == L'\n')
        e--;
    return e;
}

static void ClampScroll() {
    EnsureLayout();
    RECT ta{};
    TextArea(&ta);
    int viewH = ta.bottom - ta.top;
    int maxY = ContentHeight() - viewH;
    if (maxY < 0) maxY = 0;
    if (g_scrollY > maxY) g_scrollY = maxY;
    if (g_scrollY < 0) g_scrollY = 0;
}

static void EnsureCaretVisible() {
    EnsureLayout();
    RECT ta{};
    TextArea(&ta);
    int viewH = ta.bottom - ta.top;
    int cx, cy;
    CaretXY(g_caret, cx, cy);
    if (cy < g_scrollY) g_scrollY = cy;
    if (cy + g_lineH > g_scrollY + viewH) g_scrollY = cy + g_lineH - viewH;
    ClampScroll();
}

static size_t HitTest(int px, int py) {
    EnsureLayout();
    int line = LineAtY(py + g_scrollY);
    if (line < 0) line = 0;
    if (line >= LineCount()) line = LineCount() - 1;

    size_t start = (size_t)g_lineStarts[line];
    size_t end = (line + 1 < LineCount()) ? (size_t)g_lineStarts[line + 1] : g_units.size();

    for (size_t i = start; i < end; ++i) {
        if (g_units[i].chip < 0 && g_units[i].ch == L'\n') return i;
        if (px < g_gl[i].x + g_gl[i].w / 2) return SnapPastMarks(i);
    }
    return LineEndIndex(line);
}

static size_t PosFromX(int line, int x) {
    if (line < 0) line = 0;
    if (line >= LineCount()) line = LineCount() - 1;
    size_t start = (size_t)g_lineStarts[line];
    size_t end = (line + 1 < LineCount()) ? (size_t)g_lineStarts[line + 1] : g_units.size();
    for (size_t i = start; i < end; ++i) {
        if (g_units[i].chip < 0 && g_units[i].ch == L'\n') return i;
        if (x < g_gl[i].x + g_gl[i].w / 2) return SnapPastMarks(i);
    }
    return LineEndIndex(line);
}

// ------------------------------------------------------------ status / toast
static void UpdateStatus() {
    if (!g_hwnd) return;

    int line = 0, col = 1;
    CaretLineCol(line, col);

    wstring L = L"Ln " + std::to_wstring(line + 1) + L", Col " + std::to_wstring(col);
    L += L"   \x00B7   " + Commas(g_charCount) + L" chars";
    L += L"   \x00B7   " + Commas(g_lineCount) + (g_lineCount == 1 ? L" line" : L" lines");
    if (g_fileCount > 0)
        L += L"   \x00B7   " + Commas(g_fileCount) + (g_fileCount == 1 ? L" file" : L" files");
    if (g_linkCount > 0)
        L += L"   \x00B7   " + Commas(g_linkCount) + (g_linkCount == 1 ? L" link" : L" links");

    wstring R = g_toast.empty()
        ? wstring(L"Ctrl+V image/file \u2192 path   \x00B7   Ctrl+Enter copy all   \x00B7   Ctrl+Alt+P")
        : g_toast;

    if (L != g_statusL || R != g_statusR) {
        g_statusL = L;
        g_statusR = R;
        RECT rc{};
        GetClientRect(g_hwnd, &rc);
        RECT sb{ 0, rc.bottom - Dpi(30), rc.right, rc.bottom };
        InvalidateRect(g_hwnd, &sb, FALSE);
    }
}

static void Toast(const wstring& msg) {
    g_toast = msg;
    g_toastUntil = GetTickCount64() + 2200;
    UpdateStatus();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------- persistence
static void SaveAll() {
    if (!g_hwnd) return;

    std::string u8 = ToUtf8(SerializeDoc());

    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;

    RegSetValueExW(k, L"Text", 0, REG_BINARY, (const BYTE*)u8.data(), (DWORD)u8.size());

    wstring blocks;
    for (const Block& b : g_blocks) {
        if (!blocks.empty()) blocks += L',';
        blocks += std::to_wstring(b.start) + L"-" + std::to_wstring(b.end) +
                  L"-" + std::to_wstring(b.lang);
    }
    RegSetValueExW(k, L"Blocks", 0, REG_SZ, (const BYTE*)blocks.c_str(),
                   (DWORD)((blocks.size() + 1) * sizeof(wchar_t)));

    DWORD v = (DWORD)g_fontPt;
    RegSetValueExW(k, L"FontPt", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));

    v = g_topMost ? 1u : 0u;
    RegSetValueExW(k, L"TopMost", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));

    if (!IsIconic(g_hwnd) && !IsZoomed(g_hwnd)) {
        RECT r{};
        GetWindowRect(g_hwnd, &r);
        RegSetValueExW(k, L"Rect", 0, REG_BINARY, (const BYTE*)&r, sizeof(r));
    }
    RegCloseKey(k);

    g_dirty = false;
}

static void LoadPrefs() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) {
        // first run after the rename: pick up the notes from the old key
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKeyOld, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
            return;
    }

    DWORD type = 0, cb = 0;

    DWORD pt = 0;
    cb = sizeof(pt);
    if (RegQueryValueExW(k, L"FontPt", nullptr, &type, (BYTE*)&pt, &cb) == ERROR_SUCCESS &&
        type == REG_DWORD && pt >= 8 && pt <= 42)
        g_fontPt = (int)pt;

    DWORD top = 0;
    cb = sizeof(top);
    if (RegQueryValueExW(k, L"TopMost", nullptr, &type, (BYTE*)&top, &cb) == ERROR_SUCCESS &&
        type == REG_DWORD)
        g_topMost = top != 0;

    RECT r{};
    cb = sizeof(r);
    if (RegQueryValueExW(k, L"Rect", nullptr, &type, (BYTE*)&r, &cb) == ERROR_SUCCESS &&
        type == REG_BINARY && cb == sizeof(r) && r.right > r.left && r.bottom > r.top) {
        g_savedRect = r;
        g_hasRect = true;
    }

    cb = 0;
    if (RegQueryValueExW(k, L"Text", nullptr, &type, nullptr, &cb) == ERROR_SUCCESS &&
        (type == REG_BINARY || type == REG_SZ) && cb > 0) {
        std::string u8((size_t)cb + 1, '\0');
        DWORD cb2 = cb;
        if (RegQueryValueExW(k, L"Text", nullptr, &type, (BYTE*)&u8[0], &cb2) == ERROR_SUCCESS)
            g_loadedDoc = FromUtf8(u8.data(), (int)cb2);
    }

    cb = 0;
    if (RegQueryValueExW(k, L"Blocks", nullptr, &type, nullptr, &cb) == ERROR_SUCCESS &&
        type == REG_SZ && cb > sizeof(wchar_t)) {
        std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, 0);
        DWORD cb2 = cb;
        if (RegQueryValueExW(k, L"Blocks", nullptr, &type, (BYTE*)buf.data(), &cb2) == ERROR_SUCCESS) {
            wstring s(buf.data());
            size_t i = 0;
            while (i < s.size()) {
                size_t comma = s.find(L',', i);
                wstring item = s.substr(i, comma == wstring::npos ? wstring::npos : comma - i);
                size_t d1 = item.find(L'-');
                if (d1 != wstring::npos) {
                    size_t d2 = item.find(L'-', d1 + 1);
                    LoadedBlock lb;
                    lb.start = _wtoi(item.c_str());
                    lb.end = _wtoi(item.c_str() + d1 + 1);
                    lb.lang = (d2 != wstring::npos) ? _wtoi(item.c_str() + d2 + 1) : LANG_C;
                    if (lb.end > lb.start) g_loadedBlocks.push_back(lb);
                }
                if (comma == wstring::npos) break;
                i = comma + 1;
            }
        }
    }

    RegCloseKey(k);
}

// ----------------------------------------------------------- image -> path
static wstring ImageDirNamed(const wchar_t* leaf) {
    wchar_t tmp[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    wstring d = (n ? wstring(tmp, n) : wstring(L"C:\\Temp\\")) + leaf;
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static wstring ImageDir() { return ImageDirNamed(L"nPrompt"); }

static void CleanupOldImagesIn(const wstring& dir) {
    FILETIME nowFt{};
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now{};
    now.LowPart  = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;

    const ULONGLONG kMaxAge = 30ULL * 24 * 60 * 60 * 10000000ULL;

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        ULARGE_INTEGER t{};
        t.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
        t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        if (now.QuadPart > t.QuadPart && now.QuadPart - t.QuadPart > kMaxAge)
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

static void CleanupOldImages() {
    CleanupOldImagesIn(ImageDir());
    CleanupOldImagesIn(ImageDirNamed(L"PromptPad"));   // folder used before the rename
}

static int GetEncoderClsid(const wchar_t* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return -1;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)buf.data();
    if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok) return -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) {
            *clsid = codecs[i].Clsid;
            return (int)i;
        }
    }
    return -1;
}

static bool SaveHBitmapAsPng(HBITMAP hbm, const wstring& path) {
    if (!hbm) return false;
    CLSID png{};
    if (GetEncoderClsid(L"image/png", &png) < 0) return false;

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromHBITMAP(hbm, nullptr);
    if (!bmp) return false;
    bool ok = bmp->Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
    delete bmp;
    return ok;
}

static wstring NewImagePath() {
    static int counter = 0;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char name[128]{};
    sprintf(name, "img_%04d%02d%02d_%02d%02d%02d_%d.png",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ++counter);
    wchar_t wname[128]{};
    MultiByteToWideChar(CP_ACP, 0, name, -1, wname, 128);
    return ImageDir() + L"\\" + wname;
}

static wstring SaveClipboardImageLocked() {
    wstring path = NewImagePath();

    UINT pngFmt = RegisterClipboardFormatW(L"PNG");
    if (pngFmt && IsClipboardFormatAvailable(pngFmt)) {
        HANDLE h = GetClipboardData(pngFmt);
        if (h) {
            const void* p = GlobalLock(h);
            SIZE_T sz = GlobalSize(h);
            bool ok = false;
            if (p && sz > 8) {
                HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD wrote = 0;
                    ok = WriteFile(f, p, (DWORD)sz, &wrote, nullptr) && wrote == (DWORD)sz;
                    CloseHandle(f);
                }
            }
            if (p) GlobalUnlock(h);
            if (ok) return path;
        }
    }

    if (IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE h = GetClipboardData(CF_DIB);
        if (h) {
            BITMAPINFO* bi = (BITMAPINFO*)GlobalLock(h);
            if (bi) {
                SIZE_T total = GlobalSize(h);
                size_t off = bi->bmiHeader.biSize;

                if (bi->bmiHeader.biCompression == BI_BITFIELDS &&
                    bi->bmiHeader.biSize == sizeof(BITMAPINFOHEADER))
                    off += 3 * sizeof(DWORD);

                if (bi->bmiHeader.biBitCount <= 8) {
                    UINT colors = bi->bmiHeader.biClrUsed
                                    ? bi->bmiHeader.biClrUsed
                                    : (1u << bi->bmiHeader.biBitCount);
                    off += (size_t)colors * sizeof(RGBQUAD);
                }

                bool ok = false;
                if (off < total) {
                    HDC dc = GetDC(nullptr);
                    HBITMAP hbm = CreateDIBitmap(dc, &bi->bmiHeader, CBM_INIT,
                                                 (BYTE*)bi + off, bi, DIB_RGB_COLORS);
                    if (dc) ReleaseDC(nullptr, dc);
                    if (hbm) {
                        ok = SaveHBitmapAsPng(hbm, path);
                        DeleteObject(hbm);
                    }
                }
                GlobalUnlock(h);
                if (ok) return path;
            }
        }
    }

    if (IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP src = (HBITMAP)GetClipboardData(CF_BITMAP);
        if (src) {
            HBITMAP copy = (HBITMAP)CopyImage(src, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            if (copy) {
                bool ok = SaveHBitmapAsPng(copy, path);
                DeleteObject(copy);
                if (ok) return path;
            }
        }
    }

    DeleteFileW(path.c_str());
    return wstring();
}

// ------------------------------------------------------------ clipboard out
static bool PutClipboardText(const wstring& text) {
    if (!OpenClipboard(g_hwnd)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

static wstring SelectionText(bool crlf) {
    size_t a, b;
    Selection(a, b);
    wstring s;
    for (size_t i = a; i < b; ++i) {
        const Unit& u = g_units[i];
        if (u.chip >= 0) {
            s += g_chips[(size_t)u.chip].path;
        } else if (crlf && u.ch == L'\n') {
            s += L"\r\n";
        } else {
            s += u.ch;
        }
    }
    return s;
}

static void DocChanged(int undoKind = 0);

static void CopySelection(bool cut) {
    size_t a, b;
    Selection(a, b);
    if (a == b) {
        Toast(L"Nothing selected");
        return;
    }
    wstring s = SelectionText(true);
    if (!PutClipboardText(s)) {
        Toast(L"Could not access the clipboard");
        return;
    }
    if (cut) {
        PushUndo(0);
        DeleteUnits(a, b);
        g_caret = g_anchor = a;
        DocChanged(0);
        Toast(L"Cut to clipboard");
    } else {
        Toast(L"Copied to clipboard");
    }
}

static void CopyAll() {
    wstring t = DocText(true);
    if (t.empty()) {
        Toast(L"Nothing to copy yet");
        return;
    }
    if (PutClipboardText(t))
        Toast(L"All text copied - paste it anywhere");
}

static void SelectAll() {
    g_anchor = 0;
    g_caret = g_units.size();
    g_swallowChar = true;
    EnsureCaretVisible();
    if (g_view) InvalidateRect(g_view, nullptr, FALSE);
    DocChanged(0);
}

// ------------------------------------------------------- smart paste pieces
struct PastePiece { bool code; wstring text; };
struct RichItem   { bool chip; wstring text; };

static bool HasCodeComment(const wstring& t) {
    for (size_t i = 0; i + 1 < t.size(); ++i) {
        if (t[i] == L'/' && t[i + 1] == L'/') {
            if (i > 0 && t[i - 1] == L':') continue;
            return true;
        }
        if (t[i] == L'/' && t[i + 1] == L'*') return true;
    }
    return false;
}

static bool HasWordToken(const wstring& t, const wchar_t* tok) {
    auto ident = [](wchar_t c) { return iswalnum(c) || c == L'_' || c == L'$'; };
    size_t n = wcslen(tok);
    size_t pos = t.find(tok);
    while (pos != wstring::npos) {
        if (pos == 0 || !ident(t[pos - 1])) return true;
        pos = t.find(tok, pos + 1);
    }
    return false;
}

static int CodeScore(const wstring& line) {
    size_t a = 0;
    while (a < line.size() && (line[a] == L' ' || line[a] == L'\t')) ++a;
    wstring t = line.substr(a);
    if (t.empty()) return -1;

    if (t.find(L'{') != wstring::npos || t.find(L'}') != wstring::npos) return 2;
    if (t.back() == L';') return 2;
    if (HasCodeComment(t)) return 2;
    if (t.rfind(L"#include", 0) == 0 || t.rfind(L"#define", 0) == 0) return 2;
    if (t.find(L"=>") != wstring::npos || t.find(L"->") != wstring::npos) return 2;

    static const wchar_t* tokens[] = {
        L"function ", L"def ", L"class ", L"const ", L"let ", L"var ",
        L"import ", L"return ", L"if (", L"for (", L"while (", L"else",
        L"public ", L"private ", L"void ", L"print(", L"local "
    };
    for (const wchar_t* k : tokens)
        if (HasWordToken(t, k)) return 2;

    if (t.back() == L':') {
        if (t.find(L"__") != wstring::npos || t.find(L'(') != wstring::npos ||
            t.find(L"==") != wstring::npos || t.find(L"!=") != wstring::npos)
            return 2;

        static const wchar_t* heads[] = { L"if ", L"elif ", L"for ", L"while ",
                                          L"else", L"try", L"except ", L"with ",
                                          L"def ", L"class ", L"match ", L"case " };
        for (const wchar_t* h : heads)
            if (HasWordToken(t, h)) return 2;

        return 1;
    }

    return (a >= 2) ? 1 : 0;
}

static wstring JoinLines(const std::vector<wstring>& lines) {
    wstring s;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) s += L'\n';
        s += lines[i];
    }
    return s;
}

static std::vector<PastePiece> SplitPaste(const wstring& raw) {
    wstring s;
    s.reserve(raw.size());
    for (wchar_t c : raw) if (c != L'\r') s += c;

    std::vector<wstring> lines;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\n') {
            lines.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    lines.push_back(s.substr(start));

    std::vector<int> cls(lines.size(), 0);
    int prev = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        int score = CodeScore(lines[i]);
        if (score < 0) {
            cls[i] = prev;
        } else if (score >= 2) {
            cls[i] = 1;
            prev = 1;
        } else if (score == 1 && prev == 1) {
            cls[i] = 1;
        } else {
            cls[i] = 0;
            prev = 0;
        }
    }

    std::vector<PastePiece> out;
    size_t i = 0;
    while (i < lines.size()) {
        int c = cls[i];
        size_t j = i;
        while (j < lines.size() && cls[j] == c) ++j;

        std::vector<wstring> run(lines.begin() + i, lines.begin() + j);
        i = j;

        if (c == 1) {
            while (!run.empty() && TrimText(run.front()).empty()) run.erase(run.begin());
            while (!run.empty() && TrimText(run.back()).empty()) run.pop_back();

            int nonEmpty = 0;
            bool strong = false;
            for (const wstring& l : run) {
                if (TrimText(l).empty()) continue;
                ++nonEmpty;
                wstring t = TrimText(l);
                if (l.find(L'{') != wstring::npos || l.find(L'}') != wstring::npos ||
                    (!t.empty() && t.back() == L';'))
                    strong = true;
            }
            if (nonEmpty == 0) continue;
            if (nonEmpty == 1 && !strong) {
                PastePiece p{ false, JoinLines(run) };
                out.push_back(p);
                continue;
            }
            PastePiece p{ true, JoinLines(run) };
            out.push_back(p);
        } else {
            PastePiece p{ false, JoinLines(run) };
            out.push_back(p);
        }
    }

    std::vector<PastePiece> merged;
    for (const PastePiece& p : out) {
        if (!p.code && !merged.empty() && !merged.back().code)
            merged.back().text += L"\n" + p.text;
        else
            merged.push_back(p);
    }
    return merged;
}

static bool PrefixAt(const wstring& s, size_t i, const wchar_t* p) {
    size_t k = wcslen(p);
    if (i + k > s.size()) return false;
    for (size_t j = 0; j < k; ++j)
        if (towlower(s[i + j]) != towlower(p[j])) return false;
    return true;
}

static bool IsPathStartChar(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'\\';
}

static size_t ScanPathAt(const wstring& s, size_t i) {
    size_t n = s.size();
    bool drive = (i + 2 < n) && iswalpha(s[i]) && s[i + 1] == L':' &&
                 (s[i + 2] == L'\\' || s[i + 2] == L'/');
    bool unc = (i + 1 < n) && s[i] == L'\\' && s[i + 1] == L'\\';
    if (!drive && !unc) return 0;

    size_t j = i + (drive ? 3 : 2);
    size_t hardEnd = j;
    while (hardEnd < n) {
        wchar_t c = s[hardEnd];
        if (c == L'\n' || c == L'\r' || c == L'"' || c == L'<' || c == L'>' ||
            c == L'|' || c == L'*' || c == L'?')
            break;
        ++hardEnd;
    }
    if (hardEnd <= i + 3) return 0;

    for (size_t e = hardEnd; e > i + 3; --e) {
        wchar_t last = s[e - 1];
        if (last == L' ' || last == L'.' || last == L',' || last == L')' ||
            last == L';' || last == L'!' || last == L'\'' || last == L'"')
            continue;
        wstring cand = s.substr(i, e - i);
        if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES)
            return e - i;
    }

    size_t e = i;
    while (e < hardEnd && s[e] != L' ' && s[e] != L'\t') ++e;
    while (e > i + 3 && (s[e - 1] == L'.' || s[e - 1] == L',' || s[e - 1] == L')' ||
                         s[e - 1] == L';' || s[e - 1] == L'!' || s[e - 1] == L'\''))
        --e;
    if (e - i >= 4 && s.substr(i, e - i).find(L'\\', 3) != wstring::npos)
        return e - i;
    return 0;
}

static size_t ScanUrlAt(const wstring& s, size_t i) {
    static const wchar_t* schemes[] = { L"https://", L"http://", L"ftp://", L"www." };
    bool ok = false;
    for (const wchar_t* sc : schemes)
        if (PrefixAt(s, i, sc)) { ok = true; break; }
    if (!ok) return 0;

    size_t n = s.size(), e = i;
    while (e < n && !iswspace(s[e]) && s[e] != L'<' && s[e] != L'>' &&
           s[e] != L'"' && s[e] != L'\'')
        ++e;
    while (e > i + 4 && (s[e - 1] == L'.' || s[e - 1] == L',' || s[e - 1] == L')' ||
                         s[e - 1] == L';' || s[e - 1] == L'!' || s[e - 1] == L'?') &&
           s[e - 2] != L'=')
        --e;
    return (e - i > 8) ? e - i : 0;
}

static void BuildRichItems(const wstring& text, std::vector<RichItem>& out) {
    size_t n = text.size(), i = 0;
    wstring buf;
    auto flush = [&]() {
        if (!buf.empty()) {
            RichItem it;
            it.chip = false;
            it.text = buf;
            out.push_back(it);
            buf.clear();
        }
    };

    while (i < n) {
        size_t len = 0;
        wchar_t c = text[i];
        if (IsPathStartChar(c))
            len = ScanPathAt(text, i);
        if (!len && (c == L'h' || c == L'H' || c == L'w' || c == L'W' || c == L'f' || c == L'F'))
            len = ScanUrlAt(text, i);

        if (len) {
            flush();
            RichItem it;
            it.chip = true;
            it.text = text.substr(i, len);
            out.push_back(it);
            i += len;
        } else {
            buf += text[i++];
        }
    }
    flush();
}

static void InsertPastedText(const wstring& text) {
    std::vector<PastePiece> pieces = SplitPaste(text);

    size_t a, b;
    Selection(a, b);
    PushUndo(0);
    if (b > a) DeleteUnits(a, b);

    size_t pos = a;
    for (size_t pi = 0; pi < pieces.size(); ++pi) {
        const PastePiece& p = pieces[pi];
        if (p.code) {
            if (pos > 0 && !IsNewlineUnit(pos - 1)) {
                InsertTextAt(pos, L"\n");
                ++pos;
            }
            if (pos > 0 && !(pos >= 2 && IsNewlineUnit(pos - 2))) {
                InsertTextAt(pos, L"\n");
                ++pos;
            }
            size_t before = g_units.size();
            InsertTextAt(pos, p.text);
            size_t added = g_units.size() - before;
            if (added) {
                Block blk;
                blk.start = pos;
                blk.end   = pos + added;
                blk.lang  = DetectLanguage(p.text);
                g_blocks.push_back(blk);
                pos += added;
            }
            if (pos == 0 || !IsNewlineUnit(pos - 1)) {
                InsertTextAt(pos, L"\n");
                ++pos;
            }
            if (pi + 1 < pieces.size() && !(pos >= 2 && IsNewlineUnit(pos - 2))) {
                InsertTextAt(pos, L"\n");
                ++pos;
            }
        } else {
            std::vector<RichItem> items;
            BuildRichItems(p.text, items);
            if (!items.empty() && items.front().chip && pos > 0 && !IsSpaceUnit(pos - 1)) {
                InsertTextAt(pos, L" ");
                ++pos;
            }
            for (const RichItem& it : items) {
                if (it.chip) {
                    if (pos > 0 && !IsSpaceUnit(pos - 1)) {
                        InsertTextAt(pos, L" ");
                        ++pos;
                    }
                    pos = InsertChipAt(pos, MakeChip(it.text));
                } else {
                    size_t before = g_units.size();
                    InsertTextAt(pos, it.text);
                    pos += g_units.size() - before;
                }
            }
            if (!items.empty() && items.back().chip && pos < g_units.size() && !IsSpaceUnit(pos))
                InsertTextAt(pos, L" ");
        }
    }

    g_caret = g_anchor = pos;
    DocChanged(0);
}

static void InsertChips(const std::vector<wstring>& paths, int forceKind) {
    if (paths.empty()) return;

    size_t a, b;
    Selection(a, b);
    PushUndo(0);
    if (b > a) DeleteUnits(a, b);

    size_t pos = a;
    if (pos > 0 && !IsSpaceUnit(pos - 1)) {
        InsertTextAt(pos, L" ");
        ++pos;
    }

    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) {
            InsertTextAt(pos, L" ");
            pos += 1;
        }
        Chip c = MakeChip(paths[i], forceKind);
        pos = InsertChipAt(pos, c);
    }
    g_caret = g_anchor = pos;

    if (pos < g_units.size() && !IsSpaceUnit(pos)) {
        InsertTextAt(pos, L" ");
    }

    DocChanged(0);
}

static bool PasteFromClipboard() {
    if (!OpenClipboard(g_hwnd)) return false;

    bool handled = false;
    std::vector<wstring> files;

    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HDROP drop = (HDROP)GetClipboardData(CF_HDROP);
        if (drop) {
            UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                wchar_t p[MAX_PATH * 4]{};
                if (DragQueryFileW(drop, i, p, ARRAYSIZE(p)))
                    files.push_back(p);
            }
        }
        if (!files.empty()) {
            CloseClipboard();
            InsertChips(files, -1);
            return true;
        }
    }

    bool hasText = false;
    wstring text;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* p = (const wchar_t*)GlobalLock(h);
            if (p) {
                text = p;
                hasText = !text.empty();
                GlobalUnlock(h);
            }
        }
    }

    if (!hasText) {
        wstring img = SaveClipboardImageLocked();
        if (!img.empty()) {
            CloseClipboard();
            InsertChips({ img }, CK_IMAGE);
            return true;
        }
    }

    CloseClipboard();

    if (hasText) {
        wstring trimmed = TrimText(text);
        if (IsUrlText(trimmed)) {
            InsertChips({ trimmed }, CK_URL);
        } else {
            InsertPastedText(text);
        }
        handled = true;
    }
    return handled;
}

// ------------------------------------------------------------ code blocks
static void ToggleCodeBlock() {
    size_t a, b;
    Selection(a, b);
    if (a == b) {
        Toast(L"Select the lines you want as code first");
        return;
    }

    EnsureLayout();

    std::vector<Block> keep;
    bool removed = false;
    for (const Block& blk : g_blocks) {
        if (a < blk.end && b > blk.start) removed = true;
        else keep.push_back(blk);
    }

    PushUndo(0);
    if (removed) {
        g_blocks.swap(keep);
        Toast(L"Code block removed");
    } else {
        int la = LineOfUnit(a);
        int lb = LineOfUnit(b > a ? b - 1 : a);
        size_t s = (size_t)g_lineStarts[la];
        size_t e = (lb + 1 < LineCount()) ? (size_t)g_lineStarts[lb + 1] : g_units.size();
        if (e > s && IsNewlineUnit(e - 1)) --e;
        if (e > s) {
            Block blk;
            blk.start = s;
            blk.end   = e;
            blk.lang  = DetectLanguage(DocRangeText(s, e));
            g_blocks.push_back(blk);
            Toast(wstring(L"Code block: ") + LangName(blk.lang));
        }
    }
    DocChanged(0);
}

static void DeleteCodeBlockAt(int firstLine) {
    const Block* found = nullptr;
    for (const Block& blk : g_blocks) {
        if (LineOfUnit(blk.start) == firstLine) { found = &blk; break; }
    }
    if (!found) return;

    size_t a = found->start;
    size_t e = found->end;
    PushUndo(0);
    if (e < g_units.size() && IsNewlineUnit(e)) ++e;
    DeleteUnits(a, e);
    if (g_caret > g_units.size()) g_caret = g_units.size();
    g_caret = g_anchor = (a < g_units.size()) ? a : g_units.size();
    g_hoverBlockLine = -1;
    g_hoverDelete = false;
    DocChanged(0);
    Toast(L"Code block deleted");
}

// ------------------------------------------------------ syntax highlighting
struct HlToken { size_t a, b; COLORREF color; };

static bool IsIdentStart(wchar_t c) { return iswalpha(c) || c == L'_' || c == L'$'; }
static bool IsIdentChar(wchar_t c)  { return iswalnum(c) || c == L'_' || c == L'$'; }

static bool InList(const wstring& w, const wchar_t* const* list, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (w == list[i]) return true;
    return false;
}

static const wchar_t* kCKeywords[] = {
    L"auto", L"bool", L"char", L"class", L"const", L"constexpr", L"double", L"enum",
    L"explicit", L"export", L"extern", L"float", L"friend", L"inline", L"int", L"long",
    L"namespace", L"new", L"operator", L"private", L"protected", L"public", L"short",
    L"signed", L"sizeof", L"static", L"struct", L"template", L"typedef", L"typename",
    L"union", L"unsigned", L"using", L"virtual", L"void", L"volatile", L"wchar_t",
    L"string", L"var", L"let", L"function", L"async", L"await", L"yield", L"type",
    L"interface", L"extends", L"implements", L"package", L"import", L"from", L"as",
    L"def", L"lambda", L"fn", L"mut", L"impl", L"trait", L"pub", L"self", L"this",
    L"super", L"nil", L"null", L"None", L"True", L"False", L"true", L"false",
    L"local", L"end", L"then", L"do", L"repeat", L"until", L"in", L"and", L"or", L"not",
    L"print", L"echo"
};
static const wchar_t* kCControl[] = {
    L"if", L"else", L"elif", L"for", L"while", L"switch", L"case", L"default",
    L"return", L"break", L"continue", L"goto", L"try", L"catch", L"finally", L"throw",
    L"raise", L"except", L"with", L"pass", L"match", L"when"
};
static const wchar_t* kPyKeywords[] = {
    L"and", L"as", L"assert", L"async", L"await", L"class", L"def", L"del", L"elif",
    L"else", L"except", L"finally", L"for", L"from", L"global", L"if", L"import", L"in",
    L"is", L"lambda", L"nonlocal", L"not", L"or", L"pass", L"raise", L"return", L"try",
    L"while", L"with", L"yield", L"None", L"True", L"False", L"self"
};
static const wchar_t* kLuaKeywords[] = {
    L"and", L"break", L"do", L"else", L"elseif", L"end", L"false", L"for", L"function",
    L"goto", L"if", L"in", L"local", L"nil", L"not", L"or", L"repeat", L"return",
    L"then", L"true", L"until", L"while"
};
static const wchar_t* kShKeywords[] = {
    L"if", L"then", L"else", L"elif", L"fi", L"for", L"while", L"do", L"done", L"case",
    L"esac", L"function", L"return", L"export", L"local", L"readonly", L"alias", L"set",
    L"unset", L"source", L"echo", L"exit", L"cd", L"sudo"
};
static const wchar_t* kSqlKeywords[] = {
    L"select", L"from", L"where", L"join", L"left", L"right", L"inner", L"outer", L"on",
    L"group", L"by", L"order", L"having", L"insert", L"into", L"values", L"update", L"set",
    L"delete", L"create", L"table", L"index", L"view", L"as", L"and", L"or", L"not",
    L"null", L"is", L"like", L"limit", L"offset", L"distinct", L"count", L"sum", L"avg",
    L"min", L"max", L"case", L"when", L"then", L"else", L"end", L"union", L"all"
};
static const wchar_t* kJsonKeywords[] = { L"true", L"false", L"null" };
static const wchar_t* kHtmlKeywords[] = { L"html", L"head", L"body", L"div", L"span", L"a",
    L"p", L"img", L"script", L"style", L"link", L"meta", L"title", L"section", L"header",
    L"footer", L"nav", L"ul", L"li", L"table", L"tr", L"td", L"th", L"form", L"input",
    L"button", L"br", L"h1", L"h2", L"h3", L"pre", L"code", L"main", L"article" };

static bool IsControlWord(const wstring& w) {
    return InList(w, kCControl, ARRAYSIZE(kCControl));
}

static void LexLine(const wstring& s, int lang, int& state, std::vector<HlToken>& out) {
    size_t n = s.size();
    size_t cursor = 0;

    auto emit = [&](size_t a, size_t b, COLORREF c) {
        if (b <= a) return;
        if (a > cursor) out.push_back(HlToken{ cursor, a, C_TEXT });
        if (a < cursor) a = cursor;
        if (b <= a) return;
        out.push_back(HlToken{ a, b, c });
        cursor = b;
    };

    size_t i = 0;

    if (state == 1) {
        size_t e = s.find(L"*/");
        if (e == wstring::npos) { emit(0, n, HL_COMMENT); return; }
        emit(0, e + 2, HL_COMMENT);
        i = e + 2;
        state = 0;
    }

    if (lang == LANG_HTML) {
        while (i < n) {
            if (s.compare(i, 4, L"<!--") == 0) {
                size_t e = s.find(L"-->", i + 4);
                if (e == wstring::npos) { emit(i, n, HL_COMMENT); return; }
                emit(i, e + 3, HL_COMMENT);
                i = e + 3;
                continue;
            }
            if (s[i] == L'<') {
                size_t e = s.find(L'>', i);
                if (e == wstring::npos) e = n - 1;
                emit(i, e + 1, HL_KEYWORD);
                i = e + 1;
                continue;
            }
            ++i;
        }
        if (cursor < n) out.push_back(HlToken{ cursor, n, C_TEXT });
        return;
    }

    bool lineStart = true;
    while (i < n) {
        wchar_t c = s[i];

        if (c == L' ' || c == L'\t') { ++i; continue; }

        if (IsCFamily(lang) || lang == LANG_JSON) {
            if (c == L'/' && i + 1 < n && s[i + 1] == L'/') { emit(i, n, HL_COMMENT); break; }
            if (c == L'/' && i + 1 < n && s[i + 1] == L'*') {
                size_t e = s.find(L"*/", i + 2);
                if (e == wstring::npos) { emit(i, n, HL_COMMENT); state = 1; break; }
                emit(i, e + 2, HL_COMMENT);
                i = e + 2;
                continue;
            }
        }
        if ((lang == LANG_PY || lang == LANG_SH) && c == L'#') {
            emit(i, n, HL_COMMENT);
            break;
        }
        if ((lang == LANG_LUA || lang == LANG_SQL) && c == L'-' && i + 1 < n && s[i + 1] == L'-') {
            emit(i, n, HL_COMMENT);
            break;
        }

        if (IsCFamily(lang) && c == L'#' && lineStart) {
            emit(i, n, HL_MACRO);
            break;
        }

        if (c == L'"' || c == L'\'' || c == L'`') {
            wchar_t quote = c;
            size_t j = i + 1;
            while (j < n) {
                if (s[j] == L'\\' && j + 1 < n) { j += 2; continue; }
                if (s[j] == quote) { ++j; break; }
                ++j;
            }
            emit(i, (std::min)(j, n), HL_STRING);
            i = (std::min)(j, n);
            lineStart = false;
            continue;
        }

        if (iswdigit(c) || (c == L'.' && i + 1 < n && iswdigit(s[i + 1]))) {
            size_t j = i;
            while (j < n && (iswalnum(s[j]) || s[j] == L'.' || s[j] == L'_' ||
                             ((s[j] == L'+' || s[j] == L'-') && j > i &&
                              (s[j - 1] == L'e' || s[j - 1] == L'E'))))
                ++j;
            emit(i, j, HL_NUMBER);
            i = j;
            lineStart = false;
            continue;
        }

        if (IsIdentStart(c)) {
            size_t j = i + 1;
            while (j < n && IsIdentChar(s[j])) ++j;
            wstring w = s.substr(i, j - i);
            wstring lower = w;
            for (auto& ch : lower) ch = (wchar_t)towlower(ch);

            if (lang == LANG_JSON) {
                emit(i, j, InList(lower, kJsonKeywords, ARRAYSIZE(kJsonKeywords)) ? HL_KEYWORD : C_TEXT);
                i = j;
                lineStart = false;
                continue;
            }

            COLORREF col = C_TEXT;

            if (lang == LANG_SQL) {
                if (InList(lower, kSqlKeywords, ARRAYSIZE(kSqlKeywords))) col = HL_KEYWORD;
            } else if (lang == LANG_PY) {
                if (InList(w, kPyKeywords, ARRAYSIZE(kPyKeywords))) col = HL_KEYWORD;
                else if (IsControlWord(w)) col = HL_CONTROL;
            } else if (lang == LANG_LUA) {
                if (InList(w, kLuaKeywords, ARRAYSIZE(kLuaKeywords))) col = HL_KEYWORD;
            } else if (lang == LANG_SH) {
                if (InList(w, kShKeywords, ARRAYSIZE(kShKeywords))) col = HL_KEYWORD;
            } else {
                if (InList(w, kCKeywords, ARRAYSIZE(kCKeywords))) col = HL_KEYWORD;
                else if (IsControlWord(w)) col = HL_CONTROL;
                else if (w.size() > 1 && iswupper(w[0]) && iswlower(w[1])) col = HL_TYPE;
                else if (InList(lower, kHtmlKeywords, ARRAYSIZE(kHtmlKeywords))) col = HL_TYPE;
            }

            if (col == C_TEXT && j < n) {
                size_t k = j;
                while (k < n && (s[k] == L' ' || s[k] == L'\t')) ++k;
                if (k < n && s[k] == L'(') col = HL_FUNC;
            }

            emit(i, j, col);
            i = j;
            lineStart = false;
            continue;
        }

        ++i;
        lineStart = false;
    }

    if (cursor < n) out.push_back(HlToken{ cursor, n, C_TEXT });
}

static int DetectLanguage(const wstring& code) {
    wstring l = code;
    for (auto& c : l) c = (wchar_t)towlower(c);

    wstring t = TrimText(l);
    if (!t.empty() && (t[0] == L'{' || t[0] == L'[')) {
        if (t.find(L"\":") != wstring::npos)
            return LANG_JSON;
    }
    if (l.find(L"<html") != wstring::npos || l.find(L"<!doctype") != wstring::npos ||
        l.find(L"<div") != wstring::npos || l.find(L"</p>") != wstring::npos ||
        l.find(L"<span") != wstring::npos)
        return LANG_HTML;
    if (l.find(L"#include") != wstring::npos || l.find(L"int main") != wstring::npos ||
        l.find(L"std::") != wstring::npos)
        return LANG_C;
    if (l.find(L"using system;") != wstring::npos || l.find(L"public void ") != wstring::npos ||
        (l.find(L"public class ") != wstring::npos && l.find(L"system.") != wstring::npos))
        return LANG_CS;
    if (l.find(L"let mut ") != wstring::npos || l.find(L"fn main()") != wstring::npos ||
        (l.find(L"fn ") != wstring::npos && l.find(L"-> ") != wstring::npos))
        return LANG_RUST;
    if (l.find(L"def ") != wstring::npos || l.find(L"elif ") != wstring::npos ||
        l.find(L"print(") != wstring::npos || l.find(L"self.") != wstring::npos)
        return LANG_PY;
    if (l.find(L"local ") != wstring::npos || l.find(L"\nend") != wstring::npos ||
        l.find(L"then\n") != wstring::npos)
        return LANG_LUA;
    if (l.rfind(L"#!", 0) == 0 || l.find(L"echo ") != wstring::npos ||
        l.find(L"\nfi\n") != wstring::npos)
        return LANG_SH;
    if (l.find(L"select ") != wstring::npos && l.find(L"from ") != wstring::npos)
        return LANG_SQL;
    if (l.find(L":\n") != wstring::npos || (!l.empty() && l.back() == L':'))
        return LANG_PY;
    if (l.find(L"function ") != wstring::npos || l.find(L"=>") != wstring::npos ||
        l.find(L"const ") != wstring::npos || l.find(L"let ") != wstring::npos ||
        l.find(L"import ") != wstring::npos)
        return LANG_JS;
    return LANG_C;
}

static const wchar_t* LangName(int lang) {
    switch (lang) {
    case LANG_C:    return L"c";
    case LANG_JS:   return L"js";
    case LANG_CS:   return L"c#";
    case LANG_RUST: return L"rust";
    case LANG_PY:   return L"py";
    case LANG_LUA:  return L"lua";
    case LANG_SH:   return L"sh";
    case LANG_JSON: return L"json";
    case LANG_HTML: return L"html";
    case LANG_SQL:  return L"sql";
    default:        return L"code";
    }
}

// --------------------------------------------------------------- icon sheet
static int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<BYTE> DecodeBase64(const char* s) {
    std::vector<BYTE> out;
    int val = 0, bits = 0;
    for (const char* p = s; *p; ++p) {
        int v = B64Val(*p);
        if (v < 0) continue;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((BYTE)((val >> bits) & 0xFF));
        }
    }
    return out;
}

static void LoadIconSheet() {
    std::vector<BYTE> png = DecodeBase64(kIconsPngB64);
    if (png.empty()) return;

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)png.size());
    if (!mem) return;
    void* p = GlobalLock(mem);
    if (!p) { GlobalFree(mem); return; }
    memcpy(p, png.data(), png.size());
    GlobalUnlock(mem);

    IStream* st = nullptr;
    if (CreateStreamOnHGlobal(mem, TRUE, &st) == S_OK && st) {
        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(st);
        st->Release();
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) g_iconSheet = bmp;
        else delete bmp;
    } else {
        GlobalFree(mem);
    }
}

// -------------------------------------------------------------- view paint
static void DrawSheetIcon(HDC dc, int iconId, int x, int y, int s) {
    if (!g_iconSheet || iconId < 0) return;
    int col = iconId % kIconCols;
    int row = iconId / kIconCols;

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.DrawImage(g_iconSheet, Gdiplus::Rect(x, y, s, s),
                col * kIconCell, row * kIconCell, kIconCell, kIconCell,
                Gdiplus::UnitPixel);
}

// small "x" shown on the first line of a hovered code block
static RECT BlockDeleteButton(const RECT& ta, int firstLine) {
    int y = ta.top + LineTextTop(firstLine) - g_scrollY;
    int s = Dpi(15);
    int top = y + (g_lineH - s) / 2;
    return RECT{ ta.right - Dpi(8) - s, top, ta.right - Dpi(8), top + s };
}

static void DrawBlockDeleteButton(HDC dc, const RECT& br) {
    COLORREF bgc = g_hoverDelete ? RGB(0x8E, 0x2F, 0x3B) : RGB(0x22, 0x2B, 0x36);
    HBRUSH bg = CreateSolidBrush(bgc);
    HPEN none = (HPEN)GetStockObject(NULL_PEN);
    HBRUSH ob = (HBRUSH)SelectObject(dc, bg);
    HPEN op = (HPEN)SelectObject(dc, none);
    RoundRect(dc, br.left, br.top, br.right, br.bottom, Dpi(5), Dpi(5));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(bg);

    int pad = Dpi(4);
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, Dpi(1)), RGB(0xE6, 0xED, 0xF3));
    HPEN old = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, br.left + pad, br.top + pad, nullptr);
    LineTo(dc, br.right - pad, br.bottom - pad);
    MoveToEx(dc, br.right - pad, br.top + pad, nullptr);
    LineTo(dc, br.left + pad, br.bottom - pad);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void DrawChip(HDC dc, const Chip& c, int x, int y, int h) {
    int icon = IconSize();
    int gap  = ChipIconGap();
    int pad  = ChipPillPad();

    DrawSheetIcon(dc, c.icon, x, y + (h - icon) / 2, icon);

    RECT pill{ x + icon + gap, y + Dpi(1), x + c.width - ChipTail(), y + h - Dpi(1) };

    HBRUSH bg = CreateSolidBrush(C_CHIPBG);
    HPEN border = CreatePen(PS_SOLID, 1, C_CHIPLINE);
    HBRUSH ob = (HBRUSH)SelectObject(dc, bg);
    HPEN op = (HPEN)SelectObject(dc, border);
    RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, Dpi(6), Dpi(6));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(bg);
    DeleteObject(border);

    SetTextColor(dc, C_TEXT);
    RECT tr{ pill.left + pad, y, pill.right - pad, y + h };
    DrawTextW(dc, c.label.c_str(), -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static void DrawPlaceholder(HDC dc, const RECT& ta) {
    RECT r = ta;
    r.bottom = r.top + g_lineH * 3;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_GHOST);
    HFONT old = (HFONT)SelectObject(dc, g_hFont);
    const wchar_t* hint =
        L"Write your prompt here\u2026\r\n"
        L"Ctrl+V an image or a file to insert it as a path";
    DrawTextW(dc, hint, -1, &r, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
    SelectObject(dc, old);
}

static void PaintView(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) { EndPaint(hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    FillRect(mem, &rc, g_brEdit);

    RECT ta{};
    TextArea(&ta);
    EnsureLayout();

    SetBkMode(mem, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(mem, g_hFont);

    int viewH = ta.bottom - ta.top;

    HRGN clip = CreateRectRgn(ta.left, ta.top, ta.right, ta.bottom);
    SelectClipRgn(mem, clip);

    size_t selA = 0, selB = 0;
    Selection(selA, selB);

    int firstLine = LineAtY(g_scrollY);
    int lastLine = LineAtY(g_scrollY + viewH);
    if (lastLine >= LineCount()) lastLine = LineCount() - 1;

    std::vector<int> stateAt(LineCount(), -1);
    for (const Block& blk : g_blocks) {
        int state = 0;
        int first = LineOfUnit(blk.start);
        for (int l = first; l < LineCount(); ++l) {
            if ((size_t)g_lineStarts[l] >= blk.end) break;
            stateAt[l] = state;
            std::vector<size_t> m;
            bool hc;
            wstring t = LineTextAndMap(l, m, hc);
            if (t.empty()) continue;
            std::vector<HlToken> tmp;
            LexLine(t, blk.lang, state, tmp);
        }
    }

    HBRUSH selBrush = CreateSolidBrush(C_SEL);

    for (int line = firstLine; line <= lastLine; ++line) {
        int ltop = ta.top + LineTop(line) - g_scrollY;
        int y = ta.top + LineTextTop(line) - g_scrollY;
        int lh = LineHeight(line);
        size_t start = (size_t)g_lineStarts[line];
        size_t end = (line + 1 < LineCount()) ? (size_t)g_lineStarts[line + 1] : g_units.size();

        if (LineIsInBlock(line)) {
            RECT pr{ ta.left - Dpi(10), ltop, ta.right + Dpi(4), ltop + lh };
            HBRUSH cb = CreateSolidBrush(C_CODEBG);
            FillRect(mem, &pr, cb);
            DeleteObject(cb);

            RECT bar{ pr.left, ltop, pr.left + Dpi(3), ltop + lh };
            HBRUSH bb = CreateSolidBrush(C_CODEBAR);
            FillRect(mem, &bar, bb);
            DeleteObject(bb);

            if (g_hoverBlockLine == line) {
                RECT br = BlockDeleteButton(ta, line);
                DrawBlockDeleteButton(mem, br);
            }
        }

        if (selA < selB) {
            for (size_t i = start; i < end; ++i) {
                if (i < selA || i >= selB) continue;
                if (g_units[i].chip < 0 && g_units[i].ch == L'\n') {
                    RECT r{ ta.left + g_gl[i].x, y, ta.left + g_gl[i].x + Dpi(4), y + g_lineH };
                    FillRect(mem, &r, selBrush);
                } else {
                    int uw = (g_units[i].chip >= 0)
                                 ? g_chips[(size_t)g_units[i].chip].width
                                 : g_gl[i].w;
                    if (uw == 0 && g_units[i].chip < 0) continue;   // combining mark
                    if (uw < 2) uw = 2;
                    RECT r{ ta.left + g_gl[i].x, y, ta.left + g_gl[i].x + uw, y + g_lineH };
                    FillRect(mem, &r, selBrush);
                }
            }
        }

        bool drewCode = false;
        if (LineIsInBlock(line)) {
            std::vector<size_t> map;
            bool hasChip = false;
            wstring text = LineTextAndMap(line, map, hasChip);
            const Block* blk = BlockOfLine(line);
            int lang = blk ? blk->lang : LANG_C;
            int state = (line < (int)stateAt.size() && stateAt[line] >= 0) ? stateAt[line] : 0;

            if (!hasChip && !text.empty()) {
                std::vector<HlToken> toks;
                LexLine(text, lang, state, toks);
                for (const HlToken& t : toks) {
                    if (t.a >= map.size()) break;
                    size_t unit = map[t.a];
                    SetTextColor(mem, t.color);
                    TextOutW(mem, ta.left + g_gl[unit].x, y, text.c_str() + t.a,
                             (int)((std::min)(t.b, (size_t)text.size()) - t.a));
                }
                drewCode = true;

                if (blk && line == LineOfUnit(blk->start)) {
                    int rightEdge = ta.right - Dpi(8);
                    if (g_hoverBlockLine == line) rightEdge -= Dpi(22);

                    HFONT of = (HFONT)SelectObject(mem, g_hStatusFont);
                    SetTextColor(mem, RGB(0x5E, 0x6E, 0x80));
                    RECT lr{ rightEdge - Dpi(70), y, rightEdge, y + g_lineH };
                    DrawTextW(mem, LangName(blk->lang), -1, &lr,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(mem, of);
                }
            }
        }

        if (!drewCode)
        for (size_t i = start; i < end; ) {
            if (g_units[i].chip >= 0) {
                const Chip& c = g_chips[(size_t)g_units[i].chip];
                SetTextColor(mem, C_TEXT);
                DrawChip(mem, c, ta.left + g_gl[i].x, y, g_lineH);
                ++i;
                continue;
            }
            if (g_units[i].ch == L'\n') { ++i; continue; }

            SetTextColor(mem, C_TEXT);
            if ((unsigned)g_units[i].ch < 128) {
                size_t j = i;
                wstring run;
                while (j < end && g_units[j].chip < 0 && g_units[j].ch != L'\n' &&
                       (unsigned)g_units[j].ch < 128) {
                    run += g_units[j].ch;
                    ++j;
                }
                TextOutW(mem, ta.left + g_gl[i].x, y, run.c_str(), (int)run.size());
                i = j;
            } else {
                wstring cl(1, g_units[i].ch);
                size_t j = i + 1;
                while (j < end && g_units[j].chip < 0 && g_units[j].ch != L'\n' &&
                       IsMarkChar(g_units[j].ch)) {
                    cl += g_units[j].ch;
                    ++j;
                }
                TextOutW(mem, ta.left + g_gl[i].x, y, cl.c_str(), (int)cl.size());
                i = j;
            }
        }
    }

    if (g_units.empty()) DrawPlaceholder(mem, ta);

    if (!g_chipDrag && GetFocus() == hwnd && g_blinkOn) {
        int cx, cy;
        CaretXY(g_caret, cx, cy);
        int sy = ta.top + cy - g_scrollY;
        RECT r{ ta.left + cx, sy, ta.left + cx + Dpi(2), sy + g_lineH };
        HBRUSH cb = CreateSolidBrush(C_CARET);
        FillRect(mem, &r, cb);
        DeleteObject(cb);
    }

    if (g_chipDrag) {
        int cx, cy;
        CaretXY(g_chipDrop, cx, cy);
        int sy = ta.top + cy - g_scrollY;
        RECT r{ ta.left + cx, sy, ta.left + cx + Dpi(2), sy + g_lineH };
        HBRUSH b = CreateSolidBrush(C_ACCENT);
        FillRect(mem, &r, b);
        DeleteObject(b);
    }

    SelectClipRgn(mem, nullptr);

    if (g_chipDrag) {
        int gw = g_chipDragChip.width;
        if (gw <= 0) gw = Dpi(140);
        int gx = g_chipMouse.x - Dpi(14);
        int gy = g_chipMouse.y - g_lineH / 2;
        if (gx + gw > rc.right - Dpi(6)) gx = rc.right - Dpi(6) - gw;
        if (gx < Dpi(6)) gx = Dpi(6);
        if (gy < Dpi(4)) gy = Dpi(4);
        if (gy + g_lineH > rc.bottom - Dpi(4)) gy = rc.bottom - Dpi(4) - g_lineH;

        int pad = Dpi(4);
        int bx = gx - pad;
        int by = gy - pad;
        int bw = gw + pad * 2;
        int bh = g_lineH + pad * 2;
        if (bx < 0) { bw += bx; bx = 0; }
        if (by < 0) { bh += by; by = 0; }
        if (bx + bw > rc.right)  bw = rc.right - bx;
        if (by + bh > rc.bottom) bh = rc.bottom - by;

        if (bw > 0 && bh > 0) {
            HDC tmp = CreateCompatibleDC(mem);
            HBITMAP bmp2 = CreateCompatibleBitmap(mem, bw, bh);
            HBITMAP ob2 = (HBITMAP)SelectObject(tmp, bmp2);

            BitBlt(tmp, 0, 0, bw, bh, mem, bx, by, SRCCOPY);

            int cx = gx - bx;
            int cy = gy - by;
            SetBkMode(tmp, TRANSPARENT);
            HFONT tf = (HFONT)SelectObject(tmp, g_hFont);
            DrawChip(tmp, g_chipDragChip, cx, cy, g_lineH);
            SelectObject(tmp, tf);

            HPEN pen = CreatePen(PS_SOLID, 1, C_ACCENT);
            HPEN op = (HPEN)SelectObject(tmp, pen);
            HBRUSH nb = (HBRUSH)SelectObject(tmp, GetStockObject(NULL_BRUSH));
            RoundRect(tmp, cx, cy + Dpi(1), cx + gw, cy + g_lineH - Dpi(1), Dpi(6), Dpi(6));
            SelectObject(tmp, op);
            SelectObject(tmp, nb);
            DeleteObject(pen);

            BLENDFUNCTION bf{ AC_SRC_OVER, 0, 160, 0 };
            AlphaBlend(mem, bx, by, bw, bh, tmp, 0, 0, bw, bh, bf);

            SelectObject(tmp, ob2);
            DeleteObject(bmp2);
            DeleteDC(tmp);
        }
    }

    int contentH = ContentHeight();
    if (contentH > viewH) {
        int trackTop = ta.top;
        int trackH = viewH;
        int thumbH = (int)((long long)trackH * viewH / contentH);
        if (thumbH < Dpi(26)) thumbH = Dpi(26);
        if (thumbH > trackH) thumbH = trackH;
        int maxScroll = contentH - viewH;
        int thumbY = trackTop;
        if (maxScroll > 0)
            thumbY += (int)((long long)(trackH - thumbH) * g_scrollY / maxScroll);

        int tw = Dpi(4);
        int x = rc.right - Dpi(10) + (Dpi(10) - tw) / 2;

        HBRUSH b = CreateSolidBrush(C_THUMB);
        HPEN p = CreatePen(PS_SOLID, 1, C_THUMB);
        HBRUSH ob = (HBRUSH)SelectObject(mem, b);
        HPEN op = (HPEN)SelectObject(mem, p);
        RoundRect(mem, x, thumbY, x + tw, thumbY + thumbH, tw, tw);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(b);
        DeleteObject(p);
    }

    DeleteObject(selBrush);
    SelectObject(mem, oldFont);
    DeleteObject(clip);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);

    EndPaint(hwnd, &ps);
}

// ------------------------------------------------------------ view actions
static void ResetBlink() {
    g_blinkOn = true;
    if (g_view) SetTimer(g_view, 2, 530, nullptr);
}

static void DocChanged(int undoKind) {
    RecountDoc();
    InvalidateLayout();
    EnsureCaretVisible();
    g_dirty = true;
    g_lastEdit = GetTickCount64();
    ResetBlink();
    if (g_view) InvalidateRect(g_view, nullptr, FALSE);
    UpdateStatus();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void SetCaretPos(size_t pos, bool extend) {
    if (pos > g_units.size()) pos = g_units.size();
    g_caret = pos;
    if (!extend) g_anchor = pos;
    EnsureCaretVisible();
    ResetBlink();
    if (g_view) InvalidateRect(g_view, nullptr, FALSE);
    UpdateStatus();
}

static size_t PrevPos(size_t pos) {
    if (pos == 0) return 0;
    size_t p = pos - 1;
    if (p > 0 && g_units[p].ch >= 0xDC00 && g_units[p].ch <= 0xDFFF &&
        g_units[p - 1].ch >= 0xD800 && g_units[p - 1].ch <= 0xDBFF)
        p--;
    while (p > 0 && IsMarkUnit(p)) --p;   // never park the caret inside a cluster
    return p;
}

static size_t NextPos(size_t pos) {
    if (pos >= g_units.size()) return g_units.size();
    size_t p = pos + 1;
    if (pos + 1 < g_units.size() && g_units[pos].ch >= 0xD800 && g_units[pos].ch <= 0xDBFF &&
        g_units[pos + 1].ch >= 0xDC00 && g_units[pos + 1].ch <= 0xDFFF)
        p = pos + 2;
    p = SnapPastMarks(p);
    return p;
}

static bool IsWordChar(wchar_t c) {
    return (c == L'_') || (c >= L'0' && c <= L'9') ||
           (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= 0x0E00 && c <= 0x0E7F);
}

static size_t WordLeft(size_t pos) {
    while (pos > 0 && !IsWordChar(g_units[PrevPos(pos)].ch)) pos = PrevPos(pos);
    while (pos > 0 && IsWordChar(g_units[PrevPos(pos)].ch)) pos = PrevPos(pos);
    return pos;
}

static size_t WordRight(size_t pos) {
    while (pos < g_units.size() && !IsWordChar(g_units[pos].ch)) pos = NextPos(pos);
    while (pos < g_units.size() && IsWordChar(g_units[pos].ch)) pos = NextPos(pos);
    return pos;
}

static void MoveVertical(int dir, bool extend) {
    EnsureLayout();
    int cx, cy;
    CaretXY(g_caret, cx, cy);
    int cur = (g_caret < g_units.size()) ? g_gl[g_caret].line : LineCount() - 1;
    int line = cur + dir;
    if (line < 0) line = 0;
    if (line > LineCount() - 1) line = LineCount() - 1;
    size_t pos = PosFromX(line, cx);
    SetCaretPos(pos, extend);
}

static void DeleteBackward() {
    size_t a, b;
    Selection(a, b);
    if (a == b) {
        if (a == 0) return;
        a = PrevPos(a);
    }
    PushUndo(2);
    DeleteUnits(a, b);
    g_caret = g_anchor = a;
    DocChanged(2);
}

static void DeleteForward() {
    size_t a, b;
    Selection(a, b);
    if (a == b) {
        if (b >= g_units.size()) return;
        b = NextPos(b);
    }
    PushUndo(2);
    DeleteUnits(a, b);
    g_caret = g_anchor = a;
    DocChanged(2);
}

static void InsertTyped(const wstring& text) {
    if (text.empty()) return;
    size_t a, b;
    Selection(a, b);
    PushUndo(1);
    if (b > a) DeleteUnits(a, b);
    InsertTextAt(a, text);
    g_caret = g_anchor = a + text.size();
    DocChanged(1);
}

static void SelectWordAt(size_t pos) {
    if (pos >= g_units.size()) return;
    size_t a = pos, b = pos;
    while (a > 0 && IsWordChar(g_units[a - 1].ch)) --a;
    while (b < g_units.size() && IsWordChar(g_units[b].ch)) ++b;
    if (a == b) { a = pos; b = pos + 1; }
    g_anchor = a;
    g_caret = b;
    if (g_view) InvalidateRect(g_view, nullptr, FALSE);
    UpdateStatus();
}

// ------------------------------------------------------- chip drag & drop
static size_t ChipUnitAtPixel(int px, int py) {
    EnsureLayout();
    int line = LineAtY(py + g_scrollY);
    if (line < 0 || line >= LineCount()) return (size_t)-1;
    size_t start = (size_t)g_lineStarts[line];
    size_t end = (line + 1 < LineCount()) ? (size_t)g_lineStarts[line + 1] : g_units.size();
    for (size_t i = start; i < end; ++i) {
        if (g_units[i].chip < 0) continue;
        if (px >= g_gl[i].x && px < g_gl[i].x + g_gl[i].w) return i;
    }
    return (size_t)-1;
}

static void StartChipDrag() {
    if (!g_chipPress || g_chipDrag) return;
    size_t pos = g_chipPressPos;
    if (pos >= g_units.size() || g_units[pos].chip < 0) {
        g_chipPress = false;
        return;
    }

    PushUndo(0);
    g_chipDragChip   = g_chips[(size_t)g_units[pos].chip];
    g_chipDragOrigin = pos;

    g_units.erase(g_units.begin() + pos);
    if (g_caret > pos) g_caret--;
    if (g_anchor > pos) g_anchor--;
    if (g_caret > g_units.size()) g_caret = g_units.size();
    if (g_anchor > g_units.size()) g_anchor = g_units.size();

    g_chipDrag  = true;
    g_chipPress = false;
    g_chipDrop  = pos;
    if (g_chipDrop > g_units.size()) g_chipDrop = g_units.size();

    InvalidateLayout();
    RecountDoc();
    UpdateStatus();
    if (g_view) InvalidateRect(g_view, nullptr, FALSE);
}

static void FinishChipDrag(size_t at) {
    if (!g_chipDrag) return;
    g_chipDrag  = false;
    g_chipPress = false;
    if (at > g_units.size()) at = g_units.size();

    if (at > 0 && !IsSpaceUnit(at - 1)) {
        InsertTextAt(at, L" ");
        ++at;
    }

    Unit u;
    u.ch = 0;
    u.chip = AddChip(g_chipDragChip);
    if (u.chip < 0) { DocChanged(0); return; }

    g_units.insert(g_units.begin() + at, u);
    ++at;

    if (!IsSpaceUnit(at) && at < g_units.size())
        InsertTextAt(at, L" ");

    g_caret = g_anchor = at;
    DocChanged(0);
}

// --------------------------------------------------------- context menu
static void RunMenuCommand(int id);
static void MenuPaint(HWND hwnd, HDC dc);

static void MenuLayout() {
    g_menuRects.clear();
    int y = Dpi(6);
    for (const MenuRow& row : g_menuRows) {
        int hh = row.separator ? Dpi(9) : Dpi(28);
        RECT r{ 0, y, g_menuW, y + hh };
        g_menuRects.push_back(r);
        y += hh;
    }
    g_menuH = y + Dpi(6);
}

static int MenuHit(int y) {
    for (size_t i = 0; i < g_menuRects.size(); ++i) {
        if (y >= g_menuRects[i].top && y < g_menuRects[i].bottom) {
            const MenuRow& row = g_menuRows[i];
            if (row.separator || !row.enabled) return -1;
            return (int)i;
        }
    }
    return -1;
}

static void MenuChoose(int idx) {
    if (idx < 0 || idx >= (int)g_menuRows.size()) return;
    const MenuRow& row = g_menuRows[idx];
    if (row.separator || !row.enabled) return;
    g_menuClosing = true;
    g_menuResult = (int)row.id;
    DestroyWindow(g_menuHwnd);
}

static LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        MenuPaint(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int idx = MenuHit(GET_Y_LPARAM(l));
        if (idx != g_menuHover) {
            g_menuHover = idx;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int idx = MenuHit(GET_Y_LPARAM(l));
        if (idx >= 0) MenuChoose(idx);
        return 0;
    }

    case WM_KEYDOWN: {
        if (w == VK_ESCAPE) {
            g_menuResult = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        if (w == VK_RETURN) {
            if (g_menuHover >= 0) MenuChoose(g_menuHover);
            return 0;
        }
        if (w == VK_DOWN || w == VK_UP) {
            int step = (w == VK_DOWN) ? 1 : -1;
            int idx = g_menuHover;
            for (int guard = 0; guard < (int)g_menuRows.size(); ++guard) {
                idx += step;
                if (idx < 0) idx = (int)g_menuRows.size() - 1;
                if (idx >= (int)g_menuRows.size()) idx = 0;
                const MenuRow& row = g_menuRows[idx];
                if (!row.separator && row.enabled) break;
            }
            g_menuHover = idx;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && !g_menuClosing) {
            g_menuClosing = true;
            g_menuResult = 0;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

static void MenuPaint(HWND hwnd, HDC dc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);

    FillRect(mem, &rc, g_brEdit);

    HBRUSH body = CreateSolidBrush(C_MENUBG);
    HPEN   edge = CreatePen(PS_SOLID, 1, RGB(0x2E, 0x37, 0x42));
    HBRUSH obr = (HBRUSH)SelectObject(mem, body);
    HPEN   ope = (HPEN)SelectObject(mem, edge);
    RoundRect(mem, 0, 0, rc.right, rc.bottom, Dpi(14), Dpi(14));
    SelectObject(mem, obr);
    SelectObject(mem, ope);
    DeleteObject(body);
    DeleteObject(edge);

    SetBkMode(mem, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(mem, g_hStatusFont);

    for (size_t i = 0; i < g_menuRows.size(); ++i) {
        const MenuRow& row = g_menuRows[i];
        RECT r = g_menuRects[i];

        if (row.separator) {
            HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
            HPEN op = (HPEN)SelectObject(mem, pen);
            int y = (r.top + r.bottom) / 2;
            MoveToEx(mem, r.left + Dpi(10), y, nullptr);
            LineTo(mem, r.right - Dpi(10), y);
            SelectObject(mem, op);
            DeleteObject(pen);
            continue;
        }

        if ((int)i == g_menuHover) {
            HBRUSH sel = CreateSolidBrush(C_MENUSEL);
            RECT hr{ r.left + Dpi(5), r.top, r.right - Dpi(5), r.bottom };
            FillRect(mem, &hr, sel);
            DeleteObject(sel);
        }

        SetTextColor(mem, row.enabled ? C_MENUTEXT : C_MENUDIS);
        RECT tr = r;
        tr.left += Dpi(16);
        tr.right -= Dpi(80);
        DrawTextW(mem, row.label, -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (row.accel) {
            SetTextColor(mem, row.enabled ? C_MENUDIM : RGB(0x39, 0x40, 0x49));
            RECT ar = r;
            ar.right -= Dpi(14);
            DrawTextW(mem, row.accel, -1, &ar,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    SelectObject(mem, of);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void ShowContextMenu(HWND hwnd, POINT screenPt) {
    size_t selA, selB;
    Selection(selA, selB);
    bool hasSel = (selA != selB);
    bool hasChip = (g_ctxChip != (size_t)-1);

    g_menuRows.clear();
    g_menuRows.push_back({ L"Undo",       L"Ctrl+Z", 1, false, !g_undo.empty() });
    g_menuRows.push_back({ nullptr,       nullptr,  0, true,  false });
    g_menuRows.push_back({ L"Cut",        L"Ctrl+X", 2, false, hasSel });
    g_menuRows.push_back({ L"Copy",       L"Ctrl+C", 3, false, hasSel });
    g_menuRows.push_back({ L"Paste",      L"Ctrl+V", 4, false, true });
    g_menuRows.push_back({ L"Delete",     L"Del",    5, false, hasSel || hasChip });
    g_menuRows.push_back({ nullptr,       nullptr,  0, true,  false });
    g_menuRows.push_back({ L"Select All", L"Ctrl+A", 6, false, true });

    int width = Dpi(160);
    HDC dc = GetDC(hwnd);
    HFONT of = (HFONT)SelectObject(dc, g_hStatusFont);
    for (const MenuRow& row : g_menuRows) {
        if (row.separator || !row.label) continue;
        SIZE s1{}, s2{};
        GetTextExtentPoint32W(dc, row.label, (int)wcslen(row.label), &s1);
        int w = s1.cx + Dpi(34);
        if (row.accel) {
            GetTextExtentPoint32W(dc, row.accel, (int)wcslen(row.accel), &s2);
            w += s2.cx + Dpi(34);
        }
        if (w > width) width = w;
    }
    SelectObject(dc, of);
    ReleaseDC(hwnd, dc);
    g_menuW = width;

    MenuLayout();

    static bool classReady = false;
    if (!classReady) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc   = MenuProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kMenuClass;
        if (!RegisterClassExW(&wc)) return;
        classReady = true;
    }

    int x = screenPt.x;
    int y = screenPt.y;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (x + g_menuW > wa.right) x = wa.right - g_menuW - Dpi(4);
    if (y + g_menuH > wa.bottom) y = screenPt.y - g_menuH;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    HWND m = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kMenuClass, L"",
                             WS_POPUP, x, y, g_menuW, g_menuH,
                             hwnd, nullptr, g_hInst, nullptr);
    if (!m) return;

    {
        typedef HRESULT(WINAPI *PFN)(HWND, DWORD, LPCVOID, DWORD);
        static PFN fn = nullptr;
        static bool tried = false;
        if (!tried) {
            tried = true;
            HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
            if (dwm) fn = (PFN)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
        }
        if (fn) {
            int pref = 2;
            fn(m, 33, &pref, sizeof(pref));
        }
    }

    g_menuHwnd = m;
    g_menuResult = 0;
    g_menuHover = -1;
    g_menuClosing = false;

    ShowWindow(m, SW_SHOW);
    SetForegroundWindow(m);

    MSG msg;
    while (IsWindow(m)) {
        if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_menuHwnd = nullptr;

    int chosen = g_menuResult;
    g_menuResult = 0;
    g_menuHover = -1;
    g_menuRows.clear();
    g_menuRects.clear();

    if (chosen) RunMenuCommand(chosen);
    g_ctxChip = (size_t)-1;
}

static void RunMenuCommand(int id) {
    size_t a, b;
    Selection(a, b);
    switch (id) {
    case 1:
        if (UndoOnce()) DocChanged(0);
        break;
    case 2:
        CopySelection(true);
        break;
    case 3:
        CopySelection(false);
        break;
    case 4:
        PasteFromClipboard();
        break;
    case 5:
        if (a != b) {
            PushUndo(0);
            DeleteUnits(a, b);
            g_caret = g_anchor = a;
            DocChanged(0);
        } else if (g_ctxChip != (size_t)-1 && g_ctxChip < g_units.size() &&
                   g_units[g_ctxChip].chip >= 0) {
            PushUndo(0);
            DeleteUnits(g_ctxChip, g_ctxChip + 1);
            g_caret = g_anchor = (g_ctxChip < g_units.size()) ? g_ctxChip : g_units.size();
            DocChanged(0);
        }
        break;
    case 6:
        SelectAll();
        break;
    default:
        break;
    }
}

// -------------------------------------------------------------- view window
static LRESULT CALLBACK ViewProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 2, 530, nullptr);
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_SIZE:
        InvalidateLayout();
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintView(hwnd);
        return 0;

    case WM_SETFOCUS:
        ResetBlink();
        g_blinkOn = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_KILLFOCUS:
        g_blinkOn = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_TIMER:
        if (w == 2) {
            g_blinkOn = !g_blinkOn;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL: {
        if ((GetKeyState(VK_CONTROL) | GetAsyncKeyState(VK_CONTROL)) & 0x8000) {
            short delta = GET_WHEEL_DELTA_WPARAM(w);
            SetFontPt(g_fontPt + (delta > 0 ? 1 : -1));
            return 0;
        }
        short delta = GET_WHEEL_DELTA_WPARAM(w);
        int lines = -MulDiv(delta, 3, WHEEL_DELTA);
        g_scrollY += lines * g_lineH;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hoverBlockLine != -1 || g_hoverDelete) {
            g_hoverBlockLine = -1;
            g_hoverDelete = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_RBUTTONUP: {
        SetFocus(hwnd);
        RECT ta{};
        TextArea(&ta);
        int mx = GET_X_LPARAM(l);
        int my = GET_Y_LPARAM(l);
        size_t pos = HitTest(mx - ta.left, my - ta.top);

        g_ctxChip = ChipUnitAtPixel(mx - ta.left, my - ta.top);
        if (g_ctxChip != (size_t)-1) {
            bool after = (mx - ta.left > g_gl[g_ctxChip].x + g_gl[g_ctxChip].w / 2);
            g_caret = g_anchor = g_ctxChip + (after ? 1 : 0);
        } else {
            size_t a, b;
            Selection(a, b);
            if (!(pos >= a && pos <= b)) g_caret = g_anchor = pos;
        }
        ResetBlink();
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateStatus();

        POINT pt{ mx, my };
        ClientToScreen(hwnd, &pt);
        ShowContextMenu(hwnd, pt);
        return 0;
    }

    case WM_COMMAND: {
        size_t a, b;
        Selection(a, b);
        switch (LOWORD(w)) {
        case 1:
            if (UndoOnce()) DocChanged(0);
            break;
        case 2:
            CopySelection(true);
            break;
        case 3:
            CopySelection(false);
            break;
        case 4:
            PasteFromClipboard();
            break;
        case 5:
            if (a != b) {
                PushUndo(0);
                DeleteUnits(a, b);
                g_caret = g_anchor = a;
                DocChanged(0);
            }
            break;
        case 6:
            SelectAll();
            break;
        default:
            break;
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        RECT ta{};
        TextArea(&ta);
        int x = GET_X_LPARAM(l) - ta.left;
        int y = GET_Y_LPARAM(l) - ta.top;

        if (g_hoverBlockLine >= 0) {
            RECT br = BlockDeleteButton(ta, g_hoverBlockLine);
            if (GET_X_LPARAM(l) >= br.left && GET_X_LPARAM(l) < br.right &&
                GET_Y_LPARAM(l) >= br.top && GET_Y_LPARAM(l) < br.bottom) {
                DeleteCodeBlockAt(g_hoverBlockLine);
                return 0;
            }
        }

        size_t pos = HitTest(x, y);
        bool shift = ((GetKeyState(VK_SHIFT) | GetAsyncKeyState(VK_SHIFT)) & 0x8000) != 0;

        size_t chipUnit = ChipUnitAtPixel(x, y);
        if (chipUnit != (size_t)-1 && !shift) {
            bool after = (x > g_gl[chipUnit].x + g_gl[chipUnit].w / 2);
            g_caret = g_anchor = chipUnit + (after ? 1 : 0);
            g_chipPress = true;
            g_chipPressPos = chipUnit;
            g_chipPressPt.x = x;
            g_chipPressPt.y = y;
            g_chipMouse.x = GET_X_LPARAM(l);
            g_chipMouse.y = GET_Y_LPARAM(l);
            SetCapture(hwnd);
            ResetBlink();
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateStatus();
            return 0;
        }

        if (shift) {
            g_caret = pos;
        } else {
            g_caret = g_anchor = pos;
        }
        g_dragging = true;
        SetCapture(hwnd);
        ResetBlink();
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateStatus();
        return 0;
    }

    case WM_MOUSEMOVE: {
        RECT ta{};
        TextArea(&ta);
        int x = GET_X_LPARAM(l) - ta.left;
        int y = GET_Y_LPARAM(l) - ta.top;

        if (!g_chipDrag && !g_dragging && !g_chipPress) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);

            int line = LineAtY(GET_Y_LPARAM(l) - ta.top + g_scrollY);
            int hoverLine = -1;
            bool overDel = false;
            if (line >= 0 && line < LineCount()) {
                const Block* blk = BlockOfLine(line);
                if (blk) {
                    hoverLine = LineOfUnit(blk->start);
                    if (line == hoverLine) {
                        RECT br = BlockDeleteButton(ta, hoverLine);
                        overDel = (GET_X_LPARAM(l) >= br.left && GET_X_LPARAM(l) < br.right &&
                                   GET_Y_LPARAM(l) >= br.top && GET_Y_LPARAM(l) < br.bottom);
                    }
                }
            }
            if (hoverLine != g_hoverBlockLine || overDel != g_hoverDelete) {
                g_hoverBlockLine = hoverLine;
                g_hoverDelete = overDel;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }

        if (g_chipPress && !g_chipDrag) {
            int dx = x - g_chipPressPt.x;
            int dy = y - g_chipPressPt.y;
            if (dx > Dpi(4) || dx < -Dpi(4) || dy > Dpi(4) || dy < -Dpi(4))
                StartChipDrag();
        }

        if (g_chipDrag) {
            g_chipMouse.x = GET_X_LPARAM(l);
            g_chipMouse.y = GET_Y_LPARAM(l);

            if (y < 0) { g_scrollY -= g_lineH; ClampScroll(); y = 0; }
            else if (y > ta.bottom - ta.top) { g_scrollY += g_lineH; ClampScroll(); y = ta.bottom - ta.top - 1; }

            size_t drop = HitTest(x, y);
            if (drop != g_chipDrop) {
                g_chipDrop = drop;
                UpdateStatus();
            }
            ResetBlink();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (!g_dragging) return 0;

        if (y < 0) { g_scrollY -= g_lineH; ClampScroll(); y = 0; }
        else if (y > ta.bottom - ta.top) { g_scrollY += g_lineH; ClampScroll(); y = ta.bottom - ta.top - 1; }

        g_caret = HitTest(x, y);
        ResetBlink();
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateStatus();
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_chipDrag) {
            FinishChipDrag(g_chipDrop);
            ReleaseCapture();
            return 0;
        }
        if (g_chipPress) {
            g_chipPress = false;
            ReleaseCapture();
            return 0;
        }
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_chipDrag) FinishChipDrag(g_chipDragOrigin);
        g_chipPress = false;
        g_dragging = false;
        return 0;

    case WM_SETCURSOR: {
        if (LOWORD(l) == HTCLIENT) {
            if (g_hoverDelete) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            RECT ta{};
            TextArea(&ta);
            size_t unit = ChipUnitAtPixel(p.x - ta.left, p.y - ta.top);
            SetCursor(LoadCursorW(nullptr, unit != (size_t)-1 ? IDC_SIZEALL : IDC_IBEAM));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        RECT ta{};
        TextArea(&ta);
        size_t pos = HitTest(GET_X_LPARAM(l) - ta.left, GET_Y_LPARAM(l) - ta.top);
        SelectWordAt(pos);
        return 0;
    }

    case WM_KEYDOWN: {
        g_swallowChar = false;

        if (w == VK_ESCAPE && g_chipDrag) {
            FinishChipDrag(g_chipDragOrigin);
            return 0;
        }

        const bool ctrl  = ((GetKeyState(VK_CONTROL) | GetAsyncKeyState(VK_CONTROL)) & 0x8000) != 0;
        const bool shift = ((GetKeyState(VK_SHIFT)   | GetAsyncKeyState(VK_SHIFT))   & 0x8000) != 0;

        if (ctrl) {
            switch (w) {
            case 'A': SelectAll(); return 0;
            case 'C': CopySelection(false); return 0;
            case 'X': CopySelection(true); return 0;
            case 'V': PasteFromClipboard(); g_swallowChar = true; return 0;
            case 'Z':
                if (UndoOnce()) DocChanged(0);
                else Toast(L"Nothing to undo");
                g_swallowChar = true;
                return 0;
            case 'T': ToggleTopMost(); g_swallowChar = true; return 0;
            case 'S':
                Toast(L"No need to save - the pad saves itself");
                g_swallowChar = true;
                return 0;
            case VK_RETURN:
                CopyAll();
                if (shift) ShowWindow(g_hwnd, SW_MINIMIZE);
                g_swallowChar = true;
                return 0;
            case VK_OEM_PLUS:
            case VK_ADD:
                SetFontPt(g_fontPt + 1);
                g_swallowChar = true;
                return 0;
            case VK_OEM_MINUS:
            case VK_SUBTRACT:
                SetFontPt(g_fontPt - 1);
                g_swallowChar = true;
                return 0;
            case '0':
                SetFontPt(kDefaultFontPt);
                g_swallowChar = true;
                return 0;
            case VK_OEM_3:
                ToggleCodeBlock();
                g_swallowChar = true;
                return 0;
            default:
                break;
            }
        }

        size_t selA, selB;
        Selection(selA, selB);

        switch (w) {
        case VK_LEFT:
            if (!shift && selA != selB) SetCaretPos(selA, false);
            else SetCaretPos(ctrl ? WordLeft(g_caret) : PrevPos(g_caret), shift);
            return 0;

        case VK_RIGHT:
            if (!shift && selA != selB) SetCaretPos(selB, false);
            else SetCaretPos(ctrl ? WordRight(g_caret) : NextPos(g_caret), shift);
            return 0;

        case VK_UP:
            MoveVertical(-1, shift);
            return 0;

        case VK_DOWN:
            MoveVertical(1, shift);
            return 0;

        case VK_HOME: {
            EnsureLayout();
            int cx, cy;
            CaretXY(g_caret, cx, cy);
            int line = (g_caret < g_units.size()) ? g_gl[g_caret].line : LineCount() - 1;
            if (line < 0) line = 0;
            if (line >= LineCount()) line = LineCount() - 1;
            size_t pos = ctrl ? 0 : (size_t)g_lineStarts[line];
            SetCaretPos(pos, shift);
            return 0;
        }

        case VK_END: {
            EnsureLayout();
            int cx, cy;
            CaretXY(g_caret, cx, cy);
            int line = (g_caret < g_units.size()) ? g_gl[g_caret].line : LineCount() - 1;
            if (line < 0) line = 0;
            if (line >= LineCount()) line = LineCount() - 1;
            size_t pos = ctrl ? g_units.size() : LineEndIndex(line);
            SetCaretPos(pos, shift);
            return 0;
        }

        case VK_PRIOR:
        case VK_NEXT: {
            RECT ta{};
            TextArea(&ta);
            int lines = (ta.bottom - ta.top) / g_lineH;
            if (lines < 1) lines = 1;
            for (int i = 0; i < lines; ++i) MoveVertical(w == VK_PRIOR ? -1 : 1, true);
            if (!shift) g_anchor = g_caret;
            return 0;
        }

        case VK_BACK:
            DeleteBackward();
            return 0;

        case VK_DELETE:
            DeleteForward();
            return 0;

        default:
            break;
        }
        break;
    }

    case WM_CHAR: {
        if (g_swallowChar) { g_swallowChar = false; return 0; }
        wchar_t c = (wchar_t)w;
        if (c == 0x16) return 0;
        if (c == 0x0D) { InsertTyped(L"\n"); return 0; }
        if (c == 0x09) { InsertTyped(L"    "); return 0; }
        if (c < 32 || c == 127) return 0;
        wchar_t buf[2] = { c, 0 };
        InsertTyped(wstring(buf, 1));
        return 0;
    }

    case WM_GETTEXT: {
        wstring s = DocText(true);
        if (w && l) {
            wchar_t* dst = (wchar_t*)l;
            size_t n = (std::min)(s.size(), (size_t)(w - 1));
            memcpy(dst, s.c_str(), n * sizeof(wchar_t));
            dst[n] = 0;
        }
        return (LRESULT)s.size();
    }

    case WM_GETTEXTLENGTH:
        return (LRESULT)DocText(false).size();

    case WM_SETTEXT: {
        DeserializeDoc(l ? (const wchar_t*)l : L"");
        g_caret = g_anchor = g_units.size();
        EnsureCaretVisible();
        if (g_view) InvalidateRect(g_view, nullptr, FALSE);
        UpdateStatus();
        return TRUE;
    }

    case WM_DROPFILES: {
        HDROP drop = (HDROP)w;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<wstring> files;
        for (UINT i = 0; i < n; ++i) {
            wchar_t p[MAX_PATH * 4]{};
            if (DragQueryFileW(drop, i, p, ARRAYSIZE(p)))
                files.push_back(p);
        }
        DragFinish(drop);
        if (!files.empty()) {
            SetFocus(hwnd);
            InsertChips(files, -1);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// ----------------------------------------------------------------- topmost
static void ToggleTopMost() {
    g_topMost = !g_topMost;
    SetWindowPos(g_hwnd, g_topMost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SaveAll();
    Toast(g_topMost ? L"Pinned on top (Ctrl+T)" : L"Unpinned (Ctrl+T)");
}

// ------------------------------------------------------------ font resizing
static void SetFontPt(int pt) {
    pt = (std::max)(8, (std::min)(42, pt));
    if (pt == g_fontPt) return;
    g_fontPt = pt;
    ApplyFonts();
    SaveAll();
    Toast(L"Font size " + std::to_wstring(pt) + L" pt");
}

// ------------------------------------------------------------------- layout
static void LayoutMain(HWND hwnd) {
    if (!g_view) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int pad = Dpi(8);
    int sh  = Dpi(30);
    int w = rc.right - pad * 2;
    int h = rc.bottom - sh - pad;
    if (w < 40) w = 40;
    if (h < 40) h = 40;
    SetWindowPos(g_view, nullptr, pad, pad, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// --------------------------------------------------------------- dark bits
static void EnableDarkMode() {
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    typedef int(WINAPI *PFN_SETPREFERREDAPPMODE)(int);
    PFN_SETPREFERREDAPPMODE fn =
        (PFN_SETPREFERREDAPPMODE)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    if (fn) fn(2);
    g_allowDarkForWindow =
        (BOOL(WINAPI*)(HWND, BOOL))(void*)GetProcAddress(ux, MAKEINTRESOURCEA(133));
}

static void EnableDarkTitleBar(HWND hwnd) {
    typedef HRESULT(WINAPI *PFN_DWM)(HWND, DWORD, LPCVOID, DWORD);
    static PFN_DWM fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) fn = (PFN_DWM)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    }
    if (!fn) return;

    BOOL dark = TRUE;
    if (FAILED(fn(hwnd, 20, &dark, sizeof(dark))))
        fn(hwnd, 19, &dark, sizeof(dark));

    COLORREF border = RGB(0x2A, 0x31, 0x3A);
    fn(hwnd, 34, &border, sizeof(border));
}

static void PaintStatus(HDC dc, const RECT& rc) {
    int sh = Dpi(30);
    RECT sb{ 0, rc.bottom - sh, rc.right, rc.bottom };
    if (sb.top < 0) return;

    FillRect(dc, &sb, g_brStatus);

    HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
    HPEN op = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, 0, sb.top, nullptr);
    LineTo(dc, rc.right, sb.top);
    SelectObject(dc, op);
    DeleteObject(pen);

    int padx = Dpi(12);
    RECT lr = sb;
    lr.left += padx;
    lr.right -= padx;

    SetBkMode(dc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(dc, g_hStatusFont);

    RECT leftRc = lr;
    leftRc.right = lr.left + (lr.right - lr.left) * 45 / 100;

    SetTextColor(dc, C_DIM);
    DrawTextW(dc, g_statusL.c_str(), -1, &leftRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT rightRc = lr;
    rightRc.left = leftRc.right + Dpi(10);
    SetTextColor(dc, g_toast.empty() ? C_DIM : C_ACCENT);

    bool fits = true;
    if (g_toast.empty() && rightRc.right > rightRc.left) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, g_statusR.c_str(), (int)g_statusR.size(), &sz);
        fits = sz.cx <= (rightRc.right - rightRc.left);
    }
    if (fits)
        DrawTextW(dc, g_statusR.c_str(), -1, &rightRc,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SelectObject(dc, of);
}

// -------------------------------------------------------------- main window
static void SummonWindow() {
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    BringWindowToTop(g_hwnd);
    if (g_view) SetFocus(g_view);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_dpi  = GetDpiForWindowSafe(hwnd);
        g_face = PickMonoFont();

        ApplyFonts();

        g_view = CreateWindowExW(0, kViewClass, L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                 0, 0, 10, 10, hwnd, nullptr, g_hInst, nullptr);
        if (g_view) {
            DragAcceptFiles(g_view, TRUE);
            DeserializeDoc(g_loadedDoc);
            g_caret = g_anchor = g_units.size();
        }

        LayoutMain(hwnd);

        CleanupOldImages();
        EnableDarkTitleBar(hwnd);
        if (g_topMost)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        SetTimer(hwnd, ID_TIMER, 400, nullptr);
        RegisterHotKey(hwnd, ID_HOTKEY, MOD_CONTROL | MOD_ALT, 'P');

        RecountDoc();
        EnsureCaretVisible();
        UpdateStatus();
        return 0;
    }

    case WM_SIZE:
        LayoutMain(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, g_brBg);
        PaintStatus(dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETFOCUS:
        if (g_view && GetFocus() != g_view) SetFocus(g_view);
        return 0;

    case WM_TIMER: {
        if (w == ID_TIMER) {
            ULONGLONG now = GetTickCount64();
            if (g_dirty && !g_chipDrag && now - g_lastEdit > 800) SaveAll();
            if (!g_toast.empty() && now > g_toastUntil) {
                g_toast.clear();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            UpdateStatus();
        }
        return 0;
    }

    case WM_HOTKEY: {
        if (w == ID_HOTKEY) {
            if (IsWindowVisible(hwnd) && GetForegroundWindow() == hwnd) {
                SaveAll();
                ShowWindow(hwnd, SW_HIDE);
            } else {
                SummonWindow();
            }
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)l;
        mmi->ptMinTrackSize.x = Dpi(440);
        mmi->ptMinTrackSize.y = Dpi(280);
        return 0;
    }

    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        ApplyFonts();
        LayoutMain(hwnd);
        RECT* pr = (RECT*)l;
        SetWindowPos(hwnd, nullptr, pr->left, pr->top,
                     pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_EXITSIZEMOVE:
        g_dirty = false;
        SaveAll();
        return 0;

    case WM_QUERYENDSESSION:
        SaveAll();
        return TRUE;

    case WM_CLOSE:
        if (g_chipDrag) FinishChipDrag(g_chipDragOrigin);
        SaveAll();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        UnregisterHotKey(hwnd, ID_HOTKEY);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// -------------------------------------------------------------- app icon
static HICON MakeAppIcon(int size) {
    using namespace Gdiplus;

    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    RectF r(0.0f, 0.0f, (REAL)size, (REAL)size);
    float rad = size * 0.22f;
    float d = rad * 2.0f;

    GraphicsPath path;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();

    SolidBrush bg(Color(255, 16, 19, 24));
    g.FillPath(&bg, &path);

    Pen border(Color(255, 0x2A, 0x31, 0x3A), (REAL)(size > 20 ? 1.2f : 1.0f));
    g.DrawPath(&border, &path);

    FontFamily ffMono(L"Consolas");
    FontFamily ffAlt(L"Courier New");
    FontFamily* ff = (ffMono.GetLastStatus() == Ok) ? &ffMono : &ffAlt;
    Font font(ff, size * 0.52f, FontStyleBold, UnitPixel);
    SolidBrush fg(Color(255, 0x7E, 0xE7, 0x87));

    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L">_", 2, &font, r, &sf, &fg);

    HBITMAP hbm = nullptr;
    if (bmp.GetHBITMAP(Color(0, 0, 0, 0), &hbm) != Ok || !hbm) return nullptr;
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmColor = hbm;
    ii.hbmMask  = mask;
    HICON ico = CreateIconIndirect(&ii);

    DeleteObject(hbm);
    if (mask) DeleteObject(mask);
    return ico;
}

// --------------------------------------------------------------------- main
static void SetAppUserModelId() {
    typedef HRESULT(WINAPI *PFN)(PCWSTR);
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    if (!sh) return;
    PFN fn = (PFN)(void*)GetProcAddress(sh, "SetCurrentProcessExplicitAppUserModelID");
    if (fn) fn(L"glitzhXEC.nPrompt");
}

static void EnableDpiAwareness() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        typedef BOOL(WINAPI *PFN)(void*);
        PFN fn = (PFN)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (fn && fn((void*)-4)) return;
    }
    HMODULE sh = LoadLibraryW(L"shcore.dll");
    if (sh) {
        typedef HRESULT(WINAPI *PFN)(int);
        PFN fn = (PFN)(void*)GetProcAddress(sh, "SetProcessDpiAwareness");
        if (fn) fn(2);
    }
}

static void CenterOnWorkArea(HWND hwnd, int width, int height) {
    RECT wa{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - width) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void RestoreOrPlaceWindow(HWND hwnd) {
    if (g_hasRect) {
        RECT r = g_savedRect;
        RECT wa{};
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi))
            wa = mi.rcWork;
        int rw = r.right - r.left;
        int rh = r.bottom - r.top;
        if (rw > 200 && rh > 150) {
            if (r.left < wa.left - 50 || r.left > wa.right - 100 ||
                r.top  < wa.top  - 50 || r.top  > wa.bottom - 100) {
                CenterOnWorkArea(hwnd, rw, rh);
            } else {
                SetWindowPos(hwnd, nullptr, r.left, r.top, rw, rh,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return;
        }
    }
    CenterOnWorkArea(hwnd, Dpi(820), Dpi(620));
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    g_hInst = hInst;
    SetAppUserModelId();
    EnableDpiAwareness();

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    LoadIconSheet();
    EnableDarkMode();

    g_brBg     = CreateSolidBrush(C_BG);
    g_brEdit   = CreateSolidBrush(C_EDITBG);
    g_brStatus = CreateSolidBrush(C_STATUS);

    g_face = PickMonoFont();
    LoadPrefs();

    WNDCLASSEXW vc{};
    vc.cbSize        = sizeof(vc);
    vc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    vc.lpfnWndProc   = ViewProc;
    vc.hInstance     = hInst;
    vc.hCursor       = LoadCursorW(nullptr, IDC_IBEAM);
    vc.lpszClassName = kViewClass;
    vc.hbrBackground = g_brEdit;
    if (!RegisterClassExW(&vc)) return 1;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                         IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm       = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                         IMAGE_ICON, 16, 16, 0);
    if (!wc.hIcon)   wc.hIcon = MakeAppIcon(32);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon ? wc.hIcon : MakeAppIcon(16);
    if (!wc.hIcon)   wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) return 1;

    g_hwnd = CreateWindowExW(
        g_topMost ? WS_EX_TOPMOST : 0,
        kWndClass, kAppName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 620,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    g_dpi = GetDpiForWindowSafe(g_hwnd);
    ApplyFonts();
    RestoreOrPlaceWindow(g_hwnd);
    LayoutMain(g_hwnd);
    UpdateStatus();

    ShowWindow(g_hwnd, nCmdShow ? nCmdShow : SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    if (g_view) {
        SetFocus(g_view);
        EnsureCaretVisible();
        InvalidateRect(g_view, nullptr, FALSE);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete g_iconSheet;
    g_iconSheet = nullptr;

    if (g_hFont)       DeleteObject(g_hFont);
    if (g_hStatusFont) DeleteObject(g_hStatusFont);
    if (g_brBg)        DeleteObject(g_brBg);
    if (g_brEdit)      DeleteObject(g_brEdit);
    if (g_brStatus)    DeleteObject(g_brStatus);

    if (gdipToken) Gdiplus::GdiplusShutdown(gdipToken);
    return 0;
}
