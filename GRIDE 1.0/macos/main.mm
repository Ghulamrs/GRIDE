// RIDE's window on macOS: an AppKit front end over the core the terminal editor and the Windows Forms window use. `RIDE --version` answers without a window; otherwise the arguments are a project and files to open.
#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstring>

#import "Text.h"
#import "WindowController.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) WindowController* window;
@property(nonatomic, copy) NSString* project;
@property(nonatomic, strong) NSMutableArray<NSString*>* files;
@end

@implementation AppDelegate

- (void)applicationWillFinishLaunching:(NSNotification*)note {
    (void)note;
    self.window = [[WindowController alloc] init];
    NSApp.mainMenu = [self.window makeMainMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    (void)note;
    [self.window showWindow:nil];
    [self.window.window makeKeyAndOrderFront:nil];
    [self.window startWithProject:self.project files:self.files];
    // Come to the front, so the menu bar is RIDE's and not the menus of whatever launched it.
    // Since macOS 14 activation is cooperative and activateIgnoringOtherApps: is ignored; activate
    // is what asks for it now.
    if (@available(macOS 14.0, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
}

// Files dropped on the Dock icon, or opened with RIDE from Finder.
- (void)application:(NSApplication*)sender openFiles:(NSArray<NSString*>*)paths {
    NSString* suffix = Str(ride_project_suffix());
    for (NSString* path in paths) {
        BOOL directory = NO;
        [NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&directory];
        // One of a CCS project's three files stands for the project, as it does on the console: the
        // core opens its folder (src/project.cpp). Shown as XML it was a project nobody could build.
        NSString* leaf = path.lastPathComponent;
        BOOL ccsFile = [leaf isEqualToString:@".project"] || [leaf isEqualToString:@".cproject"] ||
                       [leaf isEqualToString:@".ccsproject"];
        BOOL project = directory || ccsFile || (suffix.length > 0 && [path hasSuffix:suffix]);
        if (self.window.window.isVisible) {
            if (project) [self.window loadProject:path];
            else [self.window openPath:path];
        } else if (project) {
            self.project = path;
        } else {
            [self.files addObject:path];
        }
    }
    [sender replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

// A program left running would outlive the window, and a build would go on writing; both end.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    if (![self.window mayClose]) return NSTerminateCancel;
    [self.window stopEverything];
    if (![self.window stillBuilding]) return NSTerminateNow;
    [self.window quitWhenIdle];
    return NSTerminateLater;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app {
    (void)app;
    return YES;
}

@end

int main(int argc, const char* argv[]) {
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("%s %s\n", ride_product_name(), ride_version());
        return 0;
    }

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        delegate.files = [NSMutableArray array];

        // Arguments as the other two front ends take them; the ones macOS
        // itself adds (-psn_..., -NSDocumentRevisionsDebugMode YES) are skipped.
        NSString* here = NSFileManager.defaultManager.currentDirectoryPath;
        for (int i = 1; i < argc; ++i) {
            const char* word = argv[i];
            if (word[0] == '-') {
                if (std::strcmp(word, "--project") == 0 && i + 1 < argc) {
                    delegate.project = Str(argv[++i]);
                    continue;
                }
                if (std::strncmp(word, "-NS", 3) == 0 || std::strncmp(word, "-Apple", 6) == 0) ++i;
                continue;
            }
            NSString* path = Str(word);
            if (!path.isAbsolutePath) path = [here stringByAppendingPathComponent:path];
            [delegate.files addObject:path.stringByStandardizingPath];
        }

        app.delegate = delegate;
        [app run];
    }
    return 0;
}
