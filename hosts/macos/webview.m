#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "../webview_updates.h"

// Custom scheme handler to serve bundled web assets offline without CORS restrictions. With
// asset updates (docs/updates.md) a file of the bundle in use comes from the app's data
// directory, Application Support/BUNDLE_ID/ui-updates, instead (hosts/webview_updates.h).
@interface LocalSchemeHandler : NSObject <WKURLSchemeHandler> {
    UiUpdateState _updates;
}
@property(copy) NSString *baseDir;
@property(copy) NSString *updatesDir;
// The tasks still running: one WebKit stopped is not answered.
@property(strong) NSHashTable *running;
@end

static NSString *mimeFor(NSString *ext) {
    ext = ext.lowercaseString;
    NSDictionary *types = @{
        @"html": @"text/html; charset=utf-8", @"js": @"application/javascript; charset=utf-8",
        @"mjs": @"application/javascript; charset=utf-8", @"wasm": @"application/wasm", @"css": @"text/css; charset=utf-8",
        @"json": @"application/json", @"webmanifest": @"application/json", @"png": @"image/png", @"jpg": @"image/jpeg",
        @"jpeg": @"image/jpeg", @"gif": @"image/gif", @"webp": @"image/webp", @"woff": @"font/woff", @"woff2": @"font/woff2",
        @"ttf": @"font/ttf", @"mp3": @"audio/mpeg", @"mp4": @"video/mp4", @"webm": @"video/webm", @"svg": @"image/svg+xml",
        @"ico": @"image/x-icon", @"txt": @"text/plain; charset=utf-8"};
    return types[ext] ?: @"application/octet-stream";
}

@implementation LocalSchemeHandler
- (instancetype)init {
    if ((self = [super init])) {
        NSString *data = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
        NSString *name = NSBundle.mainBundle.bundleIdentifier ?: NSProcessInfo.processInfo.processName;
        self.updatesDir = [[data stringByAppendingPathComponent:name] stringByAppendingPathComponent:@"ui-updates"];
        self.running = [NSHashTable weakObjectsHashTable];
        NSString *state = [NSString stringWithContentsOfFile:[self.updatesDir stringByAppendingPathComponent:@"state"]
                                                    encoding:NSUTF8StringEncoding error:nil];
        ui_updates_parse(&_updates, state.UTF8String);
    }
    return self;
}

- (void)dealloc {
    ui_updates_free(&_updates);
}

- (void)saveUpdates {
    [NSFileManager.defaultManager createDirectoryAtPath:self.updatesDir withIntermediateDirectories:YES attributes:nil error:nil];
    char *text = ui_updates_format(&_updates);
    [[NSString stringWithUTF8String:text ?: ""] writeToFile:[self.updatesDir stringByAppendingPathComponent:@"state"]
                                                atomically:YES encoding:NSUTF8StringEncoding error:nil];
    free(text);
}

// A page loads: the waiting bundle becomes the one in use, and the files no bundle needs go.
- (void)promoteUpdates {
    if (!ui_updates_promote(&_updates)) return;
    [self saveUpdates];
    for (NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:self.updatesDir error:nil]) {
        if (!ui_updates_needed(&_updates, name.UTF8String))
            [NSFileManager.defaultManager removeItemAtPath:[self.updatesDir stringByAppendingPathComponent:name] error:nil];
    }
}

- (void)task:(id<WKURLSchemeTask>)task answer:(NSData *)data type:(NSString *)mime {
    if (![self.running containsObject:task]) return;
    [self.running removeObject:task];
    NSDictionary *headers = @{
        @"Content-Type": mime,
        @"Content-Length": [NSString stringWithFormat:@"%lu", (unsigned long)data.length],
        @"Cache-Control": @"no-store",
        @"Access-Control-Allow-Origin": @"*"
    };
    NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:task.request.URL statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:headers];
    [task didReceiveResponse:response];
    [task didReceiveData:data];
    [task didFinish];
}

- (void)task:(id<WKURLSchemeTask>)task fail:(NSInteger)code {
    if (![self.running containsObject:task]) return;
    [self.running removeObject:task];
    [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:code userInfo:nil]];
}

- (void)task:(id<WKURLSchemeTask>)task say:(NSString *)text {
    [self task:task answer:[text dataUsingEncoding:NSUTF8StringEncoding] type:@"text/plain; charset=utf-8"];
}

// The page's update operations (hosts/web/updates.mjs).
- (void)task:(id<WKURLSchemeTask>)task update:(NSString *)op query:(const char *)query {
    if ([op isEqualToString:@"ready"]) {
        BOOL ok = ui_updates_set_ready(&_updates, query);
        if (ok) [self saveUpdates];
        [self task:task say:ok ? @"" : @"not an update bundle"];
        return;
    }
    if ([op isEqualToString:@"apply"]) {
        [self promoteUpdates];
        [self task:task say:@""];
        return;
    }
    char *url = ui_updates_query(query, "url", 0), *hash = ui_updates_query(query, "hash", 0);
    NSURL *source = url ? [NSURL URLWithString:@(url)] : nil;
    NSString *hex = hash && ui_updates_hash_ok(hash) ? @(hash) : nil;
    free(url);
    free(hash);
    if (![op isEqualToString:@"fetch"] || !source || !hex) {
        [self task:task say:@"not a file of the bundle"];
        return;
    }
    NSString *blob = [self.updatesDir stringByAppendingPathComponent:hex];
    if ([NSFileManager.defaultManager fileExistsAtPath:blob]) {
        [self task:task say:@""];
        return;
    }
    [NSFileManager.defaultManager createDirectoryAtPath:self.updatesDir withIntermediateDirectories:YES attributes:nil error:nil];
    NSURLRequest *request = [NSURLRequest requestWithURL:source cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:60];
    [[NSURLSession.sharedSession dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)response).statusCode : 0;
        NSString *why = @"";
        if (error || !data || status < 200 || status >= 300) why = @"a file of the bundle did not download";
        else if (!ui_updates_matches((const uint8_t *)data.bytes, data.length, hex.UTF8String)) why = @"a file of the bundle does not match its hash";
        else if (![data writeToFile:blob atomically:YES]) why = @"a file of the bundle could not be kept";
        dispatch_async(dispatch_get_main_queue(), ^{ [self task:task say:why]; });
    }] resume];
}

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
    [self.running addObject:task];
    NSURL *url = task.request.URL;
    NSString *path = url.path;
    if (!path.length || [path isEqualToString:@"/"]) {
        path = @"/index.html";
    }
    while ([path hasPrefix:@"/"]) {
        path = [path substringFromIndex:1];
    }
    if ([path hasPrefix:@"__ui/updates/"]) {
        NSURLComponents *parts = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
        [self task:task update:[path substringFromIndex:13] query:parts.percentEncodedQuery.UTF8String ?: ""];
        return;
    }
    // Only files inside the app's web directory.
    if ([path.pathComponents containsObject:@".."]) {
        [self task:task fail:NSURLErrorNoPermissionsToReadFile];
        return;
    }
    // The page loading uses the bundle downloaded last; a file of the bundle in use is the
    // downloaded one.
    BOOL page = [path isEqualToString:@"index.html"] || path.pathExtension.length == 0;
    if (page) [self promoteUpdates];
    const char *updated = page ? NULL : ui_updates_lookup(&_updates, path.UTF8String);
    NSData *data = updated ? [NSData dataWithContentsOfFile:[self.updatesDir stringByAppendingPathComponent:@(updated)]] : nil;
    if (data) {
        [self task:task answer:data type:@(ui_updates_mime(path.UTF8String))];
        return;
    }
    NSString *fullPath = [self.baseDir stringByAppendingPathComponent:path];
    data = [NSData dataWithContentsOfFile:fullPath];
    // A path with no extension is a route: the page. A missing file is an error.
    if (!data && path.pathExtension.length == 0) {
        fullPath = [self.baseDir stringByAppendingPathComponent:@"index.html"];
        data = [NSData dataWithContentsOfFile:fullPath];
    }
    if (!data) {
        [self task:task fail:NSURLErrorFileDoesNotExist];
        return;
    }
    [self task:task answer:data type:mimeFor(fullPath.pathExtension)];
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    [self.running removeObject:task];
}
@end

// A real macOS desktop host for the shared wasm/DOM renderer. Native-core bridge
// transport is a later step; this host tests WKWebView execution and rendering.
@interface DesktopHost : NSObject <NSApplicationDelegate, WKScriptMessageHandler, WKNavigationDelegate>
@property(strong) NSWindow *window;
@property(strong) WKWebView *web;
@property(copy) NSString *url;
@property(copy) NSString *localWebDir;
@property(copy) NSString *screenshot;
@property BOOL testing;
@property NSSize size;
@end

@implementation DesktopHost

- (void)setupMenu {
    NSMenu *menubar = [NSMenu new];

    // App menu
    NSMenuItem *appMenuItem = [NSMenuItem new];
    [menubar addItem:appMenuItem];
    NSMenu *appMenu = [NSMenu new];
    // The app's name from its bundle (the window's title is the page's, set later).
    NSString *appName = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleName"];
    if (!appName.length) appName = NSProcessInfo.processInfo.processName;
    NSString *quitTitle = [NSString stringWithFormat:@"Quit %@", appName];
    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:quitTitle action:@selector(terminate:) keyEquivalent:@"q"]];
    [appMenuItem setSubmenu:appMenu];

    // File menu (Cmd+W to close window)
    NSMenuItem *fileMenuItem = [NSMenuItem new];
    [menubar addItem:fileMenuItem];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    [fileMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"]];
    [fileMenuItem setSubmenu:fileMenu];

    // Edit menu (Cmd+C, Cmd+V, Cmd+X, Cmd+A, Cmd+Z)
    NSMenuItem *editMenuItem = [NSMenuItem new];
    [menubar addItem:editMenuItem];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"]];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"]];
    [editMenuItem setSubmenu:editMenu];

    // View menu (Cmd+R to reload, Fullscreen)
    NSMenuItem *viewMenuItem = [NSMenuItem new];
    [menubar addItem:viewMenuItem];
    NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
    [viewMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Reload" action:@selector(reload:) keyEquivalent:@"r"]];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *fsItem = [[NSMenuItem alloc] initWithTitle:@"Toggle Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
    fsItem.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    [viewMenu addItem:fsItem];
    [viewMenuItem setSubmenu:viewMenu];

    // Window menu (Cmd+M to minimize)
    NSMenuItem *windowMenuItem = [NSMenuItem new];
    [menubar addItem:windowMenuItem];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"]];
    [windowMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""]];
    [windowMenuItem setSubmenu:windowMenu];

    [NSApp setMainMenu:menubar];
}

- (void)reload:(id)sender {
    [self.web reload];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    WKWebViewConfiguration *configuration = [WKWebViewConfiguration new];
    [configuration.userContentController addScriptMessageHandler:self name:@"results"];
#ifndef UI_RELEASE
    [configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    // The page's console on standard output.
    [configuration.userContentController addScriptMessageHandler:self name:@"console"];
    [configuration.userContentController addUserScript:[[WKUserScript alloc] initWithSource:@UI_WEBVIEW_CONSOLE_SCRIPT
        injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
#endif

    if (self.localWebDir) {
        LocalSchemeHandler *schemeHandler = [LocalSchemeHandler new];
        schemeHandler.baseDir = self.localWebDir;
        [configuration setURLSchemeHandler:schemeHandler forURLScheme:@"app"];
    }

    self.web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, self.size.width, self.size.height) configuration:configuration];
    self.web.navigationDelegate = self;

#ifndef UI_RELEASE
    // Safari's Web Inspector, for development builds.
    if (@available(macOS 13.3, *)) { self.web.inspectable = YES; }
#endif

    self.window = [[NSWindow alloc] initWithContentRect:self.web.frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"Eskiu UI · macOS / WKWebView";
    self.window.contentView = self.web;
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    [self.window setMinSize:NSMakeSize(320, 240)];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [self setupMenu];

    // Set dock application icon if bundled
    NSString *iconPath = [[NSBundle mainBundle] pathForResource:@"AppIcon" ofType:@"icns"];
    if (!iconPath) iconPath = [[NSBundle mainBundle] pathForResource:@"icon" ofType:@"png"];
    if (iconPath) {
        NSImage *icon = [[NSImage alloc] initWithContentsOfFile:iconPath];
        if (icon) [NSApp setApplicationIconImage:icon];
    }

    // Observe title dynamically for SPA route changes
    [self.web addObserver:self forKeyPath:@"title" options:NSKeyValueObservingOptionNew context:nil];

    [NSApp activateIgnoringOtherApps:YES];
    [self.web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:self.url]]];
}

- (void)dealloc {
    @try {
        [self.web removeObserver:self forKeyPath:@"title"];
    } @catch (id exception) {}
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    if ([keyPath isEqualToString:@"title"] && self.web.title.length > 0) {
        self.window.title = self.web.title;
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return YES; }

- (void)webView:(WKWebView *)web didFinishNavigation:(WKNavigation *)navigation {
    if (web.title.length) self.window.title = web.title;
}

- (void)webView:(WKWebView *)web didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    fprintf(stderr, "desktop load failed: %s\n", error.description.UTF8String);
    if (self.testing) exit(1);
}

- (void)userContentController:(WKUserContentController *)controller didReceiveScriptMessage:(WKScriptMessage *)message {
    if ([message.name isEqualToString:@"console"]) {
        if ([message.body isKindOfClass:NSString.class]) {
            printf("%s\n", [message.body UTF8String]);
            fflush(stdout);
        }
        return;
    }
    if (![message.body isKindOfClass:NSDictionary.class]) return;
    NSDictionary *report = message.body;
    NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
    printf("%s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
    fflush(stdout);
    BOOL passed = [report[@"status"] isEqual:@"PASS"];
    if (!self.testing) return;
    [self.web takeSnapshotWithConfiguration:nil completionHandler:^(NSImage *image, NSError *error) {
        if (error || !image) { fprintf(stderr, "snapshot failed\n"); exit(1); }
        NSBitmapImageRep *bitmap = [NSBitmapImageRep imageRepWithData:image.TIFFRepresentation];
        NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (![png writeToFile:self.screenshot atomically:YES]) { fprintf(stderr, "snapshot write failed\n"); exit(1); }
        exit(passed ? 0 : 1);
    }];
}
@end

int main(int argc, const char **argv) {
    @autoreleasepool {
        DesktopHost *host = [DesktopHost new];

        NSString *webDir = [[NSBundle mainBundle] pathForResource:@"web" ofType:nil];
        if (!webDir) {
            NSString *exeDir = [[[NSProcessInfo processInfo] arguments][0] stringByDeletingLastPathComponent];
            NSString *candidate = [exeDir stringByAppendingPathComponent:@"web"];
            if ([[NSFileManager defaultManager] fileExistsAtPath:[candidate stringByAppendingPathComponent:@"index.html"]]) {
                webDir = candidate;
            }
        }

        if (argc > 1) {
            host.url = [NSString stringWithUTF8String:argv[1]];
        } else if (webDir) {
            host.localWebDir = webDir;
            host.url = @"app://localhost/";
        } else {
            host.url = @"http://127.0.0.1:8765/?example=counter";
        }

        host.testing = argc > 2;
        host.screenshot = argc > 2 ? [NSString stringWithUTF8String:argv[2]] : @"desktop.png";
        // An optional window size, WIDTHxHEIGHT in points.
        int width = 1100, height = 860;
        if (argc > 3) sscanf(argv[3], "%dx%d", &width, &height);
        host.size = NSMakeSize(width, height);
        NSApplication *application = NSApplication.sharedApplication;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        application.delegate = host;
        [application run];
    }
    return 0;
}
