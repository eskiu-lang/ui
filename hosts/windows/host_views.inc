// host_views.inc: window procedures, native views, images, the network and screens. Part of host.cpp, which #includes it (one translation unit).

// ---- window procedures ----

static LRESULT notifyChild(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool *handled);
static Node *transformedAt(HWND parent, POINT point, POINT &local);
static bool clickTransformed(HWND parent, POINT point);
static void paintText(Node *node, HDC hdc, RECT rect);
static LRESULT paintSliderAccent(Node *node, NMCUSTOMDRAW *draw);

// Whether the click at `point` (client pixels) is on a disabled control inside `hwnd`.
static bool disabledChildAt(HWND hwnd, POINT point) {
    HWND at = hwnd;
    for (;;) {
        HWND next = ChildWindowFromPointEx(at, point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!next || next == at) return false;
        if (!IsWindowEnabled(next)) return true;
        MapWindowPoints(at, next, &point, 1);
        at = next;
    }
}

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
            // Over a scaled or turned child's picture the click is the parent's to pass on.
            POINT over = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, local;
            ScreenToClient(hwnd, &over);
            if (transformedAt(hwnd, over, local)) return HTCLIENT;
            if (!owner) return HTTRANSPARENT;
            bool takes = owner->kind == 8 || owner->kind == 12 || owner->kind == 5 || owner->kind == 13 ||
                         owner->panListening || owner->longListening || owner->hoverListening;
            if (!takes && findUp(owner, [](Node *n) { return n->panListening || n->longListening; })) takes = true;
            return takes ? HTCLIENT : HTTRANSPARENT;
        }
        case WM_LBUTTONDOWN: {
            Node *owner = node ? node : owningNode(hwnd);
            POINT at = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (clickTransformed(hwnd, at)) return 0;
            // A disabled control takes no click, and its parent (Windows gives the parent the
            // clicks on a disabled child) does not take it for its own either.
            if (disabledChildAt(hwnd, at)) return 0;
            if (owner && owner->kind == 5 && owner->inner) { SetFocus(owner->inner); return 0; }
            if (owner && owner->kind == 12) {
                // A click on a modal's backdrop (outside its content) asks it to close.
                if (owner->listening) ui_dispatch(owner->id, owner->generation);
                return 0;
            }
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
            wheelAtTop(node, GET_WHEEL_DELTA_WPARAM(wParam));
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
            applyAccessStates(node);
            InvalidateRect(node->hwnd, nullptr, FALSE);
            ui_dispatch_toggle(node->id, node->generation, node->on ? 1 : 0);
            return 0;
        }
        if (node->kind == 5 && control == node->inner) {
            if (code == EN_CHANGE && !node->settingText) {
                std::wstring text = editText(node);
                node->text = text;
                // A multiline field grows with its text.
                if (node->multiline) { YGNodeMarkDirty(node->yoga); relayout(); }
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
        if (node && node->kind == 15 && node->textColor && header->code == NM_CUSTOMDRAW) {
            *handled = true;
            return paintSliderAccent(node, (NMCUSTOMDRAW *)lParam);
        }
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
        if (node && (node->kind == 3 || (node->kind == 4 && node->styled))) return 1;
    } else if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
        // A Text is painted by the host (host_props.inc): padding, alignment, line height,
        // maximum lines.
        Node *node = nodeOf(hwnd);
        if (node && node->kind == 3) {
            if (msg == WM_PRINTCLIENT) { RECT r; GetClientRect(hwnd, &r); paintText(node, (HDC)wParam, r); return 0; }
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            buffered(hwnd, hdc, [&](HDC memory, RECT r) { paintText(node, memory, r); });
            EndPaint(hwnd, &ps);
            return 0;
        }
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
