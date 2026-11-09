#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

// Custom scheme handler to serve bundled web assets offline without CORS restrictions.
@interface LocalSchemeHandler : NSObject <WKURLSchemeHandler>
@property(copy) NSString *baseDir;
@end

@implementation LocalSchemeHandler
- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
    NSURL *url = task.request.URL;
    NSString *path = url.path;
    if (!path.length || [path isEqualToString:@"/"]) {
        path = @"/index.html";
    }
    while ([path hasPrefix:@"/"]) {
        path = [path substringFromIndex:1];
    }
    // Only files inside the app's web directory.
    if ([path.pathComponents containsObject:@".."]) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorNoPermissionsToReadFile userInfo:nil]];
        return;
    }
    NSString *fullPath = [self.baseDir stringByAppendingPathComponent:path];
    NSData *data = [NSData dataWithContentsOfFile:fullPath];
    // A path with no extension is a route: the page. A missing file is an error.
    if (!data && path.pathExtension.length == 0) {
        fullPath = [self.baseDir stringByAppendingPathComponent:@"index.html"];
        data = [NSData dataWithContentsOfFile:fullPath];
    }
    if (!data) {
        NSError *error = [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorFileDoesNotExist userInfo:nil];
        [task didFailWithError:error];
        return;
    }
    NSString *mime = @"application/octet-stream";
    NSString *ext = fullPath.pathExtension.lowercaseString;
    if ([ext isEqualToString:@"html"]) mime = @"text/html; charset=utf-8";
    else if ([ext isEqualToString:@"js"] || [ext isEqualToString:@"mjs"]) mime = @"application/javascript; charset=utf-8";
    else if ([ext isEqualToString:@"wasm"]) mime = @"application/wasm";
    else if ([ext isEqualToString:@"css"]) mime = @"text/css; charset=utf-8";
    else if ([ext isEqualToString:@"json"] || [ext isEqualToString:@"webmanifest"]) mime = @"application/json";
    else if ([ext isEqualToString:@"png"]) mime = @"image/png";
    else if ([ext isEqualToString:@"jpg"] || [ext isEqualToString:@"jpeg"]) mime = @"image/jpeg";
    else if ([ext isEqualToString:@"gif"]) mime = @"image/gif";
    else if ([ext isEqualToString:@"webp"]) mime = @"image/webp";
    else if ([ext isEqualToString:@"woff"]) mime = @"font/woff";
    else if ([ext isEqualToString:@"woff2"]) mime = @"font/woff2";
    else if ([ext isEqualToString:@"ttf"]) mime = @"font/ttf";
    else if ([ext isEqualToString:@"mp3"]) mime = @"audio/mpeg";
    else if ([ext isEqualToString:@"mp4"]) mime = @"video/mp4";
    else if ([ext isEqualToString:@"webm"]) mime = @"video/webm";
    else if ([ext isEqualToString:@"svg"]) mime = @"image/svg+xml";
    else if ([ext isEqualToString:@"ico"]) mime = @"image/x-icon";

    NSDictionary *headers = @{
        @"Content-Type": mime,
        @"Content-Length": [NSString stringWithFormat:@"%lu", (unsigned long)data.length],
        @"Access-Control-Allow-Origin": @"*"
    };
    NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:url statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:headers];
    [task didReceiveResponse:response];
    [task didReceiveData:data];
    [task didFinish];
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {}
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
