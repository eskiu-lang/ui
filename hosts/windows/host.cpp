// The Windows host: Win32 windows and common controls laid out by Yoga, as the macOS host does
// with AppKit. Components and state stay in Eskiu; the host applies commits and reports
// events (docs/hosts.md). Each element is a child window; containers, styled buttons, the
// switch, the spinner and images are drawn by the host with GDI+, text is a STATIC, a field
// an EDIT, a slider a trackbar. Sizes are points (DIPs): pixels are points times the
// window's DPI over 96, the window being per-monitor DPI aware.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <oleacc.h>
#include <psapi.h>
#include <winhttp.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}
#include <gdiplus.h>
#include <yoga/Yoga.h>
#include "../commit.h"
#include "../grid.h"
#include "ui_native.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../core.h"
#ifdef UI_RELOAD
static void ui_core_error(const char *message) { fprintf(stderr, "ui: %s\n", message); }
#endif

// Messages the host posts to its window.
static const UINT WM_UI_TICK = WM_APP + 1;
static const UINT WM_UI_RUN = WM_APP + 2;
// Timer ids: the core's timers are kTimerBase + token.
static const UINT_PTR kFramesTimer = 1, kAnimTimer = 2, kHoverTimer = 3, kLongPressTimer = 4, kReloadTimer = 5;
static const UINT_PTR kTimerBase = 0x10000;
// Menu commands.
enum { kCmdClose = 100, kCmdUndo, kCmdCut, kCmdCopy, kCmdPaste, kCmdSelectAll, kCmdBack, kCmdAbout };

static HWND mainWindow, rootWindow;
static YGNodeRef rootYoga;
static int launchExample;
static bool tickScheduled;
static std::vector<double> frameTimes;

static double scale = 1;
static bool darkMode;
static std::wstring appName = L"app";
static std::wstring exeDir;
static IAccPropServices *accProps;
static void relayout();

static double nowMs() {
    static LARGE_INTEGER frequency = {};
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

static std::wstring wide(const char *text) {
    if (!text || !text[0]) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], n);
    return out;
}

static std::string narrow(const wchar_t *text) {
    if (!text || !text[0]) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], n, nullptr, nullptr);
    return out;
}

static int px(double points) { return (int)std::lround(points * scale); }

// ---- nodes ----

// An edge offset that is not set (kUnset in src/core/state.esk).
static const int kUnset = -1000000;

struct Node {
    int id = 0, generation = 0, kind = 0;
    // The node's window, and the one its children go in: a scroll's document; a field's EDIT.
    HWND hwnd = nullptr, inner = nullptr;
    YGNodeRef yoga = nullptr;
    // Its laid out frame in points, relative to its parent's box.
    double x = 0, y = 0, w = 0, h = 0;
    bool listening = false, layoutListening = false, hoverListening = false, panListening = false, longListening = false;
    int fontSize = 0, fontWeight = 0, textColor = 0;
    std::wstring fontFile;
    int background = 0, gradientTo = 0, gradientAngle = 180, borderWidth = 0, borderColor = 0, radius = 0;
    int padding = 0, shadow = 0, blur = 0;
    // Opacity in percent; the transform (scale and rotate are kept, not drawn).
    int opacity = 100, translateX = 0, translateY = 0, scalePercent = 100, rotateDegrees = 0;
    int transitionMs = 0, easing = 0;
    // What shows now while a transition runs: the opacity, the translation, the background.
    double shownOpacity = 100, shownX = 0, shownY = 0, slideX = 0;
    int shownBackground = 0;
    bool on = false, spinning = false, cover = false, pixelated = false, styled = false;
    int sliderValue = 0, sliderMin = 0, sliderMax = 100;
    std::wstring text, label, placeholder;
    // A scroll: its offset and viewport in points, the document's height.
    double scrollY = 0, documentHeight = 0, reportedViewport = -1;
    // A screen: whether it was ever on top, and whether it is active.
    bool wasActive = false, active = true, leaving = false;
    // An image.
    std::unique_ptr<Gdiplus::Bitmap> image;
    std::wstring imageSource;
    // A native view.
    std::string nativeType;
    HWND nativeView = nullptr;
    struct UiViewEmitter *emitter = nullptr;
    int reportedX = INT32_MIN, reportedY = 0, reportedW = 0, reportedH = 0, reportedWX = 0, reportedWY = 0;
    // Where its window is, in pixels, so an unchanged frame moves nothing.
    RECT placed = {INT32_MIN, 0, 0, 0};
    bool pressed = false;
    bool settingText = false;
};

static std::unordered_map<int, Node *> nodes;
static std::unordered_map<HWND, Node *> windowNodes;
static std::vector<Node *> modals;

static Node *nodeOf(HWND hwnd) {
    auto found = windowNodes.find(hwnd);
    return found == windowNodes.end() ? nullptr : found->second;
}

// The node whose window `hwnd` is or is inside (a field's EDIT, a scroll's document).
static Node *owningNode(HWND hwnd) {
    for (HWND at = hwnd; at && at != mainWindow; at = GetParent(at)) {
        if (Node *node = nodeOf(at)) return node;
    }
    return nullptr;
}

static Node *parentNode(Node *node) {
    YGNodeRef parent = node->yoga ? YGNodeGetParent(node->yoga) : nullptr;
    return parent ? (Node *)YGNodeGetContext(parent) : nullptr;
}

// ---- colors, fonts ----

// An RGBA color from the core (0: the platform's default).
static Gdiplus::Color colorOf(int value, double alpha = 1) {
    uint32_t v = (uint32_t)value;
    return Gdiplus::Color((BYTE)std::lround((v & 255) * alpha), (BYTE)(v >> 24), (BYTE)((v >> 16) & 255), (BYTE)((v >> 8) & 255));
}
static COLORREF colorRef(int value) {
    uint32_t v = (uint32_t)value;
    return RGB(v >> 24, (v >> 16) & 255, (v >> 8) & 255);
}
static int rgba(int r, int g, int b, int a = 255) { return (int)(((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a); }

// The window's background and text, light or dark.
static int windowBackground() { return darkMode ? rgba(32, 32, 32) : rgba(255, 255, 255); }
static int defaultText() { return darkMode ? rgba(255, 255, 255) : rgba(0, 0, 0); }
static int fieldBackground() { return darkMode ? rgba(45, 45, 45) : rgba(255, 255, 255); }
static int fieldBorder() { return darkMode ? rgba(110, 110, 110) : rgba(138, 138, 138); }
static int accent() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)))
        return rgba((color >> 16) & 255, (color >> 8) & 255, color & 255);
    return rgba(0, 103, 192);
}

// `top` over `under` (both RGBA), opaque.
static int blend(int top, int under) {
    uint32_t t = (uint32_t)top, u = (uint32_t)under;
    double a = (t & 255) / 255.0;
    auto mix = [&](int shift) { return (int)std::lround(((t >> shift) & 255) * a + ((u >> shift) & 255) * (1 - a)); };
    return rgba(mix(24), mix(16), mix(8));
}

// What shows behind a node: the nearest opaque background among its ancestors, with the
// translucent ones over it.
static int backdropOf(Node *node) {
    std::vector<int> layers;
    for (Node *up = node ? parentNode(node) : nullptr; up; up = parentNode(up)) {
        int bg = up->shownBackground;
        if (!bg) continue;
        layers.push_back(bg);
        if ((bg & 255) == 255) break;
    }
    int color = windowBackground();
    if (!layers.empty() && (layers.back() & 255) == 255) { color = layers.back(); layers.pop_back(); }
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) color = blend(*it, color);
    return color;
}
static int ownBackdrop(Node *node) {
    int under = backdropOf(node);
    return node->shownBackground ? blend(node->shownBackground, under) : under;
}

static std::map<COLORREF, HBRUSH> brushes;
static HBRUSH brushOf(int color) {
    COLORREF c = colorRef(color);
    auto found = brushes.find(c);
    if (found != brushes.end()) return found->second;
    return brushes[c] = CreateSolidBrush(c);
}

// A font file the app ships, added for this process; its family name, or "" when it does not load.
static std::map<std::wstring, std::wstring> fontFamilies;
static std::wstring familyOf(const std::wstring &file) {
    auto found = fontFamilies.find(file);
    if (found != fontFamilies.end()) return found->second;
    std::wstring path = PathIsRelativeW(file.c_str()) ? exeDir + L"\\assets\\" + file : file;
    std::wstring family;
    Gdiplus::PrivateFontCollection collection;
    if (collection.AddFontFile(path.c_str()) == Gdiplus::Ok && collection.GetFamilyCount() > 0) {
        Gdiplus::FontFamily one;
        int found_count = 0;
        collection.GetFamilies(1, &one, &found_count);
        WCHAR name[LF_FACESIZE] = {};
        if (found_count == 1 && one.GetFamilyName(name) == Gdiplus::Ok) family = name;
        if (!family.empty()) AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    }
    if (family.empty()) fprintf(stderr, "ui: font %s did not load\n", narrow(file.c_str()).c_str());
    return fontFamilies[file] = family;
}

// The default text size, as the other hosts'.
static const int kTextSize = 16;

static std::map<std::tuple<int, int, std::wstring, int>, HFONT> fonts;
static HFONT fontFor(int size, int weight, const std::wstring &family) {
    int height = -px(size);
    auto key = std::make_tuple(height, weight, family, 0);
    auto found = fonts.find(key);
    if (found != fonts.end()) return found->second;
    LOGFONTW lf = {};
    lf.lfHeight = height;
    lf.lfWeight = weight > 0 ? weight : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, family.empty() ? L"Segoe UI" : family.c_str(), LF_FACESIZE - 1);
    return fonts[key] = CreateFontIndirectW(&lf);
}
static HFONT fontOf(Node *node) {
    return fontFor(node->fontSize > 0 ? node->fontSize : kTextSize, node->fontWeight,
                   node->fontFile.empty() ? std::wstring() : familyOf(node->fontFile));
}
// The system's control font (a plain button's), at the window's DPI.
static HFONT controlFont() {
    // At 96 DPI the message font's height is in points (Segoe UI 9 pt is 12).
    NONCLIENTMETRICSW metrics = {sizeof metrics};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, 96);
    int size = (int)std::abs(metrics.lfMessageFont.lfHeight);
    return fontFor(size > 0 ? size : 12, metrics.lfMessageFont.lfWeight, metrics.lfMessageFont.lfFaceName);
}

// ---- accessibility ----

// A role and a name through MSAA's dynamic annotation, which UI Automation reads too.
static void accessible(HWND hwnd, int role, const std::wstring *name) {
    if (!accProps || !hwnd) return;
    if (role) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = role;
        accProps->SetHwndProp(hwnd, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_ROLE, v);
    }
    if (name) {
        if (name->empty()) {
            MSAAPROPID ids[] = {PROPID_ACC_NAME};
            accProps->ClearHwndProps(hwnd, OBJID_CLIENT, CHILDID_SELF, ids, 1);
        } else accProps->SetHwndPropStr(hwnd, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_NAME, name->c_str());
    }
}
static void accessibleState(HWND hwnd, int state) {
    if (!accProps || !hwnd) return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = state;
    accProps->SetHwndProp(hwnd, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_STATE, v);
}

// ---- painting ----

static Gdiplus::GraphicsPath *roundedPath(Gdiplus::RectF r, float radius) {
    auto *path = new Gdiplus::GraphicsPath();
    radius = std::min(radius, std::min(r.Width, r.Height) / 2);
    if (radius <= 0.5f) { path->AddRectangle(r); return path; }
    float d = radius * 2;
    path->AddArc(r.X, r.Y, d, d, 180, 90);
    path->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path->CloseFigure();
    return path;
}

// A node's box: what is behind it, then its background (or gradient) and border in its
// rounded rectangle. `dim` lightens it while a pressable is held.
static void paintBox(Node *node, HDC hdc, RECT rect, int fill, double dim = 1) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::SolidBrush under(colorOf(backdropOf(node)));
    Gdiplus::RectF r((float)rect.left, (float)rect.top, (float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
    g.FillRectangle(&under, r);
    float radius = (float)px(node->radius);
    std::unique_ptr<Gdiplus::GraphicsPath> path(roundedPath(r, radius));
    if (node->gradientTo) {
        double a = node->gradientAngle * 3.14159265358979 / 180.0;
        float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
        // CSS's angle: 180 is top to bottom; the line's length covers the corners.
        float half = (float)(std::fabs(r.Width * std::sin(a)) + std::fabs(r.Height * std::cos(a))) / 2;
        Gdiplus::PointF from(cx - half * (float)std::sin(a), cy + half * (float)std::cos(a));
        Gdiplus::PointF to(cx + half * (float)std::sin(a), cy - half * (float)std::cos(a));
        Gdiplus::LinearGradientBrush brush(from, to, colorOf(node->shownBackground, dim), colorOf(node->gradientTo, dim));
        g.FillPath(&brush, path.get());
    } else if (fill) {
        Gdiplus::SolidBrush brush(colorOf(fill, dim));
        g.FillPath(&brush, path.get());
    }
    if (node->borderWidth > 0 && node->borderColor) {
        float bw = (float)px(node->borderWidth);
        Gdiplus::RectF inside(r.X + bw / 2, r.Y + bw / 2, r.Width - bw, r.Height - bw);
        std::unique_ptr<Gdiplus::GraphicsPath> line(roundedPath(inside, std::max(0.0f, radius - bw / 2)));
        Gdiplus::Pen pen(colorOf(node->borderColor, dim), bw);
        g.DrawPath(&pen, line.get());
    }
}

// The windows before this one in a modal's backdrop: the app's root and the modals under it.
static void paintBackdrop(HWND modal, HDC hdc, RECT rect) {
    HDC memory = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
    HGDIOBJ old = SelectObject(memory, bitmap);
    FillRect(memory, &rect, brushOf(windowBackground()));
    PrintWindow(rootWindow, memory, PW_CLIENTONLY);
    for (Node *below : modals) {
        if (below->hwnd == modal) break;
        if (!below->hwnd || !IsWindowVisible(below->hwnd)) continue;
        HDC part = CreateCompatibleDC(hdc);
        HBITMAP partBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HGDIOBJ partOld = SelectObject(part, partBitmap);
        PrintWindow(below->hwnd, part, PW_CLIENTONLY);
        BitBlt(memory, 0, 0, rect.right, rect.bottom, part, 0, 0, SRCCOPY);
        SelectObject(part, partOld);
        DeleteObject(partBitmap);
        DeleteDC(part);
    }
    BitBlt(hdc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

// Paints a container, a pressable, a modal, a screen or a field's frame.
static void paintView(HWND hwnd, HDC hdc) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    Node *node = nodeOf(hwnd);
    if (!node) {
        // The root, or a scroll's document: the node's (or the window's) background.
        Node *scroll = owningNode(hwnd);
        FillRect(hdc, &rect, brushOf(scroll ? ownBackdrop(scroll) : windowBackground()));
        return;
    }
    if (node->kind == 12) {
        // A modal: over a picture of what is under it, its color (40% black by default).
        paintBackdrop(hwnd, hdc, rect);
        if (node->shownBackground) {
            Gdiplus::Graphics g(hdc);
            Gdiplus::SolidBrush brush(colorOf(node->shownBackground));
            g.FillRectangle(&brush, (INT)rect.left, (INT)rect.top, (INT)rect.right, (INT)rect.bottom);
        }
        return;
    }
    if (node->kind == 5) {
        // A field's frame: the app's box, or the system's look (a border, an accent line
        // under it while it has the focus).
        if (node->styled) { paintBox(node, hdc, rect, node->shownBackground); return; }
        Gdiplus::Graphics g(hdc);
        Gdiplus::SolidBrush under(colorOf(backdropOf(node)));
        g.FillRectangle(&under, (INT)0, (INT)0, (INT)rect.right, (INT)rect.bottom);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::RectF r(0.5f, 0.5f, (float)rect.right - 1, (float)rect.bottom - 1);
        std::unique_ptr<Gdiplus::GraphicsPath> path(roundedPath(r, (float)px(4)));
        Gdiplus::SolidBrush fill(colorOf(fieldBackground()));
        g.FillPath(&fill, path.get());
        Gdiplus::Pen pen(colorOf(fieldBorder()), 1);
        g.DrawPath(&pen, path.get());
        if (GetFocus() == node->inner) {
            Gdiplus::SolidBrush line(colorOf(accent()));
            g.FillRectangle(&line, (float)px(1), (float)rect.bottom - px(2), (float)rect.right - px(2), (float)px(2));
        }
        return;
    }
    paintBox(node, hdc, rect, node->shownBackground, node->pressed ? 0.7 : 1);
}

// A styled button (owner drawn): its box and its title in its font and color.
static void paintButton(Node *node, const DRAWITEMSTRUCT *item) {
    RECT rect = item->rcItem;
    bool held = (item->itemState & ODS_SELECTED) != 0;
    HDC hdc = item->hDC;
    if (node->kind == 9) {
        // A switch: a pill with a knob, the accent color when on.
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::SolidBrush under(colorOf(backdropOf(node)));
        g.FillRectangle(&under, (INT)rect.left, (INT)rect.top, (INT)(rect.right - rect.left), (INT)(rect.bottom - rect.top));
        float w = (float)px(40), h = (float)px(20);
        float x = (float)rect.left, y = (float)rect.top + ((float)(rect.bottom - rect.top) - h) / 2;
        Gdiplus::RectF pill(x + 0.5f, y + 0.5f, w - 1, h - 1);
        std::unique_ptr<Gdiplus::GraphicsPath> path(roundedPath(pill, h / 2));
        float knob = h - (float)px(node->on ? 8 : 10);
        float kx = node->on ? x + w - h / 2 - knob / 2 : x + h / 2 - knob / 2;
        float ky = y + (h - knob) / 2;
        if (node->on) {
            Gdiplus::SolidBrush fill(colorOf(accent(), held ? 0.8 : 1));
            g.FillPath(&fill, path.get());
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&dot, kx, ky, knob, knob);
        } else {
            Gdiplus::Pen pen(colorOf(darkMode ? rgba(200, 200, 200) : rgba(96, 96, 96)), (float)std::max(1, px(1)));
            g.DrawPath(&pen, path.get());
            Gdiplus::SolidBrush dot(colorOf(darkMode ? rgba(200, 200, 200) : rgba(96, 96, 96)));
            g.FillEllipse(&dot, kx, ky, knob, knob);
        }
        return;
    }
    paintBox(node, hdc, rect, node->shownBackground, held ? 0.7 : 1);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colorRef(node->textColor ? node->textColor : defaultText()));
    HGDIOBJ old = SelectObject(hdc, fontOf(node));
    RECT inside = rect;
    DrawTextW(hdc, node->text.c_str(), (int)node->text.size(), &inside, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
    if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, -px(2), -px(2));
        DrawFocusRect(hdc, &focus);
    }
}

// An activity indicator: an arc turning once a second, on the host's clock.
static double hostNow();
static void paintSpinner(Node *node, HDC hdc, RECT rect) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush under(colorOf(backdropOf(node)));
    g.FillRectangle(&under, (INT)0, (INT)0, (INT)rect.right, (INT)rect.bottom);
    if (!node->spinning) return;
    float size = (float)std::min(rect.right, rect.bottom);
    float stroke = std::max(2.0f, size / 10);
    Gdiplus::Pen pen(colorOf(node->textColor ? node->textColor : accent()), stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    float angle = (float)std::fmod(hostNow() * 0.36, 360.0);
    g.DrawArc(&pen, (rect.right - size) / 2 + stroke / 2, (rect.bottom - size) / 2 + stroke / 2, size - stroke, size - stroke, angle, 270);
}

// An image, fitted in its box or covering it (cropped), smoothed or pixelated.
static void paintImage(Node *node, HDC hdc, RECT rect) {
    Gdiplus::Graphics g(hdc);
    Gdiplus::SolidBrush under(colorOf(ownBackdrop(node)));
    g.FillRectangle(&under, (INT)0, (INT)0, (INT)rect.right, (INT)rect.bottom);
    Gdiplus::Bitmap *image = node->image.get();
    if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) return;
    double iw = image->GetWidth(), ih = image->GetHeight();
    double sx = rect.right / iw, sy = rect.bottom / ih;
    double s = node->cover ? std::max(sx, sy) : std::min(sx, sy);
    Gdiplus::RectF to((float)((rect.right - iw * s) / 2), (float)((rect.bottom - ih * s) / 2), (float)(iw * s), (float)(ih * s));
    g.SetClip(Gdiplus::Rect(0, 0, rect.right, rect.bottom));
    g.SetInterpolationMode(node->pixelated ? Gdiplus::InterpolationModeNearestNeighbor : Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    if (node->radius > 0) {
        std::unique_ptr<Gdiplus::GraphicsPath> path(roundedPath(Gdiplus::RectF(0, 0, (float)rect.right, (float)rect.bottom), (float)px(node->radius)));
        g.SetClip(path.get());
    }
    g.DrawImage(image, to);
}

// Paints into `hdc` through a bitmap, so nothing flickers.
template <class F>
static void buffered(HWND hwnd, HDC hdc, F paint) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    if (rect.right <= 0 || rect.bottom <= 0) return;
    HDC memory = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
    HGDIOBJ old = SelectObject(memory, bitmap);
    paint(memory, rect);
    BitBlt(hdc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

// ---- measuring ----

// A memory DC for measuring, made once.
static HDC measureDC() {
    static HDC hdc;
    if (!hdc) hdc = CreateCompatibleDC(nullptr);
    return hdc;
}

static SIZE measureText(HFONT font, const std::wstring &text, int limit, bool wrap) {
    HDC hdc = measureDC();
    HGDIOBJ old = SelectObject(hdc, font);
    RECT r = {0, 0, wrap && limit > 0 ? limit : 0x3fffffff, 0};
    std::wstring shown = text.empty() ? L" " : text;
    UINT flags = DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS | (wrap ? DT_WORDBREAK : DT_SINGLELINE);
    DrawTextW(hdc, shown.c_str(), (int)shown.size(), &r, flags);
    if (text.empty()) r.right = 0;
    SelectObject(hdc, old);
    return {r.right - r.left, r.bottom - r.top};
}

static int lineHeight(HFONT font) {
    static std::map<HFONT, int> heights;
    auto found = heights.find(font);
    if (found != heights.end()) return found->second;
    HDC hdc = measureDC();
    HGDIOBJ old = SelectObject(hdc, font);
    TEXTMETRICW metrics;
    GetTextMetricsW(hdc, &metrics);
    SelectObject(hdc, old);
    return heights[font] = metrics.tmHeight;
}

// The field's inner insets in points when the app does not style it.
static const int kFieldInsetX = 10, kFieldInsetY = 6;

static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
    Node *node = (Node *)YGNodeGetContext(yoga);
    double limitW = widthMode == YGMeasureModeUndefined ? 1e9 : width;
    double limitH = heightMode == YGMeasureModeUndefined ? 1e9 : height;
    double w = 0, h = 0;
    if (node->kind == 3) {
        SIZE size = measureText(fontOf(node), node->text, limitW < 1e8 ? (int)std::floor(limitW * scale) : 0, true);
        w = size.cx / scale;
        h = size.cy / scale;
    } else if (node->kind == 4) {
        if (node->styled) {
            SIZE size = measureText(fontOf(node), node->text, 0, false);
            w = size.cx / scale;
            h = size.cy / scale;
        } else {
            // The system's button: its title in the control font, as Windows 11 pads it.
            SIZE size = measureText(controlFont(), node->text, 0, false);
            w = size.cx / scale + 24;
            h = std::max(32.0, size.cy / scale + 12);
        }
    } else if (node->kind == 5) {
        double line = lineHeight(fontOf(node)) / scale + 2;
        w = 120;
        h = node->styled ? line : line + 2 * kFieldInsetY;
    } else if (node->kind == 7) {
        if (node->image) { w = node->image->GetWidth(); h = node->image->GetHeight(); }
    } else if (node->kind == 9) {
        w = 40; h = 20;
    } else if (node->kind == 10) {
        w = 20; h = 20;
    } else if (node->kind == 15) {
        w = 160; h = 32;
    }
    return {(float)std::min(std::ceil(w), limitW), (float)std::min(std::ceil(h), limitH)};
}

// ---- the host's clock and transitions ----

// The host's time in ms: the platform's, or the driver's while it owns the clock (a dev
// build), where `clock settle` moves it past every transition.
static bool clockOwned;
static double driverNow, settleBonus;
static double hostNow() { return clockOwned ? driverNow + settleBonus : nowMs(); }

struct Animation {
    Node *node;
    int property;  // 0 opacity, 1 translate x, 2 translate y, 3 background, 4 a screen's slide
    double from, to, start, duration;
    int easing;
    int fromColor, toColor;
    std::function<void()> done;
};
static std::vector<Animation> animations;

static double eased(double t, int easing) {
    t = std::min(1.0, std::max(0.0, t));
    switch (easing) {
        case 1: return 1 - (1 - t) * (1 - t) * (1 - t);
        case 2: return t * t * t;
        case 3: return t;
        case 4: {
            // A spring (damping 14, stiffness 100, mass 1), as the other hosts' default.
            double w0 = 10, zeta = 0.7, wd = w0 * std::sqrt(1 - zeta * zeta), s = t * 1.2;
            return 1 - std::exp(-zeta * w0 * s) * (std::cos(wd * s) + zeta * w0 / wd * std::sin(wd * s));
        }
        default: return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    }
}

static int mixColor(int a, int b, double t) {
    uint32_t x = (uint32_t)a, y = (uint32_t)b;
    auto c = [&](int shift) { return (int)std::lround(((x >> shift) & 255) * (1 - t) + ((y >> shift) & 255) * t); };
    return (int)(((uint32_t)c(24) << 24) | ((uint32_t)c(16) << 16) | ((uint32_t)c(8) << 8) | (uint32_t)c(0));
}

static void applyOpacity(Node *node);
static void placeWindow(Node *node);

static void showAnimated(Animation &a, double t) {
    Node *node = a.node;
    double v = a.from + (a.to - a.from) * eased(t, a.easing);
    if (a.property == 0) { node->shownOpacity = v; applyOpacity(node); }
    else if (a.property == 1) { node->shownX = v; placeWindow(node); }
    else if (a.property == 2) { node->shownY = v; placeWindow(node); }
    else if (a.property == 4) { node->slideX = v; placeWindow(node); }
    else if (a.property == 5) {}
    else {
        node->shownBackground = t >= 1 ? a.toColor : mixColor(a.fromColor, a.toColor, eased(t, a.easing));
        if (node->hwnd) RedrawWindow(node->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

static bool spinnersShowing();
static void stepAnimations() {
    double now = hostNow();
    std::vector<std::function<void()>> finished;
    for (size_t i = 0; i < animations.size();) {
        Animation &a = animations[i];
        double t = a.duration <= 0 ? 1 : (now - a.start) / a.duration;
        showAnimated(a, t);
        if (t >= 1) {
            if (a.done) finished.push_back(a.done);
            animations.erase(animations.begin() + (long)i);
        } else i++;
    }
    for (auto &done : finished) done();
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (node->kind == 10 && node->spinning && node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
    }
    if (animations.empty() && !spinnersShowing()) KillTimer(mainWindow, kAnimTimer);
}

static void animationsRun() { SetTimer(mainWindow, kAnimTimer, 16, nullptr); }

// A property moved to `to`, through the node's transition when it has one and shows.
static void animate(Node *node, int property, double from, double to, std::function<void()> done = nullptr,
                    double duration = -1, int easing = -1) {
    for (size_t i = 0; i < animations.size(); i++) {
        if (animations[i].node == node && animations[i].property == property) { animations.erase(animations.begin() + (long)i); break; }
    }
    double ms = duration >= 0 ? duration : node->transitionMs;
    Animation a{node, property, from, to, hostNow(), ms, easing >= 0 ? easing : node->easing, 0, 0, done};
    if (ms <= 0 || !node->hwnd || !IsWindowVisible(node->hwnd)) {
        showAnimated(a, 1);
        if (done) done();
        return;
    }
    animations.push_back(a);
    animationsRun();
}
static void animateColor(Node *node, int to) {
    for (size_t i = 0; i < animations.size(); i++) {
        if (animations[i].node == node && animations[i].property == 3) { animations.erase(animations.begin() + (long)i); break; }
    }
    Animation a{node, 3, 0, 1, hostNow(), (double)node->transitionMs, node->easing, node->shownBackground, to, nullptr};
    if (node->transitionMs <= 0 || !node->hwnd || !IsWindowVisible(node->hwnd)) { showAnimated(a, 1); return; }
    animations.push_back(a);
    animationsRun();
}
static void dropAnimations(Node *node) {
    for (size_t i = 0; i < animations.size();) {
        if (animations[i].node == node) animations.erase(animations.begin() + (long)i); else i++;
    }
}

static bool spinnersShowing() {
    if (clockOwned) return false;
    for (auto &entry : nodes) if (entry.second->kind == 10 && entry.second->spinning) return true;
    return false;
}

// ---- windows ----

static const wchar_t *kViewClass = L"EskiuUiView", *kScrollClass = L"EskiuUiScroll", *kImageClass = L"EskiuUiImage",
                     *kSpinnerClass = L"EskiuUiSpinner", *kMainClass = L"EskiuUiWindow";

static LRESULT CALLBACK nodeSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);

// The window children of `node` go in.
static HWND containerOf(Node *node) {
    if (!node) return rootWindow;
    if (node->kind == 6 && node->inner) return node->inner;
    return node->hwnd;
}

static void applyOpacity(Node *node) {
    if (!node->hwnd) return;
    double alpha = node->shownOpacity / 100.0;
    if (node->pressed) alpha *= 0.7;
    LONG ex = GetWindowLongW(node->hwnd, GWL_EXSTYLE);
    if (alpha >= 0.999) {
        if (ex & WS_EX_LAYERED) SetWindowLongW(node->hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        return;
    }
    if (!(ex & WS_EX_LAYERED)) SetWindowLongW(node->hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(node->hwnd, 0, (BYTE)std::lround(std::max(0.0, alpha) * 255), LWA_ALPHA);
}

// The node's window at its laid out frame, moved by its translation and a screen's slide.
static void placeWindow(Node *node) {
    if (!node->hwnd || node->kind == 12) return;
    int x = px(node->x + node->shownX + node->slideX), y = px(node->y + node->shownY);
    int w = std::max(0, px(node->x + node->w) - px(node->x)), h = std::max(0, px(node->y + node->h) - px(node->y));
    RECT want = {x, y, w, h};
    if (EqualRect(&want, &node->placed)) return;
    node->placed = want;
    SetWindowPos(node->hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static HWND makeWindow(const wchar_t *cls, const wchar_t *text, DWORD style, HWND parent, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 0, 0, parent ? parent : rootWindow, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

static void applyTheme(Node *node) {
    if (!node->hwnd) return;
    const wchar_t *theme = darkMode ? L"DarkMode_Explorer" : nullptr;
    if (node->kind == 4 || node->kind == 6) SetWindowTheme(node->hwnd, theme, nullptr);
    if (node->kind == 5 && node->inner) SetWindowTheme(node->inner, darkMode ? L"DarkMode_CFD" : nullptr, nullptr);
}

// ---- press, hover, long press and drags ----

// The gesture under way: the pressable a click goes to, the node a drag goes to, and where it began.
static struct {
    Node *press = nullptr, *pan = nullptr, *longPress = nullptr;
    int pressId = 0, panId = 0;
    POINT start = {};
    bool panning = false, captured = false;
    double lastX = 0, lastY = 0, lastTime = 0, vx = 0, vy = 0;
} gesture;

static Node *findUp(Node *node, std::function<bool(Node *)> test) {
    for (Node *at = node; at; at = parentNode(at)) if (test(at)) return at;
    return nullptr;
}

static void setPressed(Node *node, bool pressed) {
    if (!node || node->pressed == pressed) return;
    node->pressed = pressed;
    applyOpacity(node);
    if (node->hwnd) RedrawWindow(node->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static bool alive(Node *node, int id) {
    auto found = nodes.find(id);
    return found != nodes.end() && found->second == node;
}

static void gestureDown(HWND hwnd, Node *node, POINT screen) {
    gesture.press = findUp(node, [](Node *n) { return n->kind == 8; });
    gesture.pan = findUp(node, [](Node *n) { return n->panListening; });
    gesture.longPress = findUp(node, [](Node *n) { return n->longListening; });
    gesture.pressId = gesture.press ? gesture.press->id : 0;
    gesture.panId = gesture.pan ? gesture.pan->id : 0;
    gesture.start = screen;
    gesture.panning = false;
    gesture.lastX = screen.x;
    gesture.lastY = screen.y;
    gesture.lastTime = nowMs();
    gesture.vx = gesture.vy = 0;
    SetCapture(hwnd);
    gesture.captured = true;
    setPressed(gesture.press, true);
    if (gesture.longPress) SetTimer(mainWindow, kLongPressTimer, 500, nullptr);
}

static void gestureMove(POINT screen) {
    if (!gesture.captured) return;
    double dx = (screen.x - gesture.start.x) / scale, dy = (screen.y - gesture.start.y) / scale;
    double now = nowMs(), dt = std::max(1.0, now - gesture.lastTime);
    gesture.vx = (screen.x - gesture.lastX) / scale * 1000 / dt;
    gesture.vy = (screen.y - gesture.lastY) / scale * 1000 / dt;
    gesture.lastX = screen.x; gesture.lastY = screen.y; gesture.lastTime = now;
    if (!gesture.panning && std::hypot(dx, dy) > 6) {
        KillTimer(mainWindow, kLongPressTimer);
        gesture.longPress = nullptr;
        if (gesture.pan && alive(gesture.pan, gesture.panId)) {
            gesture.panning = true;
            setPressed(gesture.press, false);
            gesture.press = nullptr;
            ui_dispatch_pan(gesture.pan->id, gesture.pan->generation, 0, 0, 0, 0, 0);
        } else if (gesture.press) {
            // A press that moved away from its pressable stops being one.
            RECT r;
            GetWindowRect(gesture.press->hwnd, &r);
            setPressed(gesture.press, PtInRect(&r, screen) != 0);
        }
    }
    if (gesture.panning && alive(gesture.pan, gesture.panId))
        ui_dispatch_pan(gesture.pan->id, gesture.pan->generation, 1, (int)std::lround(dx), (int)std::lround(dy),
                        (int)std::lround(gesture.vx), (int)std::lround(gesture.vy));
}

static void gestureUp(POINT screen, bool cancelled) {
    if (!gesture.captured) return;
    gesture.captured = false;
    KillTimer(mainWindow, kLongPressTimer);
    if (GetCapture()) ReleaseCapture();
    if (gesture.panning && alive(gesture.pan, gesture.panId)) {
        double dx = (screen.x - gesture.start.x) / scale, dy = (screen.y - gesture.start.y) / scale;
        ui_dispatch_pan(gesture.pan->id, gesture.pan->generation, cancelled ? 3 : 2, (int)std::lround(dx), (int)std::lround(dy),
                        (int)std::lround(gesture.vx), (int)std::lround(gesture.vy));
    }
    Node *press = gesture.press;
    if (press && alive(press, gesture.pressId)) {
        bool inside = press->pressed;
        setPressed(press, false);
        if (inside && !cancelled) ui_dispatch(press->id, press->generation);
    }
    gesture.press = gesture.pan = gesture.longPress = nullptr;
    gesture.panning = false;
}

// Hover: the listening nodes under the pointer, entered and left as it moves.
static std::set<int> hovered;
static void updateHover() {
    POINT at;
    GetCursorPos(&at);
    std::set<int> now;
    HWND under = WindowFromPoint(at);
    if (under && (under == mainWindow || IsChild(mainWindow, under))) {
        for (Node *node = owningNode(under); node; node = parentNode(node)) if (node->hoverListening) now.insert(node->id);
    }
    for (int id : hovered) {
        if (now.count(id)) continue;
        auto found = nodes.find(id);
        if (found != nodes.end()) ui_dispatch_hover(id, found->second->generation, 0);
    }
    for (int id : now) {
        if (hovered.count(id)) continue;
        ui_dispatch_hover(id, nodes[id]->generation, 1);
    }
    hovered = now;
    if (hovered.empty()) KillTimer(mainWindow, kHoverTimer); else SetTimer(mainWindow, kHoverTimer, 100, nullptr);
}

// ---- scrolling ----

static void reportWindows();
static double scrollMost(Node *node) { return std::max(0.0, node->documentHeight - node->h); }

static void scrollTo(Node *node, double y, bool report) {
    y = std::min(scrollMost(node), std::max(0.0, y));
    node->scrollY = y;
    if (node->inner) {
        RECT r;
        GetClientRect(node->hwnd, &r);
        SetWindowPos(node->inner, nullptr, 0, -px(y), r.right, std::max((int)r.bottom, px(node->documentHeight)),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SCROLLINFO info = {sizeof info, SIF_ALL | SIF_DISABLENOSCROLL};
    info.nMin = 0;
    info.nMax = std::max(0, px(node->documentHeight) - 1);
    info.nPage = (UINT)std::max(0, px(node->h));
    info.nPos = px(y);
    SetScrollInfo(node->hwnd, SB_VERT, &info, TRUE);
    ShowScrollBar(node->hwnd, SB_VERT, node->documentHeight > node->h + 0.5);
    if (!report) return;
    reportWindows();
    if (node->listening) {
        node->reportedViewport = node->h;
        ui_dispatch_scroll(node->id, node->generation, (int)std::lround(y), (int)std::lround(node->h));
    }
}

// ---- window procedures ----

static LRESULT notifyChild(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool *handled);

static LRESULT CALLBACK viewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    LRESULT result = notifyChild(hwnd, msg, wParam, lParam, &handled);
    if (handled) return result;
    Node *node = nodeOf(hwnd);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            buffered(hwnd, hdc, [&](HDC memory, RECT) { paintView(hwnd, memory); });
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT: paintView(hwnd, (HDC)wParam); return 0;
        case WM_NCHITTEST: {
            // A container that takes no input lets the pointer through to what is under it (a
            // pressable's label and rows, the window).
            Node *owner = node ? node : owningNode(hwnd);
            if (!owner) return HTTRANSPARENT;
            bool takes = owner->kind == 8 || owner->kind == 12 || owner->kind == 5 || owner->kind == 13 ||
                         owner->panListening || owner->longListening || owner->hoverListening;
            if (!takes && findUp(owner, [](Node *n) { return n->panListening || n->longListening; })) takes = true;
            return takes ? HTCLIENT : HTTRANSPARENT;
        }
        case WM_LBUTTONDOWN: {
            Node *owner = node ? node : owningNode(hwnd);
            if (owner && owner->kind == 5 && owner->inner) { SetFocus(owner->inner); return 0; }
            if (owner && owner->kind == 12) {
                // A click on a modal's backdrop (outside its content) asks it to close.
                if (owner->listening) ui_dispatch(owner->id, owner->generation);
                return 0;
            }
            POINT at;
            GetCursorPos(&at);
            if (owner) gestureDown(hwnd, owner, at);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT at;
            GetCursorPos(&at);
            gestureMove(at);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT at;
            GetCursorPos(&at);
            gestureUp(at, false);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (gesture.captured && (HWND)lParam != hwnd) { POINT at; GetCursorPos(&at); gestureUp(at, true); }
            return 0;
        case WM_SIZE:
            if (node && node->kind == 13 && node->nativeView) MoveWindow(node->nativeView, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            return 0;
        case WM_GETDLGCODE:
            if (node && node->kind == 8) return DLGC_BUTTON;
            break;
        case WM_KEYDOWN:
            if (node && node->kind == 8 && (wParam == VK_SPACE || wParam == VK_RETURN)) { ui_dispatch(node->id, node->generation); return 0; }
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (node && node->kind == 8) InvalidateRect(hwnd, nullptr, FALSE);
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK scrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Node *node = nodeOf(hwnd);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (node) {
                RECT r;
                GetClientRect(hwnd, &r);
                FillRect(hdc, &r, brushOf(ownBackdrop(node)));
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            if (node) { RECT r; GetClientRect(hwnd, &r); FillRect((HDC)wParam, &r, brushOf(ownBackdrop(node))); }
            return 0;
        case WM_MOUSEWHEEL: {
            if (!node) break;
            if (scrollMost(node) <= 0) break;
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            double delta = -(double)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * (lines == WHEEL_PAGESCROLL ? node->h : lines * 16.0);
            scrollTo(node, node->scrollY + delta, true);
            return 0;
        }
        case WM_VSCROLL: {
            if (!node) break;
            SCROLLINFO info = {sizeof info, SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &info);
            double y = node->scrollY, line = 40, page = node->h * 0.9;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: y -= line; break;
                case SB_LINEDOWN: y += line; break;
                case SB_PAGEUP: y -= page; break;
                case SB_PAGEDOWN: y += page; break;
                case SB_TOP: y = 0; break;
                case SB_BOTTOM: y = scrollMost(node); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: y = info.nTrackPos / scale; break;
                default: return 0;
            }
            scrollTo(node, y, true);
            return 0;
        }
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            return hit == HTNOWHERE ? HTCLIENT : hit;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK imageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Node *node = nodeOf(hwnd);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (node) buffered(hwnd, hdc, [&](HDC memory, RECT r) { paintImage(node, memory, r); });
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            if (node) { RECT r; GetClientRect(hwnd, &r); paintImage(node, (HDC)wParam, r); }
            return 0;
        case WM_NCHITTEST: return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK spinnerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Node *node = nodeOf(hwnd);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (node) buffered(hwnd, hdc, [&](HDC memory, RECT r) { paintSpinner(node, memory, r); });
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            if (node) { RECT r; GetClientRect(hwnd, &r); paintSpinner(node, (HDC)wParam, r); }
            return 0;
        case WM_NCHITTEST: return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// What the controls inside a window tell it: a button's click, a field's edit and focus, a
// slider's move, a label's colors, an owner-drawn button's paint.
static LRESULT notifyChild(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool *handled) {
    *handled = false;
    if (msg == WM_COMMAND && lParam) {
        HWND control = (HWND)lParam;
        Node *node = owningNode(control);
        if (node && node->nativeView && (control == node->nativeView || IsChild(node->nativeView, control))) {
            *handled = true;
            return SendMessageW(control, UI_REFLECT + msg, wParam, lParam);
        }
        if (!node) return 0;
        int code = HIWORD(wParam);
        if (node->kind == 4 && code == BN_CLICKED) { *handled = true; ui_dispatch(node->id, node->generation); return 0; }
        if (node->kind == 9 && code == BN_CLICKED) {
            *handled = true;
            node->on = !node->on;
            accessibleState(node->hwnd, node->on ? STATE_SYSTEM_CHECKED : 0);
            InvalidateRect(node->hwnd, nullptr, FALSE);
            ui_dispatch_toggle(node->id, node->generation, node->on ? 1 : 0);
            return 0;
        }
        if (node->kind == 5 && control == node->inner) {
            if (code == EN_CHANGE && !node->settingText) {
                int n = GetWindowTextLengthW(control);
                std::wstring text(n, L'\0');
                if (n) GetWindowTextW(control, &text[0], n + 1);
                node->text = text;
                ui_dispatch_text(node->id, node->generation, narrow(text.c_str()).c_str());
            } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                InvalidateRect(node->hwnd, nullptr, FALSE);
                ui_dispatch_focus(node->id, node->generation, code == EN_SETFOCUS ? 1 : 0);
            }
            *handled = true;
            return 0;
        }
        return 0;
    }
    if ((msg == WM_HSCROLL || msg == WM_VSCROLL) && lParam) {
        HWND control = (HWND)lParam;
        Node *node = owningNode(control);
        if (node && node->nativeView) { *handled = true; return SendMessageW(control, UI_REFLECT + msg, wParam, lParam); }
        if (node && node->kind == 15) {
            *handled = true;
            int value = (int)SendMessageW(control, TBM_GETPOS, 0, 0);
            if (value != node->sliderValue) {
                node->sliderValue = value;
                ui_dispatch_value(node->id, node->generation, value);
            }
            return 0;
        }
    }
    if (msg == WM_NOTIFY) {
        NMHDR *header = (NMHDR *)lParam;
        Node *node = owningNode(header->hwndFrom);
        if (node && node->nativeView) { *handled = true; return SendMessageW(header->hwndFrom, UI_REFLECT + msg, wParam, lParam); }
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORBTN) {
        HDC hdc = (HDC)wParam;
        Node *node = owningNode((HWND)lParam);
        if (!node) return 0;
        if (node->nativeView) {
            *handled = true;
            LRESULT own = SendMessageW((HWND)lParam, UI_REFLECT + msg, wParam, lParam);
            if (own) return own;
        }
        *handled = true;
        int text = node->textColor ? node->textColor : defaultText();
        SetTextColor(hdc, colorRef(text));
        int back = node->kind == 5 && !node->styled ? fieldBackground() : ownBackdrop(node);
        SetBkColor(hdc, colorRef(back));
        SetBkMode(hdc, OPAQUE);
        return (LRESULT)brushOf(back);
    }
    if (msg == WM_DRAWITEM) {
        DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lParam;
        Node *node = nodeOf(item->hwndItem);
        if (!node) return 0;
        *handled = true;
        paintButton(node, item);
        return TRUE;
    }
    return 0;
}

// Each node's window: what it is when it goes (its children with it), and the keys that
// press a button-like view.
static LRESULT CALLBACK nodeSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_NCDESTROY) {
        auto found = windowNodes.find(hwnd);
        if (found != windowNodes.end()) {
            Node *node = found->second;
            if (node->hwnd == hwnd) { node->hwnd = nullptr; node->inner = nullptr; node->nativeView = nullptr; }
            windowNodes.erase(found);
        }
        RemoveWindowSubclass(hwnd, nodeSubclass, id);
    } else if (msg == WM_ERASEBKGND) {
        Node *node = nodeOf(hwnd);
        if (node && node->kind == 4 && node->styled) return 1;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void track(Node *node, HWND hwnd) {
    windowNodes[hwnd] = node;
    SetWindowSubclass(hwnd, nodeSubclass, 1, 0);
}

// ---- native views ----

struct UiViewType { UiViewCreate create; UiViewSet set; };
static std::map<std::string, UiViewType> &viewTypes() {
    static std::map<std::string, UiViewType> types;
    return types;
}

struct UiViewEmitter { int id, generation; };

extern "C" void ui_register_view(const char *type, UiViewCreate create, UiViewSet set) { viewTypes()[type ? type : ""] = {create, set}; }
extern "C" HWND ui_native_window(void) { return mainWindow; }

// Work for the UI thread from any thread.
static void onMain(std::function<void()> work) {
    PostMessageW(mainWindow, WM_UI_RUN, 0, (LPARAM) new std::function<void()>(std::move(work)));
}

extern "C" void ui_native_emit(const char *channel, const char *payload) {
    std::string c = channel ? channel : "", p = payload ? payload : "";
    onMain([c, p]() { ui_dispatch_native(c.c_str(), p.c_str()); });
}

extern "C" void ui_view_emit(UiViewEmitter *emitter, const char *name, const char *payload) {
    if (!emitter) return;
    int id = emitter->id, generation = emitter->generation;
    std::string n = name ? name : "", p = payload ? payload : "";
    if (GetCurrentThreadId() == GetWindowThreadProcessId(mainWindow, nullptr)) ui_dispatch_view(id, generation, n.c_str(), p.c_str());
    else onMain([id, generation, n, p]() { ui_dispatch_view(id, generation, n.c_str(), p.c_str()); });
}

static void mountNative(Node *node, const std::string &type) {
    if (node->nativeView) DestroyWindow(node->nativeView);
    node->nativeView = nullptr;
    node->nativeType = type;
    auto found = viewTypes().find(type);
    if (found == viewTypes().end()) { fprintf(stderr, "ui: no native view registered as %s\n", type.c_str()); return; }
    if (!node->emitter) node->emitter = new UiViewEmitter{node->id, node->generation};
    HWND view = found->second.create(node->hwnd, node->emitter);
    if (!view) return;
    RECT r;
    GetClientRect(node->hwnd, &r);
    MoveWindow(view, 0, 0, r.right, r.bottom, TRUE);
    ShowWindow(view, SW_SHOWNA);
    node->nativeView = view;
}

static void ui_host_named(int id, const char *name, const char *value) {
    auto at = nodes.find(id);
    if (at == nodes.end() || !at->second->nativeView) return;
    auto found = viewTypes().find(at->second->nativeType);
    if (found != viewTypes().end() && found->second.set) found->second.set(at->second->nativeView, name, value);
}

// ---- images ----

static Gdiplus::Bitmap *decodeImage(const std::vector<uint8_t> &bytes) {
    IStream *stream = SHCreateMemStream(bytes.data(), (UINT)bytes.size());
    if (!stream) return nullptr;
    Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromStream(stream);
    stream->Release();
    if (bitmap && bitmap->GetLastStatus() != Gdiplus::Ok) { delete bitmap; return nullptr; }
    return bitmap;
}

static bool httpGet(const std::wstring &url, const std::string &method, const std::string &headers, const std::string *body,
                    int timeoutMs, int &status, std::vector<uint8_t> &out, std::string &responseHeaders, std::string &error);

static void imageLoaded(int id, Node *node, Gdiplus::Bitmap *bitmap) {
    if (!alive(node, id)) { delete bitmap; return; }
    node->image.reset(bitmap);
    YGNodeMarkDirty(node->yoga);
    if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
    relayout();
}

// A file the app ships (assets/ next to the executable), a path, or an http(s) URL fetched on
// a thread. The image changes the node's measured size, so the layout runs again.
static void loadImage(Node *node, const std::wstring &source) {
    node->image.reset();
    node->imageSource = source;
    YGNodeMarkDirty(node->yoga);
    if (source.empty()) return;
    if (source.rfind(L"http://", 0) == 0 || source.rfind(L"https://", 0) == 0) {
        int id = node->id;
        std::thread([id, node, source]() {
            int status = 0;
            std::vector<uint8_t> bytes;
            std::string headers, error;
            bool ok = httpGet(source, "GET", "", nullptr, 30000, status, bytes, headers, error) && status >= 200 && status < 300;
            std::shared_ptr<std::vector<uint8_t>> kept = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
            onMain([id, node, ok, kept]() { if (alive(node, id) && ok) imageLoaded(id, node, decodeImage(*kept)); });
        }).detach();
        return;
    }
    std::wstring path = source;
    if (path.rfind(L"file://", 0) == 0) path = path.substr(7);
    if (PathIsRelativeW(path.c_str())) path = exeDir + L"\\assets\\" + path;
    Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromFile(path.c_str());
    if (bitmap && bitmap->GetLastStatus() != Gdiplus::Ok) { delete bitmap; bitmap = nullptr; }
    node->image.reset(bitmap);
    if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
}

// ---- the network ----

static bool httpGet(const std::wstring &url, const std::string &method, const std::string &headers, const std::string *body,
                    int timeoutMs, int &status, std::vector<uint8_t> &out, std::string &responseHeaders, std::string &error) {
    URL_COMPONENTS parts = {sizeof parts};
    wchar_t host[256] = {}, path[4096] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 255;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4095;
    parts.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) { error = "invalid URL"; return false; }
    std::wstring target = path;
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(L"eskiu-ui", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = "the network is not available"; return false; }
    if (timeoutMs > 0) WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, wide(method.c_str()).c_str(), target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = false;
    if (request) {
        std::wstring extra = wide(headers.c_str());
        for (size_t at; (at = extra.find(L"\n")) != std::wstring::npos;) extra.replace(at, 1, L"\r\n"), extra.insert(at, L"");
        std::string sent = body ? *body : std::string();
        ok = WinHttpSendRequest(request, extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(), extra.empty() ? 0 : (DWORD)-1L,
                                sent.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)sent.data(), (DWORD)sent.size(), (DWORD)sent.size(), 0) &&
             WinHttpReceiveResponse(request, nullptr);
        if (ok) {
            DWORD code = 0, size = sizeof code;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            DWORD length = 0;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &length, WINHTTP_NO_HEADER_INDEX);
            if (length) {
                std::wstring raw(length / sizeof(wchar_t), L'\0');
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &raw[0], &length, WINHTTP_NO_HEADER_INDEX)) {
                    std::string all = narrow(raw.c_str());
                    // The status line goes; each header on a line of its own.
                    size_t first = all.find("\r\n");
                    all = first == std::string::npos ? std::string() : all.substr(first + 2);
                    for (size_t at; (at = all.find("\r\n")) != std::string::npos;) all.replace(at, 2, "\n");
                    while (!all.empty() && all.back() == '\n' && all.size() > 1 && all[all.size() - 2] == '\n') all.pop_back();
                    responseHeaders = all;
                }
            }
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                size_t at = out.size();
                out.resize(at + available);
                DWORD got = 0;
                if (!WinHttpReadData(request, out.data() + at, available, &got)) { ok = false; break; }
                out.resize(at + got);
            }
        }
        if (!ok) {
            wchar_t message[256] = {};
            DWORD code = GetLastError();
            FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, GetModuleHandleW(L"winhttp.dll"), code, 0, message, 255, nullptr);
            error = message[0] ? narrow(message) : "the request failed (error " + std::to_string(code) + ")";
            while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) error.pop_back();
        }
        WinHttpCloseHandle(request);
    } else error = "could not connect";
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// ---- screens ----

static const double kSlideMs = 300;

// A screen under the top is hidden (after a push's slide, which it stays under); one that
// comes on top for the first time over a screen that was shown slides in from the right (a
// push). A removed top screen slides out (a pop).
static bool screensOver(Node *node, bool shownOnly) {
    HWND parent = node->hwnd ? GetParent(node->hwnd) : nullptr;
    for (auto &entry : nodes) {
        Node *other = entry.second;
        if (other != node && other->kind == 14 && other->wasActive && other->hwnd && GetParent(other->hwnd) == parent &&
            (!shownOnly || IsWindowVisible(other->hwnd)))
            return true;
    }
    return false;
}

static std::vector<Node *> slidingIn;
static void screenActive(Node *node, int active) {
    node->active = active != 0;
    YGNodeStyleSetDisplay(node->yoga, active ? YGDisplayFlex : YGDisplayNone);
    if (active) {
        if (!node->wasActive && screensOver(node, true)) slidingIn.push_back(node);
        node->wasActive = true;
        if (node->hwnd) ShowWindow(node->hwnd, SW_SHOWNA);
    } else if (node->wasActive) {
        // Under the new top while it slides in, then hidden (property 5: only the wait).
        int id = node->id;
        animate(node, 5, 0, 0, [node, id]() {
            if (alive(node, id) && !node->active && node->hwnd) ShowWindow(node->hwnd, SW_HIDE);
        }, kSlideMs, 3);
    } else if (node->hwnd) ShowWindow(node->hwnd, SW_HIDE);
}

static void slideScreens() {
    for (Node *node : slidingIn) {
        if (!node->hwnd) continue;
        SetWindowPos(node->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        animate(node, 4, node->w, 0, nullptr, kSlideMs, 1);
    }
    slidingIn.clear();
}

// Removed nodes whose windows stay while a screen slides out (their screen's).
static std::set<HWND> leavingWindows;
static bool insideLeaving(HWND hwnd) {
    for (HWND up = hwnd ? GetParent(hwnd) : nullptr; up; up = GetParent(up)) if (leavingWindows.count(up)) return true;
    return false;
}

static bool removeScreen(Node *node) {
    if (node->kind != 14 || !node->hwnd || !IsWindowVisible(node->hwnd) || !screensOver(node, false)) return false;
    HWND hwnd = node->hwnd;
    leavingWindows.insert(hwnd);
    windowNodes.erase(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // The window slides on its own: a stand-in node carries the animation.
    Node *ghost = new Node();
    ghost->hwnd = hwnd;
    ghost->x = node->x; ghost->y = node->y; ghost->w = node->w; ghost->h = node->h;
    ghost->kind = -1;
    animate(ghost, 4, 0, ghost->w, [ghost, hwnd]() {
        leavingWindows.erase(hwnd);
        DestroyWindow(hwnd);
        dropAnimations(ghost);
        delete ghost;
    }, kSlideMs, 2);
    node->hwnd = nullptr;
    return true;
}

// ---- applying a commit ----

static void applyButtonStyle(Node *node) {
    bool styled = node->background || node->borderWidth || node->radius || node->textColor || node->fontSize || node->fontWeight ||
                  !node->fontFile.empty() || node->gradientTo;
    if (node->kind == 5) {
        node->styled = node->background || node->borderWidth || node->radius;
        if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
        YGNodeMarkDirty(node->yoga);
        return;
    }
    if (node->kind != 4 || !node->hwnd) return;
    LONG style = GetWindowLongW(node->hwnd, GWL_STYLE);
    LONG want = (style & ~0xF) | (styled ? BS_OWNERDRAW : BS_PUSHBUTTON);
    if (want != style) SetWindowLongW(node->hwnd, GWL_STYLE, want);
    node->styled = styled;
    SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)(styled ? fontOf(node) : controlFont()), TRUE);
    InvalidateRect(node->hwnd, nullptr, TRUE);
    YGNodeMarkDirty(node->yoga);
}

static void applyFont(Node *node) {
    if (node->kind == 4) { applyButtonStyle(node); return; }
    if (node->kind == 3 && node->hwnd) SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)fontOf(node), TRUE);
    if (node->kind == 5 && node->inner) SendMessageW(node->inner, WM_SETFONT, (WPARAM)fontOf(node), TRUE);
    YGNodeMarkDirty(node->yoga);
}

static void insertWindow(Node *node, HWND parent, Node *next) {
    if (!node->hwnd) return;
    if (GetParent(node->hwnd) != parent) { SetParent(node->hwnd, parent); node->placed.left = INT32_MIN; }
    // Later siblings draw over earlier ones: the top of Windows' z-order is the last child.
    HWND after = next && next->hwnd && GetParent(next->hwnd) == parent ? next->hwnd : HWND_TOP;
    SetWindowPos(node->hwnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (node->kind != 14 || node->active) {
        if (node->kind != 11) ShowWindow(node->hwnd, SW_SHOWNA);
    }
}

static void createNode(int id, int generation, int kind) {
    Node *node = new Node();
    node->id = id;
    node->generation = generation;
    node->kind = kind;
    node->yoga = YGNodeNew();
    YGNodeSetContext(node->yoga, node);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    (void)instance;
    if (kind == 3) {
        node->hwnd = makeWindow(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, nullptr);
        SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)fontOf(node), FALSE);
    } else if (kind == 4) {
        node->hwnd = makeWindow(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, nullptr);
        SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)controlFont(), FALSE);
    } else if (kind == 5) {
        node->hwnd = makeWindow(kViewClass, L"", WS_CLIPCHILDREN, nullptr, WS_EX_CONTROLPARENT);
        node->inner = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0,
                                      node->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(node->inner, WM_SETFONT, (WPARAM)fontOf(node), FALSE);
        SendMessageW(node->inner, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    } else if (kind == 6) {
        node->hwnd = makeWindow(kScrollClass, L"", WS_CLIPCHILDREN | WS_VSCROLL, nullptr, WS_EX_CONTROLPARENT);
        node->inner = CreateWindowExW(WS_EX_CONTROLPARENT, kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, node->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        ShowScrollBar(node->hwnd, SB_VERT, FALSE);
        YGNodeStyleSetFlexDirection(node->yoga, YGFlexDirectionColumn);
        YGNodeStyleSetOverflow(node->yoga, YGOverflowScroll);
        YGNodeStyleSetFlexShrink(node->yoga, 1);
        YGNodeStyleSetGap(node->yoga, YGGutterAll, 8);
    } else if (kind == 7) {
        node->hwnd = makeWindow(kImageClass, L"", 0, nullptr);
        accessible(node->hwnd, ROLE_SYSTEM_GRAPHIC, nullptr);
    } else if (kind == 9) {
        node->hwnd = makeWindow(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, nullptr);
        accessible(node->hwnd, ROLE_SYSTEM_CHECKBUTTON, nullptr);
        accessibleState(node->hwnd, 0);
    } else if (kind == 10) {
        node->hwnd = makeWindow(kSpinnerClass, L"", 0, nullptr);
        accessible(node->hwnd, ROLE_SYSTEM_PROGRESSBAR, nullptr);
    } else if (kind == 15) {
        node->hwnd = makeWindow(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, nullptr);
        SendMessageW(node->hwnd, TBM_SETRANGEMIN, FALSE, 0);
        SendMessageW(node->hwnd, TBM_SETRANGEMAX, FALSE, 100);
    } else {
        DWORD style = WS_CLIPCHILDREN | (kind == 8 ? WS_TABSTOP : 0);
        node->hwnd = makeWindow(kViewClass, L"", style, kind == 12 ? mainWindow : nullptr, WS_EX_CONTROLPARENT);
        if (kind == 8) accessible(node->hwnd, ROLE_SYSTEM_PUSHBUTTON, nullptr);
        if (kind == 12) {
            accessible(node->hwnd, ROLE_SYSTEM_DIALOG, nullptr);
            node->background = node->shownBackground = rgba(0, 0, 0, 102);
            YGNodeStyleSetFlexDirection(node->yoga, YGFlexDirectionColumn);
        } else if (kind == 11) {
            YGNodeStyleSetDisplay(node->yoga, YGDisplayNone);
        } else if (kind != 13) {
            YGNodeStyleSetFlexDirection(node->yoga, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
            YGNodeStyleSetGap(node->yoga, YGGutterAll, 8);
        }
    }
    if (node->hwnd) track(node, node->hwnd);
    applyTheme(node);
    if ((kind >= 3 && kind <= 5) || kind == 7 || kind == 9 || kind == 10 || kind == 15) YGNodeSetMeasureFunc(node->yoga, measure);
    nodes[id] = node;
}

static void freeNode(Node *node) {
    dropAnimations(node);
    hovered.erase(node->id);
    if (gesture.press == node) gesture.press = nullptr;
    if (gesture.pan == node) gesture.pan = nullptr;
    if (gesture.longPress == node) gesture.longPress = nullptr;
    for (size_t i = 0; i < slidingIn.size(); i++) if (slidingIn[i] == node) { slidingIn.erase(slidingIn.begin() + (long)i); break; }
    delete node->emitter;
    ui_grid_free(node->yoga);
    delete node;
}

static void removeNode(Node *node) {
    YGNodeRef parent = YGNodeGetParent(node->yoga);
    if (parent) YGNodeRemoveChild(parent, node->yoga);
    for (size_t i = 0; i < modals.size(); i++) if (modals[i] == node) { modals.erase(modals.begin() + (long)i); break; }
    if (node->hwnd) {
        if (removeScreen(node)) {}
        else if (insideLeaving(node->hwnd)) {
            // It goes with its screen once the slide ends.
            windowNodes.erase(node->hwnd);
            node->hwnd = nullptr;
        } else {
            HWND hwnd = node->hwnd;
            DestroyWindow(hwnd);
        }
    }
    nodes.erase(node->id);
    freeNode(node);
}

extern "C" void ui_host_apply(int op, int id, int generation, int parent, int before, int kind, const char *text) {
    if (op == 1) { createNode(id, generation, kind); return; }
    auto found = nodes.find(id);
    if (found == nodes.end()) {
        if (op == 6) return;
        fprintf(stderr, "ui host: op %d on unknown node %d\n", op, id);
        abort();
    }
    Node *node = found->second;
    // The grid (hosts/grid.h) keeps the props it reads.
    if (op == 2 && node->yoga) ui_grid_prop(node->yoga, kind, before);
    if (op == 9 && node->yoga) ui_grid_tracks(node->yoga, kind, text);
    if (op == 2) {
        // Props: the code in kind, the value in before.
        int v = before;
        switch (kind) {
            case 1: YGNodeStyleSetGap(node->yoga, YGGutterAll, (float)v); break;
            case 2: YGNodeStyleSetPadding(node->yoga, YGEdgeAll, (float)v); node->padding = v; break;
            case 3:
                if (node->kind == 5 && node->inner) {
                    if (v) SetFocus(node->inner);
                    else if (GetFocus() == node->inner) SetFocus(mainWindow);
                }
                break;
            case 4: if (v) YGNodeStyleSetWidth(node->yoga, (float)v); else YGNodeStyleSetWidthAuto(node->yoga); break;
            case 5: if (v) YGNodeStyleSetHeight(node->yoga, (float)v); else YGNodeStyleSetHeightAuto(node->yoga); break;
            case 6:
                YGNodeStyleSetFlexGrow(node->yoga, (float)v);
                if (node->kind != 6) YGNodeStyleSetFlexShrink(node->yoga, v > 0 ? 1 : 0);
                break;
            case 7: YGNodeStyleSetFlexWrap(node->yoga, v ? YGWrapWrap : YGWrapNoWrap); break;
            case 8:
                node->background = v;
                if (node->kind == 12 && !v) v = rgba(0, 0, 0, 102), node->background = v;
                animateColor(node, v);
                applyButtonStyle(node);
                break;
            case 9: node->textColor = v; applyButtonStyle(node); if (node->hwnd) InvalidateRect(node->hwnd, nullptr, TRUE); if (node->inner) InvalidateRect(node->inner, nullptr, TRUE); break;
            case 10: node->fontSize = v; applyFont(node); break;
            case 11: node->fontWeight = v; applyFont(node); break;
            case 12:
                node->borderWidth = v;
                YGNodeStyleSetBorder(node->yoga, YGEdgeAll, (float)v);
                applyButtonStyle(node);
                break;
            case 13: node->borderColor = v; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 14: node->radius = v; applyButtonStyle(node); if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 15: {
                double from = node->shownOpacity;
                node->opacity = v;
                animate(node, 0, from, v);
                break;
            }
            case 16: {
                const YGAlign aligns[] = {YGAlignStretch, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd, YGAlignStretch};
                YGNodeStyleSetAlignItems(node->yoga, aligns[std::min(4, std::max(0, v))]);
                break;
            }
            case 17: {
                const YGJustify justifies[] = {YGJustifyFlexStart, YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd,
                                               YGJustifySpaceBetween, YGJustifySpaceAround};
                YGNodeStyleSetJustifyContent(node->yoga, justifies[std::min(5, std::max(0, v))]);
                break;
            }
            case 18:
                node->on = v != 0;
                accessibleState(node->hwnd, node->on ? STATE_SYSTEM_CHECKED : 0);
                if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
                break;
            case 19:
                node->spinning = v != 0;
                if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
                if (spinnersShowing()) animationsRun();
                break;
            case 23: screenActive(node, v); break;
            case 24: YGNodeStyleSetPositionType(node->yoga, v ? YGPositionTypeAbsolute : YGPositionTypeRelative); break;
            case 25: case 26: case 27: case 28: {
                const YGEdge edges[] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
                YGNodeStyleSetPosition(node->yoga, edges[kind - 25], v == kUnset ? YGUndefined : (float)v);
                break;
            }
            case 29: YGNodeStyleSetAspectRatio(node->yoga, v ? v / 1000.0f : YGUndefined); break;
            case 30: node->layoutListening = v != 0; node->reportedX = INT32_MIN; break;
            case 31: node->hoverListening = v != 0; break;
            case 32: case 33: case 34:
                if (node->kind == 15 && node->hwnd) {
                    if (kind == 33) { node->sliderMin = v; SendMessageW(node->hwnd, TBM_SETRANGEMIN, TRUE, v); }
                    else if (kind == 34) { node->sliderMax = v; SendMessageW(node->hwnd, TBM_SETRANGEMAX, TRUE, v); }
                    else { node->sliderValue = v; SendMessageW(node->hwnd, TBM_SETPOS, TRUE, v); }
                }
                break;
            case 35: node->translateX = v; animate(node, 1, node->shownX, v); break;
            case 36: node->translateY = v; animate(node, 2, node->shownY, v); break;
            case 37: node->scalePercent = v; break;
            case 38: node->rotateDegrees = v; break;
            case 39: node->transitionMs = v; break;
            case 40: node->easing = v; break;
            case 41: node->panListening = v != 0; break;
            case 42: YGNodeStyleSetMinWidth(node->yoga, (float)v); break;
            case 43: YGNodeStyleSetMaxWidth(node->yoga, v ? (float)v : YGUndefined); break;
            case 44: YGNodeStyleSetMinHeight(node->yoga, (float)v); break;
            case 45: YGNodeStyleSetMaxHeight(node->yoga, v ? (float)v : YGUndefined); break;
            case 46: node->shadow = v; break;
            case 47: node->cover = v != 0; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 48: node->pixelated = v != 0; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 49: node->longListening = v != 0; break;
            case 50: node->gradientTo = v; applyButtonStyle(node); if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 51: node->gradientAngle = v; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 52: node->blur = v; break;
            default: break;
        }
    } else if (op == 9) {
        std::wstring value = wide(text);
        if (kind == 4 && node->kind == 13) mountNative(node, text ? text : "");
        else if (kind == 1) {
            node->label = value;
            int role = 0;
            if ((node->kind == 1 || node->kind == 2 || node->kind == 13 || node->kind == 14) && !value.empty()) role = ROLE_SYSTEM_GROUPING;
            accessible(node->kind == 5 ? node->inner : node->hwnd, role, &value);
        } else if (kind == 5) { node->fontFile = value; applyFont(node); }
        else if (kind == 2 && node->kind == 5 && node->inner) {
            node->placeholder = value;
            SendMessageW(node->inner, EM_SETCUEBANNER, TRUE, (LPARAM)value.c_str());
        } else if (kind == 3 && node->kind == 7) loadImage(node, value);
    } else if (op == 3) {
        std::wstring value = wide(text);
        node->text = value;
        if (node->kind == 5 && node->inner) {
            int n = GetWindowTextLengthW(node->inner);
            std::wstring now(n, L'\0');
            if (n) GetWindowTextW(node->inner, &now[0], n + 1);
            if (now != value) {
                node->settingText = true;
                SetWindowTextW(node->inner, value.c_str());
                node->settingText = false;
            }
        } else if (node->hwnd && (node->kind == 3 || node->kind == 4)) {
            SetWindowTextW(node->hwnd, value.c_str());
            InvalidateRect(node->hwnd, nullptr, TRUE);
        }
        YGNodeMarkDirty(node->yoga);
    } else if ((op == 4 || op == 5) && node->kind == 12) {
        // A Modal is a layer over the window's content, laid out on its own.
        if (std::find(modals.begin(), modals.end(), node) != modals.end()) return;
        modals.push_back(node);
        if (node->hwnd) {
            SetWindowPos(node->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ShowWindow(node->hwnd, SW_SHOWNA);
        }
    } else if (op == 4 || op == 5) {
        auto parentAt = nodes.find(parent);
        Node *parentNode = parentAt == nodes.end() ? nullptr : parentAt->second;
        YGNodeRef parentYoga = parentNode ? parentNode->yoga : rootYoga;
        YGNodeRef oldParent = YGNodeGetParent(node->yoga);
        if (oldParent) YGNodeRemoveChild(oldParent, node->yoga);
        Node *next = nullptr;
        if (before) { auto at = nodes.find(before); if (at != nodes.end()) next = at->second; }
        size_t index = YGNodeGetChildCount(parentYoga);
        if (next) {
            for (size_t i = 0; i < YGNodeGetChildCount(parentYoga); ++i) {
                if (YGNodeGetChild(parentYoga, i) == next->yoga) { index = i; break; }
            }
        }
        YGNodeInsertChild(parentYoga, node->yoga, index);
        insertWindow(node, containerOf(parentNode), next);
    } else if (op == 6) {
        removeNode(node);
    } else if (op == 7 || op == 8) {
        node->listening = op == 7;
    } else {
        fprintf(stderr, "ui host: unknown op %d\n", op);
        abort();
    }
}

// ---- layout ----

// A node's frame from the window's top left in points, from the layout: false when it is not
// in the window's tree (a screen sliding out).
static bool frameOf(Node *node, double &x, double &y) {
    x = node->x;
    y = node->y;
    for (Node *up = parentNode(node); up; up = parentNode(up)) {
        x += up->x;
        y += up->y;
        if (up->kind == 6) y -= up->scrollY;
    }
    YGNodeRef top = node->yoga;
    while (YGNodeGetParent(top)) top = YGNodeGetParent(top);
    if (top == rootYoga) return true;
    for (Node *modal : modals) if (modal->yoga == top) return true;
    return false;
}

static void place(YGNodeRef yoga) {
    Node *owner = (Node *)YGNodeGetContext(yoga);
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        Node *node = (Node *)YGNodeGetContext(child);
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        node->x = YGNodeLayoutGetLeft(child);
        node->y = YGNodeLayoutGetTop(child);
        node->w = YGNodeLayoutGetWidth(child);
        node->h = YGNodeLayoutGetHeight(child);
        placeWindow(node);
        if (node->kind == 5 && node->inner) {
            // The EDIT in the field's content box, centered on its line.
            double left = YGNodeLayoutGetPadding(child, YGEdgeLeft) + YGNodeLayoutGetBorder(child, YGEdgeLeft);
            double right = YGNodeLayoutGetPadding(child, YGEdgeRight) + YGNodeLayoutGetBorder(child, YGEdgeRight);
            if (!node->styled) left += kFieldInsetX, right += kFieldInsetX;
            int line = lineHeight(fontOf(node)) + px(2);
            int width = std::max(0, px(node->w - left - right));
            int top = (px(node->h) - line) / 2;
            SetWindowPos(node->inner, nullptr, px(left), top, width, line, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (node->layoutListening) {
            double ax = 0, ay = 0;
            frameOf(node, ax, ay);
            int x = (int)std::lround(node->x), y = (int)std::lround(node->y), w = (int)std::lround(node->w), h = (int)std::lround(node->h);
            int wx = (int)std::lround(ax), wy = (int)std::lround(ay);
            if (x != node->reportedX || y != node->reportedY || w != node->reportedW || h != node->reportedH || wx != node->reportedWX ||
                wy != node->reportedWY) {
                node->reportedX = x; node->reportedY = y; node->reportedW = w; node->reportedH = h;
                node->reportedWX = wx; node->reportedWY = wy;
                ui_dispatch_layout(node->id, node->generation, x, y, w, h, wx, wy);
            }
        }
        place(child);
    }
    // A scroll's document is as tall as its lowest child.
    if (owner && owner->kind == 6) {
        double bottom = 0;
        for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
            YGNodeRef child = YGNodeGetChild(yoga, i);
            bottom = std::max(bottom, (double)(YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child)));
        }
        owner->documentHeight = std::max((double)YGNodeLayoutGetHeight(yoga), bottom + YGNodeLayoutGetPadding(yoga, YGEdgeBottom));
        scrollTo(owner, owner->scrollY, false);
        double viewport = YGNodeLayoutGetHeight(yoga);
        if (owner->listening && viewport != owner->reportedViewport) {
            owner->reportedViewport = viewport;
            ui_dispatch_scroll(owner->id, owner->generation, (int)std::lround(owner->scrollY), (int)std::lround(viewport));
        }
    }
}

// Scrolling moves what is inside in the window without a layout: the listened nodes' window
// positions are checked again.
static void reportWindows() {
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (!node->layoutListening || !node->hwnd || node->reportedX == INT32_MIN) continue;
        double ax = 0, ay = 0;
        frameOf(node, ax, ay);
        int wx = (int)std::lround(ax), wy = (int)std::lround(ay);
        if (wx == node->reportedWX && wy == node->reportedWY) continue;
        node->reportedWX = wx;
        node->reportedWY = wy;
        ui_dispatch_layout(node->id, node->generation, node->reportedX, node->reportedY, node->reportedW, node->reportedH, wx, wy);
    }
}

static void relayout() {
    RECT client;
    GetClientRect(rootWindow, &client);
    double width = client.right / scale, height = client.bottom / scale;
    ui_grid_calculate(rootYoga, (float)width, (float)height, YGDirectionLTR);
    place(rootYoga);
    for (Node *modal : modals) {
        modal->x = modal->y = 0;
        modal->w = width;
        modal->h = height;
        if (modal->hwnd) SetWindowPos(modal->hwnd, nullptr, 0, 0, client.right, client.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
        ui_grid_calculate(modal->yoga, (float)width, (float)height, YGDirectionLTR);
        place(modal->yoga);
    }
    RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

// ---- the core's requests ----

static void ui_host_command(int request, int command, int count, const char **strings);

#ifdef UI_TEST
static void driverClockTo(double now);
#endif

extern "C" void ui_host_commit(const uint8_t *data, int len) {
#ifdef UI_TEST
    // The transitions this commit starts begin at the driver's time.
    driverClockTo(ui_driver_time());
#endif
    ui_commit_each(data, len, ui_host_apply, ui_host_command, ui_host_named);
    relayout();
    slideScreens();

}

// A frame is rendered once the messages already queued are handled (many events at once
// batch into one frame).
extern "C" void ui_host_schedule(void) {
    if (tickScheduled) return;
    tickScheduled = true;
    PostMessageW(mainWindow, WM_UI_TICK, 0, 0);
}
extern "C" void ui_host_cancel(void) { tickScheduled = false; }

extern "C" char *ui_host_asset_path(const char *name) {
    std::wstring path = exeDir + L"\\assets\\" + wide(name);
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return nullptr;
    return strdup(narrow(path.c_str()).c_str());
}

// The display clock for use_frame: a 60 Hz timer.
static bool framesOn;
extern "C" void ui_host_frames(int on) {
    framesOn = on != 0;
    if (on) SetTimer(mainWindow, kFramesTimer, 16, nullptr); else KillTimer(mainWindow, kFramesTimer);
}

// Timers for use_interval and use_timeout, by token.
static std::map<int, bool> timers;
extern "C" void ui_host_timer_start(int token, int ms, int repeat) {
    timers[token] = repeat != 0;
    SetTimer(mainWindow, kTimerBase + (UINT_PTR)token, (UINT)std::max(USER_TIMER_MINIMUM, ms), nullptr);
}
extern "C" void ui_host_timer_stop(int token) {
    KillTimer(mainWindow, kTimerBase + (UINT_PTR)token);
    timers.erase(token);
}

// An alert, shown once the commit is applied: a task dialog with the app's buttons.
struct Alert { int request; std::wstring title, message; std::vector<std::wstring> buttons; };
static void showAlert(Alert alert) {
    std::vector<TASKDIALOG_BUTTON> buttons;
    for (size_t i = 0; i < alert.buttons.size(); i++) buttons.push_back({(int)(1000 + i), alert.buttons[i].c_str()});
    TASKDIALOGCONFIG config = {sizeof config};
    config.hwndParent = mainWindow;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = appName.c_str();
    config.pszMainInstruction = alert.title.c_str();
    config.pszContent = alert.message.c_str();
    config.cButtons = (UINT)buttons.size();
    config.pButtons = buttons.empty() ? nullptr : buttons.data();
    if (buttons.empty()) config.dwCommonButtons = TDCBF_OK_BUTTON;
    typedef HRESULT(WINAPI * TaskDialogFn)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
    TaskDialogFn show = (TaskDialogFn)(void *)GetProcAddress(GetModuleHandleW(L"comctl32.dll"), "TaskDialogIndirect");
    int pressed = -1;
    if (show) show(&config, &pressed, nullptr, nullptr);
    else {
        std::wstring text = alert.title + L"\n\n" + alert.message;
        pressed = MessageBoxW(mainWindow, text.c_str(), appName.c_str(), MB_OKCANCEL) == IDOK ? 1000 : IDCANCEL;
    }
    int index = pressed >= 1000 && pressed < 1000 + (int)alert.buttons.size() ? pressed - 1000 : -1;
    ui_dispatch_alert(alert.request, index);
}

static void copyText(const std::wstring &text) {
    if (!OpenClipboard(mainWindow)) return;
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (memory) {
        memcpy(GlobalLock(memory), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

static std::wstring pasteText() {
    std::wstring text;
    if (!OpenClipboard(mainWindow)) return text;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t *p = (const wchar_t *)GlobalLock(data);
        if (p) text = p;
        GlobalUnlock(data);
    }
    CloseClipboard();
    return text;
}

// Commands: 1 an alert, 2 open a URL, 4 hide the keyboard, 5 share, 6 copy, 7 paste, 8 a
// haptic, 9 announce, 10 fetch, 11 the splash (a desktop window has none).
static void ui_host_command(int request, int command, int count, const char **strings) {
    if (command == 1 && count >= 2) {
        Alert alert{request, wide(strings[0]), wide(strings[1]), {}};
        for (int i = 2; i < count; i++) alert.buttons.push_back(wide(strings[i]));
        onMain([alert]() { showAlert(alert); });
    } else if (command == 2 && count >= 1) {
        std::wstring url = strcmp(strings[0], "app-settings:") == 0 ? L"ms-settings:" : wide(strings[0]);
        ShellExecuteW(mainWindow, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (command == 4) {
        HWND focus = GetFocus();
        if (focus && IsChild(mainWindow, focus)) SetFocus(mainWindow);
    } else if (command == 5 && count >= 2) {
        // A desktop has no share sheet here: the text and the URL go to the clipboard.
        std::wstring text = wide(strings[0]);
        if (strings[1][0]) text += (text.empty() ? L"" : L" ") + wide(strings[1]);
        copyText(text);
    } else if (command == 6 && count >= 1) {
        copyText(wide(strings[0]));
    } else if (command == 7) {
        ui_dispatch_answer(request, narrow(pasteText().c_str()).c_str());
    } else if (command == 9 && count >= 1) {
        // Announced through the window's accessible name change, which screen readers follow.
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, mainWindow, OBJID_CLIENT, CHILDID_SELF);
    } else if (command == 10 && count >= 6) {
        std::wstring url = wide(strings[0]);
        std::string method = strings[1], headers = strings[2], body = strings[3];
        bool hasBody = strings[5][0] == '1';
        int timeout = atoi(strings[4]);
        std::string saveTo = count >= 7 ? strings[6] : "";
        std::thread([request, url, method, headers, body, hasBody, timeout, saveTo]() {
            int status = 0;
            std::vector<uint8_t> bytes;
            std::string responseHeaders, error;
            bool ok = httpGet(url, method, headers, hasBody ? &body : nullptr, timeout, status, bytes, responseHeaders, error);
            std::string text;
            if (!ok) status = 0, text = error;
            else if (!saveTo.empty() && status >= 200 && status < 300) {
                FILE *file = _wfopen(wide(saveTo.c_str()).c_str(), L"wb");
                if (!file || fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) status = 0, text = "could not write " + saveTo;
                if (file) fclose(file);
            } else text.assign(bytes.begin(), bytes.end());
            onMain([request, status, text, responseHeaders]() { ui_dispatch_fetch(request, status, text.c_str(), responseHeaders.c_str()); });
        }).detach();
    }
}

// ---- the system's settings ----

static bool systemDark() {
    DWORD light = 1, size = sizeof light;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        return false;
    return light == 0;
}

// Dark menus and title bar: uxtheme's SetPreferredAppMode (an ordinal since Windows 10 1903).
static void allowDarkMenus(bool dark) {
    HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!uxtheme) uxtheme = LoadLibraryW(L"uxtheme.dll");
    typedef int(WINAPI * SetModeFn)(int);
    typedef void(WINAPI * FlushFn)();
    auto setMode = uxtheme ? (SetModeFn)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)) : nullptr;
    auto flush = uxtheme ? (FlushFn)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)) : nullptr;
    if (setMode) setMode(dark ? 2 : 3);
    if (flush) flush();
}

static void applyAppearance() {
    darkMode = systemDark();
    BOOL dark = darkMode;
    DwmSetWindowAttribute(mainWindow, 20, &dark, sizeof dark);
    allowDarkMenus(darkMode);
    for (auto &entry : nodes) applyTheme(entry.second);
    DrawMenuBar(mainWindow);
    RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_FRAME);
}

static void reportSystem() {
    ui_report_appearance(darkMode ? 1 : 0);
    HIGHCONTRASTW contrast = {sizeof contrast};
    BOOL animations = TRUE, reader = FALSE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    SystemParametersInfoW(SPI_GETSCREENREADER, 0, &reader, 0);
    (void)contrast;
    ui_report_accessibility(reader ? 1 : 0, animations ? 0 : 1, 100);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"en-US";
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    DWORD reading = 0;
    GetLocaleInfoEx(locale, LOCALE_IREADINGLAYOUT | LOCALE_RETURN_NUMBER, (LPWSTR)&reading, sizeof reading / sizeof(wchar_t));
    ui_report_locale(narrow(locale).c_str(), reading == 1 ? 1 : 0);
}

static void reportWindow() {
    RECT client;
    GetClientRect(rootWindow, &client);
    ui_report_window((int)std::lround(client.right / scale), (int)std::lround(client.bottom / scale), (int)std::lround(scale * 100));
}

static void reportEnvironment() {
    reportWindow();
    ui_report_insets(0, 0, 0, 0, 0);
    reportSystem();
}

static std::wstring dataDir() {
    PWSTR roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) {
        dir = std::wstring(roaming) + L"\\" + appName;
        CoTaskMemFree(roaming);
    }
    if (!dir.empty()) SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

// ---- reload under `ui serve` ----

#ifdef UI_RELOAD
#ifdef UI_TEST
static void driverClock(bool owned, double now, bool settle);
#endif
// `ui serve`'s builds: UI_RELOAD_FILE names a file holding the path of the app's newest DLL.
// When it changes, the app starts again on the new code in the running process: the old core
// saves its routes and signals and stops, the windows, timers and nodes it left go, and the
// new one starts on what the old one saved.
static std::string loadedCore;

static std::string newestCore() {
    const char *file = getenv("UI_RELOAD_FILE");
    if (!file) return "";
    FILE *f = fopen(file, "rb");
    if (!f) return "";
    char buffer[4096] = {};
    size_t n = fread(buffer, 1, sizeof buffer - 1, f);
    fclose(f);
    std::string text(buffer, n);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

static void restartCore(const std::string &path) {
#ifdef UI_TEST
    char *kept = ui_reload_save ? strdup(ui_reload_save()) : nullptr;
#endif
    ui_stop();
#ifdef UI_TEST
    driverClock(false, 0, false);
#endif
    tickScheduled = false;
    ui_host_frames(0);
    for (auto &timer : timers) KillTimer(mainWindow, kTimerBase + (UINT_PTR)timer.first);
    timers.clear();
    animations.clear();
    modals.clear();
    hovered.clear();
    gesture = {};
    slidingIn.clear();
    std::vector<Node *> all;
    for (auto &entry : nodes) all.push_back(entry.second);
    nodes.clear();
    for (Node *node : all) {
        if (node->hwnd) DestroyWindow(node->hwnd);
    }
    for (Node *node : all) { delete node->emitter; ui_grid_free(node->yoga); delete node; }
    windowNodes.clear();
    for (HWND hwnd : std::set<HWND>(leavingWindows)) DestroyWindow(hwnd);
    leavingWindows.clear();
    YGNodeRemoveAllChildren(rootYoga);
    if (!loadCore(path.c_str())) {
#ifdef UI_TEST
        free(kept);
#endif
        return;
    }
    loadedCore = path;
    ui_report_data_dir(narrow(dataDir().c_str()).c_str());
#ifdef UI_TEST
    if (kept && ui_reload_restore) ui_reload_restore(kept);
    free(kept);
#endif
    ui_start(launchExample);
    reportEnvironment();
    relayout();
    size_t slash = path.find_last_of("\\/");
    fprintf(stderr, "ui: restarted on %s\n", path.substr(slash == std::string::npos ? 0 : slash + 1).c_str());
}
#endif

// ---- the driver ----

#ifdef UI_TEST
static void driverOnMain(std::function<void()> work) { onMain(std::move(work)); }
static std::string driverCarry(const std::string &directive);
#define UI_DRIVER_HOST_REQUESTS 1
static bool driverHostRequest(const std::string &request, std::string &answer);
#include "../driver.h"

// While the driver owns the clock, the host's transitions and the spinner move on its time,
// which only `clock` moves; `clock settle` moves it past their ends.
static void driverClockTo(double now) {
    if (clockOwned) driverNow = now;
}
static void driverClock(bool owned, double now, bool settle) {
    if (owned) {
        if (!clockOwned) {
            // The transitions under way end where they go: they start again on the new clock.
            for (auto &a : animations) a.start = -1e12;
            stepAnimations();
            clockOwned = true;
            settleBonus = 0;
        }
        driverNow = now;
        if (settle) settleBonus += 60000;
        stepAnimations();
    } else if (clockOwned) {
        for (auto &a : animations) a.start = -1e12;
        stepAnimations();
        clockOwned = false;
        if (spinnersShowing()) animationsRun();
    }
    RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// The window's client area into a PNG, as the compositor shows it.
static bool encoderOf(const wchar_t *type, CLSID *clsid) {
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (!size) return false;
    std::vector<uint8_t> buffer(size);
    auto *codecs = (Gdiplus::ImageCodecInfo *)buffer.data();
    Gdiplus::GetImageEncoders(count, size, codecs);
    for (UINT i = 0; i < count; i++) {
        if (wcscmp(codecs[i].MimeType, type) == 0) { *clsid = codecs[i].Clsid; return true; }
    }
    return false;
}

static bool driverHostRequest(const std::string &request, std::string &answer) {
    if (driverText(request, "op") != "screenshot") return false;
    std::string file = driverText(request, "file");
    RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    RECT client;
    GetClientRect(mainWindow, &client);
    RECT window;
    GetWindowRect(mainWindow, &window);
    POINT origin = {0, 0};
    ClientToScreen(mainWindow, &origin);
    int w = client.right, h = client.bottom;
    HDC screen = GetDC(nullptr);
    HDC whole = CreateCompatibleDC(screen);
    HBITMAP wholeBitmap = CreateCompatibleBitmap(screen, window.right - window.left, window.bottom - window.top);
    HGDIOBJ oldWhole = SelectObject(whole, wholeBitmap);
    // The whole window as the compositor has it (layered children included), then its client area.
    bool printed = PrintWindow(mainWindow, whole, PW_RENDERFULLCONTENT) != 0;
    HDC part = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(part, bitmap);
    if (printed) BitBlt(part, 0, 0, w, h, whole, origin.x - window.left, origin.y - window.top, SRCCOPY);
    else PrintWindow(mainWindow, part, PW_CLIENTONLY);
    SelectObject(part, old);
    SelectObject(whole, oldWhole);
    DeleteObject(wholeBitmap);
    DeleteDC(whole);
    DeleteDC(part);
    ReleaseDC(nullptr, screen);
    Gdiplus::Bitmap image(bitmap, nullptr);
    CLSID png;
    bool ok = encoderOf(L"image/png", &png) && image.Save(wide(file.c_str()).c_str(), &png, nullptr) == Gdiplus::Ok;
    DeleteObject(bitmap);
    if (!ok) {
        answer = "{\"ok\": false, \"op\": \"screenshot\", \"error\": \"could not write the screenshot\"}";
        return true;
    }
    answer = "{\"ok\": true, \"op\": \"screenshot\", \"width\": " + std::to_string(w) + ", \"height\": " + std::to_string(h) + "}";
    return true;
}

static std::string driverCarry(const std::string &d) {
    std::string op = driverOp(d);
    if (op == "hello" || op == "clock") {
        driverClock(driverFlag(d, "virtual"), driverNumber(d, "now"), driverFlag(d, "settle"));
        return "";
    }
    if (op == "logs") return driverLogs();
    if (op == "perf") {
        PROCESS_MEMORY_COUNTERS_EX counters = {};
        counters.cb = sizeof counters;
        if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&counters, sizeof counters)) return driverPerf(0);
        return driverPerf((double)counters.PrivateUsage);
    }
    if (op == "layout") {
        std::string rows;
        char line[160];
        for (auto &entry : nodes) {
            Node *node = entry.second;
            if (!node->hwnd) continue;
            double x, y;
            bool attached = frameOf(node, x, y);
            bool shown = attached && IsWindowVisible(node->hwnd);
            for (Node *up = node; up && shown; up = parentNode(up)) if (up->opacity <= 0) shown = false;
            snprintf(line, sizeof line, "%d %.1f %.1f %.1f %.1f %d\n", node->id, x, y, node->w, node->h, shown ? 1 : 0);
            rows += line;
        }
        return rows;
    }
    int id = (int)driverNumber(d, "node");
    auto found = nodes.find(id);
    Node *node = found == nodes.end() ? nullptr : found->second;
    if (!node || !node->hwnd) return "error: #" + std::to_string(id) + " has no view";
    // The input goes in as the controls take it: a button's click, a field's text, the scroll bar.
    if (op == "tap") {
        if (node->kind == 4 || node->kind == 9) SendMessageW(node->hwnd, BM_CLICK, 0, 0);
        else if (node->kind == 5) SetFocus(node->inner);
        else if (node->kind == 12) ui_dispatch(id, node->generation);
        else if (node->kind == 8) ui_dispatch(id, node->generation);
        else return "error: the view takes no press";
        return "";
    }
    if (op == "type") {
        if (node->kind != 5 || !node->inner) return "error: not a text field";
        SetFocus(node->inner);
        SetWindowTextW(node->inner, wide(driverText(d, "text").c_str()).c_str());
        return "";
    }
    if (op == "scroll") {
        if (node->kind != 6) return "error: not a scroll view";
        scrollTo(node, node->scrollY + driverNumber(d, "dy"), true);
        return std::to_string((int)std::lround(node->scrollY));
    }
    return "error: the Windows host does not know " + op;
}
#endif

// ---- the main window ----

static HACCEL accelerators;

static void setupMenu() {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, kCmdAbout, (L"About " + appName).c_str());
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, kCmdClose, L"E&xit\tAlt+F4");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, kCmdUndo, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, kCmdCut, L"Cu&t\tCtrl+X");
    AppendMenuW(edit, MF_STRING, kCmdCopy, L"&Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING, kCmdPaste, L"&Paste\tCtrl+V");
    AppendMenuW(edit, MF_STRING, kCmdSelectAll, L"Select &All\tCtrl+A");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, kCmdBack, L"&Back\tAlt+Left");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    SetMenu(mainWindow, bar);
    ACCEL keys[] = {{FVIRTKEY | FALT, VK_LEFT, kCmdBack}, {FVIRTKEY | FCONTROL, 'W', kCmdClose}};
    accelerators = CreateAcceleratorTableW(keys, 2);
}

// The Edit menu's commands go to the field with the focus.
static void editCommand(int command) {
    HWND focus = GetFocus();
    wchar_t cls[16] = {};
    if (!focus || !GetClassNameW(focus, cls, 16) || _wcsicmp(cls, L"Edit") != 0) return;
    if (command == kCmdUndo) SendMessageW(focus, WM_UNDO, 0, 0);
    else if (command == kCmdCut) SendMessageW(focus, WM_CUT, 0, 0);
    else if (command == kCmdCopy) SendMessageW(focus, WM_COPY, 0, 0);
    else if (command == kCmdPaste) SendMessageW(focus, WM_PASTE, 0, 0);
    else if (command == kCmdSelectAll) SendMessageW(focus, EM_SETSEL, 0, -1);
}

static void runTick() {
    tickScheduled = false;
    auto start = std::chrono::steady_clock::now();
    ui_tick();
    frameTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

static LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    LRESULT result = notifyChild(hwnd, msg, wParam, lParam, &handled);
    if (handled) return result;
    switch (msg) {
        case WM_UI_TICK:
            if (tickScheduled) runTick();
            return 0;
        case WM_UI_RUN: {
            std::unique_ptr<std::function<void()>> work((std::function<void()> *)lParam);
            (*work)();
            return 0;
        }
        case WM_TIMER:
            if (wParam == kFramesTimer) { if (framesOn) ui_dispatch_frame(nowMs() / 1000.0); }
            else if (wParam == kAnimTimer) stepAnimations();
            else if (wParam == kHoverTimer) updateHover();
            else if (wParam == kLongPressTimer) {
                KillTimer(hwnd, kLongPressTimer);
                Node *node = gesture.longPress;
                if (node && nodes.count(node->id) && nodes[node->id] == node) {
                    setPressed(gesture.press, false);
                    gesture.press = nullptr;
                    ui_dispatch_long_press(node->id, node->generation);
                }
            }
#ifdef UI_RELOAD
            else if (wParam == kReloadTimer) {
                std::string path = newestCore();
                if (!path.empty() && path != loadedCore) restartCore(path);
            }
#endif
            else if (wParam >= kTimerBase) {
                int token = (int)(wParam - kTimerBase);
                auto found = timers.find(token);
                if (found == timers.end()) { KillTimer(hwnd, wParam); return 0; }
                if (!found->second) { KillTimer(hwnd, wParam); timers.erase(found); }
                ui_dispatch_timer(token);
            }
            return 0;
        case WM_SIZE:
            if (rootWindow) {
                MoveWindow(rootWindow, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
                reportWindow();
                relayout();
            }
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *info = (MINMAXINFO *)lParam;
            RECT r = {0, 0, px(320), px(240)};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, TRUE, 0, (UINT)std::lround(96 * scale));
            info->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
            return 0;
        }
        case WM_DPICHANGED: {
            scale = HIWORD(wParam) / 96.0;
            for (auto &entry : nodes) {
                Node *node = entry.second;
                if (node->kind == 3 || node->kind == 4 || node->kind == 5) applyFont(node);
                YGNodeMarkDirty(node->yoga);
            }
            RECT *suggested = (RECT *)lParam;
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            reportWindow();
            relayout();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lParam && wcscmp((const wchar_t *)lParam, L"ImmersiveColorSet") == 0) applyAppearance();
            reportSystem();
            return 0;
        case WM_ACTIVATEAPP: ui_report_active(wParam ? 1 : 0); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCmdClose: DestroyWindow(hwnd); return 0;
                case kCmdBack: ui_report_back(); return 0;
                case kCmdAbout: MessageBoxW(hwnd, appName.c_str(), (L"About " + appName).c_str(), MB_OK | MB_ICONINFORMATION); return 0;
                case kCmdUndo: case kCmdCut: case kCmdCopy: case kCmdPaste: case kCmdSelectAll: editCommand(LOWORD(wParam)); return 0;
            }
            break;
        case WM_XBUTTONUP:
            if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) { ui_report_back(); return TRUE; }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY:
            ui_stop();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void registerClasses(HINSTANCE instance) {
    WNDCLASSEXW wc = {sizeof wc};
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpfnWndProc = viewProc;
    wc.lpszClassName = kViewClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = scrollProc;
    wc.lpszClassName = kScrollClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = imageProc;
    wc.lpszClassName = kImageClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = spinnerProc;
    wc.lpszClassName = kSpinnerClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = mainProc;
    wc.lpszClassName = kMainClass;
    wc.hIcon = (HICON)LoadImageW(nullptr, (exeDir + L"\\icon.ico").c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
}

// The window's size in points: UI_WINDOW=WxH, else window.txt next to the executable (ui.toml
// [windows] window), else 900 by 700.
static void windowSize(int &w, int &h) {
    w = 900;
    h = 700;
    std::string size;
    if (const char *env = getenv("UI_WINDOW")) size = env;
    else {
        FILE *file = _wfopen((exeDir + L"\\window.txt").c_str(), L"rb");
        if (file) {
            char text[64] = {};
            fread(text, 1, sizeof text - 1, file);
            fclose(file);
            size = text;
        }
    }
    int a = 0, b = 0;
    if (sscanf(size.c_str(), "%dx%d", &a, &b) == 2 && a >= 200 && b >= 150) { w = a; h = b; }
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls = {sizeof controls, ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_IAccPropServices, (void **)&accProps);
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string link;
    for (int i = 1; argv && i < argc; ++i) {
        if (!wcscmp(argv[i], L"--link") && i + 1 < argc) link = narrow(argv[i + 1]);
        if (!wcscmp(argv[i], L"--example") && i + 1 < argc) launchExample = _wtoi(argv[i + 1]);
    }
    if (argv) LocalFree(argv);
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t folder[MAX_PATH];
    wcsncpy(folder, exePath, MAX_PATH);
    PathRemoveFileSpecW(folder);
    exeDir = folder;
    wchar_t name[MAX_PATH];
    wcsncpy(name, PathFindFileNameW(exePath), MAX_PATH - 1);
    PathRemoveExtensionW(name);
    appName = name;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    registerClasses(instance);
#ifdef UI_TEST
    // The driver: the app's output kept for `logs` (and still printed), and its socket.
    int printed = _dup(2);
    driverCapture([printed](const char *bytes, size_t n) { if (printed >= 0) _write(printed, bytes, (unsigned)n); });
    const char *agentPort = getenv("UI_AGENT_PORT");
    int port = agentPort ? atoi(agentPort) : 8770;
    if (!driverListenTcp(port)) fprintf(stderr, "ui: the driver could not listen on 127.0.0.1:%d\n", port);
#endif
#ifdef UI_RELOAD
    loadedCore = newestCore();
    if (loadedCore.empty() || !loadCore(loadedCore.c_str())) { fprintf(stderr, "ui: no app library (UI_RELOAD_FILE)\n"); return 1; }
    // The pid next to the reload file: `ui run windows` reloads a running app instead of starting it.
    {
        std::string pidFile = getenv("UI_RELOAD_FILE");
        size_t slash = pidFile.find_last_of("\\/");
        pidFile = (slash == std::string::npos ? std::string(".") : pidFile.substr(0, slash)) + "/pid";
        FILE *f = fopen(pidFile.c_str(), "wb");
        if (f) { fprintf(f, "%lu", (unsigned long)GetCurrentProcessId()); fclose(f); }
    }
#endif
    int w, h;
    windowSize(w, h);
    // The window on the primary monitor's DPI, its client area w by h points.
    POINT origin = {0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96, dpiY = 96;
    typedef HRESULT(WINAPI * DpiFn)(HMONITOR, int, UINT *, UINT *);
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    auto monitorDpi = shcore ? (DpiFn)(void *)GetProcAddress(shcore, "GetDpiForMonitor") : nullptr;
    if (monitorDpi) monitorDpi(monitor, 0, &dpiX, &dpiY);
    scale = dpiX / 96.0;
    RECT frame = {0, 0, px(w), px(h)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, TRUE, 0, dpiX);
    MONITORINFO info = {sizeof info};
    GetMonitorInfoW(monitor, &info);
    int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
    int x = info.rcWork.left + std::max(0L, (info.rcWork.right - info.rcWork.left - fw) / 2);
    int y = info.rcWork.top + std::max(0L, (info.rcWork.bottom - info.rcWork.top - fh) / 2);
    mainWindow = CreateWindowExW(0, kMainClass, appName.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, fw, fh, nullptr, nullptr,
                                 instance, nullptr);
    scale = GetDpiForWindow(mainWindow) / 96.0;
    setupMenu();
    // The menu takes some of the height: the client area is w by h.
    frame = {0, 0, px(w), px(h)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, TRUE, 0, GetDpiForWindow(mainWindow));
    SetWindowPos(mainWindow, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client;
    GetClientRect(mainWindow, &client);
    rootWindow = CreateWindowExW(WS_EX_CONTROLPARENT, kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                 client.right, client.bottom, mainWindow, nullptr, instance, nullptr);
    rootYoga = YGNodeNew();
    YGNodeStyleSetFlexDirection(rootYoga, YGFlexDirectionColumn);
    applyAppearance();
    // The app's data directory, before the app starts (ui_data_dir).
    ui_report_data_dir(narrow(dataDir().c_str()).c_str());
    if (!link.empty()) ui_report_link(link.c_str());
    ui_start(launchExample);
    reportEnvironment();
    relayout();
#ifdef UI_RELOAD
    SetTimer(mainWindow, kReloadTimer, 50, nullptr);
#endif
    // `ui agent` and `ui test` (UI_NO_OPEN=1) leave the focus where it is.
    const char *quiet = getenv("UI_NO_OPEN");
    bool background = quiet && strcmp(quiet, "1") == 0;
    ShowWindow(mainWindow, background ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(mainWindow);
    if (!background) SetForegroundWindow(mainWindow);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_MOUSEMOVE) {
            bool any = false;
            for (auto &entry : nodes) if (entry.second->hoverListening) { any = true; break; }
            if (any || !hovered.empty()) updateHover();
        }
        // Escape is back: the newest modal closes, else the app's back.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && (msg.hwnd == mainWindow || IsChild(mainWindow, msg.hwnd))) {
            ui_report_back();
            continue;
        }
        if (TranslateAcceleratorW(mainWindow, accelerators, &msg)) continue;
        if (IsDialogMessageW(mainWindow, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (accProps) accProps->Release();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}
