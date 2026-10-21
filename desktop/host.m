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
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    WKWebViewConfiguration *configuration = [WKWebViewConfiguration new];
    [configuration.userContentController addScriptMessageHandler:self name:@"results"];
    self.web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, self.size.width, self.size.height) configuration:configuration];
    self.web.navigationDelegate = self;
    self.window = [[NSWindow alloc] initWithContentRect:self.web.frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"Eskiu UI · macOS / WKWebView";
    self.window.contentView = self.web;
    [self.window center]; [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self.web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:self.url]]];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return YES; }
// The window takes the page's title (an app's name).
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
