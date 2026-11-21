// host_share.inc: the system's share sheet (command 5) through IDataTransferManagerInterop,
// Windows 10 and later. Part of host.cpp, which #includes it (one translation unit).
//
// The WinRT pieces are declared here by hand (the interfaces' vtables in the order of the
// Windows metadata, their IIDs, HSTRING as an opaque handle) and combase.dll's functions are
// looked up at run time, so the same code builds with MinGW-w64 and MSVC whatever WinRT
// headers they carry. The names are the host's own: shobjidl's IDataTransferManagerInterop
// would clash where the SDK declares it. When the sheet cannot open (an older Windows, no
// share broker, a failure on the way) the text and the URL go to the clipboard, as before.

namespace uishare {

typedef struct HStringHandle__ *HStr;
struct Token { int64_t value; };

typedef HRESULT(WINAPI *RoGetActivationFactoryFn)(HStr, REFIID, void **);
typedef HRESULT(WINAPI *WindowsCreateStringFn)(const wchar_t *, UINT32, HStr *);
typedef HRESULT(WINAPI *WindowsDeleteStringFn)(HStr);

struct Inspectable : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HStr *name) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int *level) = 0;
};

// IDataTransferManagerInterop (shobjidl_core.h), a classic COM interface.
struct ManagerInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND window, REFIID riid, void **manager) = 0;
    virtual HRESULT STDMETHODCALLTYPE ShowShareUIForWindow(HWND window) = 0;
};

// Windows.ApplicationModel.DataTransfer.IDataTransferManager.
struct Manager : Inspectable {
    virtual HRESULT STDMETHODCALLTYPE add_DataRequested(IUnknown *handler, Token *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_DataRequested(Token token) = 0;
};

struct PropertySet : Inspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HStr *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HStr value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Description(HStr *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Description(HStr value) = 0;
};

struct Package : Inspectable {
    virtual HRESULT STDMETHODCALLTYPE GetView(void **view) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Properties(PropertySet **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RequestedOperation(int *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RequestedOperation(int value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_OperationCompleted(IUnknown *handler, Token *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_OperationCompleted(Token token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Destroyed(IUnknown *handler, Token *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Destroyed(Token token) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetData(HStr format, Inspectable *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDataProvider(HStr format, IUnknown *provider) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetText(HStr value) = 0;
};

// IDataPackage2: SetWebLink.
struct Package2 : Inspectable {
    virtual HRESULT STDMETHODCALLTYPE SetApplicationLink(Inspectable *uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetWebLink(Inspectable *uri) = 0;
};

struct Request : Inspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Data(Package **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Data(Package *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Deadline(int64_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE FailWithDisplayText(HStr value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeferral(IUnknown **deferral) = 0;
};

struct RequestedArgs : Inspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Request(Request **value) = 0;
};

// Windows.Foundation.IUriRuntimeClassFactory.
struct UriFactory : Inspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateUri(HStr uri, Inspectable **instance) = 0;
};

static const IID IID_ManagerInterop = {0x3a3dcd6c, 0x3eab, 0x43dc, {0xbc, 0xde, 0x45, 0x67, 0x1c, 0xe8, 0x00, 0xc8}};
static const IID IID_Manager = {0xa5caee9b, 0x8708, 0x49d1, {0x8d, 0x36, 0x67, 0xd2, 0x5a, 0x8d, 0xa0, 0x0c}};
static const IID IID_Package2 = {0x041c1fe9, 0x2409, 0x45e1, {0xa5, 0x38, 0x4c, 0x53, 0xee, 0xee, 0x04, 0xa7}};
static const IID IID_UriFactory = {0x44a9796f, 0x723e, 0x4fdf, {0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84}};
// TypedEventHandler<DataTransferManager, DataRequestedEventArgs>: the parameterized interface's
// IID, as WinRT derives it from its signature.
static const IID IID_RequestedHandler = {0xec6f9cc8, 0x46d0, 0x5e0e, {0xb4, 0xd2, 0x7d, 0x77, 0x73, 0xae, 0x37, 0xa0}};
static const IID IID_Agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const IID IID_Unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID IID_Marshal = {0x00000003, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static RoGetActivationFactoryFn getFactory;
static WindowsCreateStringFn createString;
static WindowsDeleteStringFn deleteString;
static ManagerInterop *interop;
static Manager *manager;
static std::wstring title, text, url;
// What the last request did (the conformance run reports it): 1 the sheet was asked to open,
// 2 its DataRequested filled the package.
static int stage;

// An HSTRING for a call, deleted with the object.
struct String {
    HStr handle = nullptr;
    explicit String(const std::wstring &value) {
        if (createString) createString(value.c_str(), (UINT32)value.size(), &handle);
    }
    ~String() {
        if (handle && deleteString) deleteString(handle);
    }
};

// The DataRequested handler: the shared text, the link and the sheet's title. Agile, as WinRT
// delegates are, so the share broker may call it from its thread.
class Handler final : public IUnknown {
    LONG refs = 1;
    IUnknown *marshaler = nullptr;

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(riid, IID_Unknown) || IsEqualIID(riid, IID_RequestedHandler) || IsEqualIID(riid, IID_Agile)) {
            *out = static_cast<IUnknown *>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_Marshal)) {
            if (!marshaler && FAILED(CoCreateFreeThreadedMarshaler(static_cast<IUnknown *>(this), &marshaler))) return E_NOINTERFACE;
            return marshaler->QueryInterface(riid, out);
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG count = InterlockedDecrement(&refs);
        if (count == 0) {
            if (marshaler) marshaler->Release();
            delete this;
        }
        return count;
    }
    virtual HRESULT STDMETHODCALLTYPE Invoke(Inspectable *, RequestedArgs *args) {
        Request *request = nullptr;
        Package *package = nullptr;
        PropertySet *properties = nullptr;
        HRESULT hr = args ? args->get_Request(&request) : E_POINTER;
        if (SUCCEEDED(hr)) hr = request->get_Data(&package);
        if (SUCCEEDED(hr)) hr = package->get_Properties(&properties);
        // The sheet needs a title; the text, the link, or both.
        if (SUCCEEDED(hr)) hr = properties->put_Title(String(title).handle);
        std::wstring shared = text.empty() ? url : url.empty() ? text : text + L" " + url;
        if (SUCCEEDED(hr)) hr = package->SetText(String(shared).handle);
        if (SUCCEEDED(hr) && !url.empty()) {
            Package2 *links = nullptr;
            UriFactory *uris = nullptr;
            Inspectable *uri = nullptr;
            if (SUCCEEDED(package->QueryInterface(IID_Package2, (void **)&links)) &&
                SUCCEEDED(getFactory(String(L"Windows.Foundation.Uri").handle, IID_UriFactory, (void **)&uris)) &&
                SUCCEEDED(uris->CreateUri(String(url).handle, &uri)))
                links->SetWebLink(uri);
            if (uri) uri->Release();
            if (uris) uris->Release();
            if (links) links->Release();
        }
        if (SUCCEEDED(hr)) stage = 2;
        if (properties) properties->Release();
        if (package) package->Release();
        if (request) request->Release();
        return hr;
    }
};

// The manager for the window, its handler registered once; false when this Windows has none.
static bool ready(HWND window) {
    if (manager) return true;
    if (!getFactory) {
        HMODULE combase = LoadLibraryW(L"combase.dll");
        if (!combase) return false;
        getFactory = (RoGetActivationFactoryFn)(void *)GetProcAddress(combase, "RoGetActivationFactory");
        createString = (WindowsCreateStringFn)(void *)GetProcAddress(combase, "WindowsCreateString");
        deleteString = (WindowsDeleteStringFn)(void *)GetProcAddress(combase, "WindowsDeleteString");
        if (!getFactory || !createString || !deleteString) {
            getFactory = nullptr;
            return false;
        }
    }
    if (!interop &&
        FAILED(getFactory(String(L"Windows.ApplicationModel.DataTransfer.DataTransferManager").handle, IID_ManagerInterop, (void **)&interop)))
        return false;
    Manager *found = nullptr;
    if (FAILED(interop->GetForWindow(window, IID_Manager, (void **)&found)) || !found) return false;
    Handler *handler = new Handler();
    Token token;
    HRESULT hr = found->add_DataRequested(handler, &token);
    handler->Release();
    if (FAILED(hr)) {
        found->Release();
        return false;
    }
    manager = found;
    return true;
}

// Opens the sheet for the window with this text and URL; false when it cannot.
static bool show(HWND window, const std::wstring &name, const std::wstring &shared, const std::wstring &link) {
    stage = 0;
    if (!window || !ready(window)) return false;
    title = name.empty() ? L"Share" : name;
    text = shared;
    url = link;
    if (FAILED(interop->ShowShareUIForWindow(window))) return false;
    stage = 1;
    return true;
}

}  // namespace uishare

// Command 5: the share sheet, else the clipboard.
static void shareText(const std::wstring &text, const std::wstring &url) {
    if (uishare::show(mainWindow, appName, text, url)) return;
    copyText(text.empty() ? url : url.empty() ? text : text + L" " + url);
}
