// host_main.inc: the driver and the main window. Part of host.cpp, which #includes it (one translation unit).

// ---- the driver ----

static void runConformance();

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
        std::wstring typed = wide(driverText(d, "text").c_str());
        if (node->multiline) {
            std::wstring shown;
            for (wchar_t c : typed) { if (c == L'\n') shown += L'\r'; shown += c; }
            typed = shown;
        }
        SetWindowTextW(node->inner, typed.c_str());
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
    AppendMenuW(view, MF_STRING, kCmdRefresh, L"&Refresh\tF5");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    SetMenu(mainWindow, bar);
    ACCEL keys[] = {{FVIRTKEY | FALT, VK_LEFT, kCmdBack}, {FVIRTKEY | FCONTROL, 'W', kCmdClose}, {FVIRTKEY, VK_F5, kCmdRefresh}};
    accelerators = CreateAcceleratorTableW(keys, 3);
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
                // Yoga aborts on marking a node without a measure function.
                if (YGNodeHasMeasureFunc(node->yoga)) YGNodeMarkDirty(node->yoga);
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
            applyTextScale();
            reportSystem();
            return 0;
        case WM_ACTIVATEAPP: ui_report_active(wParam ? 1 : 0); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCmdClose: DestroyWindow(hwnd); return 0;
                case kCmdBack: ui_report_back(); return 0;
                case kCmdRefresh: refreshShown(); return 0;
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
    textScale = systemTextScale();
    // The app's data directory, before the app starts (ui_data_dir).
    ui_report_data_dir(narrow(dataDir().c_str()).c_str());
    if (!link.empty()) ui_report_link(link.c_str());
#ifdef UI_TEST
    // The conformance scene's own run (host_conformance.inc): example 3 asks it to fetch too.
    bool conformance = getenv("UI_CONFORMANCE") && !strcmp(getenv("UI_CONFORMANCE"), "1");
    if (conformance) launchExample = 3;
#endif
    ui_start(launchExample);
    reportEnvironment();
    relayout();
#ifdef UI_TEST
    if (conformance) runConformance();
#endif
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
        // Return in a one-line field the app submits on (before the dialog manager takes it).
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && submitReturn(msg.hwnd)) continue;
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
