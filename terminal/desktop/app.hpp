#pragma once
// =========================================================================
//  Obsidian Terminal — Native Win32 + GDI+ Desktop Application
//  No frameworks. No scripting. Pure C++.
// =========================================================================

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdint>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

namespace obsidian {
namespace desktop {

// ── Data ─────────────────────────────────────────────────────────────────

struct AppConfig {
    std::wstring title{L"Obsidian Terminal"};
    uint32_t width{1440};
    uint32_t height{900};
};

struct ModuleCard {
    std::wstring name;
    std::wstring desc;
    std::wstring detail;
    std::wstring shortcut;
    RECT         bounds;
    bool         hovered;

    ModuleCard(const wchar_t* n, const wchar_t* d, const wchar_t* dt, const wchar_t* s, RECT b, bool h)
        : name(n), desc(d), detail(dt), shortcut(s), bounds(b), hovered(h) {}
};

struct NavEntry {
    std::wstring label;
    RECT         bounds;
    bool         active;
    bool         hovered;

    NavEntry(const wchar_t* l, RECT b, bool a, bool h)
        : label(l), bounds(b), active(a), hovered(h) {}
};

// ── Application ──────────────────────────────────────────────────────────

class App {
public:
    App();
    ~App();

    bool initialize(const AppConfig& config);
    int  run();
    void shutdown();

private:
    // Win32
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMsg(UINT msg, WPARAM wp, LPARAM lp);

    // Events
    void onPaint();
    void onResize(int w, int h);
    void onMouseMove(int x, int y);
    void onLButtonDown(int x, int y);
    void onKeyDown(WPARAM vk);
    void onTimer();

    // Layout
    void recalcLayout();

    // Rendering (all GDI+ draw calls)
    void render(Gdiplus::Graphics& g);
    void drawCommandBar(Gdiplus::Graphics& g);
    void drawMetricsStrip(Gdiplus::Graphics& g);
    void drawNavRail(Gdiplus::Graphics& g);
    void drawModuleGrid(Gdiplus::Graphics& g);
    void drawCard(Gdiplus::Graphics& g, const ModuleCard& card);
    void drawStatusBar(Gdiplus::Graphics& g);

    // Helpers
    void initData();
    void initFonts();
    void destroyFonts();
    std::wstring currentTime() const;
    std::wstring currentDate() const;

    // ── State ────────────────────────────────────────────────────────────
    HWND         hwnd_{};
    bool         initialized_{false};
    bool         running_{false};
    int          clientW_{1440};
    int          clientH_{900};
    int          activeNav_{0};

    std::vector<ModuleCard> cards_;
    std::vector<NavEntry>   nav_;
    std::wstring            sessionId_;

    // ── GDI+ resources ───────────────────────────────────────────────────
    ULONG_PTR               gdipToken_{};
    Gdiplus::Font*           fontBrand_{};
    Gdiplus::Font*           fontBrandSub_{};
    Gdiplus::Font*           fontHeading_{};
    Gdiplus::Font*           fontBody_{};
    Gdiplus::Font*           fontSmall_{};
    Gdiplus::Font*           fontMono_{};
    Gdiplus::Font*           fontMonoSm_{};
    Gdiplus::StringFormat    sfNear_;
    Gdiplus::StringFormat    sfCenter_;
    Gdiplus::StringFormat    sfFar_;
};

} // namespace desktop
} // namespace obsidian
