#import "WindowController.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#import "LineNumbers.h"
#import "Text.h"

// ---- the model ------------------------------------------------------------------

// One open file. The window has one text view and swaps each file's text
// storage into it, so a file keeps its own text, undo and place.
@interface Sheet : NSObject
@property(nonatomic, copy) NSString* path;  // nil: never saved
@property(nonatomic, strong) NSTextStorage* storage;
@property(nonatomic, strong) NSUndoManager* undo;
@property(nonatomic) BOOL modified;
// The text as it last was on the disk (or empty for a new file): modified is "differs from this", so
// undoing back to it clears the dot - a flag set on the first keystroke and never unset did not.
@property(nonatomic, copy) NSString* savedText;
@property(nonatomic) NSRange selection;
@property(nonatomic) NSPoint scrolled;
@property(nonatomic) int language;  // RIDE_LANG_*, or -1: by the name
// As the file was on the disk, and is written back so (M5): its encoding, its line ending, a BOM.
@property(nonatomic) NSStringEncoding encoding;
@property(nonatomic, copy) NSString* lineEnding;
@property(nonatomic) BOOL byteOrderMark;
// When it was last read or written here, to see a change made elsewhere (M10).
@property(nonatomic, strong) NSDate* stamp;
// Untitled 1, 2...: what tells two unsaved files apart (M7).
@property(nonatomic) NSInteger untitled;
// The lexer's state at the start of each row, and the first row whose colour is out of date.
- (std::vector<int>&)states;
@property(nonatomic) NSInteger staleFrom;
@end

// Project > Open: a .pro, a CCS project's own file, or a folder can be chosen; a source cannot - the panel
// shows it greyed, macOS having no way to leave it out.
@interface ProjectPick : NSObject <NSOpenSavePanelDelegate>
@end

@implementation ProjectPick
- (BOOL)panel:(id)sender shouldEnableURL:(NSURL*)url {
    (void)sender;
    NSNumber* folder = nil;
    [url getResourceValue:&folder forKey:NSURLIsDirectoryKey error:nil];
    if (folder.boolValue) return YES;
    NSString* leaf = url.lastPathComponent;
    return [url.pathExtension isEqualToString:@"pro"] || [leaf isEqualToString:@".project"] ||
           [leaf isEqualToString:@".cproject"] || [leaf isEqualToString:@".ccsproject"];
}
@end

@implementation Sheet {
    std::vector<int> states_;
}
- (std::vector<int>&)states { return states_; }
- (instancetype)init {
    self = [super init];
    if (self) {
        _undo = [[NSUndoManager alloc] init];
        _language = -1;
        _selection = NSMakeRange(0, 0);
        _encoding = NSUTF8StringEncoding;
        _lineEnding = @"\n";
        _staleFrom = 0;
    }
    return self;
}
@end

// A line of the Errors tab.
@interface Issue : NSObject
@property(nonatomic, copy) NSString* file;  // absolute; nil when not known
@property(nonatomic) NSInteger line;
@property(nonatomic) NSInteger column;
@property(nonatomic, copy) NSString* message;
@property(nonatomic) BOOL warning;
@property(nonatomic) BOOL found;  // a hit of Edit > Find in Files rather than a diagnostic
@end

@implementation Issue
@end

// A row of the navigator: a section heading, a project group, or a file.
@interface NavItem : NSObject
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSString* path;  // files only
@property(nonatomic, weak) Sheet* sheet;    // a row of OPEN FILES: the file itself (M7)
@property(nonatomic, strong) NSMutableArray<NavItem*>* children;
@property(nonatomic) BOOL section;
@property(nonatomic, copy) NSString* group;  // the project group a file or group is
@end

@implementation NavItem
- (instancetype)init {
    self = [super init];
    if (self) _children = [NSMutableArray array];
    return self;
}
@end

// What a build hands back to the main thread: copied out of the core's
// objects on the thread that ran it, so nothing the core owns crosses over.
struct Outcome {
    bool ran = false;       // the compiler or program could be started
    bool ok = false;
    bool hasError = false;
    std::string output;
    std::string assembly;
    int assemblyLines = 0;
    std::string errorFile;
    int errorLine = 0;
    int errorColumn = 0;
    std::string errorMessage;
    int status = 0;
    bool stopped = false;   // Build > Stop ended it
    std::string produced;   // a conversion's file
    std::string made;       // what a project build made, which is what runs: <program>.vm for the C6747
    bool madeShalimar = false;
};

// What a build said, copied off the core's object on the thread that ran it (L15: once, not thrice).
static Outcome OutcomeOf(RIDEBuild* built) {
    Outcome outcome;
    if (built == NULL) return outcome;
    outcome.ran = true;
    outcome.ok = ride_build_ok(built) != 0;
    outcome.output = ride_build_output(built);
    outcome.hasError = ride_build_has_error(built) != 0;
    outcome.errorFile = ride_build_error_file(built);
    outcome.errorLine = ride_build_error_line(built);
    outcome.errorColumn = ride_build_error_column(built);
    outcome.errorMessage = ride_build_error_message(built);
    outcome.assembly = ride_build_assembly(built);
    outcome.assemblyLines = ride_build_assembly_lines(built);
    outcome.stopped = ride_build_stopped(built) != 0;
    outcome.made = ride_build_made(built);
    outcome.madeShalimar = ride_build_made_shalimar(built) != 0;
    return outcome;
}

// The same for a run the window watched, once it is over.
static Outcome OutcomeOf(RIDERunning* running) {
    Outcome outcome;
    outcome.ran = true;
    outcome.ok = ride_running_built(running) != 0;
    outcome.output = ride_running_build_output(running);
    outcome.hasError = ride_running_has_error(running) != 0;
    outcome.errorFile = ride_running_error_file(running);
    outcome.errorLine = ride_running_error_line(running);
    outcome.errorColumn = ride_running_error_column(running);
    outcome.errorMessage = ride_running_error_message(running);
    outcome.status = ride_running_status(running);
    outcome.stopped = ride_running_stopped(running) != 0;
    return outcome;
}

// ---- the question a build may ask -----------------------------------------------

// Installed once: "the project's own masm/link/lnk6x did not build it - use
// the native tools instead?" It is asked on whichever thread is building, and
// an alert belongs to the main thread, so it waits there for the answer.
static int AskNativeInWindow(const char* question) {
    NSString* text = Str(question);
    __block int answer = 0;
    void (^ask)(void) = ^{
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = text;
        alert.informativeText = @"A yes builds it again with them; a no leaves the build failed.";
        [alert addButtonWithTitle:@"Yes"];
        [alert addButtonWithTitle:@"No"];
        answer = [alert runModal] == NSAlertFirstButtonReturn ? 1 : 0;
    };
    if ([NSThread isMainThread]) ask();
    else dispatch_sync(dispatch_get_main_queue(), ask);
    return answer;
}

// ---- menu tags ------------------------------------------------------------------

enum {
    kTagArchBase = 1000,     // + index into ride_arch()
    kTagToolBase = 2000,     // + RIDE_TOOL_*
    kTagLangBase = 3000,     // + RIDE_LANG_*, and kTagLangAuto
    kTagLangAuto = 3999,
    kTagConfigBase = 4000,   // + RIDE_CONFIG_*
    kTagPanelBase = 5000,    // + tab index
};

enum { kPanelErrors = 0, kPanelProgress = 1, kPanelOutput = 2 };

static void RunOutput(void* user, const char* bytes, int size, int stream);

// What RunOutput hands to the main thread.
// ---- Compiler Options: a tabbed dialog drawn from the bridge's table (src/options.h) ------------
// One tab per compiler and one row per option, a configuration chosen at the top, the command line
// the build will run at the foot. It edits the bridge's draft; OK commits it, Cancel drops it.
@interface OptionsDialog : NSObject
- (instancetype)initWithProject:(RIDEProject*)project config:(int)config arch:(NSString*)arch;
- (BOOL)run:(NSWindow*)parent;
- (void)selectTab:(int)tab;
@end

@implementation OptionsDialog {
    RIDEProject* project_;
    int config_;
    NSString* arch_;
    NSPanel* panel_;
    NSTabView* tabs_;
    NSPopUpButton* configPick_;
    NSTextField* preview_;
    NSMutableDictionary<NSString*, NSControl*>* controls_;
    NSTextView* ccsText_;
    NSModalResponse answer_;
}

- (instancetype)initWithProject:(RIDEProject*)project config:(int)config arch:(NSString*)arch {
    if ((self = [super init])) {
        project_ = project;
        config_ = config;
        arch_ = arch;
        controls_ = [NSMutableDictionary dictionary];
        ride_options_begin(project_);
        [self build];
        [self push];
    }
    return self;
}

- (void)build {
    const CGFloat width = 640, height = 520;
    panel_ = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                        styleMask:NSWindowStyleMaskTitled
                                          backing:NSBackingStoreBuffered defer:NO];
    panel_.title = @"Compiler Options";
    NSView* content = panel_.contentView;

    // Which configuration's options are edited - not which one builds, which is Build > Debug/Release.
    NSTextField* configLabel = [NSTextField labelWithString:@"Edit options for:"];
    configLabel.frame = NSMakeRect(20, height - 40, 120, 20);
    [content addSubview:configLabel];
    configPick_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(140, height - 44, 130, 26) pullsDown:NO];
    [configPick_ addItemsWithTitles:@[ @"Debug", @"Release" ]];
    [configPick_ selectItemAtIndex:config_];
    configPick_.target = self;
    configPick_.action = @selector(configChosen:);
    [content addSubview:configPick_];
    NSTextField* target = [NSTextField labelWithString:[NSString stringWithFormat:@"Target: %@  (the Target menu)", arch_]];
    target.frame = NSMakeRect(290, height - 40, 330, 20);
    [content addSubview:target];

    tabs_ = [[NSTabView alloc] initWithFrame:NSMakeRect(14, 110, width - 28, height - 164)];
    NSSize page = NSMakeSize(width - 56, height - 210);
    for (int t = 0; t < ride_option_tab_count(); ++t) {
        NSString* name = Str(ride_option_tab_name(t));
        NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, page.width, page.height)];
        CGFloat y = page.height - 10;
        for (int i = 0; i < ride_option_count(); ++i) {
            if (![Str(ride_option_tab(i)) isEqualToString:name]) continue;
            NSString* identifier = Str(ride_option_id(i));
            NSString* label = Str(ride_option_label(i));
            int control = ride_option_control(i);
            NSControl* made = nil;
            y -= 26;
            if (control == RIDE_OPTION_CHECK) {
                NSButton* box = [NSButton checkboxWithTitle:label target:self action:@selector(changed:)];
                box.frame = NSMakeRect(10, y, page.width - 20, 22);
                made = box;
            } else {
                NSTextField* title = [NSTextField labelWithString:label];
                title.frame = NSMakeRect(10, y + 2, 200, 20);
                [view addSubview:title];
                if (control == RIDE_OPTION_CHOICE) {
                    NSPopUpButton* pick = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(215, y - 2, 200, 26) pullsDown:NO];
                    [pick addItemsWithTitles:[Str(ride_option_choices(i)) componentsSeparatedByString:@"|"]];
                    pick.target = self;
                    pick.action = @selector(changed:);
                    made = pick;
                } else {
                    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(215, y, page.width - 230, 22)];
                    field.target = self;
                    field.action = @selector(changed:);
                    field.delegate = (id<NSTextFieldDelegate>)self;
                    made = field;
                }
            }
            made.identifier = identifier;
            [view addSubview:made];
            controls_[identifier] = made;
            // No line under a control: what it does, or why it is greyed here, is its tooltip.
            NSString* why = Str(ride_options_why(Utf8(identifier), Utf8(arch_)));
            BOOL usable = ride_options_available(Utf8(identifier), Utf8(arch_)) != 0;
            made.enabled = usable;
            NSString* tip = usable ? Str(ride_option_hint(i)) : [@"Unavailable: " stringByAppendingString:why];
            if (tip.length > 0) made.toolTip = tip;
            y -= 10;
        }
        NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:name];
        item.label = name;
        item.view = view;
        [tabs_ addTabViewItem:item];
    }
    // A CCS project's options are read from its files and shown, not edited: a tab of what was
    // read and where each option went, every control greyed, and OK keeps nothing (bridge.h).
    if (ride_project_is_ccs(project_)) {
        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, page.width, page.height)];
        NSTextView* text = [[NSTextView alloc] initWithFrame:scroll.bounds];
        text.editable = NO;
        text.font = [NSFont userFixedPitchFontOfSize:[NSFont smallSystemFontSize]];
        text.string = Str(ride_project_ccs_mapping(project_, config_));
        scroll.documentView = text;
        scroll.hasVerticalScroller = YES;
        NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"CCS project"];
        item.label = @"CCS project";
        item.view = scroll;
        [tabs_ insertTabViewItem:item atIndex:0];
        for (NSString* identifier in controls_) {
            controls_[identifier].enabled = NO;
            controls_[identifier].toolTip = @"Read from the CCS project - edit it in CCS";
        }
        ccsText_ = text;
    }
    tabs_.delegate = (id<NSTabViewDelegate>)self;
    [content addSubview:tabs_];

    NSTextField* heading = [NSTextField labelWithString:@"Command line:"];
    heading.frame = NSMakeRect(20, 84, 200, 18);
    [content addSubview:heading];
    preview_ = [NSTextField wrappingLabelWithString:@""];
    preview_.selectable = YES;
    preview_.font = [NSFont userFixedPitchFontOfSize:[NSFont smallSystemFontSize]];
    preview_.frame = NSMakeRect(20, 50, width - 40, 34);
    [content addSubview:preview_];

    NSButton* reset = [NSButton buttonWithTitle:@"Restore Defaults" target:self action:@selector(restoreDefaults:)];
    reset.frame = NSMakeRect(14, 12, 150, 30);
    reset.enabled = ride_project_is_ccs(project_) == 0;
    [content addSubview:reset];
    NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancel:)];
    cancel.frame = NSMakeRect(width - 214, 12, 96, 30);
    cancel.keyEquivalent = @"\e";
    [content addSubview:cancel];
    NSButton* ok = [NSButton buttonWithTitle:@"OK" target:self action:@selector(ok:)];
    ok.frame = NSMakeRect(width - 110, 12, 96, 30);
    ok.keyEquivalent = @"\r";
    [content addSubview:ok];
}

// The draft's values for the configuration shown, into the controls; and back.
- (void)push {
    for (NSString* identifier in controls_) {
        NSControl* control = controls_[identifier];
        NSString* value = Str(ride_options_value(project_, config_, Utf8(identifier)));
        if ([control isKindOfClass:NSPopUpButton.class]) {
            // A value the choices do not hold - a hand-edited "-O3" - is shown as one more choice,
            // not quietly replaced by whatever was selected before at the next edit or OK.
            NSPopUpButton* pick = (NSPopUpButton*)control;
            if (value.length > 0 && [pick indexOfItemWithTitle:value] < 0) [pick addItemWithTitle:value];
            [pick selectItemWithTitle:value];
        }
        else if ([control isKindOfClass:NSButton.class]) ((NSButton*)control).state = [value isEqualToString:@"1"] ? NSControlStateValueOn : NSControlStateValueOff;
        else control.stringValue = value;
    }
    [self showPreview];
}

- (void)pull {
    for (NSString* identifier in controls_) {
        NSControl* control = controls_[identifier];
        NSString* value;
        if ([control isKindOfClass:NSPopUpButton.class]) value = ((NSPopUpButton*)control).titleOfSelectedItem ?: @"";
        else if ([control isKindOfClass:NSButton.class]) value = ((NSButton*)control).state == NSControlStateValueOn ? @"1" : @"0";
        else value = control.stringValue;
        ride_options_set(project_, config_, Utf8(identifier), Utf8(value));
    }
}

- (void)showPreview {
    NSInteger tab = tabs_.selectedTabViewItem ? [tabs_ indexOfTabViewItem:tabs_.selectedTabViewItem] : 0;
    // The bridge's table is General, c90, cpp11, shalimar; a CCS project's tab stands in front of them.
    if (ccsText_ != nil) tab = tab > 0 ? tab - 1 : 0;
    preview_.stringValue = Str(ride_options_preview(project_, config_, (int)tab, Utf8(arch_)));
}

- (void)changed:(id)sender { (void)sender; [self pull]; [self showPreview]; }
- (void)controlTextDidChange:(NSNotification*)note { (void)note; [self pull]; [self showPreview]; }
- (void)tabView:(NSTabView*)view didSelectTabViewItem:(NSTabViewItem*)item { (void)view; (void)item; [self showPreview]; }

- (void)configChosen:(id)sender {
    (void)sender;
    if (ccsText_ == nil) [self pull];
    config_ = (int)configPick_.indexOfSelectedItem;
    [self push];
    if (ccsText_ != nil) ccsText_.string = Str(ride_project_ccs_mapping(project_, config_));
}

- (void)restoreDefaults:(id)sender { (void)sender; ride_options_reset(project_, config_); [self push]; }
- (void)cancel:(id)sender { (void)sender; answer_ = NSModalResponseCancel; [NSApp stopModal]; }
- (void)ok:(id)sender { (void)sender; if (ccsText_ == nil) [self pull]; answer_ = NSModalResponseOK; [NSApp stopModal]; }

- (BOOL)run:(NSWindow*)parent {
    // A sheet on the window it belongs to, run modally as before: a free panel looked pasted onto
    // the editor, and opened while the window was on another Space it appeared nowhere at all.
    if (parent != nil) [parent beginSheet:panel_ completionHandler:nil];
    else [panel_ center];
    [NSApp runModalForWindow:panel_];
    if (parent != nil) [parent endSheet:panel_];
    [panel_ orderOut:nil];
    return answer_ == NSModalResponseOK && ride_options_commit(project_) != 0;
}

- (void)selectTab:(int)tab {
    if (ccsText_ != nil) tab = 0;   // the CCS tab first: it is what there is to read
    if (tab >= 0 && tab < tabs_.numberOfTabViewItems) [tabs_ selectTabViewItemAtIndex:tab];
    [self showPreview];
}
@end

@interface WindowController ()
- (void)runSaid:(const std::string&)piece stream:(int)stream;
- (void)queueOutput:(const char*)bytes size:(int)size stream:(int)stream;
- (void)drainOutput;
- (void)showInOutput:(NSString*)text;
- (BOOL)escapeFromFindBar;
- (void)runEnded;
@end

static const CGFloat kStatusHeight = 24;
static const CGFloat kJumpBarHeight = 26;

@implementation WindowController {
    RIDEProject* project_;

    // The compilers, found once: an environment variable, else beside the
    // editor, else by name on the PATH.
    NSString* cc1_;
    NSString* cl_;
    NSString* shc_;
    NSString* cxx1_;

    NSString* arch_;
    int toolKind_;
    int config_;
    int indentWidth_;
    int indentTabs_;
    int indentCase_;
    BOOL numbers_;
    BOOL busy_;
    BOOL finding_;     // a Find in Files is walking on another thread
    BOOL started_;
    BOOL closing_;  // the window is going; nothing more to ask
    NSDate* workStarted_;

    NSFont* codeFont_;
    NSMutableArray<Sheet*>* sheets_;
    Sheet* current_;
    Sheet* blank_;  // what the view shows with no file open

    NSMutableArray<Issue*>* issues_;
    NSMutableArray<NavItem*>* navRoots_;
    NSString* projectDirectory_;

    // Views.
    NSSplitView* across_;     // navigator | the right-hand side
    NSSplitView* down_;       // editor | bottom panel
    NSView* navigatorPane_;
    NSOutlineView* navigator_;
    NSView* editorPane_;
    NSTextField* jumpBar_;
    NSTextField* compilerHint_;
    NSScrollView* codeScroll_;
    CodeView* code_;
    LineNumbers* gutter_;
    NSView* panelPane_;
    NSTabView* panel_;
    NSTableView* issueTable_;
    NSProgressIndicator* progressBar_;
    NSTextField* progressTitle_;
    NSTextView* progressLog_;
    NSTextView* output_;
    NSTextField* statusMessage_;
    NSTextField* statusBuild_;
    NSTextField* statusWhere_;
    NSProgressIndicator* statusSpinner_;
    CGFloat panelHeight_;      // remembered while the panel is hidden
    NSTextField* argsField_;  // Build > Command-line arguments: what Run and Run Project hand the program
    NSMenu* buildMenu_;       // the Command-line arguments submenu
    NSString* fileArgs_;      // a file run on its own: this session's line; a project keeps its own
    NSButton* fold_;          // the panel's minimise button: on, the panel is only its tab strip
    BOOL folded_;

    NSTimer* recolourTimer_;
    NSMenu* recentFilesMenu_;
    NSMenu* recentProjectsMenu_;

    // A program running while the window watches it (bridge.h, README.md "Input, and Stop"):
    // its output arrives in pieces, a cut character kept in pending_ until the rest comes.
    RIDERunning* running_;
    // Whether the program itself has said anything yet, as against its compile: the input line takes
    // the keyboard then, and the status line says it is running.
    BOOL programSpoke_;
    // A quit is waiting for the build thread to come back (main.mm's NSTerminateLater).
    BOOL quitWhenIdle_;
    // Escape typed into the find bar's own field: the field keeps it, so the window watches for it.
    id escapeMonitor_;
    std::string pending_[3];
    // What the program's thread has said and the main thread not yet shown: one lock, a few bytes
    // queued per stream, and one drain at a time scheduled - not one main-queue block per chunk.
    std::mutex inboxLock_;
    std::string inbox_[3];
    bool drainQueued_;
    NSTextField* inputLine_;
    NSInteger untitledCount_;
    NSInteger staleRows_;  // how many rows from current_.staleFrom an edit reached
    NSString* runSource_;   // the file Run File built, for what its errors are about
    NSString* runCompiler_;
    NSString* runProgram_;  // Run Project's program; nil for Run File
}

// ---- starting -------------------------------------------------------------------

// A compiler: its environment variable, else beside the editor inside the bundle, else - only
// when the app sits in a checkout, beside macos/Makefile - the checkout's bin/, else its bare name,
// which the core looks for on PATH. Never a directory the app merely happens to be in (L6).
static NSString* FoundCompiler(NSString* variable, NSString* name) {
    NSString* said = NSProcessInfo.processInfo.environment[variable];
    if (said.length > 0) return said;

    NSFileManager* files = NSFileManager.defaultManager;
    NSString* program = [name stringByAppendingString:@".exe"];
    NSMutableArray<NSString*>* places = [NSMutableArray array];
    NSString* exe = NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent;
    if (exe.length > 0) [places addObject:exe];
    NSString* up = NSBundle.mainBundle.bundlePath.stringByDeletingLastPathComponent;
    for (int step = 0; step < 4 && up.length > 1; ++step) {
        if ([files fileExistsAtPath:[up stringByAppendingPathComponent:@"macos/Makefile"]]) {
            [places addObject:[up stringByAppendingPathComponent:@"bin"]];
            break;
        }
        up = up.stringByDeletingLastPathComponent;
    }
    for (NSString* place in places) {
        NSString* candidate = [place stringByAppendingPathComponent:program];
        if ([files isExecutableFileAtPath:candidate]) return candidate;
    }
    return program;
}

- (instancetype)init {
    NSRect frame = NSMakeRect(0, 0, 1180, 800);
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.minSize = NSMakeSize(760, 520);
    window.releasedWhenClosed = NO;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    [window center];
    [window setFrameAutosaveName:@"RIDEMainWindow"];

    self = [super initWithWindow:window];
    if (self) {
        project_ = ride_project_new();
        ride_ask_native(AskNativeInWindow);

        cc1_ = FoundCompiler(@"C90", Str(ride_compiler_name(RIDE_COMPILER_C)));
        cxx1_ = FoundCompiler(@"CPP11", Str(ride_compiler_name(RIDE_COMPILER_CPP)));
        shc_ = FoundCompiler(@"SHALIMAR", Str(ride_compiler_name(RIDE_COMPILER_SHALIMAR)));
        // No cl on a Mac; the slot is kept empty and the core's default stands.
        cl_ = @"";

        arch_ = Str(ride_host_arch());
        if (arch_.length == 0) arch_ = Str(ride_arch(0));
        toolKind_ = ride_default_compiler();
        config_ = ride_configuration();
        indentWidth_ = ride_default_indent_width();
        indentTabs_ = ride_default_indent_tabs();
        indentCase_ = 0;
        numbers_ = YES;
        busy_ = NO;
        started_ = NO;

        codeFont_ = [self rememberedFont];
        // Escape in the find bar closes it and gives the text back the keyboard. The bar's field
        // swallows the key - pressed there it did nothing, and the editor stayed greyed under it.
        __weak WindowController* weakSelf = self;
        escapeMonitor_ = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                               handler:^NSEvent*(NSEvent* event) {
            WindowController* strong = weakSelf;
            if (strong == nil || event.keyCode != 53 || event.window != strong.window) return event;
            return [strong escapeFromFindBar] ? nil : event;
        }];
        sheets_ = [NSMutableArray array];
        issues_ = [NSMutableArray array];
        navRoots_ = [NSMutableArray array];
        blank_ = [[Sheet alloc] init];
        blank_.storage = [[NSTextStorage alloc] initWithString:@""
                                                    attributes:[self codeAttributes]];

        window.delegate = self;
        [self lay];
        [self refreshTitle];
        [self fillNavigator];
        [self sayBuild];
        [self say:@"ready"];
    }
    return self;
}

- (void)dealloc {
    if (project_ != NULL) ride_project_free(project_);
}

- (void)startWithProject:(NSString*)project files:(NSArray<NSString*>*)files {
    // ~/.ride/settings.json, made with the installer's defaults the first time.
    ride_write_install_file_if_absent();
    if (project.length > 0) [self loadProject:project];
    BOOL named = NO;
    for (NSString* file in files) {
        if (file.length == 0) continue;
        [self openPath:file];
        named = YES;
    }
    if (!named) [self openFirstOfProject];
    started_ = YES;
    if (current_ == nil) [self showSheet:nil];
}

// The face code is drawn in: remembered in ~/.ride/state.json as "Name size",
// the person's and not the project's. A name this Mac lacks is the default.
- (NSFont*)rememberedFont {
    NSString* said = Str(ride_code_font());
    NSRange cut = [said rangeOfString:@" " options:NSBackwardsSearch];
    if (cut.location != NSNotFound && cut.location > 0) {
        NSString* name = [said substringToIndex:cut.location];
        double points = [[said substringFromIndex:cut.location + 1] doubleValue];
        if (points >= 6 && points <= 72) {
            NSFont* font = [NSFont fontWithName:name size:points];
            if (font != nil) return font;
        }
    }
    // Courier New 18, regular: the default since 03-10-2026, the user's choice.
    NSFont* courier = [NSFont fontWithName:@"CourierNewPSMT" size:18];
    return courier != nil ? courier : [NSFont monospacedSystemFontOfSize:18 weight:NSFontWeightRegular];
}

- (NSDictionary*)codeAttributes {
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    NSDictionary* measure = @{NSFontAttributeName : codeFont_};
    CGFloat space = [@" " sizeWithAttributes:measure].width;
    int width = indentWidth_ > 0 ? indentWidth_ : 4;
    paragraph.defaultTabInterval = space * width;
    paragraph.tabStops = @[];
    return @{
        NSFontAttributeName : codeFont_,
        NSForegroundColorAttributeName : [NSColor textColor],
        NSParagraphStyleAttributeName : paragraph,
    };
}

// ---- laying the window out -----------------------------------------------------

static NSTextField* Label(NSString* text) {
    NSTextField* label = [NSTextField labelWithString:text];
    label.lineBreakMode = NSLineBreakByTruncatingTail;
    label.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    return label;
}

static NSTextView* LogView(NSScrollView* inside, NSFont* font) {
    NSSize size = inside.contentSize;
    NSTextView* text = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    text.minSize = NSMakeSize(0, size.height);
    text.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    text.verticallyResizable = YES;
    text.horizontallyResizable = NO;
    text.autoresizingMask = NSViewWidthSizable;
    text.textContainer.containerSize = NSMakeSize(size.width, CGFLOAT_MAX);
    text.textContainer.widthTracksTextView = YES;
    text.editable = NO;
    text.selectable = YES;
    text.richText = NO;
    text.font = font;
    text.textColor = [NSColor textColor];
    text.textContainerInset = NSMakeSize(4, 4);
    inside.documentView = text;
    return text;
}

static NSScrollView* Scroller(NSRect frame) {
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:frame];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = NO;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSNoBorder;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    return scroll;
}

- (void)lay {
    NSView* content = self.window.contentView;
    NSRect bounds = content.bounds;

    // The one-line status bar under everything.
    NSView* status = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(bounds), kStatusHeight)];
    status.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    NSBox* rule = [[NSBox alloc] initWithFrame:NSMakeRect(0, kStatusHeight - 1, NSWidth(bounds), 1)];
    rule.boxType = NSBoxSeparator;
    rule.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [status addSubview:rule];

    statusSpinner_ = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(8, 4, 16, 16)];
    statusSpinner_.style = NSProgressIndicatorStyleSpinning;
    statusSpinner_.controlSize = NSControlSizeSmall;
    statusSpinner_.displayedWhenStopped = NO;
    [status addSubview:statusSpinner_];

    statusMessage_ = Label(@"");
    statusMessage_.frame = NSMakeRect(28, 4, NSWidth(bounds) - 28 - 470, 16);
    statusMessage_.autoresizingMask = NSViewWidthSizable;
    [status addSubview:statusMessage_];

    statusBuild_ = Label(@"");
    statusBuild_.alignment = NSTextAlignmentRight;
    statusBuild_.textColor = [NSColor secondaryLabelColor];
    statusBuild_.frame = NSMakeRect(NSWidth(bounds) - 460, 4, 330, 16);
    statusBuild_.autoresizingMask = NSViewMinXMargin;
    [status addSubview:statusBuild_];

    statusWhere_ = Label(@"");
    statusWhere_.alignment = NSTextAlignmentRight;
    statusWhere_.frame = NSMakeRect(NSWidth(bounds) - 124, 4, 112, 16);
    statusWhere_.autoresizingMask = NSViewMinXMargin;
    [status addSubview:statusWhere_];
    [content addSubview:status];

    // Navigator | (editor over panel), from the status bar to the title bar: the menus are the Mac's
    // own menu bar, and the row that repeated them inside the window is gone (02-10-2026).
    NSRect rest = NSMakeRect(0, kStatusHeight, NSWidth(bounds), NSHeight(bounds) - kStatusHeight);
    across_ = [[NSSplitView alloc] initWithFrame:rest];
    across_.vertical = YES;
    across_.dividerStyle = NSSplitViewDividerStyleThin;
    across_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    across_.delegate = self;
    across_.autosaveName = @"RIDENavigatorSplit";

    [self layNavigator:NSMakeRect(0, 0, 240, NSHeight(rest))];
    [across_ addSubview:navigatorPane_];

    down_ = [[NSSplitView alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(rest) - 241, NSHeight(rest))];
    down_.vertical = NO;
    down_.dividerStyle = NSSplitViewDividerStyleThin;
    down_.delegate = self;
    down_.autosaveName = @"RIDEPanelSplit";
    down_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    CGFloat panelHeight = floor(NSHeight(rest) / 4);
    [self layEditor:NSMakeRect(0, 0, NSWidth(down_.frame), NSHeight(rest) - panelHeight - 1)];
    [self layPanel:NSMakeRect(0, 0, NSWidth(down_.frame), panelHeight)];
    [down_ addSubview:editorPane_];
    [down_ addSubview:panelPane_];
    [across_ addSubview:down_];
    [content addSubview:across_];

    [across_ adjustSubviews];
    [down_ adjustSubviews];
    [across_ setPosition:240 ofDividerAtIndex:0];
    // The bottom panel takes a quarter of the window.
    [down_ setPosition:NSHeight(rest) - panelHeight ofDividerAtIndex:0];
    panelHeight_ = panelHeight;
}

- (void)layNavigator:(NSRect)frame {
    navigatorPane_ = [[NSView alloc] initWithFrame:frame];

    NSScrollView* scroll = Scroller(navigatorPane_.bounds);
    navigator_ = [[NSOutlineView alloc] initWithFrame:scroll.bounds];
    // The one column is the pane's width and follows it: left at NSTableColumn's default of about a
    // hundred points it cut "account.c" to "acc…t.c" with the pane half empty.
    navigator_.autoresizingMask = NSViewWidthSizable;
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    column.width = scroll.contentSize.width;
    column.resizingMask = NSTableColumnAutoresizingMask;
    [navigator_ addTableColumn:column];
    navigator_.outlineTableColumn = column;
    navigator_.headerView = nil;
    navigator_.style = NSTableViewStyleSourceList;
    navigator_.floatsGroupRows = NO;
    navigator_.rowSizeStyle = NSTableViewRowSizeStyleDefault;
    navigator_.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;
    navigator_.dataSource = self;
    navigator_.delegate = self;
    navigator_.target = self;
    navigator_.action = @selector(navigatorClicked:);
    navigator_.doubleAction = @selector(navigatorClicked:);
    navigator_.autosaveExpandedItems = NO;

    NSMenu* context = [[NSMenu alloc] initWithTitle:@""];
    [context addItemWithTitle:@"New File..." action:@selector(newProjectFile:) keyEquivalent:@""];
    [context addItemWithTitle:@"Add Files..." action:@selector(addFiles:) keyEquivalent:@""];
    [context addItem:[NSMenuItem separatorItem]];
    [context addItemWithTitle:@"Rename..." action:@selector(renameFile:) keyEquivalent:@""];
    [context addItemWithTitle:@"Move to Group..." action:@selector(moveToGroup:) keyEquivalent:@""];
    [context addItemWithTitle:@"Remove from Project" action:@selector(removeFromProject:) keyEquivalent:@""];
    [context addItemWithTitle:@"Delete..." action:@selector(deleteFile:) keyEquivalent:@""];
    [context addItem:[NSMenuItem separatorItem]];
    [context addItemWithTitle:@"Show in Finder" action:@selector(showInFinder:) keyEquivalent:@""];
    for (NSMenuItem* item in context.itemArray) item.target = self;
    navigator_.menu = context;

    scroll.documentView = navigator_;
    [navigatorPane_ addSubview:scroll];
}

- (void)layEditor:(NSRect)frame {
    editorPane_ = [[NSView alloc] initWithFrame:frame];

    // The jump bar: which file, and the compiler the next build will use.
    NSView* bar = [[NSView alloc] initWithFrame:NSMakeRect(0, NSHeight(frame) - kJumpBarHeight,
                                                           NSWidth(frame), kJumpBarHeight)];
    bar.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    jumpBar_ = Label(@"");
    jumpBar_.frame = NSMakeRect(10, 5, NSWidth(frame) - 200, 16);
    jumpBar_.autoresizingMask = NSViewWidthSizable;
    jumpBar_.lineBreakMode = NSLineBreakByTruncatingHead;
    [bar addSubview:jumpBar_];
    compilerHint_ = Label(@"");
    compilerHint_.alignment = NSTextAlignmentRight;
    compilerHint_.font = [NSFont boldSystemFontOfSize:[NSFont smallSystemFontSize]];
    compilerHint_.textColor = [NSColor secondaryLabelColor];
    compilerHint_.frame = NSMakeRect(NSWidth(frame) - 186, 5, 176, 16);
    compilerHint_.autoresizingMask = NSViewMinXMargin;
    [bar addSubview:compilerHint_];
    NSBox* rule = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(frame), 1)];
    rule.boxType = NSBoxSeparator;
    rule.autoresizingMask = NSViewWidthSizable;
    [bar addSubview:rule];
    [editorPane_ addSubview:bar];

    NSRect below = NSMakeRect(0, 0, NSWidth(frame), NSHeight(frame) - kJumpBarHeight);
    codeScroll_ = [[NSScrollView alloc] initWithFrame:below];
    codeScroll_.hasVerticalScroller = YES;
    codeScroll_.hasHorizontalScroller = YES;
    codeScroll_.autohidesScrollers = YES;
    codeScroll_.borderType = NSNoBorder;
    codeScroll_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    // TextKit 1, built by hand, so that each file's storage can be swapped
    // into the one layout manager.
    NSSize size = codeScroll_.contentSize;
    NSLayoutManager* layout = [[NSLayoutManager alloc] init];
    NSTextContainer* container = [[NSTextContainer alloc]
        initWithContainerSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
    container.widthTracksTextView = NO;
    [layout addTextContainer:container];
    [blank_.storage addLayoutManager:layout];

    code_ = [[CodeView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)
                                  textContainer:container];
    code_.minSize = NSMakeSize(0, size.height);
    code_.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    code_.verticallyResizable = YES;
    code_.horizontallyResizable = YES;
    code_.autoresizingMask = NSViewWidthSizable;
    code_.richText = NO;
    code_.importsGraphics = NO;
    code_.allowsUndo = YES;
    code_.usesFindBar = YES;
    code_.incrementalSearchingEnabled = YES;
    code_.automaticQuoteSubstitutionEnabled = NO;
    code_.automaticDashSubstitutionEnabled = NO;
    code_.automaticTextReplacementEnabled = NO;
    code_.automaticSpellingCorrectionEnabled = NO;
    code_.continuousSpellCheckingEnabled = NO;
    code_.grammarCheckingEnabled = NO;
    code_.smartInsertDeleteEnabled = NO;
    code_.automaticLinkDetectionEnabled = NO;
    code_.displaysLinkToolTips = NO;
    code_.textContainerInset = NSMakeSize(4, 4);
    code_.font = codeFont_;
    code_.typingAttributes = [self codeAttributes];
    code_.backgroundColor = [NSColor textBackgroundColor];
    code_.insertionPointColor = [NSColor textColor];
    code_.delegate = self;
    code_.host = self;
    code_.editable = NO;  // until a file is open
    codeScroll_.documentView = code_;

    gutter_ = [[LineNumbers alloc] initWithTextView:code_];
    codeScroll_.verticalRulerView = gutter_;
    codeScroll_.hasVerticalRuler = YES;
    codeScroll_.rulersVisible = YES;

    [editorPane_ addSubview:codeScroll_];
    current_ = nil;
}

- (NSTabViewItem*)tabNamed:(NSString*)name holding:(NSView*)view {
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:name];
    item.label = name;
    item.view = view;
    return item;
}

- (void)layPanel:(NSRect)frame {
    panelPane_ = [[NSView alloc] initWithFrame:frame];
    panel_ = [[NSTabView alloc] initWithFrame:NSInsetRect(panelPane_.bounds, 4, 2)];
    panel_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    panel_.controlSize = NSControlSizeSmall;
    panel_.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];

    NSRect inside = panel_.contentRect;
    NSFont* mono = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];

    // Errors: every diagnostic the last build printed, one row each.
    NSScrollView* issues = Scroller(NSMakeRect(0, 0, NSWidth(inside), NSHeight(inside)));
    issueTable_ = [[NSTableView alloc] initWithFrame:issues.bounds];
    NSArray* columns = @[ @[ @"kind", @"", @22 ], @[ @"message", @"Issue", @520 ],
                          @[ @"file", @"File", @180 ], @[ @"where", @"Line", @70 ] ];
    for (NSArray* spec in columns) {
        NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:spec[0]];
        column.title = spec[1];
        column.width = [spec[2] doubleValue];
        if ([spec[0] isEqualToString:@"kind"]) {
            column.minWidth = 22;
            column.maxWidth = 22;
        }
        [issueTable_ addTableColumn:column];
    }
    issueTable_.usesAlternatingRowBackgroundColors = YES;
    issueTable_.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    issueTable_.dataSource = self;
    issueTable_.delegate = self;
    issueTable_.target = self;
    issueTable_.doubleAction = @selector(issueChosen:);
    issueTable_.action = @selector(issueChosen:);
    issues.documentView = issueTable_;
    [panel_ addTabViewItem:[self tabNamed:@"Errors" holding:issues]];

    // Progress: what the build is doing, step by step, with a bar.
    NSView* progress = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(inside), NSHeight(inside))];
    progressTitle_ = Label(@"Nothing built yet");
    progressTitle_.frame = NSMakeRect(8, NSHeight(inside) - 22, NSWidth(inside) - 236, 16);
    progressTitle_.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [progress addSubview:progressTitle_];
    progressBar_ = [[NSProgressIndicator alloc]
        initWithFrame:NSMakeRect(NSWidth(inside) - 220, NSHeight(inside) - 22, 210, 14)];
    progressBar_.style = NSProgressIndicatorStyleBar;
    progressBar_.indeterminate = NO;
    progressBar_.minValue = 0;
    progressBar_.maxValue = 1;
    progressBar_.doubleValue = 0;
    progressBar_.controlSize = NSControlSizeSmall;
    progressBar_.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [progress addSubview:progressBar_];
    NSScrollView* log = Scroller(NSMakeRect(0, 0, NSWidth(inside), NSHeight(inside) - 28));
    progressLog_ = LogView(log, mono);
    [progress addSubview:log];
    [panel_ addTabViewItem:[self tabNamed:@"Progress" holding:progress]];

    // Output: the command, what the compiler said, and what the program printed - and under it,
    // a line for what the running program reads: Enter sends it, Ctrl-D ends its input.
    NSView* outPane = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(inside), NSHeight(inside))];
    NSScrollView* out = Scroller(NSMakeRect(0, 26, NSWidth(inside), NSHeight(inside) - 26));
    output_ = LogView(out, mono);
    [outPane addSubview:out];
    inputLine_ = [[NSTextField alloc] initWithFrame:NSMakeRect(4, 2, NSWidth(inside) - 8, 22)];
    inputLine_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    inputLine_.font = mono;
    inputLine_.placeholderString = @"input for the running program - Return sends a line, Control-D ends the input";
    inputLine_.target = self;
    inputLine_.action = @selector(inputEntered:);
    inputLine_.delegate = self;
    inputLine_.enabled = NO;
    [outPane addSubview:inputLine_];
    [panel_ addTabViewItem:[self tabNamed:@"Output" holding:outPane]];

    [panelPane_ addSubview:panel_];

    // At the right of the tab strip, where no tab reaches: down to minimise, up to restore.
    fold_ = [NSButton buttonWithTitle:@"\u25BE" target:self action:@selector(foldPanel:)];
    [fold_ setButtonType:NSButtonTypePushOnPushOff];
    fold_.bezelStyle = NSBezelStyleRecessed;
    fold_.controlSize = NSControlSizeSmall;
    fold_.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    fold_.toolTip = @"Minimise the panel to give the editor the room; click again to restore it";
    fold_.frame = NSMakeRect(NSWidth(panelPane_.bounds) - 30, NSHeight(panelPane_.bounds) - 22, 26, 18);
    fold_.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [panelPane_ addSubview:fold_];
}

- (CGFloat)foldedHeight { return 30; }

- (void)foldPanel:(id)sender {
    (void)sender;
    const BOOL want = fold_.state == NSControlStateValueOn;
    if (want == folded_) return;
    if (want) {
        panelHeight_ = NSHeight(panelPane_.frame);
        folded_ = YES;
        [down_ setPosition:NSHeight(down_.bounds) - [self foldedHeight] ofDividerAtIndex:0];
        fold_.title = @"\u25B4";
    } else {
        folded_ = NO;
        CGFloat height = panelHeight_ > 60 ? panelHeight_ : floor(NSHeight(down_.bounds) / 4);
        [down_ setPosition:NSHeight(down_.bounds) - height ofDividerAtIndex:0];
        fold_.title = @"\u25BE";
    }
}

// The navigator keeps its width and the panel its share as the window grows;
// neither can be dragged to nothing.
- (BOOL)splitView:(NSSplitView*)split shouldAdjustSizeOfSubview:(NSView*)view {
    if (split == across_) return view != navigatorPane_;
    return !(folded_ && view == panelPane_);  // minimised, it keeps its tab strip's height
}

- (CGFloat)splitView:(NSSplitView*)split constrainMinCoordinate:(CGFloat)proposed
         ofSubviewAt:(NSInteger)index {
    (void)index;
    if (split == across_) return MAX(proposed, 140);
    return MAX(proposed, 140);
}

- (CGFloat)splitView:(NSSplitView*)split constrainMaxCoordinate:(CGFloat)proposed
         ofSubviewAt:(NSInteger)index {
    (void)index;
    if (split == across_) return MIN(proposed, NSWidth(split.bounds) - 320);
    return MIN(proposed, NSHeight(split.bounds) - (folded_ ? [self foldedHeight] : 70));
}

- (BOOL)splitView:(NSSplitView*)split canCollapseSubview:(NSView*)view {
    (void)split;
    return view == navigatorPane_ || view == panelPane_;
}

// ---- saying things --------------------------------------------------------------

- (void)say:(NSString*)what {
    statusMessage_.stringValue = what ?: @"";
}

- (void)refreshTitle {
    NSString* title = [NSString stringWithFormat:@"%@ %@", Str(ride_product_name()),
                                                 Str(ride_version())];
    if (ride_project_loaded(project_)) {
        NSString* name = Str(ride_project_name(project_));
        if (name.length > 0) title = [title stringByAppendingFormat:@" — %@", name];
    }
    if (current_ != nil)
        title = [title stringByAppendingFormat:@" — %@", [self shownName:current_]];
    self.window.title = title;
    self.window.representedURL = current_.path != nil ? [NSURL fileURLWithPath:current_.path] : nil;
    self.window.documentEdited = [self anyModified];

    if (current_ == nil) {
        jumpBar_.stringValue = ride_project_loaded(project_)
                                   ? Str(ride_project_root(project_)) : @"No file open";
    } else if (current_.path == nil) {
        NSString* name = [self shownName:current_];
        jumpBar_.stringValue = current_.modified ? [@"●  " stringByAppendingString:name] : name;
    } else {
        NSString* shown = current_.path;
        if (ride_project_loaded(project_) && ride_project_holds(project_, Utf8(shown)))
            shown = [NSString stringWithFormat:@"%@  ›  %@",
                                               Str(ride_project_name(project_)),
                                               Str(ride_project_relative(project_, Utf8(shown)))];
        jumpBar_.stringValue = current_.modified
                                   ? [@"●  " stringByAppendingString:shown] : shown;
    }
}

- (NSString*)shownName:(Sheet*)sheet {
    if (sheet.path != nil) return sheet.path.lastPathComponent;
    return sheet.untitled > 1 ? [NSString stringWithFormat:@"Untitled %ld", (long)sheet.untitled] : @"Untitled";
}

- (BOOL)anyModified {
    for (Sheet* sheet in sheets_)
        if (sheet.modified) return YES;
    return NO;
}

- (int)languageNow {
    if (current_ != nil && current_.language >= 0) return current_.language;
    return ride_language_for(Utf8(current_.path ?: @""));
}

// The status bar says what the next build will use: the language, debug or release, the compiler
// that will run (with a * when the file chose it) and the target when it means anything - the
// terminal's own line, through the same core functions.
- (void)sayBuild {
    int language = [self languageNow];
    int kind = ride_resolve(toolKind_, language);
    NSString* compiler = Str(ride_toolchain_name(kind));
    NSMutableString* said = [NSMutableString stringWithFormat:@"%@   %@   %@",
                                                              Str(ride_language_name(language)),
                                                              Str(ride_config_name(config_)),
                                                              compiler];
    if (toolKind_ == RIDE_TOOL_AUTO) [said appendString:@"*"];
    if (ride_uses_arch(kind)) [said appendFormat:@"   %@", arch_];
    statusBuild_.stringValue = said;
    compilerHint_.stringValue = current_ != nil ? compiler : @"";
}

- (void)sayWhere {
    if (current_ == nil) {
        statusWhere_.stringValue = @"";
        return;
    }
    // The column the compilers count - bytes of UTF-8 - so "Col" agrees with an error's 16:15 on a
    // line holding non-ASCII text, where UTF-16 units did not (M1).
    statusWhere_.stringValue = [NSString stringWithFormat:@"Ln %ld, Col %ld",
                                                          (long)[code_ caretRow] + 1,
                                                          (long)[code_ caretByteColumn] + 1];
}

// ---- CodeViewHost ------------------------------------------------------------

- (int)indentWidth { return indentWidth_ > 0 ? indentWidth_ : 4; }
- (int)indentTabs { return indentTabs_; }
- (int)indentCase { return indentCase_; }
- (int)indentDialect { return ride_dialect_for([self languageNow]); }
- (BOOL)laysOut {
    int language = [self languageNow];
    return language == RIDE_LANG_C || language == RIDE_LANG_CPP ||
           language == RIDE_LANG_SHALIMAR;
}

// ---- colouring ------------------------------------------------------------------

static NSColor* ColourOf(unsigned char kind) {
    switch (kind) {
        case RIDE_KIND_KEYWORD: return [NSColor systemPinkColor];
        case RIDE_KIND_TYPE:    return [NSColor systemPurpleColor];
        case RIDE_KIND_STRING:  return [NSColor systemRedColor];
        case RIDE_KIND_CHAR:    return [NSColor systemRedColor];
        case RIDE_KIND_COMMENT: return [NSColor systemGreenColor];
        case RIDE_KIND_PREPROC: return [NSColor systemOrangeColor];
        case RIDE_KIND_NUMBER:  return [NSColor systemBlueColor];
        case RIDE_KIND_LABEL:   return [NSColor systemBrownColor];
        default:                return nil;
    }
}

// One row through the core's highlighter, as the layout manager's temporary attributes - not the
// text, so colouring never touches undo nor marks the file changed. macos/README.md, "Colour".
- (void)recolourRow:(NSInteger)row language:(int)language state:(int&)state
               into:(std::vector<unsigned char>&)kinds {
    NSLayoutManager* layout = code_.layoutManager;
    NSRange line = [code_ contentsOfRow:row];
    if (line.location == NSNotFound) return;
    [layout removeTemporaryAttribute:NSForegroundColorAttributeName forCharacterRange:line];
    NSString* all = code_.string;
    NSUInteger stop = NSMaxRange(line);
    while (stop > line.location && [all characterAtIndex:stop - 1] == '\r') --stop;
    NSString* text = [all substringWithRange:NSMakeRange(line.location, stop - line.location)];
    const char* bytes = Utf8(text);
    size_t length = std::strlen(bytes);
    kinds.assign(length + 1, 0);
    int howMany = ride_highlight(bytes, language, &state, kinds.data(), (int)kinds.size());

    // Kinds come a byte of UTF-8 at a time; the view counts UTF-16 units.
    NSUInteger unit = line.location;
    int byte = 0;
    unsigned char runKind = 0;
    NSUInteger runStart = unit;
    while (byte < howMany) {
        unsigned char lead = (unsigned char)bytes[byte];
        int bytesHere = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        NSUInteger unitsHere = bytesHere == 4 ? 2 : 1;
        unsigned char kind = kinds[(size_t)byte];
        if (kind != runKind) {
            NSColor* colour = ColourOf(runKind);
            if (colour != nil && unit > runStart)
                [layout addTemporaryAttribute:NSForegroundColorAttributeName value:colour
                            forCharacterRange:NSMakeRange(runStart, unit - runStart)];
            runKind = kind;
            runStart = unit;
        }
        byte += bytesHere;
        unit += unitsHere;
    }
    NSColor* colour = ColourOf(runKind);
    if (colour != nil && unit > runStart && unit <= all.length)
        [layout addTemporaryAttribute:NSForegroundColorAttributeName value:colour
                    forCharacterRange:NSMakeRange(runStart, unit - runStart)];
}

// Only what an edit can have changed (H3): from the first row out of date past the last it reached,
// until a row starts in the state it started in last time.
- (void)recolour {
    [recolourTimer_ invalidate];
    recolourTimer_ = nil;
    if (current_ == nil) return;
    NSLayoutManager* layout = code_.layoutManager;
    int language = [self languageNow];
    NSInteger rows = [code_ lineCount];
    std::vector<int>& states = [current_ states];
    if (language == RIDE_LANG_PLAIN) {
        [layout removeTemporaryAttribute:NSForegroundColorAttributeName
                       forCharacterRange:NSMakeRange(0, current_.storage.length)];
        states.clear();
        current_.staleFrom = rows;
        return;
    }
    NSInteger from = MAX((NSInteger)0, MIN(current_.staleFrom, rows));
    if ((NSInteger)states.size() != rows + 1) {
        states.resize((size_t)rows + 1, -1);
        from = MIN(from, (NSInteger)0);
    }
    NSInteger reach = MIN(rows, current_.staleFrom + staleRows_);
    std::vector<unsigned char> kinds;
    int state = states[(size_t)from] < 0 ? 0 : states[(size_t)from];
    for (NSInteger row = from; row < rows; ++row) {
        states[(size_t)row] = state;
        [self recolourRow:row language:language state:state into:kinds];
        if (row + 1 >= reach && states[(size_t)row + 1] == state) break;
        states[(size_t)row + 1] = state;
    }
    current_.staleFrom = rows;
    staleRows_ = 0;
}

// The colour is to be made again for all of the file: another file, another language.
- (void)recolourAll {
    if (current_ != nil) {
        [current_ states].clear();
        current_.staleFrom = 0;
        staleRows_ = [code_ lineCount];
    }
    [self recolour];
}

// CodeViewHost: rows from `first` were replaced, `before` of them by `after`. The lexer's states
// after them move with them, and the colour is out of date from `first` for `after` rows at least.
- (void)codeView:(NSTextView*)view rowsFrom:(NSInteger)first before:(NSInteger)before
           after:(NSInteger)after {
    (void)view;
    if (current_ == nil) return;
    std::vector<int>& states = [current_ states];
    if (!states.empty() && first + 1 <= (NSInteger)states.size()) {
        auto at = states.begin() + (long)MIN((NSInteger)states.size(), first + 1);
        if (after > before) states.insert(at, (size_t)(after - before), -1);
        else if (before > after)
            states.erase(at, at + (long)MIN((NSInteger)(states.end() - at), before - after));
    }
    if (staleRows_ == 0) {
        current_.staleFrom = first;
        staleRows_ = after;
        return;
    }
    NSInteger from = MIN(current_.staleFrom, first);
    NSInteger to = MAX(current_.staleFrom + staleRows_, first + after);
    current_.staleFrom = from;
    staleRows_ = to - from;
}

- (void)recolourSoon {
    [recolourTimer_ invalidate];
    recolourTimer_ = [NSTimer scheduledTimerWithTimeInterval:0.12
                                                      target:self
                                                    selector:@selector(recolourTick:)
                                                    userInfo:nil
                                                     repeats:NO];
}

- (void)recolourTick:(NSTimer*)timer {
    (void)timer;
    [self recolour];
}

// ---- NSTextViewDelegate ----------------------------------------------------------

// A header the line or the selection names, at the top of the right-click menu: opened as a
// compiler would find it, or said not to be in any include folder.
- (NSMenu*)textView:(NSTextView*)view menu:(NSMenu*)menu forEvent:(NSEvent*)event atIndex:(NSUInteger)charIndex {
    (void)event;
    NSString* all = view.string;
    NSRange chosen = view.selectedRange;
    NSString* text = chosen.length > 0 ? [all substringWithRange:chosen]
                   : [all substringWithRange:[all lineRangeForRange:NSMakeRange(MIN(charIndex, all.length), 0)]];
    NSString* name = Str(ride_header_named(Utf8(text)));
    if (name.length == 0) return menu;
    NSString* found = Str(ride_find_header(project_, Utf8(current_.path != nil ? current_.path : @""), Utf8(name)));
    NSMenuItem* open = [[NSMenuItem alloc]
        initWithTitle:found.length > 0 ? [NSString stringWithFormat:@"Open \u201C%@\u201D", name]
                                       : [NSString stringWithFormat:@"\u201C%@\u201D is not in any include folder", name]
               action:@selector(openHeaderItem:) keyEquivalent:@""];
    open.target = self;
    open.representedObject = found.length > 0 ? found : nil;
    open.toolTip = found;
    [menu insertItem:[NSMenuItem separatorItem] atIndex:0];
    [menu insertItem:open atIndex:0];
    return menu;
}

- (void)openHeaderItem:(NSMenuItem*)sender {
    NSString* found = sender.representedObject;
    if (found.length == 0) return;
    [self openPath:found];
    [self say:found];
}

- (NSUndoManager*)undoManagerForTextView:(NSTextView*)view {
    (void)view;
    return current_ != nil ? current_.undo : blank_.undo;
}

- (void)textDidChange:(NSNotification*)note {
    (void)note;
    if (current_ == nil) return;
    NSString* now = current_.storage.string;
    NSString* saved = current_.savedText ?: @"";
    BOOL differs = now.length != saved.length || ![now isEqualToString:saved];
    if (differs != current_.modified) {
        current_.modified = differs;
        [self refreshTitle];
        [self refreshOpenFiles];
    }
    [gutter_ textDidChange];
    [self recolourSoon];
}

- (void)textViewDidChangeSelection:(NSNotification*)note {
    (void)note;
    [self sayWhere];
}

// ---- sheets ---------------------------------------------------------------------

- (Sheet*)sheetFor:(NSString*)path {
    if (path == nil) return nil;
    NSString* wanted = path.stringByStandardizingPath;
    for (Sheet* sheet in sheets_)
        if (sheet.path != nil && [sheet.path.stringByStandardizingPath isEqualToString:wanted])
            return sheet;
    return nil;
}

- (Sheet*)makeSheet:(NSString*)path text:(NSString*)text {
    Sheet* sheet = [[Sheet alloc] init];
    sheet.path = path;
    if (path == nil) sheet.untitled = ++untitledCount_;
    sheet.storage = [[NSTextStorage alloc] initWithString:text ?: @""
                                               attributes:[self codeAttributes]];
    sheet.savedText = path != nil ? (text ?: @"") : @"";
    [sheets_ addObject:sheet];
    return sheet;
}

// Bring a sheet into the one text view - nil for none - keeping where the
// one leaving was.
- (void)showSheet:(Sheet*)sheet {
    if (current_ != nil) {
        current_.selection = code_.selectedRange;
        current_.scrolled = codeScroll_.contentView.bounds.origin;
    }
    // The typing the view was coalescing belongs to the file leaving, and so does the find
    // bar's incremental highlighting: both let go before another file's text arrives (M13, M12).
    [code_ breakUndoCoalescing];
    BOOL incremental = code_.incrementalSearchingEnabled;
    code_.incrementalSearchingEnabled = NO;
    current_ = sheet;
    Sheet* shown = sheet != nil ? sheet : blank_;
    NSLayoutManager* layout = code_.layoutManager;
    if (layout.textStorage != shown.storage) [layout replaceTextStorage:shown.storage];
    [code_ textStorageChanged];
    code_.incrementalSearchingEnabled = incremental;
    code_.editable = sheet != nil;
    code_.font = codeFont_;
    code_.typingAttributes = [self codeAttributes];

    NSUInteger length = shown.storage.length;
    NSRange selection = shown.selection;
    if (NSMaxRange(selection) > length) selection = NSMakeRange(length, 0);
    code_.selectedRange = selection;
    [codeScroll_.contentView scrollToPoint:shown.scrolled];
    [codeScroll_ reflectScrolledClipView:codeScroll_.contentView];

    [self applyErrorMarks];
    [gutter_ textDidChange];
    [self recolourAll];
    [self refreshTitle];
    [self sayBuild];
    [self sayWhere];
    [self refreshOpenFiles];
    if (sheet != nil) [self.window makeFirstResponder:code_];
}

// ---- reading and writing a file as it was --------------------------------------

// A file's text as the editor holds it - '\n' between rows, which is what the core counts - with
// what it was on the disk remembered on the sheet so Save writes it back so (M5): UTF-8, else
// Latin-1 (which reads every byte), a BOM, and CRLF or CR line endings. nil when it cannot be read.
- (NSString*)read:(NSString*)path into:(Sheet*)sheet problem:(NSString**)problem {
    NSError* error = nil;
    NSData* data = [NSData dataWithContentsOfFile:path options:0 error:&error];
    if (data == nil) {
        if (problem) *problem = error.localizedDescription ?: [@"cannot read " stringByAppendingString:path];
        return nil;
    }
    const unsigned char* bytes = (const unsigned char*)data.bytes;
    BOOL bom = data.length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
    NSData* body = bom ? [data subdataWithRange:NSMakeRange(3, data.length - 3)] : data;
    NSStringEncoding encoding = NSUTF8StringEncoding;
    NSString* text = [[NSString alloc] initWithData:body encoding:NSUTF8StringEncoding];
    if (text == nil) {
        encoding = NSISOLatin1StringEncoding;
        text = [[NSString alloc] initWithData:body encoding:NSISOLatin1StringEncoding];
    }
    if (text == nil) {
        if (problem) *problem = [@"cannot read " stringByAppendingString:path];
        return nil;
    }
    NSString* ending = @"\n";
    if ([text rangeOfString:@"\r\n"].location != NSNotFound) ending = @"\r\n";
    else if ([text rangeOfString:@"\r"].location != NSNotFound) ending = @"\r";
    text = [[text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"]
        stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
    if (sheet != nil) {
        sheet.encoding = encoding;
        sheet.lineEnding = ending;
        sheet.byteOrderMark = bom;
        sheet.stamp = [self stampOf:path];
    }
    return text;
}

- (NSDate*)stampOf:(NSString*)path {
    return [NSFileManager.defaultManager attributesOfItemAtPath:path error:NULL].fileModificationDate;
}

// Changed on the disk since it was read or written here.
- (BOOL)changedOnDisk:(Sheet*)sheet {
    if (sheet.path == nil || sheet.stamp == nil) return NO;
    NSDate* now = [self stampOf:sheet.path];
    return now != nil && ![now isEqualToDate:sheet.stamp];
}

- (void)openPath:(NSString*)path {
    if (path.length == 0) return;
    path = path.stringByStandardizingPath;
    BOOL directory = NO;
    if ([NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&directory] && directory) {
        [self say:[path.lastPathComponent stringByAppendingString:@" is a directory, not a file"]];
        return;
    }
    Sheet* already = [self sheetFor:path];
    if (already != nil) {
        // Open already and changed on the disk since - a second Convert writes its file again -
        // with nothing unsaved here: read again, or the old text is what is shown.
        NSDate* now = [self stampOf:path];
        if (!already.modified && now != nil && already.stamp != nil && ![now isEqualToDate:already.stamp])
            [self reload:already];
        [self showSheet:already];
        return;
    }
    Sheet* sheet = [[Sheet alloc] init];
    NSString* problem = nil;
    NSString* contents = [self read:path into:sheet problem:&problem];
    if (contents == nil) {
        [self say:problem];
        return;
    }
    sheet.path = path;
    sheet.storage = [[NSTextStorage alloc] initWithString:contents attributes:[self codeAttributes]];
    sheet.savedText = contents;
    [sheets_ addObject:sheet];
    [self showSheet:sheet];
    code_.selectedRange = NSMakeRange(0, 0);
    [code_ scrollRangeToVisible:NSMakeRange(0, 0)];
    ride_remember_file(Utf8(path));
    NSString* said = [NSString stringWithFormat:@"%@  %ld lines", path.lastPathComponent,
                                                (long)[code_ lineCount]];
    if (sheet.encoding != NSUTF8StringEncoding)
        said = [said stringByAppendingString:@" - not UTF-8, read as Latin-1 and kept so"];
    [self say:said];
}

// Writes the sheet as the file was: its encoding, its line ending, its BOM. A character Latin-1
// has no byte for is written as UTF-8 instead, and said so, rather than lost.
- (BOOL)writeSheet:(Sheet*)sheet {
    if (sheet == nil || sheet.path == nil) return NO;
    return [self writeSheet:sheet to:sheet.path];
}

- (BOOL)writeSheet:(Sheet*)sheet to:(NSString*)path {
    NSString* text = sheet.storage.string;
    if (![sheet.lineEnding isEqualToString:@"\n"])
        text = [text stringByReplacingOccurrencesOfString:@"\n" withString:sheet.lineEnding];
    NSData* data = [text dataUsingEncoding:sheet.encoding allowLossyConversion:NO];
    if (data == nil) {
        sheet.encoding = NSUTF8StringEncoding;
        data = [text dataUsingEncoding:NSUTF8StringEncoding];
        [self say:[path.lastPathComponent stringByAppendingString:@" now holds what Latin-1 cannot - written as UTF-8"]];
    }
    NSMutableData* all = [NSMutableData data];
    if (sheet.byteOrderMark && sheet.encoding == NSUTF8StringEncoding) {
        static const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        [all appendBytes:bom length:3];
    }
    [all appendData:data];
    NSError* problem = nil;
    // Written where a link points rather than over the link, and with the permissions it had: an
    // atomic write replaces the file, which turned a link into a copy and dropped an executable bit.
    NSString* destination = path.stringByResolvingSymlinksInPath;
    NSNumber* permissions = [NSFileManager.defaultManager attributesOfItemAtPath:destination error:nil][NSFilePosixPermissions];
    if (![all writeToFile:destination options:NSDataWritingAtomic error:&problem]) {
        [self say:problem.localizedDescription ?: @"not written"];
        return NO;
    }
    if (permissions != nil)
        [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions : permissions} ofItemAtPath:destination error:nil];
    sheet.modified = NO;
    sheet.savedText = sheet.storage.string;
    sheet.stamp = [self stampOf:path];
    return YES;
}

// Before a file is written over: if it changed on the disk since it was read, ask (M10).
- (BOOL)mayOverwrite:(Sheet*)sheet {
    if (![self changedOnDisk:sheet]) return YES;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithFormat:@"%@ has changed on the disk.", sheet.path.lastPathComponent];
    alert.informativeText = @"Something else wrote it after it was opened here. Saving writes over that.";
    [alert addButtonWithTitle:@"Save Anyway"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Reload"];
    NSModalResponse answer = [alert runModal];
    if (answer == NSAlertFirstButtonReturn) return YES;
    if (answer == NSAlertThirdButtonReturn) [self reload:sheet];
    return NO;
}

// The file read again from the disk, through the same reader as Open (M5); undoable.
- (BOOL)reload:(Sheet*)sheet {
    if (sheet.path == nil) return NO;
    NSString* problem = nil;
    NSString* contents = [self read:sheet.path into:sheet problem:&problem];
    if (contents == nil) { [self say:problem]; return NO; }
    sheet.savedText = contents;
    sheet.modified = NO;
    if (sheet != current_) {
        // In place, behind the file in front: coming back to the window used to switch to whichever
        // file had changed on the disk. Its colouring is redone when it is next shown.
        [sheet.storage setAttributedString:[[NSAttributedString alloc] initWithString:contents
                                                                           attributes:[self codeAttributes]]];
        sheet.staleFrom = 0;
        [sheet.undo removeAllActions];
        [self refreshOpenFiles];
        return YES;
    }
    [code_ replaceRange:NSMakeRange(0, current_.storage.length) with:contents];
    [code_ breakUndoCoalescing];
    current_.modified = NO;
    [self refreshTitle];
    [self refreshOpenFiles];
    return YES;
}

- (BOOL)saveSheet:(Sheet*)sheet {
    if (sheet == nil) return NO;
    if (sheet.path == nil) return [self saveSheetAs:sheet];
    if (![self mayOverwrite:sheet]) return NO;
    if (![self writeSheet:sheet]) return NO;
    [self say:[sheet.path.lastPathComponent stringByAppendingString:@" written"]];
    [self refreshTitle];
    [self refreshOpenFiles];
    return YES;
}

// The sheet takes the new name only once it is written there (L8).
- (BOOL)saveSheetAs:(Sheet*)sheet {
    NSSavePanel* pick = [NSSavePanel savePanel];
    pick.canCreateDirectories = YES;
    // File > Save As starts in ~/Documents/RIDE/programs; Project's dialogs in .../projects.
    pick.directoryURL = [NSURL fileURLWithPath:[self madeUnder:@"programs"]];
    if (sheet.path != nil) pick.nameFieldStringValue = sheet.path.lastPathComponent;
    if ([pick runModal] != NSModalResponseOK) {
        [self say:@"not saved"];
        return NO;
    }
    NSString* target = pick.URL.path;
    Sheet* other = [self sheetFor:target];
    if (other != nil && other != sheet) {
        [self say:[target.lastPathComponent stringByAppendingString:@" is open already - close it first"]];
        return NO;
    }
    if (![self writeSheet:sheet to:target]) return NO;
    sheet.path = target;
    sheet.untitled = 0;

    // Saved into the project's directory is saved into the project.
    NSString* said = [sheet.path.lastPathComponent stringByAppendingString:@" written"];
    // A build is reading the project's groups: the file joins it once the build is done, not under it.
    if (busy_ && running_ == NULL && ride_project_loaded(project_))
        said = [said stringByAppendingString:@" - it joins the project after the build"];
    else if (ride_adopt_saved(project_, Utf8(sheet.path)) != 0)
        said = Str(ride_outcome_message(project_));
    ride_remember_file(Utf8(sheet.path));
    [self say:said];
    [self refreshTitle];
    [self sayBuild];
    [self fillNavigator];
    [self recolourAll];
    return YES;
}

// Every changed file that has a name, written; NO, and the first that was not, if any failed (M4).
- (BOOL)saveEveryModified {
    for (Sheet* sheet in [sheets_ copy]) {
        if (!sheet.modified || sheet.path == nil) continue;
        if (![self mayOverwrite:sheet] || ![self writeSheet:sheet]) {
            [self refreshTitle];
            [self refreshOpenFiles];
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = [NSString stringWithFormat:@"%@ was not written, so nothing was built.",
                                                           sheet.path.lastPathComponent];
            alert.informativeText = statusMessage_.stringValue ?: @"";
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
            return NO;
        }
    }
    [self refreshTitle];
    [self refreshOpenFiles];
    return YES;
}

// Asks before a changed file goes; NO when the answer was Cancel.
- (BOOL)mayDiscard:(Sheet*)sheet {
    if (sheet == nil || !sheet.modified) return YES;
    if (sheet != current_) [self showSheet:sheet];
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithFormat:@"Save the changes to %@?", [self shownName:sheet]];
    alert.informativeText = @"Your changes will be lost if you don't save them.";
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Don't Save"];
    NSModalResponse answer = [alert runModal];
    if (answer == NSAlertSecondButtonReturn) return NO;
    if (answer == NSAlertThirdButtonReturn) return YES;
    return [self saveSheet:sheet];
}

- (void)closeSheet:(Sheet*)sheet {
    if (sheet == nil) return;
    if (![self mayDiscard:sheet]) return;
    NSUInteger at = [sheets_ indexOfObject:sheet];
    [sheets_ removeObject:sheet];
    if (sheet == current_) {
        current_ = nil;
        Sheet* next = nil;
        if (sheets_.count > 0) next = sheets_[MIN(at, sheets_.count - 1)];
        [self showSheet:next];
    } else {
        [self fillNavigator];
    }
    [self say:sheets_.count == 0 ? @"no file open" : @"closed"];
}

- (BOOL)mayClose {
    if (closing_) return YES;
    [self rememberOpen];
    for (Sheet* sheet in [sheets_ copy])
        if (![self mayDiscard:sheet]) return NO;
    return YES;
}

- (BOOL)windowShouldClose:(NSWindow*)window {
    (void)window;
    return [self mayClose];
}

// Back to the window: a file open here and changed on the disk meanwhile is read again if it has
// no changes of its own, and asked about if it has (M10).
- (void)windowDidBecomeKey:(NSNotification*)note {
    (void)note;
    for (Sheet* sheet in [sheets_ copy]) {
        if (![self changedOnDisk:sheet]) continue;
        if (![NSFileManager.defaultManager fileExistsAtPath:sheet.path]) continue;
        if (!sheet.modified) {
            if ([self reload:sheet]) [self say:[sheet.path.lastPathComponent stringByAppendingString:@" changed on the disk - read again"]];
            continue;
        }
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithFormat:@"%@ has changed on the disk.", sheet.path.lastPathComponent];
        alert.informativeText = @"It also has changes here that are not saved. Read the disk's, and lose them?";
        [alert addButtonWithTitle:@"Keep Mine"];
        [alert addButtonWithTitle:@"Read the Disk's"];
        if ([alert runModal] == NSAlertSecondButtonReturn) [self reload:sheet];
        else sheet.stamp = [self stampOf:sheet.path];
    }
}

- (void)windowWillClose:(NSNotification*)note {
    (void)note;
    // The last window going ends the application (the delegate says so),
    // and nothing is asked twice on the way out.
    closing_ = YES;
}

- (void)rememberOpen {
    if (current_.path != nil && ride_project_loaded(project_))
        ride_remember_open(project_, Utf8(current_.path));
}

- (void)openFirstOfProject {
    NSString* relative = Str(ride_project_file_to_open(project_));
    if (relative.length == 0) return;
    NSString* full = Str(ride_project_absolute(project_, Utf8(relative)));
    if (full.length > 0 && [NSFileManager.defaultManager fileExistsAtPath:full])
        [self openPath:full];
}

// ---- the navigator --------------------------------------------------------------

// The whole navigator, made again: when the project arrives, goes or changes what it holds. A file
// shown or closed only touches OPEN FILES, so groups folded by hand stay folded (L10).
- (void)fillNavigator {
    // What the user had folded stays folded: the tree is rebuilt on every Save As, Add, Rename and
    // Move, and opening every group again each time undid them (L10).
    NSMutableSet<NSString*>* folded = [NSMutableSet set];
    for (NavItem* root in navRoots_)
        for (NavItem* child in root.children)
            if (child.children.count > 0 && ![navigator_ isItemExpanded:child])
                [folded addObject:[NSString stringWithFormat:@"%@/%@", root.title, child.title]];
    [navRoots_ removeAllObjects];

    if (ride_project_loaded(project_)) {
        NavItem* section = [[NavItem alloc] init];
        section.section = YES;
        section.title = [Str(ride_project_name(project_)) uppercaseString];
        int groups = ride_project_groups(project_);
        for (int group = 0; group < groups; ++group) {
            NavItem* node = [[NavItem alloc] init];
            node.title = Str(ride_project_group_name(project_, group));
            node.group = node.title;
            int files = ride_project_files(project_, group);
            for (int file = 0; file < files; ++file) {
                NSString* relative = Str(ride_project_file(project_, group, file));
                NavItem* leaf = [[NavItem alloc] init];
                leaf.title = relative;
                leaf.group = node.title;
                leaf.path = Str(ride_project_absolute(project_, Utf8(relative)));
                [node.children addObject:leaf];
            }
            [section.children addObject:node];
        }
        [navRoots_ addObject:section];
    }

    NavItem* open = [[NavItem alloc] init];
    open.section = YES;
    open.title = @"OPEN FILES";
    [navRoots_ addObject:open];
    [self fillOpenFiles:open];

    [navigator_ reloadData];
    [navigator_ sizeLastColumnToFit];
    for (NavItem* root in navRoots_) {
        [navigator_ expandItem:root];
        for (NavItem* child in root.children)
            if (![folded containsObject:[NSString stringWithFormat:@"%@/%@", root.title, child.title]])
                [navigator_ expandItem:child];
    }
    [self selectCurrentInNavigator];
}

- (void)fillOpenFiles:(NavItem*)open {
    [open.children removeAllObjects];
    for (Sheet* sheet in sheets_) {
        NavItem* leaf = [[NavItem alloc] init];
        leaf.title = [self shownName:sheet];
        leaf.path = sheet.path;
        leaf.sheet = sheet;
        [open.children addObject:leaf];
    }
}

- (void)refreshOpenFiles {
    NavItem* open = navRoots_.lastObject;
    if (open == nil) { [self fillNavigator]; return; }
    [self fillOpenFiles:open];
    [navigator_ reloadItem:open reloadChildren:YES];
    [navigator_ expandItem:open];
    // A project row's dot says whether its open file is changed.
    for (NSInteger row = 0; row < navigator_.numberOfRows; ++row)
        [navigator_ reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
                              columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    [self selectCurrentInNavigator];
}

// The row of the file in front: its OPEN FILES row, which is that sheet and no other (M7).
- (void)selectCurrentInNavigator {
    if (current_ == nil) {
        [navigator_ deselectAll:nil];
        return;
    }
    for (NSInteger row = 0; row < navigator_.numberOfRows; ++row) {
        NavItem* item = [navigator_ itemAtRow:row];
        if (item.sheet == current_) {
            [navigator_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
                    byExtendingSelection:NO];
            return;
        }
    }
}

- (NSInteger)outlineView:(NSOutlineView*)view numberOfChildrenOfItem:(id)item {
    (void)view;
    if (item == nil) return (NSInteger)navRoots_.count;
    return (NSInteger)((NavItem*)item).children.count;
}

- (id)outlineView:(NSOutlineView*)view child:(NSInteger)index ofItem:(id)item {
    (void)view;
    if (item == nil) return navRoots_[(NSUInteger)index];
    return ((NavItem*)item).children[(NSUInteger)index];
}

- (BOOL)outlineView:(NSOutlineView*)view isItemExpandable:(id)item {
    (void)view;
    NavItem* node = item;
    return node.section || (node.path == nil && node.children.count > 0) ||
           (node.path == nil && node.group != nil);
}

- (BOOL)outlineView:(NSOutlineView*)view isGroupItem:(id)item {
    (void)view;
    return ((NavItem*)item).section;
}

- (BOOL)outlineView:(NSOutlineView*)view shouldSelectItem:(id)item {
    (void)view;
    return !((NavItem*)item).section;
}

- (NSView*)outlineView:(NSOutlineView*)view viewForTableColumn:(NSTableColumn*)column item:(id)item {
    (void)column;
    NavItem* node = item;
    NSString* identifier = node.section ? @"section" : @"cell";
    NSTableCellView* cell = [view makeViewWithIdentifier:identifier owner:self];
    if (cell == nil) {
        cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 200, 20)];
        cell.identifier = identifier;
        NSTextField* text = [NSTextField labelWithString:@""];
        // What does not fit loses its end, so files still read apart by how their names begin.
        text.lineBreakMode = NSLineBreakByTruncatingTail;
        text.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:text];
        cell.textField = text;
        if (!node.section) {
            NSImageView* image = [[NSImageView alloc] initWithFrame:NSZeroRect];
            image.translatesAutoresizingMaskIntoConstraints = NO;
            [cell addSubview:image];
            cell.imageView = image;
            [NSLayoutConstraint activateConstraints:@[
                [image.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:2],
                [image.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                [image.widthAnchor constraintEqualToConstant:16],
                [image.heightAnchor constraintEqualToConstant:16],
                [text.leadingAnchor constraintEqualToAnchor:image.trailingAnchor constant:5],
                [text.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-2],
                [text.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
            ]];
        } else {
            [NSLayoutConstraint activateConstraints:@[
                [text.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:2],
                [text.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-2],
                [text.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
            ]];
        }
    }

    NSString* title = node.title;
    Sheet* open = node.sheet != nil ? node.sheet : [self sheetFor:node.path];
    if (open.modified) title = [title stringByAppendingString:@"  ●"];
    cell.textField.stringValue = title ?: @"";

    if (!node.section) {
        NSString* symbol = @"doc.text";
        if (node.path == nil && node.group != nil) symbol = @"folder";
        else {
            int language = ride_language_for(Utf8(node.path ?: node.title));
            if (language == RIDE_LANG_C || language == RIDE_LANG_CPP) symbol = @"chevron.left.forwardslash.chevron.right";
            else if (language == RIDE_LANG_SHALIMAR) symbol = @"s.square";
            else if (language == RIDE_LANG_ASM) symbol = @"cpu";
            else if (language == RIDE_LANG_JSON) symbol = @"curlybraces";
        }
        NSImage* image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil];
        if (image == nil) image = [NSImage imageNamed:NSImageNameMultipleDocuments];
        cell.imageView.image = image;
        cell.imageView.contentTintColor =
            [symbol isEqualToString:@"folder"] ? [NSColor systemBlueColor] : [NSColor secondaryLabelColor];
    }
    return cell;
}

- (void)navigatorClicked:(id)sender {
    (void)sender;
    NSInteger row = navigator_.clickedRow >= 0 ? navigator_.clickedRow : navigator_.selectedRow;
    if (row < 0) return;
    NavItem* item = [navigator_ itemAtRow:row];
    if (item.section) return;
    if (item.sheet != nil) {
        [self showSheet:item.sheet];
        return;
    }
    if (item.path != nil) {
        if ([NSFileManager.defaultManager fileExistsAtPath:item.path]) [self openPath:item.path];
        else [self say:[item.title stringByAppendingString:@" is not on the disk"]];
    }
}

// The file an action on the navigator is about: the row right-clicked or
// selected there, else the file in front.
- (NSString*)targetFile {
    NSInteger row = navigator_.clickedRow;
    if (row < 0 && self.window.firstResponder == navigator_) row = navigator_.selectedRow;
    if (row >= 0) {
        // A clicked group, section or unsaved file is not a file to act on - never the one in the
        // editor instead, which Remove from Project on "Sources" used to remove unasked.
        NavItem* item = [navigator_ itemAtRow:row];
        return item.path;
    }
    return current_.path;
}

- (NSString*)groupUnderCursor {
    NSInteger row = navigator_.clickedRow >= 0 ? navigator_.clickedRow : navigator_.selectedRow;
    if (row >= 0) {
        NavItem* item = [navigator_ itemAtRow:row];
        if (item.group != nil) return item.group;
    }
    return @"Sources";
}

// ---- asking ---------------------------------------------------------------------

// One line of text from the person, or nil for Cancel.
- (NSString*)ask:(NSString*)question detail:(NSString*)detail value:(NSString*)value {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = question;
    alert.informativeText = detail ?: @"";
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 360, 24)];
    field.stringValue = value ?: @"";
    alert.accessoryView = field;
    alert.window.initialFirstResponder = field;
    if ([alert runModal] != NSAlertFirstButtonReturn) return nil;
    return [field.stringValue stringByTrimmingCharactersInSet:
                                  NSCharacterSet.whitespaceAndNewlineCharacterSet];
}

- (BOOL)did:(int)outcome {
    [self say:Str(ride_outcome_message(project_))];
    return outcome != 0;
}

- (NSString*)outcomePath { return Str(ride_outcome_path(project_)); }

// Projects and single programs default beside the installation, as on Windows, or in
// ~/Documents/RIDE/<leaf> when the editor is not installed anywhere writable - made on demand and,
// missing or empty, filled from the install's own projects or programs, which is how the samples reach a new user.
- (NSString*)madeUnder:(NSString*)leaf {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    if (documents.length == 0) documents = [NSHomeDirectory() stringByAppendingPathComponent:@"Documents"];
    NSString* base = [documents stringByAppendingPathComponent:@"RIDE"];
    NSString* made = [base stringByAppendingPathComponent:leaf];
    [fm createDirectoryAtPath:made withIntermediateDirectories:YES attributes:nil error:NULL];
    NSArray<NSString*>* there = [fm contentsOfDirectoryAtPath:made error:NULL];
    NSString* seed = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:leaf];
    NSArray<NSString*>* samples = there.count == 0 ? [fm contentsOfDirectoryAtPath:seed error:NULL] : nil;
    for (NSString* item in samples)
        [fm copyItemAtPath:[seed stringByAppendingPathComponent:item]
                    toPath:[made stringByAppendingPathComponent:item] error:NULL];
    if (samples.count > 0)
        [self say:[NSString stringWithFormat:@"the sample %@ were copied into %@ to start from", leaf, made]];
    // The CCS samples reach a projects folder that already has others, each one it lacks.
    if ([leaf isEqualToString:@"projects"]) {
        NSString* from = [seed stringByAppendingPathComponent:@"ccs"];
        NSString* to = [made stringByAppendingPathComponent:@"ccs"];
        NSArray<NSString*>* ccs = [fm contentsOfDirectoryAtPath:from error:NULL];
        if (ccs.count > 0) [fm createDirectoryAtPath:to withIntermediateDirectories:YES attributes:nil error:NULL];
        for (NSString* item in ccs)
            if (![fm fileExistsAtPath:[to stringByAppendingPathComponent:item]])
                [fm copyItemAtPath:[from stringByAppendingPathComponent:item]
                            toPath:[to stringByAppendingPathComponent:item] error:NULL];
    }
    return made;
}

- (NSString*)rootNow {
    NSString* root = Str(ride_project_root(project_));
    if (root.length == 0) root = projectDirectory_;
    return root;
}

// ---- File -----------------------------------------------------------------------

- (void)newBuffer:(id)sender {
    (void)sender;
    Sheet* sheet = [self makeSheet:nil text:@""];
    [self showSheet:sheet];
    [self say:@"a new file - Save names it"];
}

- (void)openDocument:(id)sender {
    (void)sender;
    NSOpenPanel* pick = [NSOpenPanel openPanel];
    pick.allowsMultipleSelection = YES;
    pick.canChooseDirectories = NO;
    pick.directoryURL = [NSURL fileURLWithPath:[self madeUnder:@"programs"]];
    if ([pick runModal] != NSModalResponseOK) {
        [self say:@"not opened"];
        return;
    }
    for (NSURL* url in pick.URLs) {
        NSString* suffix = Str(ride_project_suffix());
        if (suffix.length > 0 && [url.path hasSuffix:suffix])
            [self loadProject:url.path];
        else
            [self openPath:url.path];
    }
}

- (void)openRecentFile:(NSMenuItem*)sender {
    NSString* where = sender.representedObject;
    if (where.length == 0) return;
    [self openPath:where];
}

- (void)saveDocument:(id)sender {
    (void)sender;
    if (current_ == nil) { [self say:@"no file is open"]; return; }
    [self saveSheet:current_];
}

- (void)saveDocumentAs:(id)sender {
    (void)sender;
    if (current_ == nil) { [self say:@"no file is open"]; return; }
    [self saveSheetAs:current_];
}

- (void)saveAll:(id)sender {
    (void)sender;
    NSUInteger written = 0;
    for (Sheet* sheet in [sheets_ copy]) {
        if (!sheet.modified) continue;
        if (sheet.path == nil) {
            [self showSheet:sheet];
            if ([self saveSheetAs:sheet]) ++written;
        } else if ([self mayOverwrite:sheet] && [self writeSheet:sheet]) {
            ++written;
        }
    }
    [self refreshTitle];
    [self refreshOpenFiles];
    [self say:[NSString stringWithFormat:@"%lu file(s) written", (unsigned long)written]];
}

- (void)revertDocumentToSaved:(id)sender {
    (void)sender;
    if (current_.path == nil) return;
    if ([self reload:current_]) [self say:@"back to what is on the disk"];
}

- (void)closeFile:(id)sender {
    (void)sender;
    if (current_ == nil) { [self.window performClose:nil]; return; }
    [self closeSheet:current_];
}

- (void)stepFile:(int)by {
    if (sheets_.count == 0) return;
    NSInteger at = current_ != nil ? (NSInteger)[sheets_ indexOfObject:current_] : 0;
    NSInteger count = (NSInteger)sheets_.count;
    [self showSheet:sheets_[(NSUInteger)(((at + by) % count + count) % count)]];
}
- (void)nextFile:(id)sender { (void)sender; [self stepFile:1]; }
- (void)previousFile:(id)sender { (void)sender; [self stepFile:-1]; }

// ---- Project --------------------------------------------------------------------

// One project of a CCS workspace, picked from a list, and its <workspace>/<project>.pro - workspace
// and project and nothing else - written if it is not there. Nil when cancelled or there is none.
- (NSString*)chooseWorkspaceProject:(NSString*)workspace {
    NSString* names = Str(ride_ccs_workspace_projects(Utf8(workspace)));
    if (names.length == 0) {
        [self say:[workspace.lastPathComponent stringByAppendingString:@" is a CCS workspace with no project RIDE can build"]];
        return nil;
    }
    NSPopUpButton* pick = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 300, 26) pullsDown:NO];
    [pick addItemsWithTitles:[names componentsSeparatedByString:@"\n"]];
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Open CCS project";
    alert.informativeText = [workspace.lastPathComponent stringByAppendingString:@" is a CCS workspace - one of its projects opens"];
    alert.accessoryView = pick;
    [alert addButtonWithTitle:@"Open"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn) { [self say:@"no project opened"]; return nil; }

    char file[1024] = {0}, why[512] = {0};
    if (!ride_ccs_workspace_pro(Utf8(workspace), Utf8(pick.titleOfSelectedItem), file, (int)sizeof file, why, (int)sizeof why)) {
        [self say:Str(why)];
        return nil;
    }
    return Str(file);
}

- (void)loadProject:(NSString*)where {
    // A build reads the project on its own thread; one is not replaced under it (H1).
    if (busy_) {
        [self say:@"a build or a program is running - stop it (Command-.) before opening a project"];
        return;
    }
    BOOL isDirectory = NO;
    [NSFileManager.defaultManager fileExistsAtPath:where isDirectory:&isDirectory];
    // A CCS workspace is a folder of projects, never one: which of them, then its .pro.
    if (isDirectory && ride_ccs_is_workspace(Utf8(where))) {
        where = [self chooseWorkspaceProject:where];
        if (where == nil) return;
        isDirectory = NO;
    }
    NSString* directory = isDirectory ? where : where.stringByDeletingLastPathComponent;

    // Tried on a project of its own, as the Windows window does: a load that fails leaves the one
    // already open exactly as it was, rather than on screen with nothing loaded behind it.
    RIDEProject* trying = ride_project_new();
    char why[512] = {0};
    int loaded = ride_project_load(trying, Utf8(where), why, (int)sizeof why);
    NSString* reason = Str(why);
    BOOL begun = !loaded && reason.length == 0 && ride_begin_from_what_is_there(trying, Utf8(directory));
    if (!loaded && !begun) {
        ride_project_free(trying);
        // With nothing open, the directory asked for is where a new file would go.
        if (!ride_project_loaded(project_)) {
            projectDirectory_ = directory;
            ride_project_set_root(project_, Utf8(directory));
        }
        [self say:reason.length > 0 ? reason : @"no .pro project in that directory"];
        return;
    }
    ride_project_free(project_);
    project_ = trying;
    projectDirectory_ = directory;
    if (begun) {
        [self projectArrived:where];
        [self say:Str(ride_outcome_message(project_))];
        return;
    }
    [self projectArrived:where];
    [self say:[NSString stringWithFormat:@"ready - %@, %d groups",
                                         Str(ride_project_name(project_)),
                                         ride_project_groups(project_)]];
    [self sayCcsProject];
    if (started_) {
        NSString* said = statusMessage_.stringValue;
        [self openFirstOfProject];
        [self say:said];
    }
}

// A CCS project opened as it is (bridge.h): its remembered configuration, and the line naming
// what of it RIDE cannot honour - said, and kept on the Output pane where the next message does not overwrite it.
- (void)sayCcsProject {
    if (!ride_project_is_ccs(project_)) return;
    int remembered = ride_project_ccs_configuration(project_);
    if (remembered >= 0) config_ = remembered;
    NSString* report = Str(ride_project_ccs_report(project_, config_));
    if (report.length == 0) return;
    // On a line of its own, never run on from whatever the pane ended with.
    NSString* before = output_.string.length > 0 && ![output_.string hasSuffix:@"\n"] ? @"\n" : @"";
    [self append:[NSString stringWithFormat:@"%@%@\n", before, report] to:output_];
    [self say:[report componentsSeparatedByString:@"\n"].firstObject];
    [self sayBuild];
}

- (void)projectArrived:(NSString*)where {
    indentWidth_ = ride_project_indent_width(project_);
    indentTabs_ = ride_project_indent_tabs(project_);
    indentCase_ = ride_project_case_indent(project_);
    toolKind_ = ride_project_toolchain(project_) != RIDE_TOOL_AUTO
                    ? ride_project_toolchain(project_) : ride_default_compiler();
    config_ = ride_configuration();
    NSString* arch = Str(ride_project_arch(project_));
    if (arch.length > 0) arch_ = arch;
    ride_remember_project(Utf8(where));
    [self fillNavigator];
    [self refreshTitle];
    [self sayBuild];
}

- (void)newProject:(id)sender {
    (void)sender;
    NSOpenPanel* pick = [NSOpenPanel openPanel];
    pick.canChooseFiles = NO;
    pick.canChooseDirectories = YES;
    pick.canCreateDirectories = YES;
    pick.prompt = @"Choose";
    pick.message = @"Where to put the project";
    pick.directoryURL = [NSURL fileURLWithPath:[self madeUnder:@"projects"]];
    if ([pick runModal] != NSModalResponseOK) { [self say:@"no project made"]; return; }

    NSString* place = pick.URL.path;
    NSString* name = [self ask:@"Project name"
                        detail:[@"It will be made in " stringByAppendingString:place]
                         value:@"Project"];
    if (name.length == 0) { [self say:@"no project made"]; return; }

    if ([self did:ride_begin_project(project_, Utf8(place), Utf8(name),
                                     Utf8(current_.path ?: @""))]) {
        projectDirectory_ = place;
        [self projectArrived:Str(ride_project_root(project_))];
    }
}

- (void)openProject:(id)sender {
    (void)sender;
    NSOpenPanel* pick = [NSOpenPanel openPanel];
    pick.canChooseFiles = YES;
    pick.canChooseDirectories = YES;
    pick.message = @"Choose a project's .pro file, the directory it is in, or a CCS project's folder";
    pick.directoryURL = [NSURL fileURLWithPath:[self madeUnder:@"projects"]];
    ProjectPick* only = [[ProjectPick alloc] init];
    pick.delegate = only;
    if ([pick runModal] != NSModalResponseOK) { [self say:@"no project opened"]; return; }
    [self loadProject:pick.URL.path];
}

- (void)openRecentProject:(NSMenuItem*)sender {
    NSString* where = sender.representedObject;
    if (where.length > 0) [self loadProject:where];
}

- (void)saveProjectAs:(id)sender {
    (void)sender;
    if (!ride_project_loaded(project_)) { [self say:@"there is no project to save"]; return; }
    NSString* suffix = Str(ride_project_suffix());
    NSSavePanel* pick = [NSSavePanel savePanel];
    pick.nameFieldStringValue = [Str(ride_project_name(project_)) stringByAppendingString:suffix];
    pick.directoryURL = [NSURL fileURLWithPath:[self madeUnder:@"projects"]];
    if ([pick runModal] != NSModalResponseOK) { [self say:@"not saved"]; return; }

    char why[512] = {0};
    if (!ride_project_save_as(project_, Utf8(pick.URL.path), why, (int)sizeof why)) {
        [self say:Str(why)];
        return;
    }
    projectDirectory_ = pick.URL.path.stringByDeletingLastPathComponent;
    [self fillNavigator];
    [self say:[pick.URL.lastPathComponent
                  stringByAppendingString:@" written - the project is saved there from now on"]];
}

- (void)closeProject:(id)sender {
    (void)sender;
    if (!ride_project_loaded(project_)) { [self say:@"there is no project open"]; return; }
    NSString* was = Str(ride_project_name(project_));
    [self rememberOpen];

    // The project's files go with it; each unsaved one asks first, and one
    // refusal keeps the project open with everything as it was.
    NSMutableArray<Sheet*>* theirs = [NSMutableArray array];
    for (Sheet* sheet in sheets_)
        if (sheet.path != nil && ride_project_holds(project_, Utf8(sheet.path)))
            [theirs addObject:sheet];
    for (Sheet* sheet in theirs)
        if (![self mayDiscard:sheet]) {
            [self say:[@"not closed - unsaved changes in " stringByAppendingString:[self shownName:sheet]]];
            return;
        }
    [sheets_ removeObjectsInArray:theirs];
    ride_project_close(project_);
    // A file that stays keeps its caret and scroll: showSheet stores them from current_, so current_
    // is let go only when it was one of the project's.
    Sheet* next = [sheets_ containsObject:current_] ? current_ : sheets_.firstObject;
    if (![sheets_ containsObject:current_]) current_ = nil;
    // What the window had taken from the project goes back to the installation's - its folder, target,
    // compiler, configuration and indentation - and the navigator loses the project's groups.
    projectDirectory_ = nil;
    arch_ = Str(ride_host_arch());
    if (arch_.length == 0) arch_ = Str(ride_arch(0));
    toolKind_ = ride_default_compiler();
    config_ = ride_configuration();
    indentWidth_ = ride_default_indent_width();
    indentTabs_ = ride_default_indent_tabs();
    indentCase_ = 0;
    [self fillNavigator];
    [self showSheet:next];
    [self refreshTitle];
    [self sayBuild];
    // Every pane was the project's: Errors alone was cleared, and Progress and Output kept
    // the closed project's build and run.
    [self clearIssues];
    progressLog_.string = @"";
    progressTitle_.stringValue = @"Nothing built yet";
    output_.string = @"";
    [self say:[NSString stringWithFormat:@"%@ closed, and %lu file(s) with it", was,
                                         (unsigned long)theirs.count]];
}

- (void)newProjectFile:(id)sender {
    (void)sender;
    NSString* root = [self rootNow];
    if (!ride_project_loaded(project_) || root.length == 0) {
        // No project: a single program, in a known place.
        NSString* programs = [self madeUnder:@"programs"];
        NSString* only = [self ask:@"New program"
                            detail:[@"A name, or one directory and a name. It will be made in "
                                       stringByAppendingString:programs]
                             value:@""];
        if (only.length == 0) { [self say:@"nothing made"]; return; }
        if ([only.lastPathComponent rangeOfString:@"."].location == NSNotFound)
            only = [only stringByAppendingString:toolKind_ == RIDE_TOOL_SHC ? @".shl"
                                                 : (toolKind_ == RIDE_TOOL_CC1 || toolKind_ == RIDE_TOOL_AUTO) ? @".c"
                                                                                                               : @".cpp"];
        NSString* target = [programs stringByAppendingPathComponent:only];
        if ([NSFileManager.defaultManager fileExistsAtPath:target]) {
            [self say:[only stringByAppendingString:@" is already there"]];
            return;
        }
        [NSFileManager.defaultManager createDirectoryAtPath:target.stringByDeletingLastPathComponent
                                withIntermediateDirectories:YES attributes:nil error:NULL];
        if (![@"" writeToFile:target atomically:YES encoding:NSUTF8StringEncoding error:NULL]) {
            [self say:[@"could not make " stringByAppendingString:only]];
            return;
        }
        [self openPath:target];
        [self say:[NSString stringWithFormat:@"%@ made in %@", only, programs]];
        return;
    }

    NSString* name = [self ask:@"New file"
                        detail:[@"A name, or one directory and a name. It will be made in "
                                   stringByAppendingString:root]
                         value:@""];
    if (name.length == 0) { [self say:@"nothing made"]; return; }
    NSString* group = Str(ride_group_for_file(Utf8(name.lastPathComponent)));
    if (group.length == 0) group = [self groupUnderCursor];
    if (![self did:ride_create_file(project_, Utf8(name), Utf8(group), toolKind_)]) return;
    [self fillNavigator];
    [self openPath:[self outcomePath]];
}

- (void)addCurrentFile:(id)sender {
    (void)sender;
    if (current_.path == nil) { [self say:@"save the file first, so it has a name"]; return; }
    NSString* wanted = Str(ride_group_for_file(Utf8(current_.path.lastPathComponent)));
    if (wanted.length == 0) wanted = @"Sources";
    NSString* group = [self ask:@"Add to group" detail:nil value:wanted];
    if (group.length == 0) { [self say:@"not added"]; return; }
    if ([self did:ride_add_existing(project_, Utf8(current_.path), Utf8(group))])
        [self fillNavigator];
}

- (void)addFiles:(id)sender {
    (void)sender;
    if (!ride_project_loaded(project_)) { [self say:@"there is no project open"]; return; }
    NSOpenPanel* pick = [NSOpenPanel openPanel];
    pick.allowsMultipleSelection = YES;
    pick.directoryURL = [NSURL fileURLWithPath:[self rootNow]];
    if ([pick runModal] != NSModalResponseOK) { [self say:@"nothing added"]; return; }
    NSString* fallback = [self groupUnderCursor];
    for (NSURL* url in pick.URLs) {
        NSString* group = Str(ride_group_for_file(Utf8(url.lastPathComponent)));
        if (group.length == 0) group = fallback;
        [self did:ride_add_existing(project_, Utf8(url.path), Utf8(group))];
    }
    [self fillNavigator];
}

- (void)removeFromProject:(id)sender {
    (void)sender;
    NSString* target = [self targetFile];
    if (target == nil) { [self say:@"this file has no name to look for"]; return; }
    if ([self did:ride_remove_from_project(project_, Utf8(target))]) [self fillNavigator];
}

- (void)renameFile:(id)sender {
    (void)sender;
    NSString* target = [self targetFile];
    if (target == nil) { [self say:@"no file to rename"]; return; }
    NSString* shown = ride_project_holds(project_, Utf8(target))
                          ? Str(ride_project_relative(project_, Utf8(target)))
                          : target.lastPathComponent;
    NSString* name = [self ask:[NSString stringWithFormat:@"Rename %@ to", shown] detail:nil value:shown];
    if (name.length == 0) { [self say:@"not renamed"]; return; }
    if (![self did:ride_rename_file(project_, Utf8(target), Utf8(name))]) return;
    NSString* now = [self outcomePath];
    Sheet* open = [self sheetFor:target];
    if (open != nil) open.path = now;
    [self refreshTitle];
    [self sayBuild];
    [self fillNavigator];
}

- (void)deleteFile:(id)sender {
    (void)sender;
    NSString* target = [self targetFile];
    if (target == nil) { [self say:@"no file to delete"]; return; }
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = [NSString stringWithFormat:@"Delete %@ from the disk?", target.lastPathComponent];
    alert.informativeText = @"This cannot be undone.";
    // Delete is the action and goes where the action goes, marked destructive, and Return does
    // not press it; Cancel is Escape's (L3).
    NSButton* remove = [alert addButtonWithTitle:@"Delete"];
    NSButton* cancel = [alert addButtonWithTitle:@"Cancel"];
    remove.keyEquivalent = @"";
    cancel.keyEquivalent = @"\033";
    if (@available(macOS 11.0, *)) remove.hasDestructiveAction = YES;
    if ([alert runModal] != NSAlertFirstButtonReturn) { [self say:@"not deleted"]; return; }
    if (![self did:ride_delete_file(project_, Utf8(target))]) return;
    Sheet* open = [self sheetFor:target];
    if (open != nil) {
        open.modified = NO;
        [self closeSheet:open];
    }
    [self fillNavigator];
}

- (void)moveToGroup:(id)sender {
    (void)sender;
    NSString* target = [self targetFile];
    if (target == nil) { [self say:@"no file to move"]; return; }
    NSString* group = [self ask:@"Move to group" detail:nil value:[self groupUnderCursor]];
    if (group.length == 0) { [self say:@"not moved"]; return; }
    if ([self did:ride_move_to_group(project_, Utf8(target), Utf8(group))])
        [self fillNavigator];
}

- (void)showInFinder:(id)sender {
    (void)sender;
    NSString* target = [self targetFile];
    if (target == nil) target = [self rootNow];
    if (target.length == 0) return;
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:target] ]];
}

// Option > Compiler Options: the tabbed dialog, opened on the configuration the window builds with
// and on the tab of the compiler that builds the file in front; what OK keeps, the next build uses.
- (void)compilerOptions:(id)sender {
    (void)sender;
    // Opened once the menu has closed: run from inside the menu's action, the modal loop starts
    // while the menu is still fading, which leaves its image traced over the window.
    [self performSelector:@selector(openCompilerOptions) withObject:nil afterDelay:0];
}

- (void)openCompilerOptions {
    int kind = ride_resolve(toolKind_, [self languageNow]);
    int tab = kind == RIDE_TOOL_CC1 ? 1 : kind == RIDE_TOOL_CXX1 ? 2 : kind == RIDE_TOOL_SHC ? 3 : 0;
    OptionsDialog* dialog = [[OptionsDialog alloc] initWithProject:project_ config:config_ arch:arch_];
    [dialog selectTab:tab];
    if ([dialog run:self.window])
        [self say:ride_project_loaded(project_) ? @"compiler options written to the project"
                                                 : @"compiler options written to settings.json"];
    else if (ride_project_is_ccs(project_))
        [self say:@"a CCS project's options are read from it - edit them in CCS"];
    else
        [self say:@"compiler options unchanged"];
}

- (void)projectIncludes:(id)sender {
    (void)sender;
    if (!ride_project_loaded(project_)) {
        [self say:@"there is no project open - Option > Shared Include Paths is the installation's"];
        return;
    }
    NSString* line = [self ask:@"Project include paths"
                        detail:@"Kept in the project's .pro, relative to it, ';' between them"
                         value:Str(ride_project_includes(project_))];
    if (line == nil) { [self say:@"the project's include paths are unchanged"]; return; }
    [self did:ride_project_set_includes(project_, Utf8(line))];
}

- (void)projectLibraries:(id)sender {
    (void)sender;
    if (!ride_project_loaded(project_)) {
        [self say:@"there is no project open - Option > Shared Libraries is the installation's"];
        return;
    }
    NSString* line = [self ask:@"Project libraries"
                        detail:@"Kept in the project's .pro, relative to it, ';' between them, linked before the shared ones"
                         value:Str(ride_project_libraries(project_))];
    if (line == nil) { [self say:@"the project's libraries are unchanged"]; return; }
    [self did:ride_project_set_libraries(project_, Utf8(line))];
}

// ---- the panel: Errors, Progress, Output -----------------------------------------

- (void)showPanel:(NSInteger)which {
    if (panelPane_.hidden) [self togglePanel:nil];
    [panel_ selectTabViewItemAtIndex:which];
}

- (void)showPanelTab:(NSMenuItem*)sender { [self showPanel:sender.tag - kTagPanelBase]; }

- (void)cleanBuild:(id)sender {
    (void)sender;
    if (busy_) { [self say:@"a build or a program is running - stop it (Command-.) before cleaning"]; return; }
    NSArray<NSString*>* files = [Str(ride_project_clean(project_)) componentsSeparatedByString:@"\n"];
    files = [files filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"length > 0"]];
    output_.string = @"";
    progressLog_.string = @"";
    [self clearIssues];
    NSString* name = ride_project_loaded(project_) ? Str(ride_project_name(project_)) : @"no project";
    NSString* head = [NSString stringWithFormat:@"Clean succeeded: %@ - %@", name,
                         files.count == 0 ? @"nothing was left to remove"
                                          : [NSString stringWithFormat:@"%lu removed", (unsigned long)files.count]];
    NSMutableString* said = [NSMutableString stringWithFormat:@"========== %@ ==========\n", head];
    for (NSString* f in files) [said appendFormat:@"%@\n", f];
    [self append:said to:output_];
    [self showPanel:kPanelOutput];
    [self say:head];
}

- (void)clearIssues {
    [issues_ removeAllObjects];
    [issueTable_ reloadData];
    [self labelErrorsTab];
    [self applyErrorMarks];
}

- (void)clearIssuesAction:(id)sender {
    (void)sender;
    [self clearIssues];
    [self say:@"issues cleared"];
}

- (void)labelErrorsTab {
    NSUInteger errors = 0, warnings = 0, found = 0;
    for (Issue* issue in issues_) if (issue.found) ++found;
    if (found > 0) {
        [panel_ tabViewItemAtIndex:kPanelErrors].label = [NSString stringWithFormat:@"Found (%lu)", (unsigned long)found];
        return;
    }
    for (Issue* issue in issues_) {
        if (issue.warning) ++warnings;
        else ++errors;
    }
    NSString* label = @"Errors";
    if (errors > 0 || warnings > 0)
        label = warnings > 0 ? [NSString stringWithFormat:@"Errors (%lu)  Warnings (%lu)",
                                                          (unsigned long)errors, (unsigned long)warnings]
                             : [NSString stringWithFormat:@"Errors (%lu)", (unsigned long)errors];
    [panel_ tabViewItemAtIndex:kPanelErrors].label = label;
}

// The red and orange marks in the gutter, for the file in front.
- (void)applyErrorMarks {
    NSMutableIndexSet* errors = [NSMutableIndexSet indexSet];
    NSMutableIndexSet* warnings = [NSMutableIndexSet indexSet];
    NSString* here = current_.path.stringByStandardizingPath;
    for (Issue* issue in issues_) {
        if (here == nil || issue.line <= 0 || issue.file == nil) continue;
        if (![issue.file.stringByStandardizingPath isEqualToString:here]) continue;
        if (issue.warning) [warnings addIndex:(NSUInteger)issue.line];
        else [errors addIndex:(NSUInteger)issue.line];
    }
    gutter_.errorLines = errors;
    gutter_.warningLines = warnings;
}

// The file a diagnostic names, made absolute; nil when it names none or one that is not there -
// a linker's message, say - rather than the source, or the project's directory (M3).
- (NSString*)absoluteFor:(NSString*)file source:(NSString*)source {
    if (file.length == 0) return source.length > 0 ? source : nil;
    if (file.isAbsolutePath) return file.stringByStandardizingPath;
    if (ride_project_loaded(project_)) {
        NSString* full = Str(ride_project_absolute(project_, Utf8(file)));
        if ([NSFileManager.defaultManager fileExistsAtPath:full]) return full;
    }
    if (source.length > 0) {
        NSString* beside = [source.stringByDeletingLastPathComponent stringByAppendingPathComponent:file];
        if ([NSFileManager.defaultManager fileExistsAtPath:beside]) return beside.stringByStandardizingPath;
    }
    return nil;
}

// Every diagnostic in what the compilers printed, read line by line with the
// core's own parser - GNU, MSVC, Shalimar and cc1's preprocessor spellings -
// and the one the build itself reported added if the reading missed it.
- (void)collectIssues:(const Outcome&)outcome source:(NSString*)source {
    [issues_ removeAllObjects];
    std::string sourcePath = StdString(source);
    const std::string& text = outcome.output;
    std::string previous;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        // The line on its own first; then with the one before it, which is
        // how cc1's two-line preprocessor form is read. Through the bridge (M11).
        int row = 0, column = 0;
        const char* file = "";
        const char* message = "";
        int found = ride_parse_diagnostic(line.c_str(), sourcePath.c_str(), &row, &column, &file, &message);
        if (!found && !previous.empty())
            found = ride_parse_diagnostic((previous + "\n" + line).c_str(), sourcePath.c_str(), &row,
                                          &column, &file, &message);
        if (found) {
            Issue* issue = [[Issue alloc] init];
            issue.file = [self absoluteFor:StrLossy(file, std::strlen(file)) source:source];
            issue.line = row;
            issue.column = column;
            issue.message = StrLossy(message, std::strlen(message));
            issue.warning = line.find("warning") != std::string::npos &&
                            line.find("error") == std::string::npos;
            BOOL seen = NO;
            for (Issue* other in issues_)
                if (other.line == issue.line && other.column == issue.column &&
                    [other.message isEqualToString:issue.message]) { seen = YES; break; }
            if (!seen) [issues_ addObject:issue];
        }
        previous = line;
        if (end == std::string::npos) break;
        at = end + 1;
    }

    if (outcome.hasError) {
        NSString* message = StrLossy(outcome.errorMessage);
        BOOL seen = NO;
        for (Issue* issue in issues_)
            if (!issue.warning && issue.line == outcome.errorLine &&
                [issue.message isEqualToString:message]) { seen = YES; break; }
        if (!seen) {
            Issue* issue = [[Issue alloc] init];
            issue.file = [self absoluteFor:StrLossy(outcome.errorFile) source:source];
            issue.line = outcome.errorLine;
            issue.column = outcome.errorColumn;
            issue.message = message;
            [issues_ insertObject:issue atIndex:0];
        }
    }
    [issueTable_ reloadData];
    [self labelErrorsTab];
    [self applyErrorMarks];
}

- (void)goToIssue:(Issue*)issue {
    if (issue == nil) return;
    BOOL directory = NO;
    BOOL there = issue.file != nil &&
                 [NSFileManager.defaultManager fileExistsAtPath:issue.file isDirectory:&directory] &&
                 !directory;
    if (there) [self openPath:issue.file];
    // A diagnostic in no file this window can show goes nowhere, rather than to a line of
    // whatever file is in front.
    if (current_ == nil || !there || ![current_.path.stringByStandardizingPath
                                          isEqualToString:issue.file.stringByStandardizingPath]) {
        [self say:issue.message ?: @""];
        return;
    }
    if (issue.found && issue.line == 0) { [self.window makeFirstResponder:code_]; [self say:issue.file]; return; }
    [code_ goToLine:issue.line column:issue.column];
    [self.window makeFirstResponder:code_];
    [self say:[NSString stringWithFormat:@"%@:%ld:%ld: %@: %@",
                                         issue.file.lastPathComponent ?: @"", (long)issue.line,
                                         (long)issue.column, issue.warning ? @"warning" : @"error",
                                         issue.message]];
}

- (void)issueChosen:(id)sender {
    (void)sender;
    NSInteger row = issueTable_.clickedRow >= 0 ? issueTable_.clickedRow : issueTable_.selectedRow;
    if (row < 0 || row >= (NSInteger)issues_.count) return;
    [self goToIssue:issues_[(NSUInteger)row]];
}

- (void)nextIssue:(id)sender {
    (void)sender;
    if (issues_.count == 0) { [self say:@"no issues"]; return; }
    NSInteger row = issueTable_.selectedRow + 1;
    if (row >= (NSInteger)issues_.count) row = 0;
    [issueTable_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    [self goToIssue:issues_[(NSUInteger)row]];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)table {
    (void)table;
    return (NSInteger)issues_.count;
}

- (NSView*)tableView:(NSTableView*)table viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
    Issue* issue = issues_[(NSUInteger)row];
    NSString* which = column.identifier;
    NSTableCellView* cell = [table makeViewWithIdentifier:which owner:self];
    if (cell == nil) {
        cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, column.width, 18)];
        cell.identifier = which;
        if ([which isEqualToString:@"kind"]) {
            NSImageView* image = [[NSImageView alloc] initWithFrame:NSMakeRect(3, 1, 16, 16)];
            [cell addSubview:image];
            cell.imageView = image;
        } else {
            NSTextField* text = [NSTextField labelWithString:@""];
            text.frame = NSMakeRect(2, 1, column.width - 4, 16);
            text.autoresizingMask = NSViewWidthSizable;
            text.lineBreakMode = NSLineBreakByTruncatingTail;
            text.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
            [cell addSubview:text];
            cell.textField = text;
        }
    }
    if ([which isEqualToString:@"kind"]) {
        NSString* symbol = issue.found ? @"magnifyingglass"
                         : issue.warning ? @"exclamationmark.triangle.fill" : @"xmark.octagon.fill";
        cell.imageView.image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil];
        cell.imageView.contentTintColor = issue.found ? [NSColor secondaryLabelColor]
                                        : issue.warning ? [NSColor systemOrangeColor] : [NSColor systemRedColor];
    } else if ([which isEqualToString:@"message"]) {
        cell.textField.stringValue = issue.message ?: @"";
    } else if ([which isEqualToString:@"file"]) {
        cell.textField.stringValue = issue.file.lastPathComponent ?: @"";
    } else {
        cell.textField.stringValue = issue.column > 0
            ? [NSString stringWithFormat:@"%ld:%ld", (long)issue.line, (long)issue.column]
            : [NSString stringWithFormat:@"%ld", (long)issue.line];
    }
    return cell;
}

// Progress, a step at a time with the time it happened.
- (void)progress:(NSString*)step {
    static NSDateFormatter* stamp = nil;
    if (stamp == nil) {
        stamp = [[NSDateFormatter alloc] init];
        stamp.dateFormat = @"HH:mm:ss";
    }
    NSString* line = [NSString stringWithFormat:@"[%@]  %@\n", [stamp stringFromDate:[NSDate date]], step];
    [self append:line to:progressLog_];
}

- (void)append:(NSString*)text to:(NSTextView*)view {
    [self append:text to:view colour:nil];
}

- (void)append:(NSString*)text to:(NSTextView*)view colour:(NSColor*)colour {
    NSDictionary* attributes = @{
        NSFontAttributeName : view.font ?: [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName : colour ?: [NSColor textColor],
    };
    [view.textStorage appendAttributedString:[[NSAttributedString alloc] initWithString:text
                                                                             attributes:attributes]];
    [view scrollRangeToVisible:NSMakeRange(view.string.length, 0)];
}

// A report shown in Output: under a running program it goes after what the program printed rather
// than over it, which lost the program's output.
- (void)showInOutput:(NSString*)text {
    if (running_ != NULL) [self append:[@"\n" stringByAppendingString:text] to:output_];
    else [self setOutput:text];
}

- (void)setOutput:(NSString*)text {
    [output_.textStorage setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    [self append:text to:output_];
}

// A piece of work begins: the bar runs, the spinner turns, the log starts over.
- (void)beginWork:(NSString*)title steps:(double)steps {
    busy_ = YES;
    workStarted_ = [NSDate date];
    [progressLog_.textStorage setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    progressTitle_.stringValue = title;
    progressBar_.indeterminate = steps <= 0;
    progressBar_.maxValue = steps > 0 ? steps : 1;
    progressBar_.doubleValue = 0;
    if (steps <= 0) [progressBar_ startAnimation:nil];
    [statusSpinner_ startAnimation:nil];
    [self progress:title];
    [self say:title];
}

- (void)advanceWork:(NSString*)step {
    if (!progressBar_.indeterminate) progressBar_.doubleValue += 1;
    [self progress:step];
}

- (void)endWork:(NSString*)verdict ok:(BOOL)ok {
    busy_ = NO;
    if (quitWhenIdle_) {
        quitWhenIdle_ = NO;
        [NSApp replyToApplicationShouldTerminate:YES];
        return;
    }
    [progressBar_ stopAnimation:nil];
    progressBar_.indeterminate = NO;
    progressBar_.doubleValue = progressBar_.maxValue;
    [statusSpinner_ stopAnimation:nil];
    NSTimeInterval took = -[workStarted_ timeIntervalSinceNow];
    progressTitle_.stringValue = [NSString stringWithFormat:@"%@ %@ (%.2f s)", ok ? @"✓" : @"✗",
                                                            verdict, took];
    [self progress:[NSString stringWithFormat:@"%@ - %.2f s", verdict, took]];
    [self say:verdict];
}

// ---- Build ----------------------------------------------------------------------

- (BOOL)mayStartWork {
    if (busy_) {
        [self say:@"still working - Build > Stop (Command-.) ends it"];
        return NO;
    }
    return YES;
}

- (void)compileFile:(id)sender { (void)sender; [self buildFile:NO]; }
- (void)runFile:(id)sender { (void)sender; ride_run_next(RIDE_RUN_PROGRAM); [self buildFile:YES]; }
- (void)buildProjectAction:(id)sender { (void)sender; ride_run_next(RIDE_RUN_PROGRAM); [self buildProject:NO]; }
- (void)runProjectAction:(id)sender { (void)sender; ride_run_next(RIDE_RUN_PROGRAM); [self buildProject:YES]; }

// **Build > Emulate on vm6747 and Build > Verify, for tms6747.** The Run the file or the project in
// front would make, with the bridge told which runs the program: the assembly on vm6747 and no .out
// linked, or both runs compared. Said to the next start (ride_run_next), which takes it.
- (void)runOn:(int)runner {
    if (![arch_ isEqualToString:@"tms6747"]) {
        [self say:runner == RIDE_RUN_EMULATOR ? @"Emulate on vm6747 is for tms6747 - choose it under Target"
                                              : @"Verify is for tms6747 - choose it under Target"];
        return;
    }
    ride_run_next(runner);
    if ([self projectMode]) [self buildProject:YES];
    else [self buildFile:YES];
}
- (void)emulateRun:(id)sender { (void)sender; [self runOn:RIDE_RUN_EMULATOR]; }
- (void)verifyRun:(id)sender { (void)sender; [self runOn:RIDE_RUN_VERIFY]; }

// Build > Stop: the program running, or the build - its compiler or linker killed, and the build
// then ending as a failure that says it was stopped (H2).
- (void)stopWork:(id)sender {
    (void)sender;
    if (finding_) { ride_find_stop(); [self say:@"stopping the search..."]; return; }
    if (running_ != NULL) ride_running_stop(running_);
    else if (busy_) ride_cancel_builds();
    else return;
    [self say:@"stopping..."];
}

// Stops what is running and waits for it, for the application going (main.mm).
- (BOOL)stillBuilding { return busy_ && running_ == NULL; }

- (void)quitWhenIdle {
    quitWhenIdle_ = YES;
    // A build that does not come back in ten seconds is not waited on for ever.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        if (!self->quitWhenIdle_) return;
        self->quitWhenIdle_ = NO;
        [NSApp replyToApplicationShouldTerminate:YES];
    });
}

- (void)stopEverything {
    ride_find_stop();
    if (running_ != NULL) {
        ride_running_stop(running_);
        ride_running_wait(running_, 5000);
    }
    ride_cancel_builds();
}

- (void)buildFile:(BOOL)andRun {
    if (![self mayStartWork]) return;
    if (current_ == nil) { [self say:@"no file is open"]; return; }
    if (current_.path == nil && ![self saveSheetAs:current_]) return;
    if (![self saveEveryModified]) return;

    NSString* path = current_.path;
    int of = ride_project_runs_as_project(project_, Utf8(path));
    if (of > 0) {
        [self say:[NSString stringWithFormat:@"%@ is one of %d sources of %@ - building the project",
                                             path.lastPathComponent, of,
                                             Str(ride_project_name(project_))]];
        [self buildProject:andRun];
        return;
    }

    int language = [self languageNow];
    int kind = ride_resolve(toolKind_, language);
    if (!ride_can_compile(kind, language)) {
        [self say:Str(ride_refusal(kind, language))];
        return;
    }
    if (andRun && !ride_runs_here(kind, Utf8(arch_))) {
        [self say:Str(ride_why_not_run(kind, Utf8(arch_)))];
        return;
    }

    std::string cc1 = StdString(cc1_), cl = StdString(cl_), shc = StdString(shc_), cxx1 = StdString(cxx1_);
    std::string source = StdString(path), arch = StdString(arch_);
    int config = config_;
    RIDEProject* project = project_;

    NSString* command = andRun
        ? Str(ride_shown_run_command(project, cc1.c_str(), cl.c_str(), shc.c_str(), cxx1.c_str(),
                                         kind, source.c_str(), language, arch.c_str(), config))
        : Str(ride_shown_command(project, cc1.c_str(), cl.c_str(), shc.c_str(), cxx1.c_str(),
                                     kind, source.c_str(), language, arch.c_str(), config));
    NSString* compiler = Str(ride_toolchain_name(kind));

    [self clearIssues];
    [self setOutput:[NSString stringWithFormat:@"$ %@\n", command]];
    [self beginWork:[NSString stringWithFormat:@"%@ %@ with %@", andRun ? @"Building and running" : @"Compiling",
                                                path.lastPathComponent, compiler]
              steps:andRun ? 3 : 2];
    [self advanceWork:[@"$ " stringByAppendingString:command]];
    [self showPanel:kPanelProgress];

    if (andRun) {
        // Built and run on the core's worker, the program's output here as it comes and the input
        // line open to it; the project is read now, on this thread, and not again.
        [self showPanel:kPanelOutput];
        NSString* args = [self currentArgs];
        std::string argsLine = StdString(args), argsBase = StdString([self argsBase]);
        if (args.length > 0)
            [self append:[NSString stringWithFormat:@"$ %@ %@\n", path.lastPathComponent.stringByDeletingPathExtension, args]
                      to:output_];
        [self startRun:ride_run_start_line(project, cc1.c_str(), cl.c_str(), shc.c_str(), cxx1.c_str(), kind,
                                           source.c_str(), language, arch.c_str(), config,
                                           argsLine.c_str(), argsBase.c_str(), RunOutput, (__bridge void*)self)
                source:path compiler:compiler program:nil];
        return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        RIDEBuild* built = ride_build(project, cc1.c_str(), cl.c_str(), shc.c_str(), cxx1.c_str(),
                                      kind, source.c_str(), language, arch.c_str(), config);
        Outcome outcome = OutcomeOf(built);
        ride_build_free(built);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self finishFile:outcome source:path compiler:compiler run:NO];
        });
    });
}

- (void)finishFile:(const Outcome&)outcome source:(NSString*)path
          compiler:(NSString*)compiler run:(BOOL)andRun {
    // A run's build streamed its lines into Output as it went; a compile's come now.
    if (!andRun) [self append:StrLossy(outcome.output) to:output_];
    [self collectIssues:outcome source:path];

    if (outcome.stopped && !andRun) {
        [self endWork:[compiler stringByAppendingString:@" stopped"] ok:NO];
        [self showPanel:kPanelOutput];
        return;
    }
    if (outcome.hasError) {
        [self advanceWork:[NSString stringWithFormat:@"%@ stopped at line %d: %@", compiler,
                                                     outcome.errorLine, StrLossy(outcome.errorMessage)]];
        NSString* where = outcome.errorFile.empty() ? path.lastPathComponent
                                                    : StrLossy(outcome.errorFile).lastPathComponent;
        [self endWork:[NSString stringWithFormat:@"%lu issue(s) - %@:%d:%d: error: %@",
                                                 (unsigned long)issues_.count, where,
                                                 outcome.errorLine, outcome.errorColumn,
                                                 StrLossy(outcome.errorMessage)]
                   ok:NO];
        [self showPanel:kPanelErrors];
        if (issues_.count > 0) {
            [issueTable_ selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
            [self goToIssue:issues_.firstObject];
        }
        return;
    }
    if (!outcome.ok) {
        [self advanceWork:[compiler stringByAppendingString:outcome.stopped ? @" stopped" : @" did not finish"]];
        [self endWork:outcome.stopped ? [compiler stringByAppendingString:@" stopped"]
                     : [NSString stringWithFormat:andRun ? @"%@ built no program - see Output"
                                                         : @"%@ failed - see Output", compiler]
                   ok:NO];
        [self showPanel:issues_.count > 0 ? kPanelErrors : kPanelOutput];
        return;
    }

    if (!andRun) {
        [self advanceWork:[NSString stringWithFormat:@"%@ wrote %d lines of assembly", compiler,
                                                     outcome.assemblyLines]];
        if (!outcome.assembly.empty()) {
            [self append:[NSString stringWithFormat:@"\n---- assembly, %d lines ----\n", outcome.assemblyLines]
                      to:output_];
            [self append:StrLossy(outcome.assembly) to:output_];
            [output_ scrollRangeToVisible:NSMakeRange(0, 0)];
        }
        // Said in so many words, as Xcode and CCS do: the first line of Output, and the status line.
        NSString* verdict = [NSString stringWithFormat:@"Compilation succeeded%@: %@ - 0 errors, %d lines of assembly",
                                                       issues_.count > 0 ? @" with warnings" : @"",
                                                       path.lastPathComponent, outcome.assemblyLines];
        [self append:[NSString stringWithFormat:@"\n========== %@ ==========\n", verdict] to:output_];
        [self endWork:verdict ok:YES];
        [self showPanel:issues_.count > 0 ? kPanelErrors : kPanelOutput];
        return;
    }
    [self finishRun:outcome name:path.lastPathComponent];
}

// ---- a program the window watches -------------------------------------------

// RIDEOutput: on the core's worker thread, so the bytes are copied and carried to the main one.
static void RunOutput(void* user, const char* bytes, int size, int stream) {
    WindowController* window = (__bridge WindowController*)user;
    if (bytes == NULL) {
        dispatch_async(dispatch_get_main_queue(), ^{ [window runEnded]; });
        return;
    }
    [window queueOutput:bytes size:size stream:stream];
}

// The most a stream holds while the main thread is behind; past it the oldest bytes go, and Output
// says so - a program printing without end costs this and no more.
static const size_t kInboxMost = 1 << 20;
// The most Output keeps; past it the oldest lines are dropped.
static const NSUInteger kOutputMost = 2000000;

// On the program's thread: into the inbox, and one drain asked for, 40 ms on - about 25 a second.
- (void)queueOutput:(const char*)bytes size:(int)size stream:(int)stream {
    if (stream < 0 || stream > 2) stream = 0;
    bool schedule = false;
    {
        std::lock_guard<std::mutex> hold(inboxLock_);
        std::string& box = inbox_[stream];
        box.append(bytes, (size_t)size);
        if (box.size() > kInboxMost) {
            size_t cut = box.size() - kInboxMost;
            size_t line = box.find('\n', cut);
            box.erase(0, line == std::string::npos ? cut : line + 1);
            box.insert(0, "[... output dropped while the window caught up ...]\n");
        }
        if (!drainQueued_) { drainQueued_ = true; schedule = true; }
    }
    if (schedule)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 40 * NSEC_PER_MSEC), dispatch_get_main_queue(),
                       ^{ [self drainOutput]; });
}

// On the main thread: what the inbox holds, shown in one go per stream.
- (void)drainOutput {
    std::string taken[3];
    {
        std::lock_guard<std::mutex> hold(inboxLock_);
        for (int i = 0; i < 3; ++i) taken[i].swap(inbox_[i]);
        drainQueued_ = false;
    }
    if (running_ == NULL) return;   // stopped: what was queued is not wanted
    for (int i = 0; i < 3; ++i)
        if (!taken[i].empty()) [self runSaid:taken[i] stream:i];
}

- (void)startRun:(RIDERunning*)running source:(NSString*)source compiler:(NSString*)compiler
         program:(NSString*)program {
    if (running == NULL) {
        [self endWork:@"the program could not be started - no thread for it" ok:NO];
        return;
    }
    running_ = running;
    runSource_ = source;
    runCompiler_ = compiler;
    runProgram_ = program;
    for (std::string& pending : pending_) pending.clear();
    inputLine_.enabled = YES;
    inputLine_.stringValue = @"";
    // Not the keyboard yet: what is typed during the compile belongs to the editor (it went to the
    // program's input). The first thing the program prints hands it over.
    programSpoke_ = NO;
}

- (void)runSaid:(const std::string&)piece stream:(int)stream {
    if (stream < 0 || stream > 2) stream = 0;
    pending_[stream] += piece;
    NSString* text = StrLossy(WholeCharacters(pending_[stream]));
    if (text.length == 0) return;
    NSColor* colour = stream == RIDE_STREAM_ERR ? [NSColor systemRedColor]
                    : stream == RIDE_STREAM_BUILD ? [NSColor secondaryLabelColor] : nil;
    if (stream != RIDE_STREAM_BUILD && !programSpoke_ && running_ != NULL) {
        programSpoke_ = YES;
        [self say:[NSString stringWithFormat:@"%@ is running - its input goes in the line under Output",
                                             runProgram_.lastPathComponent ?: @"the program"]];
        [self.window makeFirstResponder:inputLine_];
    }
    [self append:text to:output_ colour:colour];
    // Output keeps its last two million characters or so, from a line's start.
    NSTextStorage* kept = output_.textStorage;
    if (kept.length > kOutputMost) {
        NSUInteger cut = kept.length - kOutputMost * 3 / 4;
        NSRange line = [kept.string rangeOfString:@"\n" options:0 range:NSMakeRange(cut, kept.length - cut)];
        if (line.location != NSNotFound) cut = line.location + 1;
        [kept replaceCharactersInRange:NSMakeRange(0, cut) withString:@"[... earlier output dropped ...]\n"];
    }
}

- (void)runEnded {
    if (running_ == NULL) return;
    [self drainOutput];
    for (int stream = 0; stream < 3; ++stream)
        if (!pending_[stream].empty()) {
            [self append:StrLossy(pending_[stream]) to:output_];
            pending_[stream].clear();
        }
    Outcome outcome = OutcomeOf(running_);
    bool ran = ride_running_ran(running_) != 0;
    ride_running_free(running_);
    running_ = NULL;
    inputLine_.enabled = NO;
    if (self.window.firstResponder == inputLine_.currentEditor || self.window.firstResponder == inputLine_)
        [self.window makeFirstResponder:current_ != nil ? (NSResponder*)code_ : nil];

    if (runProgram_ != nil) {
        // Run Project: the build is done; this was the program.
        outcome.ok = ran;
        [self finishRun:outcome name:runProgram_.lastPathComponent];
    } else {
        [self finishFile:outcome source:runSource_ compiler:runCompiler_ run:YES];
    }
}

- (void)finishRun:(const Outcome&)outcome name:(NSString*)name {
    if (outcome.stopped) {
        [self append:@"\n[stopped]\n" to:output_];
        [self endWork:[name stringByAppendingString:@" was stopped"] ok:NO];
        [self showPanel:kPanelOutput];
        return;
    }
    [self advanceWork:@"built; ran it"];
    [self append:[NSString stringWithFormat:@"\n[program returned %d]\n", outcome.status] to:output_];
    [self advanceWork:[NSString stringWithFormat:@"the program returned %d", outcome.status]];
    [self endWork:[NSString stringWithFormat:@"%@ ran - it returned %d", name, outcome.status]
               ok:outcome.status == 0];
    [self showPanel:kPanelOutput];
}

// The input line: Return sends it and a newline; the program's terminal shows it as it reads.
- (void)inputEntered:(id)sender {
    (void)sender;
    if (running_ == NULL) return;
    std::string line = StdString(inputLine_.stringValue) + "\n";
    if (!ride_running_send(running_, line.data(), (int)line.size()))
        [self say:@"the program is not reading any more"];
    inputLine_.stringValue = @"";
}

// A line pasted with its newline is sent as typed and Return pressed: every whole line goes to the
// program, what follows the last newline stays to be finished. Pasted, it used to vanish.
- (void)controlTextDidChange:(NSNotification*)note {
    if (note.object != inputLine_ || running_ == NULL) return;
    NSString* text = [inputLine_.stringValue stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
    text = [text stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
    NSRange last = [text rangeOfString:@"\n" options:NSBackwardsSearch];
    if (last.location == NSNotFound) return;
    std::string lines = StdString([text substringToIndex:last.location + 1]);
    if (!ride_running_send(running_, lines.data(), (int)lines.size()))
        [self say:@"the program is not reading any more"];
    inputLine_.stringValue = [text substringFromIndex:last.location + 1];
}

// Control-D on the input line ends the program's input, as at a terminal; the field editor asks
// it as deleteForward:, which Control-D is bound to.
- (BOOL)control:(NSControl*)control textView:(NSTextView*)view doCommandBySelector:(SEL)command {
    (void)view;
    if (control != inputLine_ || command != @selector(deleteForward:)) return NO;
    NSEvent* event = NSApp.currentEvent;
    if (event.type != NSEventTypeKeyDown || !(event.modifierFlags & NSEventModifierFlagControl)) return NO;
    if (running_ == NULL) return YES;
    if (inputLine_.stringValue.length > 0) [self inputEntered:nil];
    ride_running_close_input(running_);
    [self say:@"the program's input is closed - its next read sees the end of it"];
    return YES;
}

- (void)buildProject:(BOOL)andRun {
    if (![self mayStartWork]) return;
    if (!ride_project_loaded(project_)) { [self say:@"there is no project open"]; return; }
    if (!ride_project_target_ready(project_)) {
        NSString* why = Str(ride_project_target_why(project_));
        NSString* detail = Str(ride_project_target_detail(project_));
        [self say:why];
        [self setOutput:detail.length > 0 ? [NSString stringWithFormat:@"%@\n\n%@\n", why, detail] : why];
        [self showPanel:kPanelOutput];
        return;
    }
    if (![self saveEveryModified]) return;

    std::string cc1 = StdString(cc1_), cl = StdString(cl_), shc = StdString(shc_), cxx1 = StdString(cxx1_);
    std::string arch = StdString(arch_);
    int config = config_;
    int toolKind = toolKind_;
    RIDEProject* project = project_;

    // Every part to its own compiler, checked before anything runs.
    int parts = ride_project_target_parts(project);
    NSMutableArray<NSString*>* compilers = [NSMutableArray array];
    NSMutableArray<NSString*>* plan = [NSMutableArray array];
    for (int i = 0; i < parts; ++i) {
        int language = ride_project_part_language(project, i);
        int kind = ride_project_part_toolchain(project, i, cc1.c_str(), cl.c_str(), shc.c_str(),
                                               cxx1.c_str(), toolKind);
        if (!ride_can_compile(kind, language)) {
            [self say:Str(ride_refusal(kind, language))];
            return;
        }
        if (andRun && !ride_runs_here(kind, arch.c_str())) {
            [self say:Str(ride_why_not_run(kind, arch.c_str()))];
            return;
        }
        NSString* word = Str(ride_toolchain_name(kind));
        if (![compilers containsObject:word]) [compilers addObject:word];
        [plan addObject:[NSString stringWithFormat:@"part %d: %@ (%@) with %@", i + 1,
                                                   Str(ride_project_part_group(project, i)),
                                                   Str(ride_language_name(language)), word]];
    }

    NSString* program = Str(ride_project_target_program(project));
    int howMany = ride_project_target_sources(project);
    NSMutableString* said = [NSMutableString stringWithFormat:@"$ %@ %d %@ -o %@\n",
                                                              [compilers componentsJoinedByString:@", "],
                                                              howMany, howMany == 1 ? @"source" : @"sources",
                                                              program];
    for (int i = 0; i < howMany; ++i)
        [said appendFormat:@"    %@\n", Str(ride_project_target_source(project, i))];

    [self clearIssues];
    [self setOutput:said];
    [self beginWork:[NSString stringWithFormat:@"%@ %@ from %d %@", andRun ? @"Building and running" : @"Building",
                                               program.lastPathComponent, howMany,
                                               howMany == 1 ? @"source" : @"sources"]
              steps:parts + (andRun ? 2 : 1)];
    for (NSString* step in plan) [self progress:step];
    [self showPanel:kPanelProgress];

    NSString* joined = [compilers componentsJoinedByString:@", "];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        RIDEBuild* made = ride_build_target(project, cc1.c_str(), cl.c_str(), shc.c_str(), cxx1.c_str(),
                                            toolKind, arch.c_str(), config);
        Outcome outcome = OutcomeOf(made);
        if (made != NULL) ride_build_free(made);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self finishProject:outcome program:program parts:parts compilers:joined run:andRun];
        });
    });
}

- (void)finishProject:(const Outcome&)outcome program:(NSString*)program
                parts:(int)parts compilers:(NSString*)compilers run:(BOOL)andRun {
    if (!outcome.ran) {
        [self endWork:Str(ride_project_target_why(project_)) ok:NO];
        return;
    }
    [self append:StrLossy(outcome.output) to:output_];
    // A diagnostic with no file of its own - a linker's - belongs to no file (M3).
    [self collectIssues:outcome source:nil];
    for (int i = 0; i < parts; ++i) [self advanceWork:[NSString stringWithFormat:@"part %d done", i + 1]];

    if (outcome.hasError) {
        [self endWork:[NSString stringWithFormat:@"%lu issue(s) - %@:%d:%d: error: %@",
                                                 (unsigned long)issues_.count,
                                                 StrLossy(outcome.errorFile).lastPathComponent,
                                                 outcome.errorLine, outcome.errorColumn,
                                                 StrLossy(outcome.errorMessage)]
                   ok:NO];
        [self showPanel:kPanelErrors];
        if (issues_.count > 0) {
            [issueTable_ selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
            [self goToIssue:issues_.firstObject];
        }
        return;
    }
    if (!outcome.ok) {
        [self endWork:outcome.stopped ? @"the build was stopped"
                                      : [compilers stringByAppendingString:@" did not build it - see Output"]
                   ok:NO];
        [self showPanel:issues_.count > 0 ? kPanelErrors : kPanelOutput];
        return;
    }
    [self advanceWork:[@"linked " stringByAppendingString:program.lastPathComponent]];
    NSString* succeeded = [NSString stringWithFormat:@"Build succeeded%@: %@ - %d %@, 0 errors",
                                                     issues_.count > 0 ? @" with warnings" : @"",
                                                     program.lastPathComponent.stringByDeletingPathExtension,
                                                     ride_project_target_sources(project_),
                                                     ride_project_target_sources(project_) == 1 ? @"source" : @"sources"];
    [self append:[NSString stringWithFormat:@"\n========== %@ ==========\n", succeeded] to:output_];
    if (!andRun) {
        [self append:[NSString stringWithFormat:@"%@\n", outcome.made.empty() ? program : Str(outcome.made.c_str())]
                  to:output_];
        [self endWork:succeeded ok:YES];
        [self showPanel:issues_.count > 0 ? kPanelErrors : kPanelOutput];
        return;
    }
    [self showPanel:kPanelOutput];
    // What the build made is what runs - <program>.vm for a C6747 project - and never the target's name.
    NSString* made = outcome.made.empty() ? program : Str(outcome.made.c_str());
    // The command run, arguments and all: a tool handed none prints its usage, and that should not read as a fault.
    NSString* args = [self currentArgs];
    std::string argsLine = StdString(args), argsBase = StdString([self argsBase]);
    [self append:[NSString stringWithFormat:@"\n%@\n", Take(ride_run_line(project_, Utf8(made), argsLine.c_str()))]
              to:output_];
    [self startRun:ride_run_made_start_line(Utf8(made), outcome.madeShalimar ? 1 : 0, argsLine.c_str(), argsBase.c_str(),
                                            RunOutput, (__bridge void*)self)
            source:nil compiler:compilers program:made];
}

// ---- Edit > Find in Files --------------------------------------------------------------

- (void)findInFiles:(id)sender {
    if (finding_) { [self say:@"a search is running - Command-. ends it"]; return; }
    (void)sender;
    static NSString* types = @"*.c;*.cpp;*.h;*.hpp;*.shl;*.s";
    static BOOL matchCase = NO, wholeWord = NO, subfolders = YES;
    NSString* lookFor = @"";
    if (current_ != nil && code_.selectedRange.length > 0) {
        NSString* picked = [code_.string substringWithRange:code_.selectedRange];
        if ([picked rangeOfString:@"\n"].location == NSNotFound) lookFor = picked;
    }
    NSString* where = ride_project_loaded(project_) != 0 && projectDirectory_.length > 0 ? projectDirectory_
                      : current_.path.length > 0 ? current_.path.stringByDeletingLastPathComponent
                      : NSHomeDirectory();

    NSView* form = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 440, 150)];
    NSTextField* (^field)(NSString*, CGFloat, NSString*) = ^NSTextField*(NSString* label, CGFloat y, NSString* value) {
        NSTextField* l = [NSTextField labelWithString:label];
        l.frame = NSMakeRect(0, y + 3, 80, 17);
        [form addSubview:l];
        NSTextField* box = [[NSTextField alloc] initWithFrame:NSMakeRect(84, y, 356, 22)];
        box.stringValue = value ?: @"";
        [form addSubview:box];
        return box;
    };
    NSButton* (^check)(NSString*, CGFloat, CGFloat, BOOL) = ^NSButton*(NSString* label, CGFloat x, CGFloat y, BOOL on) {
        NSButton* b = [NSButton checkboxWithTitle:label target:nil action:nil];
        b.frame = NSMakeRect(x, y, 200, 18);
        b.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        [form addSubview:b];
        return b;
    };
    NSTextField* text = field(@"Find what:", 126, lookFor);
    NSTextField* folder = field(@"Look in:", 98, where);
    NSTextField* kinds = field(@"File types:", 70, types);
    NSButton* caseBox = check(@"Match case", 84, 44, matchCase);
    NSButton* wordBox = check(@"Whole word", 200, 44, wholeWord);
    NSButton* subBox = check(@"Include subfolders", 316, 44, subfolders);
    NSButton* namesBox = check(@"Find files by name - not what is in them", 84, 18, NO);
    namesBox.frame = NSMakeRect(84, 18, 340, 18);

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Find in Files";
    alert.informativeText = @"A text in every file under a folder, or the files whose names hold it.";
    [alert addButtonWithTitle:@"Find"];
    [alert addButtonWithTitle:@"Cancel"];
    alert.accessoryView = form;
    alert.window.initialFirstResponder = text;
    if ([alert runModal] != NSAlertFirstButtonReturn) { [self say:@"nothing searched"]; return; }
    types = kinds.stringValue;
    matchCase = caseBox.state == NSControlStateValueOn;
    wholeWord = wordBox.state == NSControlStateValueOn;
    subfolders = subBox.state == NSControlStateValueOn;
    const BOOL names = namesBox.state == NSControlStateValueOn;
    if (text.stringValue.length == 0 && !names) { [self say:@"nothing to find"]; return; }

    // Off the main thread, so a search of a large tree leaves the window live and Stop ends it.
    NSString* lookingFor = [text.stringValue copy];
    NSString* lookIn = [folder.stringValue copy];
    NSString* kindsNow = [types copy];
    const BOOL byCase = matchCase, byWord = wholeWord, below = subfolders;
    finding_ = YES;
    [self say:@"searching ... Command-. ends it"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString* found = Take(ride_find_in_files(Utf8(lookingFor), Utf8(lookIn), Utf8(kindsNow),
                                                  byCase, byWord, below, names));
        dispatch_async(dispatch_get_main_queue(), ^{
            self->finding_ = NO;
            [self showFound:found byName:names];
        });
    });
}

- (void)showFound:(NSString*)found byName:(BOOL)names {
    NSArray<NSString*>* lines = [found componentsSeparatedByString:@"\n"];
    [issues_ removeAllObjects];
    NSRegularExpression* hit = [NSRegularExpression regularExpressionWithPattern:@"^(.*):(\\d+):(\\d+): (.*)$"
                                                                         options:0 error:nil];
    for (NSUInteger i = 1; i < lines.count; ++i) {
        NSString* line = lines[i];
        if (line.length == 0) continue;
        Issue* issue = [[Issue alloc] init];
        issue.found = YES;
        NSTextCheckingResult* m = names ? nil : [hit firstMatchInString:line options:0 range:NSMakeRange(0, line.length)];
        if (m != nil) {
            issue.file = [line substringWithRange:[m rangeAtIndex:1]];
            issue.line = [[line substringWithRange:[m rangeAtIndex:2]] integerValue];
            issue.column = [[line substringWithRange:[m rangeAtIndex:3]] integerValue];
            issue.message = [[line substringWithRange:[m rangeAtIndex:4]]
                stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        } else {
            issue.file = line;
            issue.message = line.lastPathComponent;
        }
        [issues_ addObject:issue];
    }
    [issueTable_ reloadData];
    [self labelErrorsTab];
    [self append:[found stringByAppendingString:@"\n"] to:output_];
    [self showPanel:kPanelErrors];
    [self say:[lines.firstObject stringByAppendingString:@" - click a row to go to it"]];
}

// ---- Build > Command-line arguments ------------------------------------------------

- (NSString*)currentArgs {
    if (ride_project_loaded(project_) != 0) return Take(ride_project_arguments(project_));
    return fileArgs_ ?: @"";
}

- (NSString*)argsBase {
    if (ride_project_loaded(project_) != 0 && projectDirectory_.length > 0) return projectDirectory_;
    return current_.path.stringByDeletingLastPathComponent ?: @"";
}

- (void)commitArgs {
    NSString* typed = [argsField_.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    if ([typed isEqualToString:[self currentArgs]]) return;
    NSString* said = [@"command-line arguments: " stringByAppendingString:typed.length > 0 ? typed : @"none"];
    if (ride_project_loaded(project_) != 0) {
        if (ride_project_set_arguments(project_, Utf8(typed)) == 0) said = @"the command-line arguments could not be kept with the project";
    } else {
        fileArgs_ = typed;
    }
    [self say:said];
}

- (void)argsEntered:(id)sender {
    (void)sender;
    [self commitArgs];
    [buildMenu_ cancelTracking];
    [buildMenu_.supermenu cancelTracking];
}

- (void)menuWillOpen:(NSMenu*)menu {
    if (menu == buildMenu_) argsField_.stringValue = [self currentArgs];
}

- (void)menuDidClose:(NSMenu*)menu {
    if (menu == buildMenu_) [self commitArgs];
}

- (void)convertFile:(id)sender {
    (void)sender;
    if (![self mayStartWork]) return;
    if (current_ == nil) { [self say:@"no file is open"]; return; }
    int toShalimar = 0;
    if (!ride_converts_from([self languageNow], &toShalimar)) {
        [self say:@"c2s converts between C and Shalimar - set Target > Language if that is wrong"];
        return;
    }
    if (![self saveSheet:current_]) return;

    NSString* converter = Take(ride_find_converter());
    if (converter.length == 0) {
        [self say:@"no c2s beside this editor - build Converter-C2S here, or set C2S"];
        return;
    }
    NSString* path = current_.path;
    NSString* produced = Take(ride_converted_name(Utf8(path), toShalimar));
    if (produced.length == 0 || [produced isEqualToString:path]) {
        [self say:@"that would write over the file it is reading"];
        return;
    }

    std::string where = StdString(converter), source = StdString(path), into = StdString(produced);
    [self clearIssues];
    [self setOutput:[NSString stringWithFormat:@"$ c2s %@ %@\n", toShalimar ? @"--to-shalimar" : @"--to-c", produced]];
    [self beginWork:[NSString stringWithFormat:@"Converting %@ to %@", path.lastPathComponent,
                                               toShalimar ? @"Shalimar" : @"C"]
              steps:1];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        Outcome outcome;
        RIDEConversion* made = ride_convert(where.c_str(), source.c_str(), into.c_str(), toShalimar);
        outcome.ran = ride_conversion_ran(made) != 0;
        outcome.ok = ride_conversion_ok(made) != 0;
        outcome.output = ride_conversion_output(made);
        outcome.produced = ride_conversion_produced(made);
        outcome.stopped = ride_conversion_stopped(made) != 0;
        ride_conversion_free(made);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self append:StrLossy(outcome.output) to:self->output_];
            if (outcome.stopped) {
                [self endWork:@"the conversion was stopped" ok:NO];
                return;
            }
            if (!outcome.ran) {
                [self endWork:[@"could not run " stringByAppendingString:converter] ok:NO];
                return;
            }
            NSString* written = Str(outcome.produced.c_str());
            if (written.length == 0) {
                [self endWork:@"nothing was written - c2s could not read or write a file" ok:NO];
                return;
            }
            [self advanceWork:[@"wrote " stringByAppendingString:written]];
            [self endWork:outcome.ok ? [written.lastPathComponent stringByAppendingString:@" - converted"]
                                     : [written.lastPathComponent stringByAppendingString:
                                           @" - written with unconverted parts marked; search for BEYOND"]
                       ok:outcome.ok];
            [self openPath:written];
        });
    });
}

// ---- Edit -----------------------------------------------------------------------

- (void)reindent:(id)sender {
    (void)sender;
    if (current_ == nil) { [self say:@"no file is open"]; return; }
    [code_ reindentSelectionOrAll];
    [self say:code_.selectedRange.length > 0 ? @"the selection laid out" : @"the file laid out"];
}

- (void)goToLine:(id)sender {
    (void)sender;
    if (current_ == nil) { [self say:@"no file is open"]; return; }
    NSString* said = [self ask:@"Go to line" detail:@"A line, or line:column" value:@""];
    if (said.length == 0) return;
    NSArray<NSString*>* parts = [said componentsSeparatedByString:@":"];
    NSInteger line = parts[0].integerValue;
    NSInteger column = parts.count > 1 ? parts[1].integerValue : 1;
    if (line <= 0) { [self say:@"not a line number"]; return; }
    [code_ hideFindBar];
    [code_ goToLine:line column:column];
    [self.window makeFirstResponder:code_];
}

// Whether the keyboard is in the find bar, open over the text: then Escape closes it.
- (BOOL)escapeFromFindBar {
    NSScrollView* scroll = code_.enclosingScrollView;
    if (!scroll.findBarVisible) return NO;
    NSResponder* first = self.window.firstResponder;
    NSView* inside = [first isKindOfClass:NSTextView.class] && ((NSTextView*)first).isFieldEditor
                         ? (NSView*)((NSTextView*)first).delegate
                         : ([first isKindOfClass:NSView.class] ? (NSView*)first : nil);
    if (inside == nil || ![inside isDescendantOf:scroll.findBarView]) return NO;
    [code_ hideFindBar];
    [self.window makeFirstResponder:code_];
    return YES;
}

// The rows the selection touches, handed to `change` without their '\n's and put back as one
// undoable edit, selected - what Shift and Comment share (L15). Rows are the core's (M2).
- (void)rewriteSelectedRows:(NSArray<NSString*>* (^)(NSArray<NSString*>* rows))change {
    if (current_ == nil) return;
    NSRange lines = [code_ rowsOfRange:code_.selectedRange];
    NSString* block = [code_.string substringWithRange:lines];
    BOOL endsWithNewline = [block hasSuffix:@"\n"];
    if (endsWithNewline) block = [block substringToIndex:block.length - 1];
    NSString* changed = [change([block componentsSeparatedByString:@"\n"]) componentsJoinedByString:@"\n"];
    if (endsWithNewline) changed = [changed stringByAppendingString:@"\n"];
    [code_ replaceRange:lines with:changed];
    code_.selectedRange = NSMakeRange(lines.location, changed.length);
}

// Shift the selected lines a step left or right, as the project spells one.
- (void)shiftBy:(int)direction {
    NSString* unit = indentTabs_ ? @"\t" : [@"" stringByPaddingToLength:(NSUInteger)[self indentWidth]
                                                            withString:@" " startingAtIndex:0];
    [self rewriteSelectedRows:^NSArray<NSString*>*(NSArray<NSString*>* rows) {
        NSMutableArray<NSString*>* out = [NSMutableArray array];
        for (NSString* line in rows) {
            if (direction > 0) {
                [out addObject:line.length > 0 ? [unit stringByAppendingString:line] : line];
            } else {
                NSUInteger cut = 0;
                if ([line hasPrefix:@"\t"]) cut = 1;
                else while (cut < unit.length && cut < line.length && [line characterAtIndex:cut] == ' ') ++cut;
                [out addObject:[line substringFromIndex:cut]];
            }
        }
        return out;
    }];
}
- (void)shiftLeft:(id)sender { (void)sender; [self shiftBy:-1]; }
- (void)shiftRight:(id)sender { (void)sender; [self shiftBy:1]; }

// "//" on or off for the selected lines; Shalimar and C90 are both written
// with it here, as the syntax colouring reads them.
- (void)toggleComment:(id)sender {
    (void)sender;
    [self rewriteSelectedRows:^NSArray<NSString*>*(NSArray<NSString*>* rows) {
        BOOL allCommented = YES;
        for (NSString* row in rows) {
            NSString* trimmed = [row stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
            if (trimmed.length > 0 && ![trimmed hasPrefix:@"//"]) { allCommented = NO; break; }
        }
        NSMutableArray<NSString*>* out = [NSMutableArray array];
        for (NSString* row in rows) {
            if (allCommented) {
                NSRange mark = [row rangeOfString:@"//"];
                if (mark.location == NSNotFound) { [out addObject:row]; continue; }
                NSUInteger end = NSMaxRange(mark);
                if (end < row.length && [row characterAtIndex:end] == ' ') ++end;
                [out addObject:[[row substringToIndex:mark.location] stringByAppendingString:[row substringFromIndex:end]]];
            } else {
                [out addObject:row.length > 0 ? [@"// " stringByAppendingString:row] : row];
            }
        }
        return out;
    }];
}

// ---- View -----------------------------------------------------------------------

- (void)toggleNavigator:(id)sender {
    (void)sender;
    navigatorPane_.hidden = !navigatorPane_.hidden;
    [across_ adjustSubviews];
    if (!navigatorPane_.hidden) [across_ setPosition:240 ofDividerAtIndex:0];
}

- (void)togglePanel:(id)sender {
    (void)sender;
    if (folded_) { fold_.state = NSControlStateValueOff; [self foldPanel:nil]; }
    if (!panelPane_.hidden) panelHeight_ = NSHeight(panelPane_.frame);
    panelPane_.hidden = !panelPane_.hidden;
    [down_ adjustSubviews];
    if (!panelPane_.hidden) {
        CGFloat height = panelHeight_ > 60 ? panelHeight_ : floor(NSHeight(down_.bounds) / 4);
        [down_ setPosition:NSHeight(down_.bounds) - height ofDividerAtIndex:0];
    }
}

- (void)toggleLineNumbers:(id)sender {
    (void)sender;
    numbers_ = !numbers_;
    codeScroll_.rulersVisible = numbers_;
}

- (void)useFont:(NSFont*)font {
    if (font == nil) return;
    codeFont_ = font;
    NSDictionary* attributes = [self codeAttributes];
    for (Sheet* sheet in sheets_)
        [sheet.storage setAttributes:attributes range:NSMakeRange(0, sheet.storage.length)];
    [blank_.storage setAttributes:attributes range:NSMakeRange(0, blank_.storage.length)];
    code_.font = font;
    code_.typingAttributes = attributes;
    [code_ breakUndoCoalescing];
    [gutter_ textDidChange];
    [self recolourAll];
    ride_remember_code_font(Utf8([NSString stringWithFormat:@"%@ %g", font.fontName, font.pointSize]));
    [self say:[NSString stringWithFormat:@"font: %@ %g", font.displayName, font.pointSize]];
}

- (void)biggerFont:(id)sender {
    (void)sender;
    [self useFont:[NSFontManager.sharedFontManager convertFont:codeFont_ toSize:MIN(72, codeFont_.pointSize + 1)]];
}
- (void)smallerFont:(id)sender {
    (void)sender;
    [self useFont:[NSFontManager.sharedFontManager convertFont:codeFont_ toSize:MAX(8, codeFont_.pointSize - 1)]];
}
- (void)actualFontSize:(id)sender {
    (void)sender;
    [self useFont:[NSFontManager.sharedFontManager convertFont:codeFont_ toSize:13]];
}

// ---- Target ---------------------------------------------------------------------

- (NSString*)writtenToProject:(int)outcome {
    if (outcome != 0) return [@" - written to " stringByAppendingString:[self outcomePath].lastPathComponent];
    return [@" - but " stringByAppendingString:Str(ride_outcome_message(project_))];
}

- (void)chooseArch:(NSMenuItem*)sender { [self useArch:(int)(sender.tag - kTagArchBase)]; }

- (void)useArch:(int)index {
    arch_ = Str(ride_arch(index));
    NSString* said = [@"target: " stringByAppendingString:arch_];
    if (ride_project_loaded(project_))
        said = [said stringByAppendingString:[self writtenToProject:ride_project_set_arch(project_, Utf8(arch_))]];
    [self sayBuild];
    [self say:said];
}

- (void)chooseTool:(NSMenuItem*)sender { [self useTool:(int)(sender.tag - kTagToolBase)]; }

- (void)useTool:(int)kind {
    toolKind_ = kind;
    NSString* said = toolKind_ == RIDE_TOOL_AUTO
                         ? @"compiler: chosen by the file"
                         : [@"compiler: " stringByAppendingString:Str(ride_toolchain_name(toolKind_))];
    if (ride_project_loaded(project_))
        said = [said stringByAppendingString:[self writtenToProject:ride_project_set_toolchain(project_, toolKind_)]];
    else
        ride_remember_default_compiler(toolKind_);
    [self sayBuild];
    [self say:said];
}

- (void)chooseLanguage:(NSMenuItem*)sender {
    if (current_ == nil) { [self say:@"no file is open"]; return; }
    current_.language = sender.tag == kTagLangAuto ? -1 : (int)(sender.tag - kTagLangBase);
    [self sayBuild];
    [self recolourAll];
    [self say:[@"language: " stringByAppendingString:current_.language < 0
                                                         ? @"chosen by the name"
                                                         : Str(ride_language_name(current_.language))]];
}

- (void)chooseConfig:(NSMenuItem*)sender {
    config_ = (int)(sender.tag - kTagConfigBase);
    ride_remember_configuration(config_);
    ride_project_remember_configuration(project_, config_);
    [self sayBuild];
    [self say:Str(ride_config_name(config_))];
}

- (NSArray<NSNumber*>*)toolKinds {
    return @[ @(RIDE_TOOL_AUTO), @(RIDE_TOOL_CC1), @(RIDE_TOOL_CXX1), @(RIDE_TOOL_SHC), @(RIDE_TOOL_CXX) ];
}

- (void)nextCompiler:(id)sender {
    (void)sender;
    NSArray<NSNumber*>* kinds = [self toolKinds];
    NSUInteger at = [kinds indexOfObject:@(toolKind_)];
    NSUInteger next = at == NSNotFound ? 0 : (at + 1) % kinds.count;
    [self useTool:kinds[next].intValue];
}

- (void)nextTarget:(id)sender {
    (void)sender;
    int count = ride_arch_count();
    if (count == 0) return;
    int at = 0;
    for (int i = 0; i < count; ++i)
        if ([Str(ride_arch(i)) isEqualToString:arch_]) { at = i; break; }
    [self useArch:(at + 1) % count];
}

// ---- Option ---------------------------------------------------------------------

- (void)chooseFont:(id)sender {
    (void)sender;
    NSFontManager* fonts = NSFontManager.sharedFontManager;
    fonts.target = self;
    fonts.action = @selector(fontChosen:);
    [fonts setSelectedFont:codeFont_ isMultiple:NO];
    [fonts orderFrontFontPanel:self];
}

- (void)fontChosen:(NSFontManager*)sender {
    [self useFont:[sender convertFont:codeFont_]];
}

- (NSString*)installFileOrSay {
    NSString* file = Str(ride_install_file());
    if (file.length == 0) [self say:@"no installation directory to keep this in"];
    return file;
}

- (void)headerDirectories:(id)sender {
    (void)sender;
    NSString* file = [self installFileOrSay];
    if (file.length == 0) return;
    NSString* include = [self ask:@"cpp11's headers (include)" detail:[@"Kept in " stringByAppendingString:file]
                            value:Str(ride_include_dir())];
    if (include == nil) { [self say:@"header directories unchanged"]; return; }
    NSString* lib = [self ask:@"c90's headers (lib)" detail:[@"Kept in " stringByAppendingString:file]
                        value:Str(ride_lib_dir())];
    if (lib == nil) { [self say:@"header directories unchanged"]; return; }
    [self say:ride_remember_header_dirs(Utf8(include), Utf8(lib))
                  ? [@"header directories written to " stringByAppendingString:file]
                  : [@"cannot write " stringByAppendingString:file]];
}

- (void)sharedIncludes:(id)sender {
    (void)sender;
    NSString* file = [self installFileOrSay];
    if (file.length == 0) return;
    NSString* line = [self ask:@"Shared include paths"
                        detail:[NSString stringWithFormat:@"Kept in %@, ';' between them", file]
                         value:Str(ride_includes())];
    if (line == nil) { [self say:@"shared include paths unchanged"]; return; }
    [self say:ride_set_includes(Utf8(line)) ? [@"shared include paths written to " stringByAppendingString:file]
                                                : [@"cannot write " stringByAppendingString:file]];
}

- (void)sharedLibraries:(id)sender {
    (void)sender;
    NSString* file = [self installFileOrSay];
    if (file.length == 0) return;
    NSString* line = [self ask:@"Shared libraries"
                        detail:[NSString stringWithFormat:@"Kept in %@, ';' between them, linked after the objects", file]
                         value:Str(ride_libraries())];
    if (line == nil) { [self say:@"shared libraries unchanged"]; return; }
    [self say:ride_set_libraries(Utf8(line)) ? [@"shared libraries written to " stringByAppendingString:file]
                                                 : [@"cannot write " stringByAppendingString:file]];
}

// A program picked in Finder, handed to one of the core's remember_ functions.
- (void)pickProgram:(NSString*)title now:(NSString*)now remember:(int (*)(const char*))remember
               said:(NSString*)said {
    NSString* file = [self installFileOrSay];
    if (file.length == 0) return;
    NSOpenPanel* pick = [NSOpenPanel openPanel];
    pick.message = title;
    if (now.length > 0) pick.directoryURL = [NSURL fileURLWithPath:now.stringByDeletingLastPathComponent];
    if ([pick runModal] != NSModalResponseOK) { [self say:@"unchanged"]; return; }
    if (!remember(Utf8(pick.URL.path))) {
        [self say:[@"cannot write " stringByAppendingString:file]];
        return;
    }
    [self say:[NSString stringWithFormat:@"%@ %@ - written to %@", said, pick.URL.path, file]];
}

- (void)locateAssembler:(id)sender {
    (void)sender;
    [self pickProgram:@"The assembler for x86_64-windows (masm.exe)" now:Str(ride_assembler())
             remember:ride_remember_assembler said:@"c90 and cpp11 assemble through"];
}
- (void)locateLinker:(id)sender {
    (void)sender;
    [self pickProgram:@"The linker for x86_64-windows (link.exe)" now:Str(ride_linker())
             remember:ride_remember_linker said:@"a Windows build links through"];
}
- (void)locateTiLinker:(id)sender {
    (void)sender;
    [self pickProgram:@"The linker for tms6747 (lnk6x.exe)" now:Str(ride_tilinker())
             remember:ride_remember_tilinker said:@"a tms6747 build links through"];
}

- (void)locateTi:(id)sender {
    (void)sender;
    NSString* file = [self installFileOrSay];
    if (file.length == 0) return;
    NSOpenPanel* pick = [NSOpenPanel openPanel];
    pick.canChooseFiles = NO;
    pick.canChooseDirectories = YES;
    pick.message = @"TI's C6000 compiler directory - the one with bin/lnk6x";
    NSString* now = Str(ride_ti());
    if (now.length > 0) pick.directoryURL = [NSURL fileURLWithPath:now];
    if ([pick runModal] != NSModalResponseOK) { [self say:@"TI compiler unchanged"]; return; }
    NSString* dir = pick.URL.path;
    NSOpenPanel* lib = [NSOpenPanel openPanel];
    lib.canChooseFiles = NO;
    lib.canChooseDirectories = YES;
    lib.message = @"A directory with rts6740_elf_eh.lib, the exception-handling runtime (Cancel for none)";
    NSString* libDir = [lib runModal] == NSModalResponseOK ? lib.URL.path : @"";
    [self say:ride_remember_ti(Utf8(dir), Utf8(libDir))
                  ? [NSString stringWithFormat:@"a tms6747 build links a .out with %@ - written to %@", dir, file]
                  : [@"cannot write " stringByAppendingString:file]];
}

- (void)showCompilers:(id)sender {
    (void)sender;
    NSString* text = [NSString stringWithFormat:
        @"c90       %@\ncpp11     %@\nshalimar  %@\nc2s       %@\n\nsettings  %@\nheaders   include %@, lib %@\n"
        @"assembler %@\nlinker    %@\nlnk6x     %@\nTI        %@\n",
        cc1_, cxx1_, shc_, Take(ride_find_converter()), Str(ride_install_file()),
        Str(ride_include_dir()), Str(ride_lib_dir()), Str(ride_assembler()),
        Str(ride_linker()), Str(ride_tilinker()), Str(ride_ti())];
    [self showInOutput:text];
    [self showPanel:kPanelOutput];
    [self say:@"the tools this window drives - in Output"];
}

// ---- Help -----------------------------------------------------------------------

- (NSString*)helpDirectory {
    NSString* bundled = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"help"];
    if ([NSFileManager.defaultManager fileExistsAtPath:bundled]) return bundled;
    // Run from a checkout: RIDE/help beside RIDE/macos.
    NSString* up = NSBundle.mainBundle.bundlePath.stringByDeletingLastPathComponent;
    for (int i = 0; i < 4 && up.length > 1; ++i) {
        NSString* candidate = [up stringByAppendingPathComponent:@"help"];
        if ([NSFileManager.defaultManager fileExistsAtPath:[candidate stringByAppendingPathComponent:@"README.md"]])
            return candidate;
        up = up.stringByDeletingLastPathComponent;
    }
    return nil;
}

- (void)openHelpPage:(NSString*)page {
    NSString* dir = [self helpDirectory];
    NSString* file = [dir stringByAppendingPathComponent:page];
    if (dir == nil || ![NSFileManager.defaultManager fileExistsAtPath:file]) {
        [self say:[@"the manual is not beside this editor: " stringByAppendingString:page]];
        return;
    }
    if ([page hasSuffix:@".html"]) [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:file]];
    else [self openPath:file];
}

- (void)showHelp:(id)sender { (void)sender; [self openHelpPage:@"manual.html"]; }
- (void)showShalimarReference:(id)sender { (void)sender; [self openHelpPage:@"shalimar-language.html"]; }
- (void)showResources:(id)sender { (void)sender; [self openHelpPage:@"resources.html"]; }
- (void)showKeys:(id)sender { (void)sender; [self openHelpPage:@"keys.html"]; }
// Help > Environment in the Output tab, whose font is fixed-width: each tool and the file it resolved to.
- (void)showEnvironment:(id)sender {
    (void)sender;
    [self showInOutput:Take(ride_environment())];
    [self showPanel:kPanelOutput];
}

- (void)showAbout:(id)sender {
    (void)sender;
    // The standard panel at its own size: the credits stop before the stamp - the release, the time,
    // the CRC and what the release record says - which takes the panel's own line under them.
    NSString* about = Take(ride_about_credits());
    NSString* stamp = Take(ride_about_stamp());
    NSDictionary* options = @{
        NSAboutPanelOptionApplicationName : Str(ride_product_name()),
        NSAboutPanelOptionApplicationVersion : Str(ride_version()),
        NSAboutPanelOptionVersion : @"",
        NSAboutPanelOptionCredits : [[NSAttributedString alloc]
            initWithString:about
                attributes:@{NSFontAttributeName : [NSFont systemFontOfSize:[NSFont smallSystemFontSize]],
                             NSForegroundColorAttributeName : [NSColor labelColor]}],
        @"Copyright" : stamp.length > 0 ? stamp : @"RIDE 5.17 - G. R. Akhtar",
    };
    [NSApp orderFrontStandardAboutPanelWithOptions:options];
}

// ---- menu state -----------------------------------------------------------------

// One of the open project's files in front, or no file and a project open: the project's. A loose file -
// File > New, Open or Recent - or no project at all: the file's.
- (BOOL)projectMode {
    if (ride_project_loaded(project_) == 0) return NO;
    if (current_ == nil) return YES;
    return current_.path != nil && ride_project_holds(project_, Utf8(current_.path)) != 0;
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    SEL action = item.action;
    if (action == @selector(openHeaderItem:)) return item.representedObject != nil;
    if (action == @selector(cleanBuild:)) return !busy_;
    BOOL project = ride_project_loaded(project_) != 0;
    BOOL file = current_ != nil;
    // A build reads the project and settings.json on its own thread; a program running after it does not.
    BOOL building = busy_ && running_ == NULL;
    // A CCS project is CCS's to change: RIDE reads it again at every build, so an edit made here is
    // reported done and then lost. What it would change is offered disabled.
    BOOL ccs = project && ride_project_is_ccs(project_) != 0;

    if (ccs && (action == @selector(chooseArch:) || action == @selector(nextTarget:) ||
                action == @selector(chooseTool:) || action == @selector(nextCompiler:) ||
                action == @selector(addFiles:) || action == @selector(addCurrentFile:) ||
                action == @selector(removeFromProject:) || action == @selector(moveToGroup:) ||
                action == @selector(projectIncludes:) || action == @selector(projectLibraries:) ||
                action == @selector(newProjectFile:) || action == @selector(saveProjectAs:))) {
        if (action == @selector(chooseArch:))
            item.state = [Str(ride_arch((int)(item.tag - kTagArchBase))) isEqualToString:arch_]
                             ? NSControlStateValueOn : NSControlStateValueOff;
        if (action == @selector(chooseTool:))
            item.state = item.tag - kTagToolBase == toolKind_ ? NSControlStateValueOn : NSControlStateValueOff;
        return NO;
    }
    if (action == @selector(headerDirectories:) || action == @selector(sharedIncludes:) ||
        action == @selector(sharedLibraries:) || action == @selector(locateAssembler:) ||
        action == @selector(locateLinker:) || action == @selector(locateTi:) ||
        action == @selector(locateTiLinker:))
        return !building;

    if (action == @selector(chooseArch:)) {
        item.state = [Str(ride_arch((int)(item.tag - kTagArchBase))) isEqualToString:arch_]
                         ? NSControlStateValueOn : NSControlStateValueOff;
        return !busy_;
    }
    if (action == @selector(chooseTool:)) {
        item.state = item.tag - kTagToolBase == toolKind_ ? NSControlStateValueOn : NSControlStateValueOff;
        return !busy_;
    }
    if (action == @selector(chooseLanguage:)) {
        int chosen = current_ != nil ? current_.language : -1;
        BOOL on = item.tag == kTagLangAuto ? chosen < 0 : chosen == (int)(item.tag - kTagLangBase);
        item.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        return file;
    }
    if (action == @selector(chooseConfig:)) {
        item.state = item.tag - kTagConfigBase == config_ ? NSControlStateValueOn : NSControlStateValueOff;
        return !busy_;
    }
    if (action == @selector(toggleNavigator:)) {
        item.title = navigatorPane_.hidden ? @"Show Navigator" : @"Hide Navigator";
        return YES;
    }
    if (action == @selector(togglePanel:)) {
        item.title = panelPane_.hidden ? @"Show Bottom Panel" : @"Hide Bottom Panel";
        return YES;
    }
    if (action == @selector(toggleLineNumbers:)) {
        item.state = numbers_ ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }
    if (action == @selector(showPanelTab:)) {
        item.state = !panelPane_.hidden && panel_.selectedTabViewItem ==
                                               [panel_ tabViewItemAtIndex:item.tag - kTagPanelBase]
                         ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }
    // File or project, as the file in front decides (projectMode): a file's commands for a loose file, the
    // project's for one of its files. The navigator's own menu is always about the project file clicked.
    BOOL mode = [self projectMode];
    BOOL onNavigator = navigator_.clickedRow >= 0 || self.window.firstResponder == navigator_;
    if (action == @selector(compileFile:) || action == @selector(runFile:)) return file && !busy_ && !mode;
    if (action == @selector(convertFile:)) return file && !busy_;
    if (action == @selector(buildProjectAction:) || action == @selector(runProjectAction:)) return mode && !busy_;
    if (action == @selector(emulateRun:) || action == @selector(verifyRun:))
        return (mode || file) && !busy_ && [arch_ isEqualToString:@"tms6747"];
    if (action == @selector(nextCompiler:) || action == @selector(nextTarget:))
        return !busy_;
    if (action == @selector(saveDocument:) || action == @selector(saveDocumentAs:) ||
        action == @selector(reindent:) || action == @selector(goToLine:) ||
        action == @selector(shiftLeft:) || action == @selector(shiftRight:) ||
        action == @selector(toggleComment:))
        return file;
    if (action == @selector(revertDocumentToSaved:)) return file && current_.path != nil && current_.modified;
    if (action == @selector(saveAll:)) return [self anyModified];
    if (action == @selector(nextFile:) || action == @selector(previousFile:)) return sheets_.count > 1;
    if (action == @selector(saveProjectAs:) || action == @selector(closeProject:) ||
        action == @selector(addFiles:) || action == @selector(projectIncludes:) ||
        action == @selector(projectLibraries:))
        return project && !busy_;
    if (action == @selector(compilerOptions:)) return !busy_;
    // Nothing that loads, replaces or changes the project while a build reads it (H1).
    if (action == @selector(addCurrentFile:)) return project && file && current_.path != nil && !busy_ &&
                                                    !ride_project_holds(project_, Utf8(current_.path));
    if (action == @selector(removeFromProject:) || action == @selector(moveToGroup:)) {
        NSString* target = [self targetFile];
        return project && (mode || onNavigator) && target != nil && !busy_ && ride_project_holds(project_, Utf8(target));
    }
    // A file opens while a program runs - it may be waiting on input for minutes; a project does not.
    if (action == @selector(openDocument:) || action == @selector(openRecentFile:)) return !building;
    if (action == @selector(openRecentProject:)) return !busy_;
    if (action == @selector(stopWork:)) return busy_ || running_ != NULL || finding_;
    // The Project menu's file items act on the open project's files, so each wants a project.
    if (action == @selector(renameFile:) || action == @selector(deleteFile:)) {
        NSString* target = [self targetFile];
        return project && (mode || onNavigator) && target != nil && !busy_ && ride_project_holds(project_, Utf8(target));
    }
    if (action == @selector(nextIssue:) || action == @selector(clearIssuesAction:)) return issues_.count > 0;
    if (action == @selector(newProjectFile:)) return project && (mode || onNavigator) && !busy_;
    if (action == @selector(newProject:) || action == @selector(openProject:))
        return !busy_;
    return YES;
}

// The two recent lists are filled as their menus open.
- (void)menuNeedsUpdate:(NSMenu*)menu {
    if (menu != recentProjectsMenu_ && menu != recentFilesMenu_) return;  // the recent lists only
    BOOL projects = menu == recentProjectsMenu_;
    [menu removeAllItems];
    for (int i = 0; i < 8; ++i) {
        NSString* where = Str(projects ? ride_recent_project(i) : ride_recent_file(i));
        if (where.length == 0) break;
        if (![NSFileManager.defaultManager fileExistsAtPath:where]) continue;
        // The name and the two folders it is in: two projects' main.c - or two copies of one project,
        // each in a folder of the same name - read the same by name alone.
        NSString* dir = where.stringByDeletingLastPathComponent;
        NSString* up = dir.stringByDeletingLastPathComponent.lastPathComponent;
        NSString* folder = up.length > 0 && ![up isEqualToString:@"/"]
                               ? [up stringByAppendingPathComponent:dir.lastPathComponent]
                               : dir.lastPathComponent;
        NSString* title = folder.length > 0
                              ? [NSString stringWithFormat:@"%@  \u2014  %@", where.lastPathComponent, folder]
                              : where.lastPathComponent;
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:title
                   action:projects ? @selector(openRecentProject:) : @selector(openRecentFile:)
            keyEquivalent:@""];
        item.target = self;
        item.representedObject = where;
        item.toolTip = where;
        [menu addItem:item];
    }
    if (menu.numberOfItems == 0) {
        NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"None" action:NULL keyEquivalent:@""];
        none.enabled = NO;
        [menu addItem:none];
    }
}

// ---- the main menu --------------------------------------------------------------

- (NSMenuItem*)add:(NSString*)title to:(NSMenu*)menu action:(SEL)action
               key:(NSString*)key mods:(NSEventModifierFlags)mods {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key ?: @""];
    item.keyEquivalentModifierMask = mods;
    // The window's own actions go straight to it; the standard ones (Cut,
    // Undo, Find, Minimize...) travel the responder chain to whoever has them.
    if (action != NULL && [self respondsToSelector:action]) item.target = self;
    [menu addItem:item];
    return item;
}

- (NSMenuItem*)add:(NSString*)title to:(NSMenu*)menu action:(SEL)action key:(NSString*)key {
    return [self add:title to:menu action:action key:key mods:NSEventModifierFlagCommand];
}

- (NSMenu*)submenu:(NSString*)title of:(NSMenu*)parent {
    NSMenuItem* holder = [[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""];
    NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
    holder.submenu = menu;
    [parent addItem:holder];
    return menu;
}

static NSString* Key(unichar c) { return [NSString stringWithCharacters:&c length:1]; }

- (NSMenu*)makeMainMenu {
    const NSEventModifierFlags cmd = NSEventModifierFlagCommand;
    const NSEventModifierFlags shift = NSEventModifierFlagShift;
    const NSEventModifierFlags opt = NSEventModifierFlagOption;
    const NSEventModifierFlags ctrl = NSEventModifierFlagControl;
    NSString* product = Str(ride_product_name());

    NSMenu* bar = [[NSMenu alloc] initWithTitle:@"Main"];

    // The application menu, which macOS puts first and names after the app.
    NSMenu* app = [self submenu:product of:bar];
    [self add:[@"About " stringByAppendingString:product] to:app action:@selector(showAbout:) key:@""];
    [app addItem:[NSMenuItem separatorItem]];
    NSMenu* services = [self submenu:@"Services" of:app];
    NSApp.servicesMenu = services;
    [app addItem:[NSMenuItem separatorItem]];
    [self add:[@"Hide " stringByAppendingString:product] to:app action:@selector(hide:) key:@"h"];
    [self add:@"Hide Others" to:app action:@selector(hideOtherApplications:) key:@"h" mods:cmd | opt];
    [self add:@"Show All" to:app action:@selector(unhideAllApplications:) key:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [self add:[@"Quit " stringByAppendingString:product] to:app action:@selector(terminate:) key:@"q"];

    // File
    NSMenu* file = [self submenu:@"File" of:bar];
    [self add:@"New" to:file action:@selector(newBuffer:) key:@"n"];
    [self add:@"Open..." to:file action:@selector(openDocument:) key:@"o"];
    recentFilesMenu_ = [self submenu:@"Open Recent" of:file];
    recentFilesMenu_.delegate = self;
    [file addItem:[NSMenuItem separatorItem]];
    [self add:@"Close File" to:file action:@selector(closeFile:) key:@"w"];
    [self add:@"Save" to:file action:@selector(saveDocument:) key:@"s"];
    [self add:@"Save As..." to:file action:@selector(saveDocumentAs:) key:@"s" mods:cmd | shift];
    [self add:@"Save All" to:file action:@selector(saveAll:) key:@"s" mods:cmd | opt];
    [self add:@"Revert to Saved" to:file action:@selector(revertDocumentToSaved:) key:@""];
    [file addItem:[NSMenuItem separatorItem]];
    [self add:@"Show in Finder" to:file action:@selector(showInFinder:) key:@"r" mods:cmd | shift | opt];

    // Edit
    NSMenu* edit = [self submenu:@"Edit" of:bar];
    [self add:@"Undo" to:edit action:@selector(undo:) key:@"z"];
    [self add:@"Redo" to:edit action:@selector(redo:) key:@"z" mods:cmd | shift];
    [edit addItem:[NSMenuItem separatorItem]];
    [self add:@"Cut" to:edit action:@selector(cut:) key:@"x"];
    [self add:@"Copy" to:edit action:@selector(copy:) key:@"c"];
    [self add:@"Paste" to:edit action:@selector(paste:) key:@"v"];
    [self add:@"Delete" to:edit action:@selector(delete:) key:@""];
    [self add:@"Select All" to:edit action:@selector(selectAll:) key:@"a"];
    [edit addItem:[NSMenuItem separatorItem]];
    NSMenu* find = [self submenu:@"Find" of:edit];
    [self add:@"Find..." to:find action:@selector(performTextFinderAction:) key:@"f"].tag =
        NSTextFinderActionShowFindInterface;
    [self add:@"Find and Replace..." to:find action:@selector(performTextFinderAction:) key:@"f" mods:cmd | opt].tag =
        NSTextFinderActionShowReplaceInterface;
    [self add:@"Find Next" to:find action:@selector(performTextFinderAction:) key:@"g"].tag =
        NSTextFinderActionNextMatch;
    [self add:@"Find Previous" to:find action:@selector(performTextFinderAction:) key:@"g" mods:cmd | shift].tag =
        NSTextFinderActionPreviousMatch;
    [self add:@"Use Selection for Find" to:find action:@selector(performTextFinderAction:) key:@"e"].tag =
        NSTextFinderActionSetSearchString;
    // After Find: every file under a folder, or the files by name - the hits listed in the panel, a click opening one.
    [self add:@"Find in Files..." to:edit action:@selector(findInFiles:) key:@"f" mods:cmd | shift];
    [self add:@"Go to Line..." to:edit action:@selector(goToLine:) key:@"l"];
    [edit addItem:[NSMenuItem separatorItem]];
    [self add:@"Re-indent" to:edit action:@selector(reindent:) key:@"i" mods:ctrl];
    [self add:@"Shift Left" to:edit action:@selector(shiftLeft:) key:@"["];
    [self add:@"Shift Right" to:edit action:@selector(shiftRight:) key:@"]"];
    [self add:@"Comment Selection" to:edit action:@selector(toggleComment:) key:@"/"];

    // View
    NSMenu* view = [self submenu:@"View" of:bar];
    [self add:@"Hide Navigator" to:view action:@selector(toggleNavigator:) key:@"0"];
    [self add:@"Hide Bottom Panel" to:view action:@selector(togglePanel:) key:@"y" mods:cmd | shift];
    [view addItem:[NSMenuItem separatorItem]];
    NSArray<NSString*>* tabs = @[ @"Errors", @"Progress", @"Output" ];
    for (NSUInteger i = 0; i < tabs.count; ++i)
        [self add:tabs[i] to:view action:@selector(showPanelTab:)
              key:[NSString stringWithFormat:@"%lu", (unsigned long)i + 1] mods:ctrl].tag =
            kTagPanelBase + (NSInteger)i;
    [view addItem:[NSMenuItem separatorItem]];
    [self add:@"Line Numbers" to:view action:@selector(toggleLineNumbers:) key:@""];
    // Command-= is the key without Shift on most keyboards; Command-+ stays, hidden, for the rest (L5).
    [self add:@"Bigger Font" to:view action:@selector(biggerFont:) key:@"="];
    NSMenuItem* plus = [self add:@"Bigger Font" to:view action:@selector(biggerFont:) key:@"+"];
    plus.hidden = YES;
    plus.allowsKeyEquivalentWhenHidden = YES;
    [self add:@"Smaller Font" to:view action:@selector(smallerFont:) key:@"-"];
    [self add:@"Actual Size" to:view action:@selector(actualFontSize:) key:@"0" mods:cmd | ctrl];
    [view addItem:[NSMenuItem separatorItem]];
    [self add:@"Next File" to:view action:@selector(nextFile:) key:Key(NSRightArrowFunctionKey) mods:cmd | ctrl];
    [self add:@"Previous File" to:view action:@selector(previousFile:) key:Key(NSLeftArrowFunctionKey) mods:cmd | ctrl];
    [view addItem:[NSMenuItem separatorItem]];
    [self add:@"Enter Full Screen" to:view action:@selector(toggleFullScreen:) key:@"f" mods:cmd | ctrl];

    // Project
    NSMenu* proj = [self submenu:@"Project" of:bar];
    [self add:@"New..." to:proj action:@selector(newProject:) key:@"n" mods:cmd | shift];
    [self add:@"Open..." to:proj action:@selector(openProject:) key:@"o" mods:cmd | shift];
    recentProjectsMenu_ = [self submenu:@"Recent" of:proj];
    recentProjectsMenu_.delegate = self;
    [self add:@"Save As..." to:proj action:@selector(saveProjectAs:) key:@""];
    [self add:@"Close" to:proj action:@selector(closeProject:) key:@""];
    [proj addItem:[NSMenuItem separatorItem]];
    [self add:@"New File" to:proj action:@selector(newProjectFile:) key:@""];
    [self add:@"Add File" to:proj action:@selector(addCurrentFile:) key:@""];
    [self add:@"Remove" to:proj action:@selector(removeFromProject:) key:@""];
    [self add:@"Rename..." to:proj action:@selector(renameFile:) key:@""];
    [self add:@"Delete..." to:proj action:@selector(deleteFile:) key:@""];
    [proj addItem:[NSMenuItem separatorItem]];
    [self add:@"Include Paths..." to:proj action:@selector(projectIncludes:) key:@""];
    [self add:@"Libraries..." to:proj action:@selector(projectLibraries:) key:@""];

    // Build
    NSMenu* build = [self submenu:@"Build" of:bar];
    [self add:@"Compile File" to:build action:@selector(compileFile:) key:@"b"];
    [self add:@"Run File" to:build action:@selector(runFile:) key:@"r"];
    [build addItem:[NSMenuItem separatorItem]];
    [self add:@"Build Project" to:build action:@selector(buildProjectAction:) key:@"b" mods:cmd | shift];
    [self add:@"Run Project" to:build action:@selector(runProjectAction:) key:@"r" mods:cmd | shift];
    // The command line Run and Run Project hand the program: a submenu, as Recent is, holding a box to type it in.
    {
        NSMenu* argsMenu = [self submenu:@"Command-line arguments" of:build];
        NSView* holder = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 340, 30)];
        argsField_ = [[NSTextField alloc] initWithFrame:NSMakeRect(14, 4, 312, 22)];
        argsField_.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
        argsField_.placeholderString = @"e.g. -run sample.cpp";
        argsField_.toolTip = @"Handed to the program by Run and Run Project; a word in quotes may hold spaces. Return keeps it.";
        argsField_.target = self;
        argsField_.action = @selector(argsEntered:);
        [holder addSubview:argsField_];
        NSMenuItem* argsItem = [[NSMenuItem alloc] initWithTitle:@"arguments" action:nil keyEquivalent:@""];
        argsItem.view = holder;
        [argsMenu addItem:argsItem];
        buildMenu_ = argsMenu;
        argsMenu.delegate = self;
        fileArgs_ = @"";
    }
    // tms6747's linked .out on sim6747, the C6747 simulator, and that run compared with the emulator's (5.0).
    [self add:@"Emulate on vm6747" to:build action:@selector(emulateRun:) key:@""];
    [self add:@"Verify" to:build action:@selector(verifyRun:) key:@""];
    [self add:@"Stop" to:build action:@selector(stopWork:) key:@"."];
    // What a build made, removed, and the panes emptied - said in so many words.
    [self add:@"Clean" to:build action:@selector(cleanBuild:) key:@""];
    [build addItem:[NSMenuItem separatorItem]];
    // Debug or Release, one of the two, in a submenu as Recent is; the check says which is in force.
    NSMenu* configuration = [self submenu:@"Configuration" of:build];
    [self add:@"Debug" to:configuration action:@selector(chooseConfig:) key:@""].tag =
        kTagConfigBase + RIDE_CONFIG_DEBUG;
    [self add:@"Release" to:configuration action:@selector(chooseConfig:) key:@""].tag =
        kTagConfigBase + RIDE_CONFIG_RELEASE;
    [build addItem:[NSMenuItem separatorItem]];
    [self add:@"Convert C ⇄ Shalimar" to:build action:@selector(convertFile:) key:@""];
    [build addItem:[NSMenuItem separatorItem]];
    [self add:@"Jump to Next Issue" to:build action:@selector(nextIssue:) key:@"'"];
    [self add:@"Clear Issues" to:build action:@selector(clearIssuesAction:) key:@"k" mods:cmd | shift];

    // Target: where the build is for, which compiler, which language.
    NSMenu* target = [self submenu:@"Target" of:bar];
    for (int i = 0; i < ride_arch_count(); ++i)
        [self add:Str(ride_arch(i)) to:target action:@selector(chooseArch:) key:@""].tag = kTagArchBase + i;
    // Command-Option, not Control: Control-T and Control-K are the text view's own (M9).
    [self add:@"Next Target" to:target action:@selector(nextTarget:) key:@"t" mods:cmd | opt];
    [target addItem:[NSMenuItem separatorItem]];
    NSMenu* compilers = [self submenu:@"Compiler" of:target];
    for (NSNumber* kind in [self toolKinds]) {
        NSString* name = kind.intValue == RIDE_TOOL_AUTO ? @"By Language"
                         : kind.intValue == RIDE_TOOL_CXX
                             ? [NSString stringWithFormat:@"Host (%@)", Str(ride_toolchain_name(RIDE_TOOL_CXX))]
                             : Str(ride_toolchain_name(kind.intValue));
        [self add:name to:compilers action:@selector(chooseTool:) key:@""].tag = kTagToolBase + kind.intValue;
    }
    [compilers addItem:[NSMenuItem separatorItem]];
    [self add:@"Next Compiler" to:compilers action:@selector(nextCompiler:) key:@"k" mods:cmd | opt];
    NSMenu* languages = [self submenu:@"Language" of:target];
    [self add:@"By Extension" to:languages action:@selector(chooseLanguage:) key:@""].tag = kTagLangAuto;
    NSArray<NSArray*>* langs = @[ @[ @"C", @(RIDE_LANG_C) ], @[ @"C++", @(RIDE_LANG_CPP) ],
                                  @[ @"Shalimar", @(RIDE_LANG_SHALIMAR) ], @[ @"Assembly", @(RIDE_LANG_ASM) ],
                                  @[ @"JSON", @(RIDE_LANG_JSON) ], @[ @"Plain Text", @(RIDE_LANG_PLAIN) ] ];
    for (NSArray* lang in langs)
        [self add:lang[0] to:languages action:@selector(chooseLanguage:) key:@""].tag =
            kTagLangBase + [lang[1] intValue];

    // Option: how the editor looks and where the tools it drives are.
    NSMenu* option = [self submenu:@"Option" of:bar];
    [self add:@"Font..." to:option action:@selector(chooseFont:) key:@"t"];
    [option addItem:[NSMenuItem separatorItem]];
    [self add:@"Header Directories..." to:option action:@selector(headerDirectories:) key:@""];
    [self add:@"Shared Include Paths..." to:option action:@selector(sharedIncludes:) key:@""];
    [self add:@"Shared Libraries..." to:option action:@selector(sharedLibraries:) key:@""];
    [option addItem:[NSMenuItem separatorItem]];
    [self add:@"Assembler for x86_64-windows..." to:option action:@selector(locateAssembler:) key:@""];
    [self add:@"Linker for x86_64-windows..." to:option action:@selector(locateLinker:) key:@""];
    [self add:@"TI Compiler for tms6747..." to:option action:@selector(locateTi:) key:@""];
    [self add:@"Linker for tms6747..." to:option action:@selector(locateTiLinker:) key:@""];
    [option addItem:[NSMenuItem separatorItem]];
    [self add:@"Compiler Options..." to:option action:@selector(compilerOptions:) key:@""];
    [self add:@"Show Tools in Use" to:option action:@selector(showCompilers:) key:@""];

    // Window, which macOS expects and fills with the open windows.
    NSMenu* window = [self submenu:@"Window" of:bar];
    [self add:@"Minimize" to:window action:@selector(performMiniaturize:) key:@"m"];
    [self add:@"Zoom" to:window action:@selector(performZoom:) key:@""];
    [window addItem:[NSMenuItem separatorItem]];
    [self add:@"Bring All to Front" to:window action:@selector(arrangeInFront:) key:@""];
    NSApp.windowsMenu = window;

    // Help
    NSMenu* help = [self submenu:@"Help" of:bar];
    [self add:[product stringByAppendingString:@" Help"] to:help action:@selector(showHelp:) key:@"?"];
    [self add:@"Keys" to:help action:@selector(showKeys:) key:@""];
    [self add:@"Shalimar Language Reference" to:help action:@selector(showShalimarReference:) key:@""];
    // The one page of everything installed, as the Windows window has it.
    [self add:@"Resources" to:help action:@selector(showResources:) key:@""];
    [self add:@"Environment" to:help action:@selector(showEnvironment:) key:@""];
    // About RIDE lives in the application menu, where macOS puts it, and not here too.
    NSApp.helpMenu = help;

    return bar;
}

@end
