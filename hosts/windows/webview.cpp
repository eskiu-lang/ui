// The Windows desktop host: the web build in a WebView2 window. The tool compiles it with
// the WebView2 SDK's header (its NuGet package, which tools/ui/windows.esk fetches) and puts
// WebView2Loader.dll next to the executable. With no argument it shows the app bundled in
// web/ beside the executable, served as https://app.assets/; an argument is a URL to open
// instead (ui serve's). UI_RELEASE builds turn the developer tools off.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <WebView2.h>

typedef HRESULT(STDAPICALLTYPE* CreateEnvironmentFn)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder, ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

static HWND g_hwnd = NULL;
static ICoreWebView2Controller* g_controller = NULL;
static ICoreWebView2* g_webview = NULL;
static WCHAR g_target_url[2048] = {0};
static WCHAR g_local_web_dir[MAX_PATH] = {0};
static WCHAR g_name[MAX_PATH] = L"app";

// A failure the user can act on, instead of a blank window.
static void report(const wchar_t* what, HRESULT hr) {
    WCHAR text[512];
    swprintf(text, 512, L"%ls (error 0x%08lX).\n\nInstall or repair the Microsoft Edge WebView2 Runtime "
             L"(https://go.microsoft.com/fwlink/p/?LinkId=2124703) if this persists.", what, (unsigned long)hr);
    MessageBoxW(g_hwnd, text, g_name, MB_ICONERROR | MB_OK);
}

// One COM handler: answers QueryInterface for IUnknown and its own interface only, frees
// itself on its last Release.
template <class I>
class Handler : public I {
    LONG refs = 1;
    const IID& iid;

protected:
    explicit Handler(const IID& id) : iid(id) {}
    virtual ~Handler() {}

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid)) {
            *ppv = static_cast<I*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&refs);
        if (count == 0) delete this;
        return count;
    }
};

// The page's title on the window.
class TitleChanged : public Handler<ICoreWebView2DocumentTitleChangedEventHandler> {
public:
    TitleChanged() : Handler(IID_ICoreWebView2DocumentTitleChangedEventHandler) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown*) override {
        LPWSTR title = NULL;
        if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
            if (title[0]) SetWindowTextW(g_hwnd, title);
            CoTaskMemFree(title);
        }
        return S_OK;
    }
};

// F5 and Ctrl+R reload, Ctrl+W closes: the keys reach WebView2's own window, not ours.
class AcceleratorKey : public Handler<ICoreWebView2AcceleratorKeyPressedEventHandler> {
public:
    AcceleratorKey() : Handler(IID_ICoreWebView2AcceleratorKeyPressedEventHandler) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        COREWEBVIEW2_KEY_EVENT_KIND kind;
        UINT key = 0;
        if (FAILED(args->get_KeyEventKind(&kind)) || kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) return S_OK;
        args->get_VirtualKey(&key);
        bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (key == VK_F5 || (control && key == 'R')) {
            if (g_webview) g_webview->Reload();
            args->put_Handled(TRUE);
        } else if (control && key == 'W') {
            PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            args->put_Handled(TRUE);
        }
        return S_OK;
    }
};

class ControllerCreated : public Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
public:
    ControllerCreated() : Handler(IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller* controller) override {
        if (FAILED(errorCode) || !controller) {
            report(L"WebView2 could not create its view", errorCode);
            return S_OK;
        }
        g_controller = controller;
        g_controller->AddRef();
        RECT bounds;
        GetClientRect(g_hwnd, &bounds);
        g_controller->put_Bounds(bounds);
        g_controller->put_IsVisible(TRUE);
        EventRegistrationToken token;
        AcceleratorKey* keys = new AcceleratorKey();
        g_controller->add_AcceleratorKeyPressed(keys, &token);
        keys->Release();
        if (FAILED(g_controller->get_CoreWebView2(&g_webview)) || !g_webview) {
            report(L"WebView2 gave no web view", E_FAIL);
            return S_OK;
        }
        ICoreWebView2Settings* settings = NULL;
        if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
#ifdef UI_RELEASE
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
#else
            settings->put_AreDevToolsEnabled(TRUE);
#endif
            settings->Release();
        }
        TitleChanged* title = new TitleChanged();
        g_webview->add_DocumentTitleChanged(title, &token);
        title->Release();
        if (g_local_web_dir[0] != 0) {
            ICoreWebView2_3* wv3 = NULL;
            HRESULT hr = g_webview->QueryInterface(IID_ICoreWebView2_3, (void**)&wv3);
            if (FAILED(hr) || !wv3) {
                report(L"This WebView2 Runtime is too old to show the bundled app", hr);
                return S_OK;
            }
            wv3->SetVirtualHostNameToFolderMapping(L"app.assets", g_local_web_dir,
                                                  COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
            wv3->Release();
        }
        g_webview->Navigate(g_target_url);
        return S_OK;
    }
};

class EnvironmentCreated : public Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
public:
    EnvironmentCreated() : Handler(IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment* environment) override {
        if (FAILED(errorCode) || !environment) {
            report(L"WebView2 could not start", errorCode);
            return S_OK;
        }
        ControllerCreated* created = new ControllerCreated();
        HRESULT hr = environment->CreateCoreWebView2Controller(g_hwnd, created);
        created->Release();
        if (FAILED(hr)) report(L"WebView2 could not create its view", hr);
        return S_OK;
    }
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (g_controller) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                g_controller->put_Bounds(bounds);
            }
            return 0;
        case WM_DESTROY:
            if (g_webview) {
                g_webview->Release();
                g_webview = NULL;
            }
            if (g_controller) {
                g_controller->Close();
                g_controller->Release();
                g_controller = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    WCHAR exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    WCHAR exeDir[MAX_PATH];
    wcsncpy(exeDir, exePath, MAX_PATH);
    PathRemoveFileSpecW(exeDir);
    // The app's name, from the executable's: the window's first title and its data folder.
    wcsncpy(g_name, PathFindFileNameW(exePath), MAX_PATH - 1);
    PathRemoveExtensionW(g_name);

    WCHAR webDir[MAX_PATH] = {0};
    PathCombineW(webDir, exeDir, L"web");
    WCHAR indexPath[MAX_PATH] = {0};
    PathCombineW(indexPath, webDir, L"index.html");
    if (argc > 1 && argv[1][0]) {
        wcsncpy(g_target_url, argv[1], 2047);
    } else if (PathFileExistsW(indexPath)) {
        // The app's root route; the page is index.html.
        wcsncpy(g_local_web_dir, webDir, MAX_PATH - 1);
        wcsncpy(g_target_url, L"https://app.assets/", 2047);
    } else {
        MessageBoxW(NULL, L"There is no web/index.html next to this program. Build it with `ui build windows`.",
                    g_name, MB_ICONERROR | MB_OK);
        return 1;
    }
    int width = 1100, height = 860;
    if (argc > 2) swscanf(argv[2], L"%dx%d", &width, &height);
    if (argv) LocalFree(argv);

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"EskiuUiWindow";
    RegisterClassExW(&wc);
    int posX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, g_name, WS_OVERLAPPEDWINDOW | WS_VISIBLE, posX, posY, width,
                             height, NULL, NULL, hInstance, NULL);

    WCHAR iconPath[MAX_PATH] = {0};
    PathCombineW(iconPath, exeDir, L"icon.ico");
    HICON icon = (HICON)LoadImageW(NULL, iconPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (icon) {
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }

    // WebView2 keeps its data in %LOCALAPPDATA%\NAME, which is writable when the program is
    // installed under Program Files (its default, beside the executable, is not).
    WCHAR dataDir[MAX_PATH] = {0};
    PWSTR local = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &local)) && local) {
        PathCombineW(dataDir, local, g_name);
        CoTaskMemFree(local);
    }

    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    CreateEnvironmentFn createEnv =
        loader ? (CreateEnvironmentFn)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions") : NULL;
    if (!createEnv) {
        MessageBoxW(g_hwnd, L"WebView2Loader.dll is missing next to this program. Build or package it with `ui`, "
                    L"which puts it there.", g_name, MB_ICONERROR | MB_OK);
        return 1;
    }
    EnvironmentCreated* created = new EnvironmentCreated();
    HRESULT hr = createEnv(NULL, dataDir[0] ? dataDir : NULL, NULL, created);
    created->Release();
    if (FAILED(hr)) report(L"WebView2 could not start", hr);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    FreeLibrary(loader);
    CoUninitialize();
    return 0;
}
