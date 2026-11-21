// host_conformance.inc: the host's own run of the conformance scene (tests/conformance built
// with UI_TEST=1, started with UI_CONFORMANCE=1): what the driver's ops cannot reach, read from
// Windows (each labeled element's MSAA role and UI Automation control type, what the host
// drew, the modal's cover) after the controls' own input (BM_CLICK, an EDIT's text, a
// trackbar's position, the task dialog's button, the wheel at the feed's top, Escape). It
// prints one line, CONFORMANCE and a JSON object, which tools/conformance.py compares with the
// other hosts'. Part of host.cpp, which #includes it in a test build only.

static Node *labeled(const std::wstring &label) {
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if ((node->kind == 4 ? node->text : node->label) == label) return node;
    }
    return nullptr;
}

// The role a labeled element has for assistive technologies, in the suite's words: MSAA's
// role as a screen reader gets it, and UI Automation's localized type for a switch.
static std::string conformanceRole(Node *node) {
    if (node->kind == 6) return "scroll";
    IAccessible *object = nullptr;
    if (FAILED(AccessibleObjectFromWindow(accessibleOf(node), (DWORD)OBJID_CLIENT, IID_IAccessible, (void **)&object)) || !object)
        return "none";
    VARIANT self, role;
    VariantInit(&self);
    VariantInit(&role);
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    object->get_accRole(self, &role);
    object->Release();
    switch (role.vt == VT_I4 ? role.lVal : 0) {
    case ROLE_SYSTEM_PUSHBUTTON: return "button";
    case ROLE_SYSTEM_CHECKBUTTON: return node->kind == 9 || node->role == 8 ? "switch" : "checkbox";
    case ROLE_SYSTEM_SLIDER: return "slider";
    case ROLE_SYSTEM_PROGRESSBAR: return "progressbar";
    case ROLE_SYSTEM_GRAPHIC: return "image";
    case ROLE_SYSTEM_TEXT: return "textfield";
    case ROLE_SYSTEM_STATICTEXT: return "text";
    case ROLE_SYSTEM_GROUPING: return "group";
    default: return "role " + std::to_string(role.vt == VT_I4 ? role.lVal : -1);
    }
}

static std::string jsonText(const std::wstring &text) {
    std::string out = "\"";
    for (char c : narrow(text.c_str())) {
        if (c == '"' || c == '\\') out += '\\';
        if ((unsigned char)c >= 32) out += c;
    }
    return out + "\"";
}

static std::string number(double value) {
    char text[32];
    snprintf(text, sizeof text, "%g", std::round(value * 100) / 100);
    return text;
}

// A window's box in points from another's top left.
static std::string boxIn(HWND hwnd, HWND space) {
    if (!hwnd || !space) return "null";
    RECT r;
    GetWindowRect(hwnd, &r);
    MapWindowPoints(nullptr, space, (POINT *)&r, 2);
    return "[" + number(r.left / scale) + "," + number(r.top / scale) + "," + number((r.right - r.left) / scale) + "," +
           number((r.bottom - r.top) / scale) + "]";
}

static std::string hexColor(int value) {
    char text[16];
    snprintf(text, sizeof text, "\"%08x\"", (uint32_t)value);
    return text;
}

static void click(const wchar_t *label) {
    Node *node = labeled(label);
    if (node && node->hwnd) SendMessageW(node->hwnd, BM_CLICK, 0, 0);
}

static void runConformance() {
    static std::string frames, roles, styles, modal = "null";
    static std::vector<bool> refresh;
    static std::vector<std::function<void()>> steps = {
        []() {
            Node *suite = labeled(L"Suite");
            for (auto &entry : nodes) {
                Node *node = entry.second;
                std::wstring label = node->kind == 4 ? node->text : node->label;
                if (label.empty() || !node->hwnd || !IsWindowVisible(node->hwnd)) continue;
                frames += (frames.empty() ? "" : ",") + jsonText(label) + ":" + boxIn(node->hwnd, suite->hwnd);
                roles += (roles.empty() ? "" : ",") + jsonText(label) + ":\"" + conformanceRole(node) + "\"";
            }
            Node *painted = labeled(L"Painted"), *title = labeled(L"Title"), *moved = labeled(L"Moved");
            LOGFONTW font = {};
            GetObjectW(fontOf(title), sizeof font, &font);
            styles = "{\"Painted\":{\"background\":" + hexColor(painted->shownBackground) + ",\"radius\":" + number(painted->radius) +
                     ",\"border\":" + number(painted->borderWidth) + ",\"borderColor\":" + hexColor(painted->borderColor) +
                     ",\"opacity\":" + number(painted->shownOpacity / 100) + "},\"Title\":{\"color\":" + hexColor(title->textColor) +
                     ",\"fontSize\":" + number(std::abs(font.lfHeight) / scale / textScale) + ",\"fontWeight\":" +
                     number(font.lfWeight) + "},\"Moved\":{\"translateX\":" + number(moved->shownX) + ",\"translateY\":" +
                     number(moved->shownY) + ",\"scale\":" + number(moved->shownScale / 100) + ",\"rotate\":" +
                     number(moved->shownRotate) + "}}";
            click(L"Tap");
            click(L"Tap");
            SetWindowTextW(labeled(L"Field")->inner, L"abc");
        },
        []() {
            ui_dispatch(labeled(L"Press")->id, labeled(L"Press")->generation);
            click(L"Toggle");
        },
        []() {
            HWND volume = labeled(L"Volume")->hwnd;
            SendMessageW(volume, TBM_SETPOS, TRUE, 70);
            SendMessageW(GetParent(volume), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 70), (LPARAM)volume);
        },
        []() { click(L"Copy"); },
        []() { click(L"Ask"); },
        // The task dialog's second button.
        []() { if (alertWindow) SendMessageW(alertWindow, TDM_CLICK_BUTTON, 1001, 0); },
        []() { click(L"Open sheet"); },
        []() {
            Node *layer = modals.empty() ? nullptr : modals.back();
            Node *sheet = labeled(L"Sheet");
            RECT client;
            GetClientRect(rootWindow, &client);
            if (layer && sheet) {
                modal = "{\"cover\":" + boxIn(layer->hwnd, rootWindow) + ",\"window\":[" + number(client.right / scale) + "," +
                        number(client.bottom / scale) + "],\"sheet\":" + boxIn(sheet->hwnd, rootWindow) + "}";
            }
            click(L"Close");
        },
        []() { click(L"Open sheet"); },
        []() { SendMessageW(mainWindow, WM_COMMAND, kCmdBack, 0); },
        []() {
            // The wheel turned up at the top of the feed, two notches.
            HWND feed = labeled(L"Feed")->hwnd;
            for (int i = 0; i < 2; i++) SendMessageW(feed, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        },
        []() {
            refresh.push_back(labeled(L"Feed")->refreshing);
            click(L"Loaded");
        },
        []() {
            refresh.push_back(labeled(L"Feed")->refreshing);
            scrollTo(labeled(L"Feed"), 2000, true);
        },
        []() {
            Node *suite = labeled(L"Suite");
            std::string scrolled;
            for (auto &entry : nodes) {
                Node *node = entry.second;
                if (!node->hwnd || !(node->label.rfind(L"Row ", 0) == 0 || node->label == L"Feed")) continue;
                scrolled += (scrolled.empty() ? "" : ",") + jsonText(node->label) + ":" + boxIn(node->hwnd, suite->hwnd);
            }
            std::string result = "{\"status\":\"DONE\",\"frames\":{" + frames + "},\"roles\":{" + roles + "},\"styles\":" + styles +
                                 ",\"scrolled\":{" + scrolled + "},\"modal\":" + modal + ",\"refresh\":[" +
                                 (refresh.size() > 0 && refresh[0] ? "true" : "false") + "," +
                                 (refresh.size() > 1 && refresh[1] ? "true" : "false") + "],\"log\":" +
                                 jsonText(wide(ui_conformance_log())) + "}";
            printf("CONFORMANCE %s\n", result.c_str());
            fflush(stdout);
            ui_stop();
            ExitProcess(0);
        },
    };
    // One step every 300 ms, after the scene's first frame and its timer; on the window's
    // thread, which a task dialog's own loop serves too.
    std::thread([]() {
        Sleep(500);
        for (size_t i = 0; i < steps.size(); i++) {
            onMain([i]() { steps[i](); });
            Sleep(300);
        }
    }).detach();
}
