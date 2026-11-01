// WinTetris - a small Tetris-like game built on the Win32 API and GDI.
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>

#include "audio.h"
#include "game.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

namespace {

// ---------------------------------------------------------------- layout ----
// All layout values are in device-independent pixels (96 DPI).
constexpr int kDipClientW = 620;
constexpr int kDipClientH = 688;
constexpr int kDipCell = 32;
constexpr int kDipBoardX = 24;
constexpr int kDipBoardY = 24;
constexpr int kDipCardPad = 8;
constexpr int kDipPanelX = 368;
constexpr int kDipPanelY = 16;
constexpr int kDipPanelW = 236;
constexpr int kDipPanelH = 656;
constexpr int kDipInnerPad = 14;

// ----------------------------------------------------------------- colors ---
constexpr COLORREF kBg = RGB(9, 13, 22);
constexpr COLORREF kCard = RGB(15, 22, 38);
constexpr COLORREF kBorder = RGB(30, 42, 64);
constexpr COLORREF kBoardBg = RGB(11, 17, 29);
constexpr COLORREF kGrid = RGB(21, 31, 48);
constexpr COLORREF kText = RGB(232, 236, 244);
constexpr COLORREF kMuted = RGB(122, 138, 163);
constexpr COLORREF kAccent = RGB(56, 189, 248);
constexpr COLORREF kGold = RGB(250, 204, 21);
constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kBlack = RGB(0, 0, 0);

constexpr COLORREF kPieceColor[8] = {
    RGB(0, 0, 0),        // unused
    RGB(34, 211, 238),   // I cyan
    RGB(59, 130, 246),   // J blue
    RGB(245, 158, 11),   // L amber
    RGB(250, 204, 21),   // O yellow
    RGB(34, 197, 94),    // S green
    RGB(168, 85, 247),   // T purple
    RGB(239, 68, 68),    // Z red
};

COLORREF Mix(COLORREF a, COLORREF b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto mixChannel = [t](int x, int y) {
        return static_cast<int>(std::lround(x + (y - x) * t));
    };
    return RGB(mixChannel(GetRValue(a), GetRValue(b)),
               mixChannel(GetGValue(a), GetGValue(b)),
               mixChannel(GetBValue(a), GetBValue(b)));
}

COLORREF Lighten(COLORREF c, float t) { return Mix(c, kWhite, t); }
COLORREF Darken(COLORREF c, float t) { return Mix(c, kBlack, t); }

// ------------------------------------------------------------- resources ----
struct Fonts {
    HFONT title = nullptr;
    HFONT label = nullptr;
    HFONT score = nullptr;
    HFONT value = nullptr;
    HFONT tiny = nullptr;
    HFONT popup = nullptr;
    HFONT overlayTitle = nullptr;
    HFONT overlayText = nullptr;
};

struct Brushes {
    HBRUSH bg = nullptr;
    HBRUSH card = nullptr;
    HBRUSH border = nullptr;
    HBRUSH board = nullptr;
    HBRUSH grid = nullptr;
    HBRUSH box = nullptr;
    HBRUSH accent = nullptr;
    HBRUSH blockEdge[8]{};
    HBRUSH ghostFill[8]{};
    HBRUSH ghostEdge[8]{};
};

struct App {
    HWND hwnd = nullptr;
    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    int bufW = 0;
    int bufH = 0;

    HDC dimDC = nullptr;
    HBITMAP dimBmp = nullptr;
    HDC whiteDC = nullptr;
    HBITMAP whiteBmp = nullptr;

    UINT dpi = 96;
    float scale = 1.0f;

    Fonts fonts;
    Brushes brushes;
    tetris::Game game;
    MusicPlayer music;
    tetris::Game::State lastState = tetris::Game::State::GameOver;

    LARGE_INTEGER qpf{};
    LONGLONG lastTick = 0;
    bool hasLastTick = false;
    int savedBest = -1;

    int px(int dip) const { return static_cast<int>(std::lround(dip * scale)); }
};

RECT Rect(int l, int t, int r, int b) { return RECT{l, t, r, b}; }

void FillRectC(HDC dc, const RECT& r, HBRUSH brush) { FillRect(dc, &r, brush); }

void FrameRectC(HDC dc, RECT r, HBRUSH brush) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    FrameRect(dc, &r, brush);
}

void FillVGradient(HDC dc, const RECT& r, COLORREF top, COLORREF bottom) {
    TRIVERTEX v[2];
    v[0].x = r.left;
    v[0].y = r.top;
    v[0].Red = static_cast<COLOR16>(GetRValue(top) << 8);
    v[0].Green = static_cast<COLOR16>(GetGValue(top) << 8);
    v[0].Blue = static_cast<COLOR16>(GetBValue(top) << 8);
    v[0].Alpha = 0;
    v[1].x = r.right;
    v[1].y = r.bottom;
    v[1].Red = static_cast<COLOR16>(GetRValue(bottom) << 8);
    v[1].Green = static_cast<COLOR16>(GetGValue(bottom) << 8);
    v[1].Blue = static_cast<COLOR16>(GetBValue(bottom) << 8);
    v[1].Alpha = 0;
    GRADIENT_RECT g{0, 1};
    GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
}

void AlphaFill(HDC dc, const RECT& r, BYTE alpha, bool white, HDC dimDC, HDC whiteDC) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(dc, r.left, r.top, r.right - r.left, r.bottom - r.top,
               white ? whiteDC : dimDC, 0, 0, 1, 1, bf);
}

HBITMAP MakeSolidBitmap(HDC dc, COLORREF color) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1;
    bi.bmiHeader.biHeight = 1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bits) {
        const BYTE r = GetRValue(color);
        const BYTE g = GetGValue(color);
        const BYTE b = GetBValue(color);
        auto* p = static_cast<BYTE*>(bits);
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 255;
    }
    return bmp;
}

HFONT MakeFont(float scale, int dipHeight, int weight, const wchar_t* face) {
    const int height = -static_cast<int>(std::lround(dipHeight * scale));
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

void DestroyFonts(Fonts& f) {
    HFONT* all[] = {&f.title, &f.label, &f.score, &f.value,
                    &f.tiny, &f.popup, &f.overlayTitle, &f.overlayText};
    for (HFONT* p : all) {
        if (*p) DeleteObject(*p);
        *p = nullptr;
    }
}

void CreateFonts(App& a) {
    DestroyFonts(a.fonts);
    const float s = a.scale;
    a.fonts.title = MakeFont(s, 30, FW_HEAVY, L"Segoe UI");
    a.fonts.label = MakeFont(s, 11, FW_SEMIBOLD, L"Segoe UI");
    a.fonts.score = MakeFont(s, 27, FW_BOLD, L"Segoe UI");
    a.fonts.value = MakeFont(s, 15, FW_SEMIBOLD, L"Segoe UI");
    a.fonts.tiny = MakeFont(s, 10, FW_NORMAL, L"Segoe UI");
    a.fonts.popup = MakeFont(s, 26, FW_HEAVY, L"Segoe UI");
    a.fonts.overlayTitle = MakeFont(s, 34, FW_HEAVY, L"Segoe UI");
    a.fonts.overlayText = MakeFont(s, 15, FW_SEMIBOLD, L"Segoe UI");
}

void CreateBrushes(Brushes& b) {
    b.bg = CreateSolidBrush(kBg);
    b.card = CreateSolidBrush(kCard);
    b.border = CreateSolidBrush(kBorder);
    b.board = CreateSolidBrush(kBoardBg);
    b.grid = CreateSolidBrush(kGrid);
    b.box = CreateSolidBrush(RGB(10, 16, 28));
    b.accent = CreateSolidBrush(kAccent);
    for (int i = 1; i < 8; ++i) {
        b.blockEdge[i] = CreateSolidBrush(Mix(kPieceColor[i], kBlack, 0.55f));
        b.ghostFill[i] = CreateSolidBrush(Mix(kBoardBg, kPieceColor[i], 0.12f));
        b.ghostEdge[i] = CreateSolidBrush(Mix(kBoardBg, kPieceColor[i], 0.55f));
    }
}

void DestroyBrushes(Brushes& b) {
    HBRUSH* all[] = {&b.bg, &b.card, &b.border, &b.board, &b.grid, &b.box, &b.accent};
    for (HBRUSH* p : all) {
        if (*p) DeleteObject(*p);
        *p = nullptr;
    }
    for (int i = 1; i < 8; ++i) {
        DeleteObject(b.blockEdge[i]);
        DeleteObject(b.ghostFill[i]);
        DeleteObject(b.ghostEdge[i]);
        b.blockEdge[i] = b.ghostFill[i] = b.ghostEdge[i] = nullptr;
    }
}

void Text(HDC dc, HFONT font, COLORREF color, const RECT& r, const std::wstring& s,
          UINT flags) {
    if (!font) return;
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT rc = r;
    DrawTextW(dc, s.c_str(), -1, &rc, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

std::wstring Number(int value) {
    wchar_t buf[32];
    swprintf_s(buf, L"%d", value);
    std::wstring digits = buf;
    std::wstring out;
    int count = 0;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        out.push_back(digits[i]);
        if (++count % 3 == 0 && i > 0) out.push_back(L',');
    }
    std::reverse(out.begin(), out.end());
    return out;
}

RECT BoardRect(const App& a) {
    const int cell = a.px(kDipCell);
    const int x = a.px(kDipBoardX);
    const int y = a.px(kDipBoardY);
    return Rect(x, y, x + cell * tetris::kCols, y + cell * tetris::kRows);
}

RECT BoardCardRect(const App& a) {
    RECT r = BoardRect(a);
    const int pad = a.px(kDipCardPad);
    r.left -= pad;
    r.top -= pad;
    r.right += pad;
    r.bottom += pad;
    return r;
}

RECT PanelRect(const App& a) {
    const int x = a.px(kDipPanelX);
    const int y = a.px(kDipPanelY);
    return Rect(x, y, x + a.px(kDipPanelW), y + a.px(kDipPanelH));
}

void DrawBlock(HDC dc, const App& a, int x, int y, int size, int type) {
    if (type < 1 || type > 7 || size < 4) return;
    const COLORREF color = kPieceColor[type];
    RECT outer = Rect(x, y, x + size, y + size);
    RECT inner = Rect(x + 1, y + 1, x + size - 1, y + size - 1);
    FillVGradient(dc, inner, Lighten(color, 0.32f), Darken(color, 0.22f));
    FrameRectC(dc, outer, a.brushes.blockEdge[type]);
}

void DrawGhost(HDC dc, const App& a, int x, int y, int size, int type) {
    if (type < 1 || type > 7) return;
    RECT r = Rect(x + 1, y + 1, x + size - 1, y + size - 1);
    FillRectC(dc, r, a.brushes.ghostFill[type]);
    FrameRectC(dc, r, a.brushes.ghostEdge[type]);
}

void DrawMiniPiece(HDC dc, const App& a, const RECT& box, int type) {
    if (type < 1 || type > 7) return;
    const uint16_t mask = tetris::Game::Mask(type, 0);
    int minRow = 4, maxRow = -1, minCol = 4, maxCol = -1;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!(mask & (1u << (r * 4 + c)))) continue;
            minRow = std::min(minRow, r);
            maxRow = std::max(maxRow, r);
            minCol = std::min(minCol, c);
            maxCol = std::max(maxCol, c);
        }
    }
    if (maxRow < 0) return;
    const int cell = a.px(16);
    const int cols = maxCol - minCol + 1;
    const int rows = maxRow - minRow + 1;
    const int startX = box.left + (box.right - box.left - cols * cell) / 2;
    const int startY = box.top + (box.bottom - box.top - rows * cell) / 2;
    for (int r = minRow; r <= maxRow; ++r) {
        for (int c = minCol; c <= maxCol; ++c) {
            if (!(mask & (1u << (r * 4 + c)))) continue;
            DrawBlock(dc, a, startX + (c - minCol) * cell, startY + (r - minRow) * cell,
                      cell, type);
        }
    }
}

void DrawPieceBox(HDC dc, const App& a, const RECT& box, int type) {
    FillRectC(dc, box, a.brushes.box);
    FrameRectC(dc, box, a.brushes.border);
    DrawMiniPiece(dc, a, box, type);
}

void DrawBoard(App& a) {
    using tetris::Game;
    HDC dc = a.memDC;
    const RECT board = BoardRect(a);
    const RECT card = BoardCardRect(a);
    const int cell = a.px(kDipCell);
    const Game& g = a.game;

    FillRectC(dc, card, a.brushes.card);
    FrameRectC(dc, card, a.brushes.border);
    FillRectC(dc, board, a.brushes.board);

    for (int c = 1; c < tetris::kCols; ++c) {
        const int x = board.left + c * cell;
        FillRectC(dc, Rect(x, board.top, x + 1, board.bottom), a.brushes.grid);
    }
    for (int r = 1; r < tetris::kRows; ++r) {
        const int y = board.top + r * cell;
        FillRectC(dc, Rect(board.left, y, board.right, y + 1), a.brushes.grid);
    }

    for (int r = 0; r < tetris::kRows; ++r) {
        for (int c = 0; c < tetris::kCols; ++c) {
            if (!g.Occupied(r, c)) continue;
            DrawBlock(dc, a, board.left + c * cell, board.top + r * cell, cell, g.board()[r][c]);
        }
    }

    if (g.state() == Game::State::Playing && g.piece().type != 0) {
        const int ghost = g.GhostY();
        const uint16_t mask = Game::Mask(g.piece().type, g.piece().rot);
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                if (!(mask & (1u << (r * 4 + c)))) continue;
                const int y = ghost + r;
                if (y < 0) continue;
                DrawGhost(dc, a, board.left + (g.piece().x + c) * cell,
                          board.top + y * cell, cell, g.piece().type);
            }
        }
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                if (!(mask & (1u << (r * 4 + c)))) continue;
                const int y = g.piece().y + r;
                if (y < 0) continue;
                DrawBlock(dc, a, board.left + (g.piece().x + c) * cell,
                          board.top + y * cell, cell, g.piece().type);
            }
        }
    }

    if (g.state() == Game::State::Clearing) {
        const double progress = g.clearProgress();
        const BYTE alpha = static_cast<BYTE>(std::clamp(70.0 + 150.0 * progress, 0.0, 255.0));
        for (int row : g.clearingRows()) {
            if (row < 0 || row >= tetris::kRows) continue;
            RECT rr = Rect(board.left, board.top + row * cell, board.right,
                           board.top + (row + 1) * cell);
            AlphaFill(dc, rr, alpha, true, a.dimDC, a.whiteDC);
        }
    }

    FrameRectC(dc, board, a.brushes.border);
}

void DrawSidebar(App& a) {
    HDC dc = a.memDC;
    const tetris::Game& g = a.game;
    const RECT panel = PanelRect(a);
    const int left = panel.left + a.px(kDipInnerPad);
    const int right = panel.right - a.px(kDipInnerPad);
    const int top = panel.top;
    const int boxSize = a.px(68);
    const int boxX = (panel.left + panel.right - boxSize) / 2;

    FillRectC(dc, panel, a.brushes.card);
    FrameRectC(dc, panel, a.brushes.border);

    Text(dc, a.fonts.title, kText, Rect(left, top + a.px(16), right, top + a.px(56)),
         L"TETRIS", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    FillRectC(dc, Rect(left, top + a.px(62), left + a.px(46), top + a.px(65)),
              a.brushes.accent);

    Text(dc, a.fonts.label, kMuted, Rect(left, top + a.px(80), right, top + a.px(96)),
         L"HOLD", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT holdBox = Rect(boxX, top + a.px(102), boxX + boxSize, top + a.px(102) + boxSize);
    DrawPieceBox(dc, a, holdBox, g.hold());

    Text(dc, a.fonts.label, kMuted, Rect(left, top + a.px(182), right, top + a.px(198)),
         L"NEXT", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    for (int i = 0; i < 3; ++i) {
        const int y = top + a.px(204 + i * 70);
        RECT box = Rect(boxX, y, boxX + boxSize, y + boxSize);
        const int type = (static_cast<int>(g.queue().size()) > i) ? g.queue()[i] : 0;
        DrawPieceBox(dc, a, box, type);
    }

    const auto divider = [&](int y) {
        FillRectC(dc, Rect(left, top + a.px(y), right, top + a.px(y) + 1), a.brushes.border);
    };
    divider(430);

    Text(dc, a.fonts.label, kMuted, Rect(left, top + a.px(442), right, top + a.px(458)),
         L"SCORE", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    Text(dc, a.fonts.score, kText, Rect(left, top + a.px(458), right, top + a.px(492)),
         Number(g.score()), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    const auto statRow = [&](int y, const wchar_t* label, const std::wstring& value,
                             COLORREF color) {
        RECT r = Rect(left, top + a.px(y), right, top + a.px(y + 18));
        Text(dc, a.fonts.label, kMuted, r, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        Text(dc, a.fonts.value, color, r, value, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    };
    statRow(500, L"BEST", Number(g.best()), kText);
    statRow(524, L"LEVEL", std::to_wstring(g.level()), kText);
    statRow(548, L"LINES", std::to_wstring(g.lines()), kText);

    divider(576);

    static const wchar_t* const kControls[4] = {
        L"\u2190 \u2192  MOVE      \u2191 / X  ROTATE",
        L"Z  ROTATE      \u2193  SOFT DROP",
        L"SPACE  HARD DROP      C  HOLD",
        L"P  PAUSE   R  RESTART   M  MUTE",
    };
    for (int i = 0; i < 4; ++i) {
        const int y = top + a.px(586 + i * 16);
        const COLORREF color = (i == 3 && a.music.Muted()) ? kAccent : kMuted;
        Text(dc, a.fonts.tiny, color, Rect(left, y, right, y + a.px(14)), kControls[i],
             DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

void DrawMessage(App& a) {
    const double alpha = a.game.messageAlpha();
    if (alpha <= 0.0 || a.game.message().empty()) return;
    HDC dc = a.memDC;
    const RECT board = BoardRect(a);
    const int y = board.top + a.px(180);
    const RECT r = Rect(board.left, y, board.right, y + a.px(34));
    const COLORREF color = Mix(kBoardBg, kWhite, static_cast<float>(alpha));
    Text(dc, a.fonts.popup, Mix(kBoardBg, kBlack, 0.6f),
         Rect(r.left + a.px(1), r.top + a.px(1), r.right + a.px(1), r.bottom + a.px(1)),
         a.game.message(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    Text(dc, a.fonts.popup, color, r, a.game.message(),
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DrawPauseOverlay(App& a) {
    HDC dc = a.memDC;
    const RECT board = BoardRect(a);
    const int cy = (board.top + board.bottom) / 2;
    AlphaFill(dc, board, 150, false, a.dimDC, a.whiteDC);
    Text(dc, a.fonts.overlayTitle, kText,
         Rect(board.left, cy - a.px(44), board.right, cy + a.px(4)), L"PAUSED",
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    Text(dc, a.fonts.overlayText, kMuted,
         Rect(board.left, cy + a.px(20), board.right, cy + a.px(44)),
         L"PRESS P TO RESUME", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DrawGameOverOverlay(App& a) {
    HDC dc = a.memDC;
    const tetris::Game& g = a.game;
    const RECT board = BoardRect(a);
    const int cy = (board.top + board.bottom) / 2;
    AlphaFill(dc, board, 205, false, a.dimDC, a.whiteDC);

    Text(dc, a.fonts.overlayTitle, kText,
         Rect(board.left, cy - a.px(92), board.right, cy - a.px(40)), L"GAME OVER",
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    Text(dc, a.fonts.overlayText, kText,
         Rect(board.left, cy - a.px(16), board.right, cy + a.px(8)),
         L"SCORE  " + Number(g.score()), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    if (g.newBest()) {
        Text(dc, a.fonts.overlayText, kGold,
             Rect(board.left, cy + a.px(14), board.right, cy + a.px(38)), L"NEW BEST!",
             DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    } else {
        Text(dc, a.fonts.overlayText, kMuted,
             Rect(board.left, cy + a.px(14), board.right, cy + a.px(38)),
             L"BEST  " + Number(g.best()), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    Text(dc, a.fonts.tiny, kMuted,
         Rect(board.left, cy + a.px(66), board.right, cy + a.px(84)),
         L"PRESS R TO RESTART", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void RenderFrame(App& a) {
    using tetris::Game;
    if (!a.memDC) return;
    HDC dc = a.memDC;
    const RECT client = Rect(0, 0, a.bufW, a.bufH);
    FillRectC(dc, client, a.brushes.bg);
    DrawBoard(a);
    DrawSidebar(a);
    DrawMessage(a);
    if (a.game.state() == Game::State::Paused) DrawPauseOverlay(a);
    if (a.game.state() == Game::State::GameOver) DrawGameOverOverlay(a);
}

// ------------------------------------------------------- app plumbing ------
void RecreateBackbuffer(App& a) {
    RECT cr{};
    GetClientRect(a.hwnd, &cr);
    const int w = cr.right - cr.left;
    const int h = cr.bottom - cr.top;
    if (w <= 0 || h <= 0) return;
    if (a.memDC && w == a.bufW && h == a.bufH) return;

    if (a.memDC) {
        DeleteDC(a.memDC);
        a.memDC = nullptr;
    }
    if (a.memBmp) {
        DeleteObject(a.memBmp);
        a.memBmp = nullptr;
    }

    HDC screen = GetDC(a.hwnd);
    a.memDC = CreateCompatibleDC(screen);
    a.memBmp = CreateCompatibleBitmap(screen, w, h);
    SelectObject(a.memDC, a.memBmp);
    ReleaseDC(a.hwnd, screen);
    a.bufW = w;
    a.bufH = h;
}

void InitPaintResources(App& a) {
    CreateBrushes(a.brushes);
    CreateFonts(a);

    HDC screen = GetDC(nullptr);
    a.dimDC = CreateCompatibleDC(screen);
    a.dimBmp = MakeSolidBitmap(screen, kBlack);
    SelectObject(a.dimDC, a.dimBmp);
    a.whiteDC = CreateCompatibleDC(screen);
    a.whiteBmp = MakeSolidBitmap(screen, kWhite);
    SelectObject(a.whiteDC, a.whiteBmp);
    ReleaseDC(nullptr, screen);
}

void DestroyPaintResources(App& a) {
    DestroyFonts(a.fonts);
    DestroyBrushes(a.brushes);
    if (a.memDC) DeleteDC(a.memDC);
    if (a.memBmp) DeleteObject(a.memBmp);
    if (a.dimDC) DeleteDC(a.dimDC);
    if (a.dimBmp) DeleteObject(a.dimBmp);
    if (a.whiteDC) DeleteDC(a.whiteDC);
    if (a.whiteBmp) DeleteObject(a.whiteBmp);
    a.memDC = nullptr;
    a.memBmp = nullptr;
    a.dimDC = nullptr;
    a.dimBmp = nullptr;
    a.whiteDC = nullptr;
    a.whiteBmp = nullptr;
}

std::wstring BestScorePath() {
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &base))) {
        return {};
    }
    std::wstring dir = base;
    CoTaskMemFree(base);
    dir += L"\\WinTetris";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\best.txt";
}

int LoadBest() {
    const std::wstring path = BestScorePath();
    if (path.empty()) return 0;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return 0;
    int value = 0;
    if (fscanf_s(f, "%d", &value) != 1) value = 0;
    fclose(f);
    return std::max(0, value);
}

void SaveBest(int value) {
    const std::wstring path = BestScorePath();
    if (path.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    fprintf(f, "%d\n", value);
    fclose(f);
}

// ------------------------------------------------------- window / loop -----
constexpr wchar_t kClassName[] = L"WinTetrisWindow";
constexpr UINT_PTR kTimerId = 1;

void ApplyDarkTitlebar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
    const int round = 2;  // DWMWCP_ROUND on Windows 11
    DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
}

void SyncMusic(App& a) {
    using State = tetris::Game::State;
    const State state = a.game.state();
    if (!a.music.Available()) {
        a.lastState = state;
        return;
    }

    switch (state) {
        case State::Playing:
        case State::Clearing:
            if (a.lastState == State::Paused) {
                a.music.SetPaused(false);
            } else if (a.lastState == State::GameOver) {
                a.music.Start();
            }
            break;
        case State::Paused:
            a.music.SetPaused(true);
            break;
        case State::GameOver:
            if (a.lastState != State::GameOver) a.music.PlayGameOver();
            break;
    }
    a.lastState = state;
}

void Tick(App& a) {
    a.music.Pump();
    if (IsIconic(a.hwnd)) return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    double dt = 0.0;
    if (a.hasLastTick) {
        dt = static_cast<double>(now.QuadPart - a.lastTick) * 1000.0 /
             static_cast<double>(a.qpf.QuadPart);
    }
    a.lastTick = now.QuadPart;
    a.hasLastTick = true;
    dt = std::clamp(dt, 0.0, 100.0);

    a.game.Update(dt);
    SyncMusic(a);
    if (a.game.state() == tetris::Game::State::GameOver && a.game.best() != a.savedBest) {
        a.savedBest = a.game.best();
        SaveBest(a.savedBest);
    }
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

void OnKeyDown(App& a, WPARAM vk, bool repeat) {
    using A = tetris::Game::Action;
    switch (vk) {
        case VK_LEFT:
            if (!repeat) a.game.Handle(A::LeftDown);
            break;
        case VK_RIGHT:
            if (!repeat) a.game.Handle(A::RightDown);
            break;
        case VK_DOWN:
            if (!repeat) a.game.Handle(A::SoftDown);
            break;
        case VK_UP:
        case 'X':
            if (!repeat) a.game.Handle(A::RotateCW);
            break;
        case 'Z':
            if (!repeat) a.game.Handle(A::RotateCCW);
            break;
        case VK_SPACE:
            if (!repeat) a.game.Handle(A::HardDrop);
            break;
        case 'C':
            if (!repeat) a.game.Handle(A::Hold);
            break;
        case 'M':
            if (!repeat) a.music.ToggleMute();
            break;
        case 'P':
        case VK_ESCAPE:
            if (!repeat) a.game.Handle(A::TogglePause);
            break;
        case 'R':
            if (!repeat) {
                a.game.Handle(A::Restart);
                a.music.Start();
                a.lastState = a.game.state();
            }
            break;
        case VK_RETURN:
            if (!repeat) {
                if (a.game.state() == tetris::Game::State::GameOver) {
                    a.game.Handle(A::Restart);
                    a.music.Start();
                    a.lastState = a.game.state();
                } else if (a.game.state() == tetris::Game::State::Paused) {
                    a.game.Handle(A::TogglePause);
                }
            }
            break;
        default:
            break;
    }
    SyncMusic(a);
}

void OnKeyUp(App& a, WPARAM vk) {
    using A = tetris::Game::Action;
    switch (vk) {
        case VK_LEFT: a.game.Handle(A::LeftUp); break;
        case VK_RIGHT: a.game.Handle(A::RightUp); break;
        case VK_DOWN: a.game.Handle(A::SoftUp); break;
        default: break;
    }
}

float ComputeScale(UINT dpi, const RECT& work) {
    const float native = static_cast<float>(dpi) / 96.0f;
    const float chromeH = 44.0f * native;  // title bar + borders, roughly
    const float availW = static_cast<float>(work.right - work.left) - 24.0f;
    const float availH =
        static_cast<float>(work.bottom - work.top) - chromeH - 16.0f;
    float s = native;
    s = std::min(s, availW / static_cast<float>(kDipClientW));
    s = std::min(s, availH / static_cast<float>(kDipClientH));
    return std::clamp(s, 0.5f, native);
}

void ApplyLayout(App& a, UINT dpi) {
    a.dpi = dpi;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{};
    const HMONITOR monitor = MonitorFromWindow(a.hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    a.scale = ComputeScale(dpi, work);
    CreateFonts(a);

    const DWORD style = static_cast<DWORD>(GetWindowLongW(a.hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(a.hwnd, GWL_EXSTYLE));
    RECT wr = Rect(0, 0, a.px(kDipClientW), a.px(kDipClientH));
    AdjustWindowRectExForDpi(&wr, style, FALSE, exStyle, dpi);
    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int winX = work.left + (workW - winW) / 2;
    const int winY = work.top + (workH - winH) / 2;

    SetWindowPos(a.hwnd, nullptr, winX, winY, winW, winH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RecreateBackbuffer(a);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* a = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        a = static_cast<App*>(cs->lpCreateParams);
        a->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    if (!a) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            if (a->memDC) {
                RenderFrame(*a);
                BitBlt(dc, 0, 0, a->bufW, a->bufH, a->memDC, 0, 0, SRCCOPY);
            } else {
                RECT cr{};
                GetClientRect(hwnd, &cr);
                FillRect(dc, &cr, a->brushes.bg);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            a->hasLastTick = false;
            RecreateBackbuffer(*a);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            if (wParam == kTimerId) Tick(*a);
            return 0;
        case WM_KEYDOWN:
            OnKeyDown(*a, wParam, (lParam & (1LL << 30)) != 0);
            return 0;
        case WM_KEYUP:
            OnKeyUp(*a, wParam);
            return 0;
        case WM_KILLFOCUS:
            a->game.Pause();
            SyncMusic(*a);
            a->hasLastTick = false;
            return 0;
        case WM_SETFOCUS:
            a->hasLastTick = false;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;
        case WM_DPICHANGED:
            ApplyLayout(*a, HIWORD(wParam));
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerId);
            if (a->game.best() > a->savedBest) {
                a->savedBest = a->game.best();
                SaveBest(a->savedBest);
            }
            a->music.Shutdown();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    App app;
    QueryPerformanceFrequency(&app.qpf);

    HDC screen = GetDC(nullptr);
    app.dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
    ReleaseDC(nullptr, screen);
    if (app.dpi == 0) app.dpi = 96;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    app.scale = ComputeScale(app.dpi, work);

    InitPaintResources(app);

    app.savedBest = LoadBest();
    app.game.SetBest(app.savedBest);

    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT wr = Rect(0, 0, app.px(kDipClientW), app.px(kDipClientH));
    AdjustWindowRectExForDpi(&wr, style, FALSE, 0, app.dpi);
    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;

    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int winX = work.left + (workW - winW) / 2;
    const int winY = work.top + (workH - winH) / 2;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        DestroyPaintResources(app);
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, kClassName, L"Tetris", style, winX, winY, winW, winH,
                                nullptr, nullptr, hInstance, &app);
    if (!hwnd) {
        DestroyPaintResources(app);
        return 1;
    }

    ApplyDarkTitlebar(hwnd);

    const UINT windowDpi = GetDpiForWindow(hwnd);
    ApplyLayout(app, windowDpi != 0 ? windowDpi : app.dpi);

    if (app.music.Init()) app.music.Start();
    app.lastState = app.game.state();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetTimer(hwnd, kTimerId, 15, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyPaintResources(app);
    return static_cast<int>(msg.wParam);
}
