// =========================================================================
//  Obsidian Terminal — Native Win32 + GDI+ Application Implementation
//  Hardcore C++. Double-buffered GDI+ rendering. Dark Bloomberg aesthetics.
// =========================================================================

#include "app.hpp"
#include <windowsx.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>

namespace obsidian {
namespace desktop {

// ── Layout constants (pixels) ────────────────────────────────────────────

static constexpr int kCmdBarH    = 48;
static constexpr int kMetricsH   = 44;
static constexpr int kNavRailW   = 62;
static constexpr int kStatusBarH = 28;
static constexpr int kCardGap    = 8;
static constexpr int kGridPad    = 10;

// ── Colour palette ───────────────────────────────────────────────────────
//    Hand-picked. Warm blacks. Desaturated amber. No AI gradients.

static const Gdiplus::Color cBgVoid     (255,   6,   6,   8);
static const Gdiplus::Color cBgPrimary  (255,  10,  10,  14);
static const Gdiplus::Color cBgPanel    (255,  14,  14,  20);
static const Gdiplus::Color cBgElevated (255,  19,  19,  25);
static const Gdiplus::Color cBgHover    (255,  26,  26,  33);

static const Gdiplus::Color cAmber      (255, 212, 137,  26);
static const Gdiplus::Color cAmberDim   (255, 184, 115,  21);
static const Gdiplus::Color cAmberGlow  ( 18, 212, 137,  26);

static const Gdiplus::Color cTextBright (255, 234, 234, 237);
static const Gdiplus::Color cTextNormal (255, 194, 194, 202);
static const Gdiplus::Color cTextDim    (255, 122, 122, 138);
static const Gdiplus::Color cTextGhost  (255,  68,  68,  90);
static const Gdiplus::Color cTextVoid   (255,  42,  42,  58);

static const Gdiplus::Color cGreen      (255,  48, 184,  84);
static const Gdiplus::Color cRed        (255, 232,  65,  58);

static const Gdiplus::Color cBorder1    (255,  28,  28,  40);
static const Gdiplus::Color cBorder2    (255,  38,  38,  52);

static const Gdiplus::Color cFkeyGreen  (255,  30, 110,  48);
static const Gdiplus::Color cFkeyGold   (255, 138, 114,   0);

// ── Metric strip labels ──────────────────────────────────────────────────

static const wchar_t* kMetricLabels[] = {
    L"NET P&L", L"EXPOSURE", L"POSITIONS", L"MARGIN", L"VOL INDEX", L"ALERTS"
};
static const wchar_t* kMetricValues[] = {
    L"\u2014", L"\u2014", L"\u2014", L"\u2014", L"\u2014", L"0"
};
static constexpr int kMetricCount = 6;


// ═════════════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ═════════════════════════════════════════════════════════════════════════

App::App() {
    srand(static_cast<unsigned>(time(nullptr)));
}

App::~App() {
    shutdown();
}

// ═════════════════════════════════════════════════════════════════════════
//  Initialise — window, GDI+, dark mode, data
// ═════════════════════════════════════════════════════════════════════════

bool App::initialize(const AppConfig& config) {
    if (initialized_) return true;

    // ── GDI+ startup ─────────────────────────────────────────────────
    Gdiplus::GdiplusStartupInput gsi;
    if (Gdiplus::GdiplusStartup(&gdipToken_, &gsi, nullptr) != Gdiplus::Ok)
        return false;

    // ── Register window class ────────────────────────────────────────
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = App::wndProc;
    wc.hInstance      = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint everything ourselves
    wc.lpszClassName = L"ObsidianTerminal";
    wc.hIcon         = LoadIconA(nullptr, (LPCSTR)IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);

    // ── Calculate window rect for desired client size ────────────────
    RECT rc = {0, 0, static_cast<LONG>(config.width), static_cast<LONG>(config.height)};
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rc, style, FALSE);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    hwnd_ = CreateWindowExW(
        0, L"ObsidianTerminal", config.title.c_str(), style,
        (screenW - winW) / 2, (screenH - winH) / 2, winW, winH,
        nullptr, nullptr, wc.hInstance, this
    );
    if (!hwnd_) return false;

    // (Dark title bar requires dwmapi.h and MSVC or newer MinGW)

    // ── Store client dimensions ─────────────────────────────────────
    RECT client;
    GetClientRect(hwnd_, &client);
    clientW_ = client.right;
    clientH_ = client.bottom;

    // ── Fonts, data, layout ──────────────────────────────────────────
    initFonts();
    initData();
    recalcLayout();

    // ── 1-second timer for the clock ────────────────────────────────
    SetTimer(hwnd_, 1, 1000, nullptr);

    // ── Show ─────────────────────────────────────────────────────────
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Enable mouse tracking for WM_MOUSELEAVE
    TRACKMOUSEEVENT tme{};
    tme.cbSize    = sizeof(tme);
    tme.dwFlags   = TME_LEAVE;
    tme.hwndTrack = hwnd_;
    TrackMouseEvent(&tme);

    initialized_ = true;
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
//  Run — standard Win32 message pump
// ═════════════════════════════════════════════════════════════════════════

int App::run() {
    if (!initialized_) return 1;
    running_ = true;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    running_ = false;
    return static_cast<int>(msg.wParam);
}

void App::shutdown() {
    if (!initialized_) return;
    destroyFonts();
    if (gdipToken_) {
        Gdiplus::GdiplusShutdown(gdipToken_);
        gdipToken_ = 0;
    }
    initialized_ = false;
}

// ═════════════════════════════════════════════════════════════════════════
//  Window procedure — static thunk → instance dispatch
// ═════════════════════════════════════════════════════════════════════════

LRESULT CALLBACK App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMsg(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::handleMsg(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT:
        onPaint();
        return 0;

    case WM_ERASEBKGND:
        return TRUE; // we handle all painting

    case WM_SIZE:
        onResize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_MOUSEMOVE: {
        onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        // Re-register tracking for WM_MOUSELEAVE
        TRACKMOUSEEVENT tme{};
        tme.cbSize    = sizeof(tme);
        tme.dwFlags   = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        // Clear all hovers
        for (auto& c : cards_) c.hovered = false;
        for (auto& n : nav_)   n.hovered = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        onLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_KEYDOWN:
        onKeyDown(wp);
        return 0;

    case WM_TIMER:
        if (wp == 1) onTimer();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 900;
        mmi->ptMinTrackSize.y = 600;
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd_, 1);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  Event handlers
// ═════════════════════════════════════════════════════════════════════════

void App::onPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);

    // Double-buffer: draw to off-screen bitmap, blit once
    HDC memDC        = CreateCompatibleDC(hdc);
    HBITMAP memBmp   = CreateCompatibleBitmap(hdc, clientW_, clientH_);
    HBITMAP oldBmp   = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        render(g);
    }

    BitBlt(hdc, 0, 0, clientW_, clientH_, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd_, &ps);
}

void App::onResize(int w, int h) {
    clientW_ = w;
    clientH_ = h;
    recalcLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::onMouseMove(int x, int y) {
    bool changed = false;
    POINT pt = {x, y};

    for (auto& c : cards_) {
        bool hit = PtInRect(&c.bounds, pt) != 0;
        if (hit != c.hovered) { c.hovered = hit; changed = true; }
    }
    for (auto& n : nav_) {
        bool hit = PtInRect(&n.bounds, pt) != 0;
        if (hit != n.hovered) { n.hovered = hit; changed = true; }
    }

    if (changed) {
        // Set hand cursor when hovering interactive elements
        bool anyHover = false;
        for (auto& c : cards_) if (c.hovered) anyHover = true;
        for (auto& n : nav_)   if (n.hovered) anyHover = true;
        SetCursor(LoadCursorA(nullptr, (LPCSTR)(anyHover ? IDC_HAND : IDC_ARROW)));
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::onLButtonDown(int x, int y) {
    POINT pt = {x, y};

    // Nav rail click
    for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
        if (PtInRect(&nav_[i].bounds, pt)) {
            for (auto& n : nav_) n.active = false;
            nav_[i].active = true;
            activeNav_ = i;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // Module card click → select corresponding nav
    for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
        if (PtInRect(&cards_[i].bounds, pt)) {
            int navIdx = i + 1; // offset by 1 (nav[0] = DASH)
            if (navIdx < static_cast<int>(nav_.size())) {
                for (auto& n : nav_) n.active = false;
                nav_[navIdx].active = true;
                activeNav_ = navIdx;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }
}

void App::onKeyDown(WPARAM vk) {
    // F-key module switching
    int navIdx = -1;
    switch (vk) {
        case VK_F3: navIdx = 6; break; // NEWS
        case VK_F4: navIdx = 1; break; // PORTFOLIO
        case VK_F5: navIdx = 2; break; // MARKETS
        case VK_F6: navIdx = 5; break; // RISK
        case VK_F7: navIdx = 3; break; // ANALYTICS
        case VK_ESCAPE: navIdx = 0; break; // DASHBOARD
        default: return;
    }
    if (navIdx >= 0 && navIdx < static_cast<int>(nav_.size())) {
        for (auto& n : nav_) n.active = false;
        nav_[navIdx].active = true;
        activeNav_ = navIdx;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::onTimer() {
    // Repaint status bar & command bar clock region
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ═════════════════════════════════════════════════════════════════════════
//  Layout — compute all rectangles from current client size
// ═════════════════════════════════════════════════════════════════════════

void App::recalcLayout() {
    int contentX = kNavRailW;
    int contentY = kCmdBarH + kMetricsH;
    int contentW = clientW_ - kNavRailW;
    int contentH = clientH_ - kCmdBarH - kMetricsH - kStatusBarH;

    int cols = 3;
    int rows = 3;
    int cardW = (contentW - kGridPad * 2 - kCardGap * (cols - 1)) / cols;
    int cardH = (contentH - kGridPad * 2 - kCardGap * (rows - 1)) / rows;

    // Clamp card height for very tall windows
    cardH = (std::min)(cardH, 200);

    for (int i = 0; i < 9 && i < static_cast<int>(cards_.size()); ++i) {
        int col = i % cols;
        int row = i / cols;
        cards_[i].bounds.left   = contentX + kGridPad + col * (cardW + kCardGap);
        cards_[i].bounds.top    = contentY + kGridPad + row * (cardH + kCardGap);
        cards_[i].bounds.right  = cards_[i].bounds.left + cardW;
        cards_[i].bounds.bottom = cards_[i].bounds.top  + cardH;
    }

    // Nav rail entries
    int navItemH = 44;
    int navStartY = kCmdBarH + 4;
    for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
        nav_[i].bounds = {
            0, navStartY + i * navItemH,
            kNavRailW, navStartY + (i + 1) * navItemH
        };
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  Rendering — master draw, delegates to component painters
// ═════════════════════════════════════════════════════════════════════════

void App::render(Gdiplus::Graphics& g) {
    // Full background clear
    Gdiplus::SolidBrush bgBrush(cBgPrimary);
    g.FillRectangle(&bgBrush, 0, 0, clientW_, clientH_);

    drawCommandBar(g);
    drawNavRail(g);
    drawMetricsStrip(g);
    drawModuleGrid(g);
    drawStatusBar(g);
}

// ── Command Bar ──────────────────────────────────────────────────────────

void App::drawCommandBar(Gdiplus::Graphics& g) {
    float w = static_cast<float>(clientW_);
    float h = static_cast<float>(kCmdBarH);

    // Background
    Gdiplus::SolidBrush bg(cBgVoid);
    g.FillRectangle(&bg, 0.0f, 0.0f, w, h);

    // Bottom border
    Gdiplus::Pen border(cBorder1);
    g.DrawLine(&border, 0.0f, h - 1, w, h - 1);

    // Subtle amber accent glow on bottom edge
    Gdiplus::Pen glow(cAmberGlow, 1.0f);
    g.DrawLine(&glow, w * 0.15f, h, w * 0.85f, h);

    // ── Brand: diamond ◆ + OBSIDIAN ─────────────────────────────────
    Gdiplus::SolidBrush amberBr(cAmber);
    Gdiplus::SolidBrush brightBr(cTextBright);
    Gdiplus::SolidBrush dimBr(cTextDim);
    Gdiplus::SolidBrush ghostBr(cTextGhost);

    g.DrawString(L"\u25C6", -1, fontBrand_, Gdiplus::PointF(12, 10), &amberBr);
    g.DrawString(L"OBSIDIAN", -1, fontBrand_, Gdiplus::PointF(36, 6), &brightBr);
    g.DrawString(L"TERMINAL  v0.1.0", -1, fontSmall_, Gdiplus::PointF(36, 28), &dimBr);

    // ── Function keys ───────────────────────────────────────────────
    struct FKey { const wchar_t* label; Gdiplus::Color bg; };
    FKey fkeys[] = {
        {L"F1 HELP", cFkeyGreen}, {L"F2 SRCH", cFkeyGold},
        {L"F3 NEWS", cFkeyGreen}, {L"F4 PORT", cFkeyGold},
        {L"F5 MARK", cFkeyGreen}, {L"F6 RISK", cFkeyGold},
        {L"F7 ANLT", cFkeyGreen}, {L"F8 OPTS", cFkeyGold},
    };

    float fkX = 200.0f;
    float fkY = 12.0f;
    float fkH = 22.0f;

    Gdiplus::SolidBrush fkText(Gdiplus::Color(255, 216, 216, 216));
    for (auto& fk : fkeys) {
        Gdiplus::SolidBrush fkBg(fk.bg);
        Gdiplus::RectF rc(fkX, fkY, 62, fkH);
        g.FillRectangle(&fkBg, rc);
        g.DrawString(fk.label, -1, fontMonoSm_, rc, &sfCenter_, &fkText);
        fkX += 66;
    }

    // ── Status: ● LIVE  HH:MM:SS ────────────────────────────────────
    float rightX = w - 180;

    // Green dot
    Gdiplus::SolidBrush greenBr(cGreen);
    g.FillEllipse(&greenBr, rightX, 18.0f, 7.0f, 7.0f);

    // LIVE label
    Gdiplus::SolidBrush greenText(cGreen);
    g.DrawString(L"LIVE", -1, fontMonoSm_, Gdiplus::PointF(rightX + 12, 16), &greenText);

    // Clock
    std::wstring t = currentTime();
    g.DrawString(t.c_str(), -1, fontMono_, Gdiplus::PointF(rightX + 60, 14), &brightBr);
}

// ── Metrics Strip ────────────────────────────────────────────────────────

void App::drawMetricsStrip(Gdiplus::Graphics& g) {
    float x0 = static_cast<float>(kNavRailW);
    float y0 = static_cast<float>(kCmdBarH);
    float w  = static_cast<float>(clientW_ - kNavRailW);
    float h  = static_cast<float>(kMetricsH);

    // Background
    Gdiplus::SolidBrush bg(cBgPanel);
    g.FillRectangle(&bg, x0, y0, w, h);

    // Bottom border
    Gdiplus::Pen border(cBorder1);
    g.DrawLine(&border, x0, y0 + h - 1, x0 + w, y0 + h - 1);

    // Cells
    float cellW = w / static_cast<float>(kMetricCount);
    Gdiplus::SolidBrush ghostBr(cTextGhost);
    Gdiplus::SolidBrush dimBr(cTextDim);
    Gdiplus::SolidBrush amberBr(cAmber);
    Gdiplus::Pen sepPen(cBorder1);

    for (int i = 0; i < kMetricCount; ++i) {
        float cx = x0 + i * cellW;

        // Separator
        if (i > 0)
            g.DrawLine(&sepPen, cx, y0 + 6, cx, y0 + h - 6);

        // Label (top, centered)
        Gdiplus::RectF labelRc(cx, y0 + 4, cellW, 14);
        g.DrawString(kMetricLabels[i], -1, fontMonoSm_, labelRc, &sfCenter_, &ghostBr);

        // Value (bottom, centered)
        Gdiplus::RectF valRc(cx, y0 + 20, cellW, 20);
        bool isAlerts = (i == 5);
        g.DrawString(kMetricValues[i], -1, fontMono_, valRc, &sfCenter_,
                     isAlerts ? &amberBr : &dimBr);
    }
}

// ── Navigation Rail ──────────────────────────────────────────────────────

void App::drawNavRail(Gdiplus::Graphics& g) {
    float w  = static_cast<float>(kNavRailW);
    float rh = static_cast<float>(clientH_ - kStatusBarH);

    // Background
    Gdiplus::SolidBrush bg(cBgVoid);
    g.FillRectangle(&bg, 0.0f, static_cast<float>(kCmdBarH), w, rh);

    // Right border
    Gdiplus::Pen border(cBorder1);
    g.DrawLine(&border, w - 1, static_cast<float>(kCmdBarH), w - 1, rh);

    Gdiplus::SolidBrush amberBr(cAmber);
    Gdiplus::SolidBrush brightBr(cTextBright);
    Gdiplus::SolidBrush dimBr(cTextDim);
    Gdiplus::SolidBrush ghostBr(cTextGhost);
    Gdiplus::SolidBrush hoverBg(cBgHover);
    Gdiplus::SolidBrush activeBg(cBgElevated);
    Gdiplus::Pen amberPen(cAmber, 2.0f);

    for (auto& n : nav_) {
        float nx = static_cast<float>(n.bounds.left);
        float ny = static_cast<float>(n.bounds.top);
        float nw = static_cast<float>(n.bounds.right  - n.bounds.left);
        float nh = static_cast<float>(n.bounds.bottom - n.bounds.top);

        // Background fill
        if (n.active)
            g.FillRectangle(&activeBg, nx, ny, nw, nh);
        else if (n.hovered)
            g.FillRectangle(&hoverBg, nx, ny, nw, nh);

        // Active indicator: amber left border
        if (n.active)
            g.DrawLine(&amberPen, nx + 1, ny + 4, nx + 1, ny + nh - 4);

        // Label (centered)
        Gdiplus::RectF labelRc(nx, ny, nw, nh);
        Gdiplus::SolidBrush* br = n.active  ? &amberBr
                                : n.hovered ? &dimBr
                                            : &ghostBr;
        g.DrawString(n.label.c_str(), -1, fontMonoSm_, labelRc, &sfCenter_, br);
    }
}

// ── Module Grid ──────────────────────────────────────────────────────────

void App::drawModuleGrid(Gdiplus::Graphics& g) {
    for (auto& card : cards_)
        drawCard(g, card);
}

void App::drawCard(Gdiplus::Graphics& g, const ModuleCard& card) {
    float x = static_cast<float>(card.bounds.left);
    float y = static_cast<float>(card.bounds.top);
    float w = static_cast<float>(card.bounds.right  - card.bounds.left);
    float h = static_cast<float>(card.bounds.bottom - card.bounds.top);

    if (w <= 0 || h <= 0) return;

    // ── Background ──────────────────────────────────────────────────
    Gdiplus::SolidBrush panelBg(card.hovered ? cBgHover : cBgPanel);
    g.FillRectangle(&panelBg, x, y, w, h);

    // ── Border ──────────────────────────────────────────────────────
    Gdiplus::Pen borderPen(card.hovered ? cAmberDim : cBorder1);
    g.DrawRectangle(&borderPen, x, y, w, h);

    // ── Amber accent line (top, visible on hover) ───────────────────
    if (card.hovered) {
        Gdiplus::Pen accent(cAmber, 2.0f);
        g.DrawLine(&accent, x + 1, y, x + w - 1, y);
    }

    // ── Glass edge highlight (very subtle top highlight) ────────────
    Gdiplus::Pen glassEdge(Gdiplus::Color(10, 255, 255, 255));
    g.DrawLine(&glassEdge, x + 8, y + 1, x + w - 8, y + 1);

    // ── Title ───────────────────────────────────────────────────────
    Gdiplus::SolidBrush titleBr(card.hovered ? cAmber : cTextBright);
    Gdiplus::RectF titleRc(x + 14, y + 14, w - 120, 20);
    g.DrawString(card.name.c_str(), -1, fontHeading_, titleRc, &sfNear_, &titleBr);

    // ── STANDBY badge ───────────────────────────────────────────────
    Gdiplus::SolidBrush badgeBg(Gdiplus::Color(12, 255, 255, 255));
    Gdiplus::SolidBrush badgeText(cTextGhost);
    float badgeX = x + w - 72;
    float badgeY = y + 14;
    g.FillRectangle(&badgeBg, badgeX, badgeY, 58.0f, 16.0f);
    Gdiplus::RectF badgeRc(badgeX, badgeY, 58.0f, 16.0f);
    g.DrawString(L"STANDBY", -1, fontMonoSm_, badgeRc, &sfCenter_, &badgeText);

    // ── Description ─────────────────────────────────────────────────
    Gdiplus::SolidBrush descBr(cTextDim);
    Gdiplus::RectF descRc(x + 14, y + 40, w - 28, 18);
    g.DrawString(card.desc.c_str(), -1, fontBody_, descRc, &sfNear_, &descBr);

    // ── Detail line ─────────────────────────────────────────────────
    Gdiplus::SolidBrush detailBr(cTextGhost);
    Gdiplus::RectF detailRc(x + 14, y + 60, w - 28, 36);
    g.DrawString(card.detail.c_str(), -1, fontSmall_, detailRc, &sfNear_, &detailBr);

    // ── Footer separator ────────────────────────────────────────────
    Gdiplus::Pen sepPen(Gdiplus::Color(20, 255, 255, 255));
    float footY = y + h - 30;
    g.DrawLine(&sepPen, x + 14, footY, x + w - 14, footY);

    // ── Shortcut badge ──────────────────────────────────────────────
    if (!card.shortcut.empty()) {
        Gdiplus::SolidBrush scBg(Gdiplus::Color(8, 255, 255, 255));
        g.FillRectangle(&scBg, x + 14, footY + 6, 28.0f, 16.0f);
        Gdiplus::Pen scBorder(Gdiplus::Color(20, 255, 255, 255));
        g.DrawRectangle(&scBorder, x + 14, footY + 6, 28.0f, 16.0f);
        Gdiplus::RectF scRc(x + 14, footY + 6, 28.0f, 16.0f);
        Gdiplus::SolidBrush scText(cTextGhost);
        g.DrawString(card.shortcut.c_str(), -1, fontMonoSm_, scRc, &sfCenter_, &scText);
    }

    // ── Arrow → ─────────────────────────────────────────────────────
    Gdiplus::SolidBrush arrowBr(card.hovered ? cAmber : cTextGhost);
    Gdiplus::RectF arrowRc(x + w - 28, footY + 4, 18, 18);
    g.DrawString(L"\u2192", -1, fontBody_, arrowRc, &sfCenter_, &arrowBr);
}

// ── Status Bar ───────────────────────────────────────────────────────────

void App::drawStatusBar(Gdiplus::Graphics& g) {
    float y  = static_cast<float>(clientH_ - kStatusBarH);
    float w  = static_cast<float>(clientW_);
    float h  = static_cast<float>(kStatusBarH);

    // Background
    Gdiplus::SolidBrush bg(cBgVoid);
    g.FillRectangle(&bg, 0.0f, y, w, h);

    // Top border
    Gdiplus::Pen border(cBorder1);
    g.DrawLine(&border, 0.0f, y, w, y);

    // Amber accent
    Gdiplus::Pen glow(cAmberGlow, 1.0f);
    g.DrawLine(&glow, w * 0.2f, y, w * 0.8f, y);

    float ty = y + 6;

    // ── Left: ● CONNECTED ───────────────────────────────────────────
    Gdiplus::SolidBrush greenBr(cGreen);
    g.FillEllipse(&greenBr, 12.0f, ty + 2, 5.0f, 5.0f);

    Gdiplus::SolidBrush dimBr(cTextDim);
    g.DrawString(L"CONNECTED", -1, fontMonoSm_, Gdiplus::PointF(22, ty - 1), &dimBr);

    // ── Centre: SESSION | DATE | TIME ───────────────────────────────
    Gdiplus::SolidBrush ghostBr(cTextGhost);
    std::wstring centre = L"SESSION: " + sessionId_ + L"  \u2502  "
                        + currentDate() + L"  \u2502  " + currentTime();
    Gdiplus::RectF centreRc(0, ty - 1, w, 16);
    g.DrawString(centre.c_str(), -1, fontMonoSm_, centreRc, &sfCenter_, &dimBr);

    // ── Right: version ──────────────────────────────────────────────
    Gdiplus::RectF rightRc(0, ty - 1, w - 12, 16);
    g.DrawString(L"OBSIDIAN v0.1.0", -1, fontMonoSm_, rightRc, &sfFar_, &ghostBr);
}

// ═════════════════════════════════════════════════════════════════════════
//  Helpers — data init, fonts, time formatting
// ═════════════════════════════════════════════════════════════════════════

void App::initData() {
    // Session ID
    const wchar_t chars[] = L"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    sessionId_ = L"OBS-";
    for (int i = 0; i < 4; ++i)
        sessionId_ += chars[rand() % 31];

    // Navigation entries
    nav_ = {
        NavEntry{L"DASH",  {}, true,  false},
        NavEntry{L"PORT",  {}, false, false},
        NavEntry{L"MRKT",  {}, false, false},
        NavEntry{L"ANLT",  {}, false, false},
        NavEntry{L"RSRCH", {}, false, false},
        NavEntry{L"RISK",  {}, false, false},
        NavEntry{L"NEWS",  {}, false, false},
        NavEntry{L"SCRN",  {}, false, false},
        NavEntry{L"ORDR",  {}, false, false},
    };

    // Module cards
    cards_ = {
        ModuleCard{L"PORTFOLIO",    L"Holdings & P&L Overview",
         L"Track positions, unrealized gains, asset allocation across accounts.",
         L"F4", {}, false},

        ModuleCard{L"MARKETS",      L"Global Index & Futures Monitor",
         L"Live equity indices, commodities, FX pairs, and futures across exchanges.",
         L"F5", {}, false},

        ModuleCard{L"ANALYTICS",    L"Technical & Quantitative Analysis",
         L"Charting, indicators, backtesting engines, and statistical models.",
         L"F7", {}, false},

        ModuleCard{L"RESEARCH",     L"Fundamental Data & Reports",
         L"Earnings, balance sheets, filings, analyst estimates, and sector comps.",
         L"", {}, false},

        ModuleCard{L"RISK MONITOR", L"Exposure & Risk Metrics",
         L"VaR, Greeks, concentration limits, stress scenarios, and drawdown analysis.",
         L"F6", {}, false},

        ModuleCard{L"NEWS & EVENTS",L"Real-Time News Feed",
         L"Wire services, economic calendar, corporate actions, and event alerts.",
         L"F3", {}, false},

        ModuleCard{L"SCREENER",     L"Stock & Asset Screening",
         L"Multi-factor screening with custom filters, saved scans, and ranked output.",
         L"", {}, false},

        ModuleCard{L"ORDERS",       L"Order Management System",
         L"Route, execute, and track orders across brokers and dark pools.",
         L"", {}, false},

        ModuleCard{L"WATCHLIST",    L"Custom Watchlists",
         L"Create, share, and monitor curated symbol lists with real-time quotes.",
         L"", {}, false},
    };
}

void App::initFonts() {
    fontBrand_   = new Gdiplus::Font(L"Segoe UI",    13.0f, Gdiplus::FontStyleBold,   Gdiplus::UnitPoint);
    fontBrandSub_= new Gdiplus::Font(L"Segoe UI",     8.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    fontHeading_ = new Gdiplus::Font(L"Segoe UI",    10.0f, Gdiplus::FontStyleBold,   Gdiplus::UnitPoint);
    fontBody_    = new Gdiplus::Font(L"Segoe UI",     9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    fontSmall_   = new Gdiplus::Font(L"Segoe UI",     7.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    fontMono_    = new Gdiplus::Font(L"Consolas",     10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    fontMonoSm_  = new Gdiplus::Font(L"Consolas",      8.0f, Gdiplus::FontStyleBold,   Gdiplus::UnitPoint);

    sfNear_.SetAlignment(Gdiplus::StringAlignmentNear);
    sfNear_.SetLineAlignment(Gdiplus::StringAlignmentNear);
    sfNear_.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    sfNear_.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    sfCenter_.SetAlignment(Gdiplus::StringAlignmentCenter);
    sfCenter_.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    sfFar_.SetAlignment(Gdiplus::StringAlignmentFar);
    sfFar_.SetLineAlignment(Gdiplus::StringAlignmentNear);
}

void App::destroyFonts() {
    delete fontBrand_;    fontBrand_    = nullptr;
    delete fontBrandSub_; fontBrandSub_ = nullptr;
    delete fontHeading_;  fontHeading_  = nullptr;
    delete fontBody_;     fontBody_     = nullptr;
    delete fontSmall_;    fontSmall_    = nullptr;
    delete fontMono_;     fontMono_     = nullptr;
    delete fontMonoSm_;   fontMonoSm_   = nullptr;
}

std::wstring App::currentTime() const {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[16];
    _snwprintf(buf, 16, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring App::currentDate() const {
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const wchar_t* months[] = {
        L"JAN",L"FEB",L"MAR",L"APR",L"MAY",L"JUN",
        L"JUL",L"AUG",L"SEP",L"OCT",L"NOV",L"DEC"
    };
    wchar_t buf[32];
    _snwprintf(buf, 32, L"%02d %s %d", st.wDay, months[st.wMonth - 1], st.wYear);
    return buf;
}

} // namespace desktop
} // namespace obsidian
