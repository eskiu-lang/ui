// host_requests.inc: the core's requests (commands, timers, frames, the clipboard, fetch) and the system's settings. Part of host.cpp, which #includes it (one translation unit).

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
    ui_report_accessibility(reader ? 1 : 0, animations ? 0 : 1, (int)std::lround(textScale * 100));
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
