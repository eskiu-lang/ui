// The WebView2 host's files (part of webview.cpp): https://app.assets/PATH is web/PATH, a path
// with no extension is a route and gets index.html, and asset updates (docs/updates.md,
// hosts/webview_updates.h) serve a file of the bundle in use from %APPDATA%\NAME\ui-updates.
// The page's update operations at __ui/updates/ are answered here too, a download from a
// thread of its own.

#define WM_UI_UPDATE_DONE (WM_APP + 1)
#define UI_CONSOLE_SCRIPT L"" UI_WEBVIEW_CONSOLE_SCRIPT

static std::wstring g_updates_dir;
static UiUpdateState g_updates = {};

static std::string toUtf8(const wchar_t* text) {
    int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    std::string out(length > 0 ? (size_t)length - 1 : 0, '\0');
    if (length > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], length, NULL, NULL);
    return out;
}

static std::wstring toWide(const std::string& text) {
    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    std::wstring out(length > 0 ? (size_t)length - 1 : 0, L'\0');
    if (length > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], length);
    return out;
}

static bool readAll(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(file, &size) != 0 && size.QuadPart < ((LONGLONG)1 << 31);
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = out.empty() || (ReadFile(file, &out[0], (DWORD)out.size(), &got, NULL) && got == out.size());
    }
    CloseHandle(file);
    return ok;
}

static bool writeAll(const std::wstring& path, const void* data, size_t length) {
    std::wstring part = path + L".part";
    HANDLE file = CreateFileW(part.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = length == 0 || (WriteFile(file, data, (DWORD)length, &wrote, NULL) && wrote == length);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok) DeleteFileW(part.c_str());
    return ok;
}

static void saveUpdates() {
    SHCreateDirectoryExW(NULL, g_updates_dir.c_str(), NULL);
    char* text = ui_updates_format(&g_updates);
    writeAll(g_updates_dir + L"\\state", text ? text : "", text ? strlen(text) : 0);
    free(text);
}

// A page loads: the waiting bundle becomes the one in use, and the files no bundle needs go.
static void promoteUpdates() {
    if (g_updates_dir.empty() || !ui_updates_promote(&g_updates)) return;
    saveUpdates();
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((g_updates_dir + L"\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!ui_updates_needed(&g_updates, toUtf8(found.cFileName).c_str()))
            DeleteFileW((g_updates_dir + L"\\" + found.cFileName).c_str());
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static void respond(ICoreWebView2WebResourceRequestedEventArgs* args, int status, const char* mime, const std::string& body) {
    std::wstring headers = L"Content-Type: " + toWide(mime) + L"\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store";
    IStream* stream = SHCreateMemStream(body.empty() ? NULL : (const BYTE*)body.data(), (UINT)body.size());
    ICoreWebView2WebResourceResponse* response = NULL;
    if (g_environment && SUCCEEDED(g_environment->CreateWebResourceResponse(stream, status, status == 200 ? L"OK" : L"Not Found",
                                                                            headers.c_str(), &response)) && response) {
        args->put_Response(response);
        response->Release();
    }
    if (stream) stream->Release();
}

// The body of an http(s) GET, and its status; false when it did not arrive.
static bool httpGet(const std::wstring& url, int& status, std::string& out) {
    URL_COMPONENTS parts = {sizeof parts};
    wchar_t host[256] = {}, path[4096] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 255;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4095;
    parts.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;
    std::wstring target = path;
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(L"eskiu-ui", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", target.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, NULL);
    if (ok) {
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                            &size, WINHTTP_NO_HEADER_INDEX);
        status = (int)code;
        for (;;) {
            DWORD available = 0, got = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            size_t at = out.size();
            out.resize(at + available);
            if (!WinHttpReadData(request, &out[at], available, &got)) { ok = false; break; }
            out.resize(at + got);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// A file of the bundle downloading: the page's request waits for it (a deferral).
struct UpdateJob {
    ICoreWebView2WebResourceRequestedEventArgs* args;
    ICoreWebView2Deferral* deferral;
    std::wstring url, blob;
    std::string hash;
    const char* why;
};

static DWORD WINAPI downloadUpdate(LPVOID data) {
    UpdateJob* job = (UpdateJob*)data;
    int status = 0;
    std::string body;
    if (!httpGet(job->url, status, body) || status < 200 || status >= 300) job->why = "a file of the bundle did not download";
    else if (!ui_updates_matches((const uint8_t*)body.data(), body.size(), job->hash.c_str()))
        job->why = "a file of the bundle does not match its hash";
    else if (!writeAll(job->blob, body.data(), body.size())) job->why = "a file of the bundle could not be kept";
    PostMessageW(g_hwnd, WM_UI_UPDATE_DONE, 0, (LPARAM)job);
    return 0;
}

static void finishUpdate(UpdateJob* job) {
    respond(job->args, 200, "text/plain; charset=utf-8", job->why);
    job->deferral->Complete();
    job->deferral->Release();
    job->args->Release();
    delete job;
}

// The page's update operations (hosts/web/updates.mjs).
static void updateRequest(ICoreWebView2WebResourceRequestedEventArgs* args, const std::string& op, const std::string& query) {
    const char* text = "";
    if (op == "ready") {
        if (ui_updates_set_ready(&g_updates, query.c_str())) saveUpdates();
        else text = "not an update bundle";
    } else if (op == "apply") {
        promoteUpdates();
    } else {
        char *url = ui_updates_query(query.c_str(), "url", 0), *hash = ui_updates_query(query.c_str(), "hash", 0);
        bool ok = op == "fetch" && url && hash && ui_updates_hash_ok(hash) && !g_updates_dir.empty() &&
                  (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
        std::wstring blob = ok ? g_updates_dir + L"\\" + toWide(hash) : std::wstring();
        ICoreWebView2Deferral* deferral = NULL;
        if (!ok) {
            text = "not a file of the bundle";
        } else if (GetFileAttributesW(blob.c_str()) == INVALID_FILE_ATTRIBUTES && SUCCEEDED(args->GetDeferral(&deferral))) {
            SHCreateDirectoryExW(NULL, g_updates_dir.c_str(), NULL);
            UpdateJob* job = new UpdateJob{args, deferral, toWide(url), blob, hash, ""};
            args->AddRef();
            HANDLE thread = CreateThread(NULL, 0, downloadUpdate, job, 0, NULL);
            if (thread) CloseHandle(thread);
            else {
                job->why = "a file of the bundle did not download";
                finishUpdate(job);
            }
            free(url);
            free(hash);
            return;
        }
        free(url);
        free(hash);
    }
    respond(args, 200, "text/plain; charset=utf-8", text);
}

static int fromHex(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

class ResourceRequested : public Handler<ICoreWebView2WebResourceRequestedEventHandler> {
public:
    ResourceRequested() : Handler(IID_ICoreWebView2WebResourceRequestedEventHandler) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) override {
        ICoreWebView2WebResourceRequest* request = NULL;
        LPWSTR uri = NULL;
        if (FAILED(args->get_Request(&request)) || !request) return S_OK;
        request->get_Uri(&uri);
        request->Release();
        std::string url = uri ? toUtf8(uri) : std::string();
        CoTaskMemFree(uri);
        const std::string origin = "https://app.assets/";
        if (url.compare(0, origin.size(), origin) != 0) return S_OK;
        std::string rest = url.substr(origin.size()), query;
        size_t mark = rest.find_first_of("?#");
        if (mark != std::string::npos) {
            if (rest[mark] == '?') query = rest.substr(mark + 1, rest.find('#', mark) - mark - 1);
            rest.resize(mark);
        }
        if (rest.compare(0, 13, "__ui/updates/") == 0) {
            updateRequest(args, rest.substr(13), query);
            return S_OK;
        }
        std::string path;
        for (size_t i = 0; i < rest.size(); i++) {
            if (rest[i] == '%' && i + 2 < rest.size() && fromHex(rest[i + 1]) >= 0 && fromHex(rest[i + 2]) >= 0) {
                path += (char)(fromHex(rest[i + 1]) * 16 + fromHex(rest[i + 2]));
                i += 2;
            } else {
                path += rest[i];
            }
        }
        // Only files inside the app's web directory.
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos || path.find(':') != std::string::npos) {
            respond(args, 404, "text/plain", "");
            return S_OK;
        }
        if (path.empty()) path = "index.html";
        size_t slash = path.rfind('/');
        bool route = path.find('.', slash == std::string::npos ? 0 : slash) == std::string::npos;
        // The page loading uses the bundle downloaded last; a file of the bundle in use is the
        // downloaded one.
        if (route || path == "index.html") promoteUpdates();
        std::string body;
        const char* updated = route ? NULL : ui_updates_lookup(&g_updates, path.c_str());
        if (updated && readAll(g_updates_dir + L"\\" + toWide(updated), body)) {
            respond(args, 200, ui_updates_mime(path.c_str()), body);
            return S_OK;
        }
        std::wstring file = std::wstring(g_local_web_dir) + L"\\" + toWide(path);
        for (wchar_t& c : file) if (c == L'/') c = L'\\';
        if (!readAll(file, body)) {
            if (!route) {
                respond(args, 404, "text/plain", "");
                return S_OK;
            }
            path = "index.html";
            readAll(std::wstring(g_local_web_dir) + L"\\index.html", body);
        }
        const char* mime = ui_updates_mime(path.c_str());
        size_t dot = path.rfind('.');
        std::string ext = dot == std::string::npos ? std::string() : path.substr(dot);
        if (ext == ".js" || ext == ".mjs") mime = "text/javascript; charset=utf-8";
        else if (ext == ".webmanifest") mime = "application/manifest+json";
        respond(args, 200, mime, body);
        return S_OK;
    }
};

#ifndef UI_RELEASE
// The page's console (UI_CONSOLE_SCRIPT posts each line) on standard output.
class MessageReceived : public Handler<ICoreWebView2WebMessageReceivedEventHandler> {
public:
    MessageReceived() : Handler(IID_ICoreWebView2WebMessageReceivedEventHandler) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR message = NULL;
        if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
            std::string line = toUtf8(message) + "\n";
            CoTaskMemFree(message);
            HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD wrote = 0;
            if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, line.data(), (DWORD)line.size(), &wrote, NULL);
        }
        return S_OK;
    }
};
#endif
