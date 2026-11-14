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

#include "host_input.inc"
#include "host_views.inc"
#include "host_commit.inc"
#include "host_main.inc"
