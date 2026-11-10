// The <ui/webapp> host on macOS: AppKit windows holding a WKWebView each, for a program
// that serves a web front end (src/webapp.esk has the program's side and the contract of
// each uiw_ function). The page's messages arrive through a script message handler, the
// program's URL schemes through WKURLSchemeHandler, and the program's scripts run with
// evaluateJavaScript on the main queue, from whatever thread asked.
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The program's side (src/webapp.esk), on the main thread.
extern void uiw_on_message(int window, const char *text);
extern void uiw_on_request(int window, int64_t request, const char *scheme, const char *url, const char *method, const char *range);
extern int uiw_on_close(int window);
extern void uiw_on_closed(int window);

@class UiwWindow;
static NSString *gAppName = @"app";
static NSString *gBridge = @"";
static NSMutableArray<NSString *> *gSchemes;
static NSMutableDictionary<NSNumber *, UiwWindow *> *gWindows;
static int gNextWindow;
static int gExitCode;
static BOOL gStopped;
// The scheme requests waiting for uiw_respond, by id; a request the page stopped is gone.
static NSMutableDictionary<NSNumber *, id<WKURLSchemeTask>> *gTasks;
static int64_t gNextRequest;

static void uiwOnMain(dispatch_block_t block) {
    if ([NSThread isMainThread]) block();
    else dispatch_async(dispatch_get_main_queue(), block);
}

// Ends [NSApp run]: stop takes effect after the next event, so one is posted.
static void uiwStop(void) {
    if (gStopped) return;
    gStopped = YES;
    [NSApp stop:nil];
    NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0
                                      timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    [NSApp postEvent:wake atStart:NO];
}

// The script message handler, kept apart so the content controller does not retain the window.
@interface UiwMessages : NSObject <WKScriptMessageHandler>
@property int window;
@end

@implementation UiwMessages
- (void)userContentController:(WKUserContentController *)controller didReceiveScriptMessage:(WKScriptMessage *)message {
    if (![message.body isKindOfClass:NSString.class]) return;
    uiw_on_message(self.window, [(NSString *)message.body UTF8String]);
}
@end

@interface UiwWindow : NSObject <NSWindowDelegate, WKURLSchemeHandler, WKNavigationDelegate, WKUIDelegate>
@property int ident;
@property(strong) NSWindow *window;
@property(strong) WKWebView *web;
@property BOOL followTitle;
@property(copy) NSString *origin;
@end

// "scheme://host:port" of a URL, lowercased.
static NSString *uiwOrigin(NSURL *url) {
    if (!url.scheme) return @"";
    return [NSString stringWithFormat:@"%@://%@%@", url.scheme.lowercaseString, url.host.lowercaseString ?: @"",
                                      url.port ? [NSString stringWithFormat:@":%@", url.port] : @""];
}

@implementation UiwWindow

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    if (self.followTitle && self.web.title.length) self.window.title = self.web.title;
}

// ---- Closing ----

- (BOOL)windowShouldClose:(NSWindow *)sender { return uiw_on_close(self.ident) == 1; }

- (void)windowWillClose:(NSNotification *)notification {
    if (self.followTitle) {
        @try { [self.web removeObserver:self forKeyPath:@"title"]; } @catch (id ignored) {}
    }
    [self.web.configuration.userContentController removeScriptMessageHandlerForName:@"eskiu"];
    [self.web stopLoading];
    int ident = self.ident;
    [gWindows removeObjectForKey:@(ident)];
    uiw_on_closed(ident);
    if (gWindows.count == 0) uiwStop();
    // The web view's configuration holds this object as its scheme handler: let both go.
    dispatch_async(dispatch_get_main_queue(), ^{
        self.window.contentView = nil;
        self.web = nil;
    });
}

// ---- The program's schemes ----

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
    int64_t request = ++gNextRequest;
    gTasks[@(request)] = task;
    NSURLRequest *r = task.request;
    NSString *range = [r valueForHTTPHeaderField:@"Range"] ?: @"";
    uiw_on_request(self.ident, request, r.URL.scheme.lowercaseString.UTF8String, r.URL.absoluteString.UTF8String,
                   (r.HTTPMethod ?: @"GET").UTF8String, range.UTF8String);
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    NSArray *keys = [gTasks allKeysForObject:task];
    [gTasks removeObjectsForKeys:keys];
}

// ---- Loading ----

- (void)webView:(WKWebView *)web didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    fprintf(stderr, "webapp: %s did not load: %s\n", web.URL.absoluteString.UTF8String ?: "the page",
            error.localizedDescription.UTF8String);
}

// The page stays on the app's origin (its schemes, or the URL the window opened): a link
// elsewhere opens in the browser, so no other site gets the bridge.
- (void)webView:(WKWebView *)web decidePolicyForNavigationAction:(WKNavigationAction *)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decide {
    NSURL *url = action.request.URL;
    NSString *scheme = url.scheme.lowercaseString ?: @"";
    BOOL inside = !action.targetFrame.isMainFrame || [gSchemes containsObject:scheme] || [scheme isEqualToString:@"about"]
        || [scheme isEqualToString:@"blob"] || [scheme isEqualToString:@"data"] || [uiwOrigin(url) isEqualToString:self.origin];
    if (!inside && url) [NSWorkspace.sharedWorkspace openURL:url];
    decide(inside ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}

// A link that opens a new window (target="_blank", window.open) goes to the browser.
- (WKWebView *)webView:(WKWebView *)web createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration
   forNavigationAction:(WKNavigationAction *)action windowFeatures:(WKWindowFeatures *)features {
    NSURL *url = action.request.URL;
    if (url) [NSWorkspace.sharedWorkspace openURL:url];
    return nil;
}

// The page's alert, confirm and prompt, which WKWebView leaves to the app.
- (void)webView:(WKWebView *)web runJavaScriptAlertPanelWithMessage:(NSString *)message initiatedByFrame:(WKFrameInfo *)frame
    completionHandler:(void (^)(void))done {
    NSAlert *alert = [NSAlert new];
    alert.messageText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
    done();
}

- (void)webView:(WKWebView *)web runJavaScriptConfirmPanelWithMessage:(NSString *)message initiatedByFrame:(WKFrameInfo *)frame
    completionHandler:(void (^)(BOOL))done {
    NSAlert *alert = [NSAlert new];
    alert.messageText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];
    done([alert runModal] == NSAlertFirstButtonReturn);
}

- (void)webView:(WKWebView *)web runJavaScriptTextInputPanelWithPrompt:(NSString *)prompt defaultText:(NSString *)text
    initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(NSString *))done {
    NSAlert *alert = [NSAlert new];
    alert.messageText = prompt;
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
    field.stringValue = text ?: @"";
    alert.accessoryView = field;
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];
    done([alert runModal] == NSAlertFirstButtonReturn ? field.stringValue : nil);
}

// <input type="file">.
- (void)webView:(WKWebView *)web runOpenPanelWithParameters:(WKOpenPanelParameters *)parameters
    initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(NSArray<NSURL *> *))done {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    panel.canChooseDirectories = parameters.allowsDirectories;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        done(result == NSModalResponseOK ? panel.URLs : nil);
    }];
}
@end

// The menu's actions on the key window.
@interface UiwApp : NSObject <NSApplicationDelegate>
@end

static UiwWindow *uiwKeyWindow(void) {
    for (UiwWindow *w in gWindows.allValues)
        if (w.window == NSApp.keyWindow) return w;
    return nil;
}

@implementation UiwApp
- (void)reload:(id)sender { [uiwKeyWindow().web reload]; }

// Quit asks every window, as closing each would; the program's main goes on after
// webapp_run instead of the process ending here.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    for (UiwWindow *w in gWindows.allValues)
        if (uiw_on_close(w.ident) != 1) return NSTerminateCancel;
    uiwStop();
    return NSTerminateCancel;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [NSApp activateIgnoringOtherApps:YES];
}
@end

static UiwApp *gApp;

static NSMenuItem *uiwItem(NSString *title, SEL action, NSString *key) {
    return [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
}

static NSMenu *uiwSubmenu(NSMenu *bar, NSString *title) {
    NSMenuItem *item = [NSMenuItem new];
    [bar addItem:item];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    item.submenu = menu;
    return menu;
}

// The app's menu (About, Hide, Quit), Edit (for the page's fields, through the responder
// chain), View (Reload, Full Screen) and Window.
static void uiwMenu(void) {
    NSMenu *bar = [NSMenu new];
    NSMenu *app = uiwSubmenu(bar, gAppName);
    [app addItem:uiwItem([NSString stringWithFormat:@"About %@", gAppName], @selector(orderFrontStandardAboutPanel:), @"")];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:uiwItem([NSString stringWithFormat:@"Hide %@", gAppName], @selector(hide:), @"h")];
    NSMenuItem *others = uiwItem(@"Hide Others", @selector(hideOtherApplications:), @"h");
    others.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    [app addItem:others];
    [app addItem:uiwItem(@"Show All", @selector(unhideAllApplications:), @"")];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:uiwItem([NSString stringWithFormat:@"Quit %@", gAppName], @selector(terminate:), @"q")];

    NSMenu *edit = uiwSubmenu(bar, @"Edit");
    [edit addItem:uiwItem(@"Undo", @selector(undo:), @"z")];
    [edit addItem:uiwItem(@"Redo", @selector(redo:), @"Z")];
    [edit addItem:NSMenuItem.separatorItem];
    [edit addItem:uiwItem(@"Cut", @selector(cut:), @"x")];
    [edit addItem:uiwItem(@"Copy", @selector(copy:), @"c")];
    [edit addItem:uiwItem(@"Paste", @selector(paste:), @"v")];
    [edit addItem:uiwItem(@"Select All", @selector(selectAll:), @"a")];

    NSMenu *view = uiwSubmenu(bar, @"View");
    NSMenuItem *reload = uiwItem(@"Reload", @selector(reload:), @"r");
    reload.target = gApp;
    [view addItem:reload];
    [view addItem:NSMenuItem.separatorItem];
    NSMenuItem *full = uiwItem(@"Enter Full Screen", @selector(toggleFullScreen:), @"f");
    full.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    [view addItem:full];

    NSMenu *window = uiwSubmenu(bar, @"Window");
    [window addItem:uiwItem(@"Minimize", @selector(performMiniaturize:), @"m")];
    [window addItem:uiwItem(@"Zoom", @selector(performZoom:), @"")];
    [window addItem:uiwItem(@"Close", @selector(performClose:), @"w")];
    [window addItem:NSMenuItem.separatorItem];
    [window addItem:uiwItem(@"Bring All to Front", @selector(arrangeInFront:), @"")];
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = window;
}

// ---- What the program calls ----

void uiw_init(const char *app_name, const char *bridge) {
    gAppName = app_name && app_name[0] ? @(app_name) : NSProcessInfo.processInfo.processName;
    gBridge = bridge ? @(bridge) : @"";
    gSchemes = [NSMutableArray new];
    gWindows = [NSMutableDictionary new];
    gTasks = [NSMutableDictionary new];
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    gApp = [UiwApp new];
    NSApp.delegate = gApp;
    uiwMenu();
    NSString *icon = [NSBundle.mainBundle pathForResource:@"AppIcon" ofType:@"icns"];
    if (icon) NSApp.applicationIconImage = [[NSImage alloc] initWithContentsOfFile:icon];
}

const char *uiw_platform(void) { return "macos"; }

char *uiw_resource_dir(void) {
    NSBundle *bundle = NSBundle.mainBundle;
    NSString *dir = [bundle.bundlePath hasSuffix:@".app"] ? bundle.resourcePath
        : NSProcessInfo.processInfo.arguments[0].stringByResolvingSymlinksInPath.stringByDeletingLastPathComponent;
    return strdup(dir.fileSystemRepresentation);
}

void uiw_scheme(const char *name) {
    NSString *scheme = @(name).lowercaseString;
    if (![gSchemes containsObject:scheme]) [gSchemes addObject:scheme];
}

int uiw_window_open(const char *title, const char *url, int width, int height, int min_width, int min_height, int flags) {
    UiwWindow *w = [UiwWindow new];
    w.ident = ++gNextWindow;
    WKWebViewConfiguration *configuration = [WKWebViewConfiguration new];
    UiwMessages *messages = [UiwMessages new];
    messages.window = w.ident;
    [configuration.userContentController addScriptMessageHandler:messages name:@"eskiu"];
    NSString *source = [NSString stringWithFormat:@"window.__ESKIU_HOST__={os:\"macos\",window:%d};\n%@", w.ident, gBridge];
    WKUserScript *script = [[WKUserScript alloc] initWithSource:source injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                               forMainFrameOnly:YES];
    [configuration.userContentController addUserScript:script];
    for (NSString *scheme in gSchemes) [configuration setURLSchemeHandler:w forURLScheme:scheme];
    configuration.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
    if (@available(macOS 12.3, *)) configuration.preferences.elementFullscreenEnabled = YES;
    if (flags & 4) [configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];

    NSRect frame = NSMakeRect(0, 0, width, height);
    w.web = [[WKWebView alloc] initWithFrame:frame configuration:configuration];
    w.web.navigationDelegate = w;
    w.web.UIDelegate = w;
    if (flags & 4) {
        if (@available(macOS 13.3, *)) w.web.inspectable = YES;
    }
    NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    if (flags & 1) mask |= NSWindowStyleMaskResizable;
    w.window = [[NSWindow alloc] initWithContentRect:frame styleMask:mask backing:NSBackingStoreBuffered defer:NO];
    w.window.releasedWhenClosed = NO;
    w.window.delegate = w;
    w.window.contentView = w.web;
    w.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    w.window.title = title && title[0] ? @(title) : gAppName;
    w.followTitle = !(title && title[0]);
    if (min_width > 0 || min_height > 0) w.window.contentMinSize = NSMakeSize(MAX(min_width, 1), MAX(min_height, 1));
    if (gWindows.count == 0) [w.window center];
    else [w.window cascadeTopLeftFromPoint:NSMakePoint(NSMinX(gWindows.allValues.lastObject.window.frame) + 24,
                                                      NSMaxY(gWindows.allValues.lastObject.window.frame) - 24)];
    if (w.followTitle) [w.web addObserver:w forKeyPath:@"title" options:NSKeyValueObservingOptionNew context:nil];
    gWindows[@(w.ident)] = w;
    [w.window makeKeyAndOrderFront:nil];
    if (flags & 2) dispatch_async(dispatch_get_main_queue(), ^{ [w.window toggleFullScreen:nil]; });
    NSURL *target = [NSURL URLWithString:@(url)];
    w.origin = target ? uiwOrigin(target) : @"";
    if (target) [w.web loadRequest:[NSURLRequest requestWithURL:target]];
    else fprintf(stderr, "webapp: not a URL: %s\n", url);
    return w.ident;
}

static UiwWindow *uiwWindow(int window) { return gWindows[@(window)]; }

void uiw_window_title(int window, const char *title) {
    UiwWindow *w = uiwWindow(window);
    if (!w) return;
    w.window.title = title && title[0] ? @(title) : gAppName;
}

void uiw_window_size(int window, int width, int height) {
    UiwWindow *w = uiwWindow(window);
    if (w && width > 0 && height > 0) [w.window setContentSize:NSMakeSize(width, height)];
}

void uiw_window_min_size(int window, int width, int height) {
    UiwWindow *w = uiwWindow(window);
    if (w) w.window.contentMinSize = NSMakeSize(MAX(width, 1), MAX(height, 1));
}

int uiw_window_is_fullscreen(int window) {
    UiwWindow *w = uiwWindow(window);
    return w && (w.window.styleMask & NSWindowStyleMaskFullScreen) ? 1 : 0;
}

void uiw_window_fullscreen(int window, int on) {
    UiwWindow *w = uiwWindow(window);
    if (w && uiw_window_is_fullscreen(window) != (on ? 1 : 0)) [w.window toggleFullScreen:nil];
}

void uiw_window_focus(int window) {
    UiwWindow *w = uiwWindow(window);
    if (!w) return;
    [w.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

// The Web Inspector has no public call to open it; the private one is used when there.
void uiw_window_devtools(int window) {
    UiwWindow *w = uiwWindow(window);
    if (!w) return;
    @try {
        id inspector = [w.web valueForKey:@"_inspector"];
        SEL show = NSSelectorFromString(@"show");
        if ([inspector respondsToSelector:show]) ((void (*)(id, SEL))[inspector methodForSelector:show])(inspector, show);
    } @catch (id ignored) {}
}

// After the page's message handler returns: the web view goes with the window.
void uiw_window_close(int window) {
    dispatch_async(dispatch_get_main_queue(), ^{ [uiwWindow(window).window close]; });
}

int uiw_run(void) {
    if (gWindows.count > 0 && !gStopped) [NSApp run];
    return gExitCode;
}

void uiw_quit(int code) {
    uiwOnMain(^{
        gExitCode = code;
        uiwStop();
    });
}

void uiw_eval(int window, const char *script) {
    NSString *source = @(script);
    uiwOnMain(^{
        if (window == 0) {
            for (UiwWindow *w in gWindows.allValues) [w.web evaluateJavaScript:source completionHandler:nil];
        } else {
            [uiwWindow(window).web evaluateJavaScript:source completionHandler:nil];
        }
    });
}

static NSDictionary *uiwHeaders(const char *headers) {
    NSMutableDictionary *out = [NSMutableDictionary new];
    for (NSString *line in [@(headers ? headers : "") componentsSeparatedByString:@"\n"]) {
        NSRange colon = [line rangeOfString:@":"];
        if (colon.location == NSNotFound) continue;
        NSString *name = [line substringToIndex:colon.location];
        NSString *value = [[line substringFromIndex:colon.location + 1]
            stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        out[name] = value;
    }
    return out;
}

// The answer to a request still waiting; a file's bytes are read off the main thread
// (mapped, so a part of a big file reads only that part).
void uiw_respond(int64_t request, int status, const char *headers, const char *file, int64_t offset, int64_t length,
                 const uint8_t *data) {
    id<WKURLSchemeTask> task = gTasks[@(request)];
    if (!task) return;
    NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:task.request.URL statusCode:status
                                                             HTTPVersion:@"HTTP/1.1" headerFields:uiwHeaders(headers)];
    void (^deliver)(NSData *) = ^(NSData *body) {
        if (gTasks[@(request)] != task) return;
        [gTasks removeObjectForKey:@(request)];
        if (!body) {
            [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotOpenFile userInfo:nil]];
            return;
        }
        [task didReceiveResponse:response];
        if (body.length) [task didReceiveData:body];
        [task didFinish];
    };
    if (!file || length == 0) {
        deliver(length > 0 && data ? [NSData dataWithBytes:data length:(NSUInteger)length] : [NSData data]);
        return;
    }
    NSString *path = @(file);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSData *all = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil];
        NSData *part = nil;
        if (all && (uint64_t)(offset + length) <= all.length)
            part = [all subdataWithRange:NSMakeRange((NSUInteger)offset, (NSUInteger)length)];
        dispatch_async(dispatch_get_main_queue(), ^{ deliver(part); });
    });
}

char *uiw_dialog_file(int window, int flags, const char *title, const char *default_path, const char *filters) {
    NSSavePanel *panel;
    if (flags & 4) {
        panel = [NSSavePanel savePanel];
    } else {
        NSOpenPanel *open = [NSOpenPanel openPanel];
        open.canChooseFiles = !(flags & 2);
        open.canChooseDirectories = (flags & 2) != 0;
        open.allowsMultipleSelection = (flags & 1) != 0;
        panel = open;
    }
    panel.canCreateDirectories = YES;
    if (title && title[0]) {
        panel.title = @(title);
        panel.message = @(title);
    }
    if (default_path && default_path[0]) {
        NSString *path = @(default_path).stringByExpandingTildeInPath;
        BOOL directory = NO;
        if ([NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&directory] && directory) {
            panel.directoryURL = [NSURL fileURLWithPath:path isDirectory:YES];
        } else {
            if (path.isAbsolutePath) panel.directoryURL = [NSURL fileURLWithPath:path.stringByDeletingLastPathComponent isDirectory:YES];
            if (flags & 4) panel.nameFieldStringValue = path.lastPathComponent;
        }
    }
    // The filters' extensions, all of them (an open panel has no filter menu).
    NSMutableArray *types = [NSMutableArray new];
    BOOL any = NO;
    for (NSString *line in [@(filters ? filters : "") componentsSeparatedByString:@"\n"]) {
        NSRange tab = [line rangeOfString:@"\t"];
        if (tab.location == NSNotFound) continue;
        for (NSString *ext in [[line substringFromIndex:tab.location + 1] componentsSeparatedByString:@","]) {
            if ([ext isEqualToString:@"*"] || ext.length == 0) { any = YES; continue; }
            UTType *type = [UTType typeWithFilenameExtension:ext];
            if (type) [types addObject:type];
        }
    }
    if (types.count && !any) panel.allowedContentTypes = types;
    if ([panel runModal] != NSModalResponseOK) return NULL;
    NSMutableString *out = [NSMutableString new];
    NSArray<NSURL *> *urls = (flags & 4) ? (panel.URL ? @[ panel.URL ] : @[]) : ((NSOpenPanel *)panel).URLs;
    for (NSURL *url in urls) [out appendFormat:@"%@\n", url.path];
    return out.length ? strdup(out.UTF8String) : NULL;
}

int uiw_dialog_message(int window, int buttons, int level, const char *title, const char *message, const char *ok,
                       const char *cancel) {
    NSAlert *alert = [NSAlert new];
    alert.alertStyle = level == 2 ? NSAlertStyleCritical : level == 1 ? NSAlertStyleWarning : NSAlertStyleInformational;
    BOOL titled = title && title[0];
    alert.messageText = titled ? @(title) : @(message ? message : "");
    if (titled) alert.informativeText = @(message ? message : "");
    [alert addButtonWithTitle:@(ok && ok[0] ? ok : "OK")];
    if (buttons == 1) [alert addButtonWithTitle:@(cancel && cancel[0] ? cancel : "Cancel")];
    return [alert runModal] == NSAlertFirstButtonReturn ? 1 : 0;
}
