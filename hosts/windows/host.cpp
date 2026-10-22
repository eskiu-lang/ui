#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include) && __has_include(<WebView2.h>)
#include <wrl.h>
#include <WebView2.h>
#else
// Self-contained COM declarations for WebView2 so compilation works on MinGW / MSVC without NuGet.
#include <unknwn.h>

typedef struct EventRegistrationToken {
    __int64 value;
} EventRegistrationToken;

typedef enum COREWEBVIEW2_MOVE_FOCUS_REASON {
    COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC = 0
} COREWEBVIEW2_MOVE_FOCUS_REASON;

MIDL_INTERFACE("E562E4F0-D7FA-43AC-8D71-C05150499F00")
ICoreWebView2Settings : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL *isScriptEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL isScriptEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL *isWebMessageEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL isWebMessageEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL *areDefaultScriptDialogsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL areDefaultScriptDialogsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL *isStatusBarEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL isStatusBarEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL *areDevToolsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL areDevToolsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL *enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL enabled) = 0;
};

MIDL_INTERFACE("67AFD2A2-C029-450F-AD10-38A2D3C4560D")
ICoreWebView2 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings **settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR *uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE NavigateToString(LPCWSTR htmlContent) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationStarting(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationStarting(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SourceChanged(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SourceChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_HistoryChanged(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_HistoryChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationCompleted(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationCompleted(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationStarting(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationCompleted(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationCompleted(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ScriptDialogOpening(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ScriptDialogOpening(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PermissionRequested(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PermissionRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ProcessFailed(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ProcessFailed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(LPCWSTR javaScript, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveScriptToExecuteOnDocumentCreated(LPCWSTR id) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExecuteScript(LPCWSTR javaScript, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CapturePreview(int imageFormat, void *imageStream, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reload(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsJson(LPCWSTR webMessageAsJson) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsString(LPCWSTR webMessageAsString) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_WebMessageReceived(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_WebMessageReceived(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CallDevToolsProtocolMethod(LPCWSTR methodName, LPCWSTR parametersAsJson, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BrowserProcessId(UINT32 *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanGoBack(BOOL *canGoBack) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanGoForward(BOOL *canGoForward) = 0;
    virtual HRESULT STDMETHODCALLTYPE GoBack(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE GoForward(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDevToolsProtocolEventReceiver(LPCWSTR eventName, void **receiver) = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NewWindowRequested(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NewWindowRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_DocumentTitleChanged(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_DocumentTitleChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DocumentTitle(LPWSTR *title) = 0;
};

MIDL_INTERFACE("4D00C0D4-0FB9-40CA-9F88-073D557D93D3")
ICoreWebView2Controller : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL *isVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL isVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Bounds(RECT *bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ZoomFactor(double *zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ZoomFactor(double zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT bounds, double zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON reason) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_GotFocus(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_LostFocus(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND *topLevelWindow) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND topLevelWindow) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2 **coreWebView2) = 0;
};

MIDL_INTERFACE("B96D755E-0319-4E92-A296-23436F46A1FC")
ICoreWebView2Environment : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND parentWindow, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(void *content, int statusCode, LPCWSTR reasonPhrase, LPCWSTR headers, void **response) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR *versionInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddNewBrowserVersionAvailable(void *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveNewBrowserVersionAvailable(EventRegistrationToken token) = 0;
};

MIDL_INTERFACE("4E8A3389-A788-4CD0-BB42-E604A60B3F16")
ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment *created_environment) = 0;
};

MIDL_INTERFACE("6C4819F3-C9B7-4260-8127-CEDE4F1595F3")
ICoreWebView2CreateCoreWebView2ControllerCompletedHandler : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller *created_controller) = 0;
};

MIDL_INTERFACE("F5F2B487-CF04-453B-9B21-8E40788390FA")
ICoreWebView2DocumentTitleChangedEventHandler : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *args) = 0;
};
#endif

typedef HRESULT (STDAPICALLTYPE *CreateCoreWebView2EnvironmentWithOptionsFn)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    IUnknown *environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *environmentCreatedHandler);

static HWND g_hwnd = NULL;
static ICoreWebView2Controller *g_controller = NULL;
static ICoreWebView2 *g_webview = NULL;
static WCHAR g_target_url[2048] = {0};

class TitleChangeHandler : public ICoreWebView2DocumentTitleChangedEventHandler {
    LONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2DocumentTitleChangedEventHandler *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *args) override {
        LPWSTR title = NULL;
        if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
            SetWindowTextW(g_hwnd, title);
            CoTaskMemFree(title);
        }
        return S_OK;
    }
};

class ControllerCreatedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    LONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller *controller) override {
        if (FAILED(errorCode) || !controller) return errorCode;
        g_controller = controller;
        g_controller->AddRef();

        RECT bounds;
        GetClientRect(g_hwnd, &bounds);
        g_controller->put_Bounds(bounds);
        g_controller->put_IsVisible(TRUE);

        if (SUCCEEDED(g_controller->get_CoreWebView2(&g_webview)) && g_webview) {
            ICoreWebView2Settings *settings = NULL;
            if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
                settings->put_AreDevToolsEnabled(TRUE);
                settings->put_AreDefaultContextMenusEnabled(TRUE);
                settings->Release();
            }

            EventRegistrationToken token;
            g_webview->add_DocumentTitleChanged(new TitleChangeHandler(), &token);
            g_webview->Navigate(g_target_url);
        }
        return S_OK;
    }
};

class EnvironmentCreatedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    LONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment *environment) override {
        if (FAILED(errorCode) || !environment) return errorCode;
        environment->CreateCoreWebView2Controller(g_hwnd, new ControllerCreatedHandler());
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
        case WM_KEYDOWN:
            if (wParam == VK_F5 || (wParam == 'R' && (GetKeyState(VK_CONTROL) & 0x8000))) {
                if (g_webview) g_webview->Reload();
                return 0;
            }
            if (wParam == 'W' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (g_controller) {
                g_controller->Close();
                g_controller->Release();
                g_controller = NULL;
            }
            if (g_webview) {
                g_webview->Release();
                g_webview = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    LPCWSTR url = (argc > 1 && argv[1][0]) ? argv[1] : L"http://127.0.0.1:8765/?example=counter";
    wcsncpy(g_target_url, url, 2047);

    int width = 1100, height = 860;
    if (argc > 2) {
        swscanf(argv[2], L"%dx%d", &width, &height);
    }
    if (argv) LocalFree(argv);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"EskiuDesktopWindow";
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - width) / 2;
    int posY = (screenH - height) / 2;

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Eskiu UI · Windows / WebView2",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             posX, posY, width, height, NULL, NULL, hInstance, NULL);

    HICON hIcon = (HICON)LoadImageW(NULL, L"icon.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
    if (hIcon) {
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    HMODULE hLoader = LoadLibraryW(L"WebView2Loader.dll");
    CreateCoreWebView2EnvironmentWithOptionsFn createEnv = NULL;
    if (hLoader) {
        createEnv = (CreateCoreWebView2EnvironmentWithOptionsFn)GetProcAddress(hLoader, "CreateCoreWebView2EnvironmentWithOptions");
    }

    if (createEnv) {
        createEnv(NULL, NULL, NULL, new EnvironmentCreatedHandler());
    } else {
        MessageBoxW(g_hwnd,
                    L"Failed to load WebView2Loader.dll. Please ensure the Microsoft Edge WebView2 runtime is installed.",
                    L"Eskiu UI Error", MB_ICONERROR | MB_OK);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hLoader) FreeLibrary(hLoader);
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv) {
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}
