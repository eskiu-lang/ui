#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

// A real macOS desktop host for the shared wasm/DOM renderer. Native-core bridge
// transport is a later step; this host tests WKWebView execution and rendering.
@interface DesktopHost : NSObject <NSApplicationDelegate, WKScriptMessageHandler, WKNavigationDelegate>
@property(strong) NSWindow *window;
@property(strong) WKWebView *web;
@property(copy) NSString *url;
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
    NSString *appName = self.window.title.length ? self.window.title : @"Eskiu UI";
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
    [configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];

    self.web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, self.size.width, self.size.height) configuration:configuration];
    self.web.navigationDelegate = self;

    // Enable web inspector for developer ergonomics
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    if ([self.web respondsToSelector:NSSelectorFromString(@"setInspectable:")]) {
        [self.web performSelector:NSSelectorFromString(@"setInspectable:") withObject:@YES];
    }
    #pragma clang diagnostic pop

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
        host.url = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : @"http://127.0.0.1:8765/?example=counter";
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
