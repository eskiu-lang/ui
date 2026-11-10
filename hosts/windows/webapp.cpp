// The <ui/webapp> host on Windows: Win32 windows holding a WebView2 each, for a program that
// serves a web front end (src/webapp.esk has the program's side and the contract of each
// uiw_ function). The page's messages arrive through WebMessageReceived; the program's URL
// schemes are https://NAME.localhost/ (WebView2 takes other schemes only when registered
// before it starts), answered through WebResourceRequested; the program's scripts run from
// the message loop, whatever thread asked. The tool compiles it with the WebView2 SDK's
// header (tools/ui/windows.esk fetches it) and puts WebView2Loader.dll beside the program.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <WebView2.h>

// The program's side (src/webapp.esk), on the main thread.
extern "C" {
void uiw_on_message(int window, const char *text);
void uiw_on_request(int window, int64_t request, const char *scheme, const char *url, const char *method, const char *range);
int uiw_on_close(int window);
void uiw_on_closed(int window);
}

typedef HRESULT(STDAPICALLTYPE *CreateEnvironmentFn)(PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
                                                     ICoreWebView2EnvironmentOptions *options,
                                                     ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler);

enum { WM_UIW_EVAL = WM_APP + 1, WM_UIW_QUIT, WM_UIW_CLOSE };

struct UiwWindow {
    int id = 0;
    HWND hwnd = NULL;
    ICoreWebView2Controller *controller = NULL;
    ICoreWebView2 *web = NULL;
    std::wstring url;
    std::string origin;
    bool follow_title = true;
    bool fullscreen = false;
    bool devtools = false;
    int min_width = 0;
    int min_height = 0;
    LONG style = 0;
    WINDOWPLACEMENT placement = {sizeof(WINDOWPLACEMENT)};
};

static std::wstring g_app_name = L"app";
static std::wstring g_bridge;
static std::vector<std::string> g_schemes;
static std::map<int, UiwWindow *> g_windows;
static int g_next_window;
static int g_exit_code;
static HWND g_messages;
static HMODULE g_loader;
static ICoreWebView2Environment *g_environment;
static bool g_environment_started;
static std::vector<UiwWindow *> g_waiting;
// The scheme requests being answered, by id.
static std::map<int64_t, ICoreWebView2WebResourceRequestedEventArgs *> g_requests;
static int64_t g_next_request;

static std::wstring widen(const char *text) {
    if (!text || !text[0]) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], n);
    return out;
}

static std::string narrow(const wchar_t *text) {
    if (!text || !text[0]) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], n, NULL, NULL);
    return out;
}

static UiwWindow *find(int id) {
    auto it = g_windows.find(id);
    return it == g_windows.end() ? NULL : it->second;
}

// A COM handler around a function: QueryInterface for IUnknown and its interface, freed on its
// last Release.
template <class I, class... A>
class Handler : public I {
    LONG refs = 1;
    const IID &iid;
    std::function<HRESULT(A...)> fn;

public:
    Handler(const IID &id, std::function<HRESULT(A...)> f) : iid(id), fn(std::move(f)) {}
    virtual ~Handler() {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid)) {
            *out = static_cast<I *>(this);
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&refs);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(A... args) override { return fn(args...); }
};

static void report(HWND parent, const wchar_t *what, HRESULT hr) {
    wchar_t text[512];
    swprintf(text, 512, L"%ls (error 0x%08lX).\n\nInstall or repair the Microsoft Edge WebView2 Runtime "
             L"(https://go.microsoft.com/fwlink/p/?LinkId=2124703) if this persists.", what, (unsigned long)hr);
    MessageBoxW(parent, text, g_app_name.c_str(), MB_ICONERROR | MB_OK);
}

static void fit(UiwWindow *w) {
    if (!w->controller) return;
    RECT bounds;
    GetClientRect(w->hwnd, &bounds);
    w->controller->put_Bounds(bounds);
}

static void set_fullscreen(UiwWindow *w, bool on) {
    if (on == w->fullscreen) return;
    if (on) {
        w->style = GetWindowLongW(w->hwnd, GWL_STYLE);
        GetWindowPlacement(w->hwnd, &w->placement);
        MONITORINFO monitor = {sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongW(w->hwnd, GWL_STYLE, w->style & ~WS_OVERLAPPEDWINDOW);
        RECT r = monitor.rcMonitor;
        SetWindowPos(w->hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(w->hwnd, GWL_STYLE, w->style);
        SetWindowPlacement(w->hwnd, &w->placement);
        SetWindowPos(w->hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    w->fullscreen = on;
}

// "scheme://host:port" of a URL, lowercased.
static std::string origin_of(const std::string &url) {
    size_t start = url.find("://");
    if (start == std::string::npos) return std::string();
    size_t end = url.find_first_of("/?#", start + 3);
    std::string out = url.substr(0, end == std::string::npos ? url.size() : end);
    for (char &c : out) c = (char)tolower((unsigned char)c);
    return out;
}

// Whether a URL stays in the app: one of its schemes (https://NAME.localhost), the origin its
// window opened, or about:, blob: and data:.
static bool inside(UiwWindow *w, const std::string &url) {
    std::string origin = origin_of(url);
    if (origin == w->origin || url.rfind("about:", 0) == 0 || url.rfind("blob:", 0) == 0 || url.rfind("data:", 0) == 0) return true;
    for (const std::string &scheme : g_schemes)
        if (origin == "https://" + scheme + ".localhost") return true;
    return false;
}

// ---- The program's schemes ----

static void on_resource(int window, ICoreWebView2WebResourceRequestedEventArgs *args) {
    ICoreWebView2WebResourceRequest *request = NULL;
    if (FAILED(args->get_Request(&request)) || !request) return;
    LPWSTR uri = NULL, method = NULL, range = NULL;
    request->get_Uri(&uri);
    request->get_Method(&method);
    ICoreWebView2HttpRequestHeaders *headers = NULL;
    if (SUCCEEDED(request->get_Headers(&headers)) && headers) {
        BOOL has = FALSE;
        if (SUCCEEDED(headers->Contains(L"Range", &has)) && has) headers->GetHeader(L"Range", &range);
        headers->Release();
    }
    std::string url = narrow(uri);
    // https://NAME.localhost/...: the scheme is NAME.
    std::string scheme;
    size_t start = url.find("://");
    if (start != std::string::npos) {
        size_t dot = url.find(".localhost", start + 3);
        if (dot != std::string::npos) scheme = url.substr(start + 3, dot - start - 3);
    }
    int64_t id = ++g_next_request;
    args->AddRef();
    g_requests[id] = args;
    uiw_on_request(window, id, scheme.c_str(), url.c_str(), method ? narrow(method).c_str() : "GET", range ? narrow(range).c_str() : "");
    auto left = g_requests.find(id);
    if (left != g_requests.end()) {
        left->second->Release();
        g_requests.erase(left);
    }
    CoTaskMemFree(uri);
    CoTaskMemFree(method);
    CoTaskMemFree(range);
    request->Release();
}

// ---- A window's web view ----

static void attach(UiwWindow *w) {
    HRESULT hr = g_environment->CreateCoreWebView2Controller(w->hwnd,
        new Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller *>(
            IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
            [id = w->id](HRESULT error, ICoreWebView2Controller *controller) -> HRESULT {
                UiwWindow *w = find(id);
                if (!w) return S_OK;
                if (FAILED(error) || !controller) {
                    report(w->hwnd, L"WebView2 could not create its view", error);
                    return S_OK;
                }
                w->controller = controller;
                controller->AddRef();
                controller->get_CoreWebView2(&w->web);
                fit(w);
                controller->put_IsVisible(TRUE);
                ICoreWebView2Settings *settings = NULL;
                if (SUCCEEDED(w->web->get_Settings(&settings)) && settings) {
                    settings->put_AreDevToolsEnabled(w->devtools ? TRUE : FALSE);
                    settings->put_AreDefaultContextMenusEnabled(w->devtools ? TRUE : FALSE);
                    settings->put_IsStatusBarEnabled(FALSE);
                    settings->Release();
                }
                wchar_t prefix[96];
                swprintf(prefix, 96, L"window.__ESKIU_HOST__={os:\"windows\",window:%d};\n", id);
                w->web->AddScriptToExecuteOnDocumentCreated((std::wstring(prefix) + g_bridge).c_str(), NULL);
                EventRegistrationToken token;
                w->web->add_WebMessageReceived(
                    new Handler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *>(
                        IID_ICoreWebView2WebMessageReceivedEventHandler,
                        [id](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                            LPWSTR text = NULL;
                            if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text) {
                                uiw_on_message(id, narrow(text).c_str());
                                CoTaskMemFree(text);
                            }
                            return S_OK;
                        }),
                    &token);
                for (const std::string &scheme : g_schemes) {
                    std::wstring filter = L"https://" + widen(scheme.c_str()) + L".localhost/*";
                    w->web->AddWebResourceRequestedFilter(filter.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                }
                w->web->add_WebResourceRequested(
                    new Handler<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *>(
                        IID_ICoreWebView2WebResourceRequestedEventHandler,
                        [id](ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *args) -> HRESULT {
                            on_resource(id, args);
                            return S_OK;
                        }),
                    &token);
                w->web->add_DocumentTitleChanged(
                    new Handler<ICoreWebView2DocumentTitleChangedEventHandler, ICoreWebView2 *, IUnknown *>(
                        IID_ICoreWebView2DocumentTitleChangedEventHandler,
                        [id](ICoreWebView2 *sender, IUnknown *) -> HRESULT {
                            UiwWindow *w = find(id);
                            LPWSTR title = NULL;
                            if (w && w->follow_title && SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                                if (title[0]) SetWindowTextW(w->hwnd, title);
                                CoTaskMemFree(title);
                            }
                            return S_OK;
                        }),
                    &token);
                // The page stays in the app: a link elsewhere opens in the browser, so no other
                // site gets the bridge.
                w->web->add_NavigationStarting(
                    new Handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *>(
                        IID_ICoreWebView2NavigationStartingEventHandler,
                        [id](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                            UiwWindow *w = find(id);
                            LPWSTR uri = NULL;
                            if (w && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                if (!inside(w, narrow(uri))) {
                                    ShellExecuteW(NULL, L"open", uri, NULL, NULL, SW_SHOWNORMAL);
                                    args->put_Cancel(TRUE);
                                }
                                CoTaskMemFree(uri);
                            }
                            return S_OK;
                        }),
                    &token);
                // A link that opens a new window (target="_blank", window.open) goes to the browser.
                w->web->add_NewWindowRequested(
                    new Handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *>(
                        IID_ICoreWebView2NewWindowRequestedEventHandler,
                        [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                            LPWSTR uri = NULL;
                            if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                ShellExecuteW(NULL, L"open", uri, NULL, NULL, SW_SHOWNORMAL);
                                CoTaskMemFree(uri);
                            }
                            args->put_Handled(TRUE);
                            return S_OK;
                        }),
                    &token);
                // F11 full screen; with the developer tools, F5 and Ctrl+R reload (F12 is WebView2's).
                controller->add_AcceleratorKeyPressed(
                    new Handler<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller *, ICoreWebView2AcceleratorKeyPressedEventArgs *>(
                        IID_ICoreWebView2AcceleratorKeyPressedEventHandler,
                        [id](ICoreWebView2Controller *, ICoreWebView2AcceleratorKeyPressedEventArgs *args) -> HRESULT {
                            UiwWindow *w = find(id);
                            COREWEBVIEW2_KEY_EVENT_KIND kind;
                            UINT key = 0;
                            if (!w || FAILED(args->get_KeyEventKind(&kind)) || kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) return S_OK;
                            args->get_VirtualKey(&key);
                            bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                            if (key == VK_F11) {
                                set_fullscreen(w, !w->fullscreen);
                                args->put_Handled(TRUE);
                            } else if (w->devtools && (key == VK_F5 || (control && key == 'R'))) {
                                w->web->Reload();
                                args->put_Handled(TRUE);
                            }
                            return S_OK;
                        }),
                    &token);
                w->web->Navigate(w->url.c_str());
                return S_OK;
            }));
    if (FAILED(hr)) report(w->hwnd, L"WebView2 could not create its view", hr);
}

static void start_environment(void) {
    if (g_environment_started) return;
    g_environment_started = true;
    g_loader = LoadLibraryW(L"WebView2Loader.dll");
    CreateEnvironmentFn create =
        g_loader ? (CreateEnvironmentFn)GetProcAddress(g_loader, "CreateCoreWebView2EnvironmentWithOptions") : NULL;
    if (!create) {
        MessageBoxW(NULL, L"WebView2Loader.dll is missing next to this program. Build or package it with `ui`, which puts it there.",
                    g_app_name.c_str(), MB_ICONERROR | MB_OK);
        ExitProcess(1);
    }
    // WebView2 keeps its data in %LOCALAPPDATA%\NAME, writable when the program is installed
    // under Program Files.
    wchar_t data[MAX_PATH] = {0};
    PWSTR local = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &local)) && local) {
        PathCombineW(data, local, g_app_name.c_str());
        CoTaskMemFree(local);
    }
    HRESULT hr = create(NULL, data[0] ? data : NULL, NULL,
        new Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment *>(
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
            [](HRESULT error, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(error) || !environment) {
                    report(NULL, L"WebView2 could not start", error);
                    return S_OK;
                }
                g_environment = environment;
                environment->AddRef();
                for (UiwWindow *w : g_waiting)
                    if (find(w->id)) attach(w);
                g_waiting.clear();
                return S_OK;
            }));
    if (FAILED(hr)) report(NULL, L"WebView2 could not start", hr);
}

// ---- Window procedures ----

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    UiwWindow *w = (UiwWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_SIZE:
        if (w) fit(w);
        return 0;
    case WM_GETMINMAXINFO:
        if (w && (w->min_width > 0 || w->min_height > 0)) {
            UINT dpi = GetDpiForWindow(hwnd);
            RECT r = {0, 0, MulDiv(w->min_width, dpi, 96), MulDiv(w->min_height, dpi, 96)};
            AdjustWindowRectExForDpi(&r, GetWindowLongW(hwnd, GWL_STYLE), FALSE, 0, dpi);
            MINMAXINFO *info = (MINMAXINFO *)lparam;
            info->ptMinTrackSize.x = r.right - r.left;
            info->ptMinTrackSize.y = r.bottom - r.top;
        }
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lparam;
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        if (w && uiw_on_close(w->id) != 1) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (w) {
            if (w->web) w->web->Release();
            if (w->controller) {
                w->controller->Close();
                w->controller->Release();
            }
            g_windows.erase(w->id);
            uiw_on_closed(w->id);
            delete w;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (g_windows.empty()) PostQuitMessage(g_exit_code);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

struct UiwEval {
    int window;
    std::wstring script;
};

static LRESULT CALLBACK messages_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_UIW_EVAL) {
        UiwEval *e = (UiwEval *)lparam;
        for (auto &entry : g_windows) {
            if ((e->window == 0 || entry.first == e->window) && entry.second->web)
                entry.second->web->ExecuteScript(e->script.c_str(), NULL);
        }
        delete e;
        return 0;
    }
    if (message == WM_UIW_QUIT) {
        g_exit_code = (int)wparam;
        PostQuitMessage((int)wparam);
        return 0;
    }
    if (message == WM_UIW_CLOSE) {
        UiwWindow *w = find((int)wparam);
        if (w) DestroyWindow(w->hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// ---- What the program calls ----

extern "C" {

void uiw_init(const char *app_name, const char *bridge) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (app_name && app_name[0]) g_app_name = widen(app_name);
    g_bridge = widen(bridge);
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW window_class = {sizeof(WNDCLASSEXW)};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = L"EskiuWebappWindow";
    RegisterClassExW(&window_class);
    WNDCLASSEXW messages_class = {sizeof(WNDCLASSEXW)};
    messages_class.lpfnWndProc = messages_proc;
    messages_class.hInstance = instance;
    messages_class.lpszClassName = L"EskiuWebappMessages";
    RegisterClassExW(&messages_class);
    g_messages = CreateWindowExW(0, L"EskiuWebappMessages", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, instance, NULL);
}

const char *uiw_platform(void) { return "windows"; }

char *uiw_resource_dir(void) {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return _strdup(narrow(path).c_str());
}

void uiw_scheme(const char *name) {
    for (const std::string &scheme : g_schemes)
        if (scheme == name) return;
    g_schemes.push_back(name);
}

int uiw_window_open(const char *title, const char *url, int width, int height, int min_width, int min_height, int flags) {
    UiwWindow *w = new UiwWindow();
    w->id = ++g_next_window;
    w->url = widen(url);
    w->origin = origin_of(url ? url : "");
    w->follow_title = !(title && title[0]);
    w->devtools = (flags & 4) != 0;
    w->min_width = min_width;
    w->min_height = min_height;
    UINT dpi = GetDpiForSystem();
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!(flags & 1)) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT r = {0, 0, MulDiv(width, dpi, 96), MulDiv(height, dpi, 96)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    int outer_width = r.right - r.left, outer_height = r.bottom - r.top;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (g_windows.empty()) {
        x = (GetSystemMetrics(SM_CXSCREEN) - outer_width) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - outer_height) / 2;
    }
    std::wstring caption = w->follow_title ? g_app_name : widen(title);
    w->hwnd = CreateWindowExW(0, L"EskiuWebappWindow", caption.c_str(), style, x, y, outer_width, outer_height, NULL, NULL,
                              GetModuleHandleW(NULL), NULL);
    if (!w->hwnd) {
        delete w;
        return 0;
    }
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w);
    wchar_t icon_path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, icon_path, MAX_PATH);
    PathRemoveFileSpecW(icon_path);
    PathAppendW(icon_path, L"icon.ico");
    HICON icon = (HICON)LoadImageW(NULL, icon_path, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (icon) {
        SendMessageW(w->hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(w->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
    g_windows[w->id] = w;
    ShowWindow(w->hwnd, SW_SHOW);
    UpdateWindow(w->hwnd);
    if (flags & 2) set_fullscreen(w, true);
    if (g_environment) attach(w);
    else {
        g_waiting.push_back(w);
        start_environment();
    }
    return w->id;
}

void uiw_window_title(int window, const char *title) {
    UiwWindow *w = find(window);
    if (!w) return;
    w->follow_title = false;
    SetWindowTextW(w->hwnd, title && title[0] ? widen(title).c_str() : g_app_name.c_str());
}

void uiw_window_size(int window, int width, int height) {
    UiwWindow *w = find(window);
    if (!w || width <= 0 || height <= 0) return;
    UINT dpi = GetDpiForWindow(w->hwnd);
    RECT r = {0, 0, MulDiv(width, dpi, 96), MulDiv(height, dpi, 96)};
    AdjustWindowRectExForDpi(&r, GetWindowLongW(w->hwnd, GWL_STYLE), FALSE, 0, dpi);
    SetWindowPos(w->hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

void uiw_window_min_size(int window, int width, int height) {
    UiwWindow *w = find(window);
    if (!w) return;
    w->min_width = width;
    w->min_height = height;
}

void uiw_window_fullscreen(int window, int on) {
    UiwWindow *w = find(window);
    if (w) set_fullscreen(w, on != 0);
}

int uiw_window_is_fullscreen(int window) {
    UiwWindow *w = find(window);
    return w && w->fullscreen ? 1 : 0;
}

void uiw_window_focus(int window) {
    UiwWindow *w = find(window);
    if (!w) return;
    ShowWindow(w->hwnd, SW_RESTORE);
    SetForegroundWindow(w->hwnd);
}

void uiw_window_devtools(int window) {
    UiwWindow *w = find(window);
    if (w && w->web && w->devtools) w->web->OpenDevToolsWindow();
}

// After the page's message handler returns: the web view goes with the window.
void uiw_window_close(int window) { PostMessageW(g_messages, WM_UIW_CLOSE, (WPARAM)window, 0); }

int uiw_run(void) {
    if (g_windows.empty()) return g_exit_code;
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // After webapp_quit the windows are still there: their web views close before the process
    // ends.
    std::vector<HWND> open;
    for (auto &entry : g_windows) open.push_back(entry.second->hwnd);
    for (HWND hwnd : open) DestroyWindow(hwnd);
    if (g_environment) {
        g_environment->Release();
        g_environment = NULL;
    }
    return g_exit_code;
}

void uiw_quit(int code) { PostMessageW(g_messages, WM_UIW_QUIT, (WPARAM)code, 0); }

// From any thread: the script runs from the message loop, in order.
void uiw_eval(int window, const char *script) {
    UiwEval *e = new UiwEval{window, widen(script)};
    if (!PostMessageW(g_messages, WM_UIW_EVAL, 0, (LPARAM)e)) delete e;
}

static const wchar_t *reason(int status) {
    switch (status) {
    case 200: return L"OK";
    case 204: return L"No Content";
    case 206: return L"Partial Content";
    case 403: return L"Forbidden";
    case 404: return L"Not Found";
    case 416: return L"Range Not Satisfiable";
    default: return L"Status";
    }
}

void uiw_respond(int64_t request, int status, const char *headers, const char *file, int64_t offset, int64_t length,
                 const uint8_t *data) {
    auto it = g_requests.find(request);
    if (it == g_requests.end() || !g_environment) return;
    ICoreWebView2WebResourceRequestedEventArgs *args = it->second;
    std::vector<uint8_t> body;
    if (file && length > 0) {
        HANDLE handle = CreateFileW(widen(file).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER at;
            at.QuadPart = offset;
            body.resize((size_t)length);
            DWORD got = 0;
            size_t done = 0;
            if (SetFilePointerEx(handle, at, NULL, FILE_BEGIN)) {
                while (done < body.size() && ReadFile(handle, body.data() + done, (DWORD)std::min((size_t)1 << 30, body.size() - done), &got, NULL) && got > 0)
                    done += got;
            }
            body.resize(done);
            CloseHandle(handle);
        }
    } else if (data && length > 0) {
        body.assign(data, data + length);
    }
    std::wstring header_text = widen(headers);
    std::wstring crlf;
    for (wchar_t c : header_text) {
        if (c == L'\n') crlf += L"\r\n";
        else crlf += c;
    }
    IStream *stream = SHCreateMemStream(body.empty() ? NULL : body.data(), (UINT)body.size());
    ICoreWebView2WebResourceResponse *response = NULL;
    if (SUCCEEDED(g_environment->CreateWebResourceResponse(stream, status, reason(status), crlf.c_str(), &response)) && response) {
        args->put_Response(response);
        response->Release();
    }
    if (stream) stream->Release();
    args->Release();
    g_requests.erase(it);
}

static HWND parent_of(int window) {
    UiwWindow *w = find(window);
    return w ? w->hwnd : NULL;
}

char *uiw_dialog_file(int window, int flags, const char *title, const char *default_path, const char *filters) {
    IFileDialog *dialog = NULL;
    bool save = (flags & 4) != 0;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                save ? IID_IFileSaveDialog : IID_IFileOpenDialog, (void **)&dialog)))
        return NULL;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM;
    if (flags & 1) options |= FOS_ALLOWMULTISELECT;
    if (flags & 2) options |= FOS_PICKFOLDERS;
    if (save) options |= FOS_OVERWRITEPROMPT;
    dialog->SetOptions(options);
    if (title && title[0]) dialog->SetTitle(widen(title).c_str());
    // The filters: a name and its patterns ("*.png;*.jpg") each.
    std::vector<std::wstring> names, patterns;
    std::string all = filters ? filters : "";
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find('\n', at);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(at, end - at);
        at = end + 1;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::wstring pattern;
        std::string extensions = line.substr(tab + 1);
        size_t from = 0;
        while (from <= extensions.size()) {
            size_t comma = extensions.find(',', from);
            if (comma == std::string::npos) comma = extensions.size();
            std::string extension = extensions.substr(from, comma - from);
            if (!extension.empty()) {
                if (!pattern.empty()) pattern += L";";
                pattern += extension == "*" ? L"*.*" : L"*." + widen(extension.c_str());
            }
            from = comma + 1;
        }
        names.push_back(tab ? widen(line.substr(0, tab).c_str()) : pattern);
        patterns.push_back(pattern);
    }
    std::vector<COMDLG_FILTERSPEC> specs;
    for (size_t i = 0; i < names.size(); i++) specs.push_back({names[i].c_str(), patterns[i].c_str()});
    if (!specs.empty() && !(flags & 2)) dialog->SetFileTypes((UINT)specs.size(), specs.data());
    if (default_path && default_path[0]) {
        std::wstring path = widen(default_path);
        for (wchar_t &c : path)
            if (c == L'/') c = L'\\';
        IShellItem *folder = NULL;
        if (PathIsDirectoryW(path.c_str())) {
            if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), NULL, IID_IShellItem, (void **)&folder))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        } else {
            std::wstring dir = path;
            PathRemoveFileSpecW(&dir[0]);
            dir.resize(wcslen(dir.c_str()));
            if (!dir.empty() && SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), NULL, IID_IShellItem, (void **)&folder))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
            if (save) dialog->SetFileName(PathFindFileNameW(path.c_str()));
        }
    }
    std::string out;
    if (SUCCEEDED(dialog->Show(parent_of(window)))) {
        if (save || !(flags & 1)) {
            IShellItem *item = NULL;
            if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                LPWSTR path = NULL;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    out = narrow(path) + "\n";
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        } else {
            IShellItemArray *items = NULL;
            if (SUCCEEDED(((IFileOpenDialog *)dialog)->GetResults(&items)) && items) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; i++) {
                    IShellItem *item = NULL;
                    LPWSTR path = NULL;
                    if (SUCCEEDED(items->GetItemAt(i, &item)) && item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                        out += narrow(path) + "\n";
                        CoTaskMemFree(path);
                    }
                    if (item) item->Release();
                }
                items->Release();
            }
        }
    }
    dialog->Release();
    return out.empty() ? NULL : _strdup(out.c_str());
}

int uiw_dialog_message(int window, int buttons, int level, const char *title, const char *message, const char *ok,
                       const char *cancel) {
    std::wstring heading = widen(title), text = widen(message), ok_label = widen(ok && ok[0] ? ok : "OK"),
                 cancel_label = widen(cancel && cancel[0] ? cancel : "Cancel");
    TASKDIALOG_BUTTON choices[2] = {{IDOK, ok_label.c_str()}, {IDCANCEL, cancel_label.c_str()}};
    TASKDIALOGCONFIG config = {sizeof(TASKDIALOGCONFIG)};
    config.hwndParent = parent_of(window);
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | (buttons == 1 ? TDF_ALLOW_DIALOG_CANCELLATION : 0);
    config.pszWindowTitle = g_app_name.c_str();
    config.pszMainIcon = level == 2 ? TD_ERROR_ICON : level == 1 ? TD_WARNING_ICON : TD_INFORMATION_ICON;
    config.pszMainInstruction = heading.empty() ? text.c_str() : heading.c_str();
    config.pszContent = heading.empty() ? NULL : text.c_str();
    config.pButtons = choices;
    config.cButtons = buttons == 1 ? 2 : 1;
    config.nDefaultButton = IDOK;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&config, &pressed, NULL, NULL))) {
        UINT type = (buttons == 1 ? MB_OKCANCEL : MB_OK) | (level == 2 ? MB_ICONERROR : level == 1 ? MB_ICONWARNING : MB_ICONINFORMATION);
        pressed = MessageBoxW(config.hwndParent, text.c_str(), heading.empty() ? g_app_name.c_str() : heading.c_str(), type);
    }
    return pressed == IDOK ? 1 : 0;
}

}
