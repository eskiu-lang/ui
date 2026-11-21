// host_access.inc: accessibility (Style's role and states, disabled), a text input's editing
// options (secure, keyboard, multiline, autocorrect, submit) and the system's text size. Part
// of host.cpp, which #includes it (one translation unit).

static void applyFont(Node *node);

// ---- accessibility ----

// Roles and states go through MSAA's dynamic annotation (IAccPropServices), which UI
// Automation and Narrator read through the MSAA proxy.

// The window that carries a node's role, states and name: a field's EDIT, else its window.
static HWND accessibleOf(Node *node) { return node->kind == 5 && node->inner ? node->inner : node->hwnd; }

// Role (prop 57) as MSAA's. MSAA has no heading (3: static text), switch (8: a check button)
// or search landmark (18: a grouping): UI Automation's properties say those (uiaRole).
static const int kRoles[] = {0, ROLE_SYSTEM_PUSHBUTTON, ROLE_SYSTEM_LINK, ROLE_SYSTEM_STATICTEXT, ROLE_SYSTEM_GRAPHIC,
                             ROLE_SYSTEM_CHECKBUTTON, ROLE_SYSTEM_RADIOBUTTON, ROLE_SYSTEM_GROUPING, ROLE_SYSTEM_CHECKBUTTON,
                             ROLE_SYSTEM_PAGETAB, ROLE_SYSTEM_PAGETABLIST, ROLE_SYSTEM_SLIDER, ROLE_SYSTEM_PROGRESSBAR,
                             ROLE_SYSTEM_MENUPOPUP, ROLE_SYSTEM_MENUITEM, ROLE_SYSTEM_LIST, ROLE_SYSTEM_LISTITEM, ROLE_SYSTEM_ALERT,
                             ROLE_SYSTEM_GROUPING};

// The role a node's kind gives it (0: the control's own).
static int kindRole(Node *node) {
    switch (node->kind) {
        case 7: return ROLE_SYSTEM_GRAPHIC;
        case 8: return ROLE_SYSTEM_PUSHBUTTON;
        case 9: return ROLE_SYSTEM_CHECKBUTTON;
        case 10: return ROLE_SYSTEM_PROGRESSBAR;
        case 12: return ROLE_SYSTEM_DIALOG;
        case 1: case 2: case 13: case 14: return node->label.empty() ? 0 : ROLE_SYSTEM_GROUPING;
        default: return 0;
    }
}

static void clearAccessible(HWND hwnd, const MSAAPROPID &prop) {
    if (!accProps || !hwnd) return;
    MSAAPROPID ids[] = {prop};
    accProps->ClearHwndProps(hwnd, OBJID_CLIENT, CHILDID_SELF, ids, 1);
}
static void accessibleText(HWND hwnd, const MSAAPROPID &prop, const std::wstring &text) {
    if (!accProps || !hwnd) return;
    if (text.empty()) clearAccessible(hwnd, prop);
    else accProps->SetHwndPropStr(hwnd, OBJID_CLIENT, CHILDID_SELF, prop, text.c_str());
}

// UI Automation's own properties, annotated as MSAA's are (Windows 10 1803 and later read
// them): the localized control type a screen reader speaks, a heading's level, a landmark.
static const GUID kLocalizedControlType = {0x8763404f, 0xa1bd, 0x452a, {0x89, 0xc4, 0x3f, 0x01, 0xd3, 0x83, 0x38, 0x06}};
static const GUID kHeadingLevel = {0x29084272, 0xaaaf, 0x4a30, {0x87, 0x96, 0x3c, 0x12, 0xf6, 0x2b, 0x6b, 0xbb}};
static const GUID kLandmarkType = {0x454045f2, 0x6f61, 0x49f7, {0xa4, 0xf8, 0xb5, 0xf0, 0xcf, 0x82, 0xda, 0x1e}};
static const GUID kAriaRole = {0xdd207b95, 0xbe4a, 0x4e0d, {0xb7, 0x27, 0x63, 0xac, 0xe9, 0x4b, 0x69, 0x16}};
static const int kHeadingLevel2 = 80052, kSearchLandmark = 80004;

static void uiaInt(HWND hwnd, const GUID &prop, int value) {
    if (!accProps || !hwnd) return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = value;
    accProps->SetHwndProp(hwnd, OBJID_CLIENT, CHILDID_SELF, prop, v);
}

// The words a screen reader says that Windows has no translation of (command 13, UiWords:
// mixed, expanded, collapsed, menu, menu item, alert, search, link), English until the app
// gives its own. MSAA's roles and states are said in the system's language, so only search,
// a landmark's localized type, is the app's.
static std::wstring words[8] = {L"mixed", L"expanded", L"collapsed", L"menu", L"menu item", L"alert", L"search", L"link"};

// A heading (UI Automation's heading level, which a screen reader says in its own language), a
// switch and a search region as UI Automation says them (a switch as its own kind 9 is one
// too). UI Automation has no switch type: its localized type is "toggle switch", as the
// Windows 11 toggle's.
static void uiaRole(Node *node, HWND target) {
    int role = node->role ? node->role : node->kind == 9 ? 8 : 0;
    const wchar_t *spoken = role == 8 ? L"toggle switch" : role == 18 ? words[6].c_str() : nullptr;
    const wchar_t *aria = role == 3 ? L"heading" : role == 8 ? L"switch" : role == 18 ? L"search" : nullptr;
    accessibleText(target, kLocalizedControlType, spoken ? spoken : L"");
    accessibleText(target, kAriaRole, aria ? aria : L"");
    if (role == 3) uiaInt(target, kHeadingLevel, kHeadingLevel2); else clearAccessible(target, kHeadingLevel);
    if (role == 18) uiaInt(target, kLandmarkType, kSearchLandmark); else clearAccessible(target, kLandmarkType);
}

static void applyAccessRole(Node *node) {
    HWND target = accessibleOf(node);
    int count = (int)(sizeof kRoles / sizeof kRoles[0]);
    int role = node->role > 0 && node->role < count && !(node->kind == 5 && node->role == 18) ? kRoles[node->role] : kindRole(node);
    if (role) accessible(target, role, nullptr);
    else clearAccessible(target, PROPID_ACC_ROLE);
    uiaRole(node, target);
}

// An announcement (command 9): UI Automation's notification event, which Narrator and NVDA
// speak, from the window's host provider (uiautomationcore.dll, Windows 10 1709 and later); a
// live region's change event before that.
static void announce(const std::wstring &text) {
    typedef HRESULT(WINAPI * HostProvider)(HWND, IRawElementProviderSimple **);
    typedef HRESULT(WINAPI * Notify)(IRawElementProviderSimple *, int, int, BSTR, BSTR);
    static HMODULE core = LoadLibraryW(L"uiautomationcore.dll");
    static HostProvider hostProvider = core ? (HostProvider)(void *)GetProcAddress(core, "UiaHostProviderFromHwnd") : nullptr;
    static Notify notify = core ? (Notify)(void *)GetProcAddress(core, "UiaRaiseNotificationEvent") : nullptr;
    IRawElementProviderSimple *provider = nullptr;
    if (hostProvider && notify && SUCCEEDED(hostProvider(mainWindow, &provider)) && provider) {
        BSTR spoken = SysAllocString(text.c_str()), activity = SysAllocString(L"announcement");
        // NotificationKind_Other (4), NotificationProcessing_ImportantMostRecent (1).
        notify(provider, 4, 1, spoken, activity);
        SysFreeString(spoken);
        SysFreeString(activity);
        provider->Release();
        return;
    }
    accessibleText(mainWindow, PROPID_ACC_NAME, text);
    NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, mainWindow, OBJID_CLIENT, CHILDID_SELF);
}

// The state the node announces. An annotated state replaces the one the control computes, so
// it is annotated only where the app or the kind sets one (checked, selected, expanded, a tab,
// a switch); it says focusable and unavailable as the window is.
static void applyAccessStates(Node *node) {
    HWND target = accessibleOf(node);
    if (!accProps || !target) return;
    int checked = node->checked ? node->checked : node->kind == 9 ? (node->on ? 2 : 1) : 0;
    if (checked || node->selected || node->expanded || node->role == 9) {
        int state = 0;
        if (checked == 2) state |= STATE_SYSTEM_CHECKED;
        else if (checked == 3) state |= STATE_SYSTEM_MIXED;
        if (node->selected) state |= STATE_SYSTEM_SELECTED;
        if (node->selected || node->role == 9) state |= STATE_SYSTEM_SELECTABLE;
        if (node->expanded == 1) state |= STATE_SYSTEM_COLLAPSED;
        else if (node->expanded == 2) state |= STATE_SYSTEM_EXPANDED;
        if (GetWindowLongW(target, GWL_STYLE) & WS_TABSTOP) state |= STATE_SYSTEM_FOCUSABLE;
        if (!IsWindowEnabled(target)) state |= STATE_SYSTEM_UNAVAILABLE;
        accessibleState(target, state);
    } else clearAccessible(target, PROPID_ACC_STATE);
    // The spoken value: the app's text, else the range's value; a field's and a slider's
    // own value stay theirs.
    std::wstring value = node->valueText;
    if (value.empty() && node->rangeMax > node->rangeMin && node->kind != 5 && node->kind != 15) value = std::to_wstring(node->rangeNow);
    accessibleText(target, PROPID_ACC_VALUE, value);
    accessibleText(target, PROPID_ACC_DESCRIPTION, node->hint);
}

// Disabled (prop 61): the node's windows and everything under it are disabled (no input, not
// in the tab order, read as unavailable), and enabled again when nothing above is disabled.
static void enableTree(Node *node, bool above) {
    bool off = above || node->disabled;
    if (node->hwnd) EnableWindow(node->hwnd, !off);
    if (node->kind == 5 && node->inner) EnableWindow(node->inner, !off);
    if (node->nativeView) EnableWindow(node->nativeView, !off);
    if (node->checked || node->selected || node->expanded || node->role == 9 || node->kind == 9) applyAccessStates(node);
    if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
    for (size_t i = 0; node->yoga && i < YGNodeGetChildCount(node->yoga); ++i)
        enableTree((Node *)YGNodeGetContext(YGNodeGetChild(node->yoga, i)), off);
}
static bool anyDisabled;
// The element above a node in the app's tree: its Yoga parent, or for a modal (a root of its
// own, a window over the content) the element it was placed in.
static Node *aboveOf(Node *node) {
    Node *up = parentNode(node);
    if (up || node->kind != 12 || !node->parentId) return up;
    auto found = nodes.find(node->parentId);
    return found == nodes.end() ? nullptr : found->second;
}
static void refreshEnabled(Node *node) {
    if (!anyDisabled) return;
    bool above = false;
    for (Node *up = aboveOf(node); up; up = aboveOf(up)) if (up->disabled) { above = true; break; }
    enableTree(node, above);
    HWND focus = GetFocus();
    if (focus && focus != mainWindow && !IsWindowEnabled(focus)) SetFocus(mainWindow);
}

// Props 57 to 64.
static void applyAccessProp(Node *node, int code, int value) {
    if (code == 61) {
        node->disabled = value != 0;
        if (node->disabled) anyDisabled = true;
        refreshEnabled(node);
        // The modals placed under it.
        for (Node *modal : modals) refreshEnabled(modal);
        return;
    }
    if (code == 57) { node->role = value; applyAccessRole(node); }
    else if (code == 58) node->checked = value;
    else if (code == 59) node->selected = value;
    else if (code == 60) node->expanded = value;
    else if (code == 62) node->rangeNow = value;
    else if (code == 63) node->rangeMin = value;
    else if (code == 64) node->rangeMax = value;
    applyAccessStates(node);
}

// String props 8 (the spoken value) and 9 (the hint).
static void applyAccessText(Node *node, int code, const std::wstring &value) {
    if (code == 8) node->valueText = value; else node->hint = value;
    applyAccessStates(node);
}

// ---- text inputs ----

// A field's text with "\n" line breaks (a multiline EDIT keeps "\r\n").
static std::wstring editText(Node *node) {
    if (!node->inner) return node->text;
    int n = GetWindowTextLengthW(node->inner);
    std::wstring text(n, L'\0');
    if (n) GetWindowTextW(node->inner, &text[0], n + 1);
    text.resize(wcslen(text.c_str()));
    std::wstring out;
    for (size_t i = 0; i < text.size(); i++) if (!(text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')) out += text[i];
    return out;
}
// The field's text, when it differs (no change event goes back to the app).
static void setEditText(Node *node, const std::wstring &value) {
    if (!node->inner || editText(node) == value) return;
    std::wstring shown;
    for (wchar_t c : value) { if (c == L'\n') shown += L'\r'; shown += c; }
    node->settingText = true;
    SetWindowTextW(node->inner, shown.c_str());
    node->settingText = false;
}

// The touch keyboard's layout (msctf's SetInputScope, loaded when there).
static void applyInputScope(Node *node) {
    typedef HRESULT(WINAPI * SetScope)(HWND, InputScope);
    static SetScope set = (SetScope)(void *)GetProcAddress(LoadLibraryW(L"msctf.dll"), "SetInputScope");
    if (!set || !node->inner) return;
    const InputScope scopes[] = {IS_DEFAULT, IS_EMAIL_SMTPEMAILADDRESS, IS_DIGITS, IS_NUMBER, IS_TELEPHONE_FULLTELEPHONENUMBER, IS_URL, IS_SEARCH};
    InputScope scope = node->keyboard >= 0 && node->keyboard <= 6 ? scopes[node->keyboard] : IS_DEFAULT;
    if (node->secure) scope = IS_PASSWORD;
    set(node->inner, scope);
}

// Secure hides what is typed (a one-line field's password character); the number keyboard
// takes digits only (ES_NUMBER: a decimal or phone keyboard would need more than digits).
static void applyInputOptions(Node *node) {
    HWND edit = node->inner;
    if (!edit) return;
    LONG style = GetWindowLongW(edit, GWL_STYLE);
    LONG want = node->keyboard == 2 ? (style | ES_NUMBER) : (style & ~ES_NUMBER);
    if (want != style) SetWindowLongW(edit, GWL_STYLE, want);
    if (!node->multiline) SendMessageW(edit, EM_SETPASSWORDCHAR, node->secure ? 0x25CF : 0, 0);
    applyInputScope(node);
    InvalidateRect(edit, nullptr, TRUE);
}

// Multiline (prop 68): the EDIT is made again with ES_MULTILINE (return inserts a line, long
// lines wrap) or without it, in the same field window, with its text, font, placeholder,
// options, accessibility, enabled state and focus.
static void makeEdit(Node *node) {
    HWND old = node->inner;
    std::wstring text = editText(node);
    bool focused = old && GetFocus() == old;
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT |
                  (node->multiline ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL : ES_AUTOHSCROLL) | (node->keyboard == 2 ? ES_NUMBER : 0);
    RECT at = {};
    if (old) { GetWindowRect(old, &at); MapWindowPoints(nullptr, node->hwnd, (POINT *)&at, 2); }
    HWND edit = CreateWindowExW(0, L"EDIT", L"", style, at.left, at.top, at.right - at.left, at.bottom - at.top, node->hwnd, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!edit) return;
    node->inner = edit;
    SendMessageW(edit, WM_SETFONT, (WPARAM)fontOf(node), FALSE);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    if (!node->placeholder.empty()) SendMessageW(edit, EM_SETCUEBANNER, TRUE, (LPARAM)node->placeholder.c_str());
    setEditText(node, text);
    if (old) DestroyWindow(old);
    applyTheme(node);
    applyInputOptions(node);
    if (!node->label.empty()) accessible(edit, 0, &node->label);
    applyAccessRole(node);
    applyAccessStates(node);
    EnableWindow(edit, node->hwnd ? IsWindowEnabled(node->hwnd) : TRUE);
    if (focused) SetFocus(edit);
    YGNodeMarkDirty(node->yoga);
}

// Props 65 to 72 on a text input. The return key (67) has nothing to show on a desktop;
// Win32's EDIT has no capitalization (69), no spell check to turn off (70) and no autofill (71).
static void applyInputProp(Node *node, int code, int value) {
    if (node->kind != 5) return;
    if (code == 65) node->secure = value;
    else if (code == 66) node->keyboard = value;
    else if (code == 70) { node->autocorrect = value; return; }
    else if (code == 72) { node->submitListening = value != 0; return; }
    else if (code == 68) {
        if (node->multiline == (value != 0)) return;
        node->multiline = value != 0;
        makeEdit(node);
        return;
    } else return;
    applyInputOptions(node);
}

// Return in a one-line field with a submit listener (from the message loop, before the dialog
// manager takes it): the app's submit with the text as it is now. False when it is not one.
static bool submitReturn(HWND hwnd) {
    Node *node = owningNode(hwnd);
    if (!node || node->kind != 5 || hwnd != node->inner || node->multiline || !node->submitListening || !IsWindowEnabled(hwnd)) return false;
    ui_dispatch_submit(node->id, node->generation, narrow(editText(node).c_str()).c_str());
    return true;
}

// ---- the system's text size ----

static double systemTextScale() {
    DWORD value = 0, size = sizeof value, type = 0;
    if (SHGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", &type, &value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD || value < 100 || value > 500)
        return 1;
    return value / 100.0;
}

// The text size changed: the texts again in their new size, laid out again.
static void applyTextScale() {
    double next = systemTextScale();
    if (std::fabs(next - textScale) < 0.001) return;
    textScale = next;
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (node->kind == 3 || node->kind == 4 || node->kind == 5) applyFont(node);
    }
    relayout();
}
