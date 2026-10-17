#import "CodeView.h"

#include <algorithm>
#include <cstring>
#include <vector>

#import "Text.h"

@implementation CodeView {
    // Where each row begins: 0, then one past every '\n'. Kept for the storage in the view.
    std::vector<NSUInteger> starts_;
    __weak NSTextStorage* indexed_;
}

- (BOOL)hideFindBar {
    if (!self.enclosingScrollView.findBarVisible) return NO;
    NSMenuItem* hide = [[NSMenuItem alloc] init];
    hide.tag = NSTextFinderActionHideFindInterface;
    [self performTextFinderAction:hide];
    return YES;
}

// Escape in the text closes the find bar before it does anything else.
- (void)cancelOperation:(id)sender {
    if (![self hideFindBar]) [super cancelOperation:sender];
}

- (instancetype)initWithFrame:(NSRect)frame textContainer:(NSTextContainer*)container {
    self = [super initWithFrame:frame textContainer:container];
    if (self) {
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(storageEdited:)
                                                     name:NSTextStorageDidProcessEditingNotification
                                                   object:nil];
        [self textStorageChanged];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

// ---- the row index ----------------------------------------------------------

static void newlinesIn(NSString* text, NSRange range, std::vector<NSUInteger>& into) {
    unichar buffer[4096];
    NSUInteger at = range.location, end = NSMaxRange(range);
    while (at < end) {
        NSUInteger take = MIN((NSUInteger)4096, end - at);
        [text getCharacters:buffer range:NSMakeRange(at, take)];
        for (NSUInteger i = 0; i < take; ++i)
            if (buffer[i] == '\n') into.push_back(at + i + 1);
        at += take;
    }
}

- (void)textStorageChanged {
    NSTextStorage* storage = self.textStorage;
    indexed_ = storage;
    starts_.assign(1, 0);
    if (storage != nil) newlinesIn(storage.string, NSMakeRange(0, storage.length), starts_);
}

// An edit replaces the row starts inside what it changed and moves the ones after it; the rest
// stand. The host hears which rows went and came, so colouring can start there.
- (void)storageEdited:(NSNotification*)note {
    NSTextStorage* storage = note.object;
    if (storage != self.textStorage) return;
    if (storage != indexed_) { [self textStorageChanged]; return; }
    if (!(storage.editedMask & NSTextStorageEditedCharacters)) return;

    NSRange edited = storage.editedRange;
    NSInteger delta = storage.changeInLength;
    NSUInteger loc = edited.location;
    NSUInteger oldEnd = (NSUInteger)((NSInteger)NSMaxRange(edited) - delta);
    auto first = std::upper_bound(starts_.begin(), starts_.end(), loc);
    auto last = std::upper_bound(starts_.begin(), starts_.end(), oldEnd);
    NSInteger firstRow = (NSInteger)(first - starts_.begin()) - 1;
    NSInteger removed = (NSInteger)(last - first);
    size_t at = (size_t)(first - starts_.begin());
    starts_.erase(first, last);
    for (size_t i = at; i < starts_.size(); ++i) starts_[i] = (NSUInteger)((NSInteger)starts_[i] + delta);
    std::vector<NSUInteger> added;
    newlinesIn(storage.string, edited, added);
    starts_.insert(starts_.begin() + (long)at, added.begin(), added.end());

    id<CodeViewHost> host = self.host;
    if ([host respondsToSelector:@selector(codeView:rowsFrom:before:after:)])
        [host codeView:self rowsFrom:firstRow before:removed + 1 after:(NSInteger)added.size() + 1];
}

- (NSInteger)lineCount { return (NSInteger)starts_.size(); }

- (NSInteger)rowOfIndex:(NSUInteger)index {
    auto after = std::upper_bound(starts_.begin(), starts_.end(), index);
    return (NSInteger)(after - starts_.begin()) - 1;
}

- (NSUInteger)indexOfRow:(NSInteger)row {
    if (row <= 0) return 0;
    if (row >= (NSInteger)starts_.size()) return NSNotFound;
    return starts_[(size_t)row];
}

- (NSInteger)caretRow { return [self rowOfIndex:self.selectedRange.location]; }

- (NSInteger)caretColumn {
    NSUInteger caret = self.selectedRange.location;
    return (NSInteger)(caret - [self indexOfRow:[self rowOfIndex:caret]]);
}

- (NSInteger)caretByteColumn {
    NSUInteger caret = self.selectedRange.location;
    NSUInteger start = [self indexOfRow:[self rowOfIndex:caret]];
    NSString* before = [self.string substringWithRange:NSMakeRange(start, caret - start)];
    return (NSInteger)std::strlen(Utf8(before));
}

- (NSRange)contentsOfRow:(NSInteger)row {
    NSUInteger start = [self indexOfRow:row];
    if (start == NSNotFound) return NSMakeRange(NSNotFound, 0);
    NSUInteger next = [self indexOfRow:row + 1];
    NSUInteger end = next == NSNotFound ? self.string.length : next - 1;
    return NSMakeRange(start, end - start);
}

- (NSRange)rowsOfRange:(NSRange)range {
    NSInteger first = [self rowOfIndex:range.location];
    NSUInteger end = NSMaxRange(range);
    // A selection ending at the start of a row does not take that row.
    if (range.length > 0 && end > 0 && [self rowOfIndex:end - 1] < [self rowOfIndex:end]) --end;
    NSInteger last = [self rowOfIndex:end];
    NSUInteger start = [self indexOfRow:first];
    NSUInteger next = [self indexOfRow:last + 1];
    NSUInteger stop = next == NSNotFound ? self.string.length : next;
    return NSMakeRange(start, stop - start);
}

static NSUInteger leadingSpace(NSString* line) {
    NSUInteger lead = 0;
    while (lead < line.length) {
        unichar c = [line characterAtIndex:lead];
        if (c != ' ' && c != '\t') break;
        ++lead;
    }
    return lead;
}

- (void)goToLine:(NSInteger)line column:(NSInteger)column {
    if (line < 1) line = 1;
    NSUInteger start = [self indexOfRow:line - 1];
    if (start == NSNotFound) start = self.string.length;
    NSRange contents = [self contentsOfRow:line - 1];
    NSUInteger at = start;
    if (contents.location != NSNotFound) {
        // The compiler's column counts bytes of UTF-8; the view counts UTF-16 units (M1).
        NSString* text = [self.string substringWithRange:contents];
        NSInteger bytes = 0, wanted = MAX((NSInteger)0, column - 1);
        NSUInteger unit = 0;
        while (unit < text.length && bytes < wanted) {
            NSRange one = [text rangeOfComposedCharacterSequenceAtIndex:unit];
            bytes += (NSInteger)std::strlen(Utf8([text substringWithRange:one]));
            unit = NSMaxRange(one);
        }
        at = start + unit;
    }
    self.selectedRange = NSMakeRange(at, 0);
    [self scrollRangeToVisible:NSMakeRange(at, 0)];
    [self showFindIndicatorForRange:(contents.location == NSNotFound
                                         ? NSMakeRange(at, 0) : contents)];
}

// ---- editing ------------------------------------------------------------------

// With the typing attributes, so text put into an empty file has the code font (L9).
- (void)replaceRange:(NSRange)range with:(NSString*)text {
    if (![self shouldChangeTextInRange:range replacementString:text]) return;
    NSAttributedString* styled = [[NSAttributedString alloc] initWithString:text
                                                                 attributes:self.typingAttributes];
    [self.textStorage replaceCharactersInRange:range withAttributedString:styled];
    [self didChangeText];
}

// A row's layout reads only the rows above it and itself, so only those are handed over.
- (NSString*)textThroughRow:(NSInteger)row {
    NSRange contents = [self contentsOfRow:row];
    NSUInteger end = contents.location == NSNotFound ? self.string.length : NSMaxRange(contents);
    return [self.string substringToIndex:end];
}

- (NSString*)indentUnit {
    id<CodeViewHost> host = self.host;
    if (host != nil && [host indentTabs] != 0) return @"\t";
    int width = host != nil ? [host indentWidth] : 4;
    if (width < 1) width = 4;
    return [@"" stringByPaddingToLength:(NSUInteger)width withString:@" " startingAtIndex:0];
}

- (void)realignRow:(NSInteger)row {
    id<CodeViewHost> host = self.host;
    if (host == nil) return;
    NSRange contents = [self contentsOfRow:row];
    if (contents.location == NSNotFound) return;

    NSString* line = [self.string substringWithRange:contents];
    NSUInteger lead = leadingSpace(line);

    NSString* want = Take(ride_indent_for(Utf8([self textThroughRow:row]), (int)row,
                                              [host indentWidth], [host indentTabs],
                                              [host indentCase], [host indentDialect]));
    if ([want isEqualToString:[line substringToIndex:lead]]) return;

    NSRange caret = self.selectedRange;
    [self replaceRange:NSMakeRange(contents.location, lead) with:want];

    // The caret keeps its place in the text after the leading space.
    NSInteger moved = (NSInteger)caret.location + (NSInteger)want.length - (NSInteger)lead;
    if (caret.location < contents.location + lead) moved = (NSInteger)(contents.location + want.length);
    moved = MAX((NSInteger)0, MIN(moved, (NSInteger)self.string.length));
    self.selectedRange = NSMakeRange((NSUInteger)moved, 0);
}

- (void)insertNewline:(id)sender {
    id<CodeViewHost> host = self.host;
    if (host == nil || ![host laysOut]) {
        [super insertNewline:sender];
        return;
    }
    NSRange selection = self.selectedRange;
    NSInteger row = [self rowOfIndex:selection.location];
    NSUInteger start = [self indexOfRow:row];
    NSString* before = [self.string substringWithRange:NSMakeRange(start, selection.location - start)];
    // The core counts columns in bytes of UTF-8, as it counts everything.
    int column = (int)std::strlen(Utf8(before));

    NSString* lead = Take(ride_indent_after_newline(Utf8([self textThroughRow:row]), (int)row, column,
                                                        [host indentWidth], [host indentTabs],
                                                        [host indentCase], [host indentDialect]));
    [self insertText:[@"\n" stringByAppendingString:lead] replacementRange:selection];
}

- (void)insertTab:(id)sender {
    id<CodeViewHost> host = self.host;
    if (host == nil || ![host laysOut] || self.selectedRange.length != 0) {
        [super insertTab:sender];
        return;
    }
    NSInteger row = [self caretRow];
    NSRange contents = [self contentsOfRow:row];
    NSString* line = [self.string substringWithRange:contents];
    NSUInteger lead = leadingSpace(line);

    // Tab in the leading space puts the line where it belongs; anywhere
    // else it is a step of indentation, as the project spells one.
    if ((NSUInteger)[self caretColumn] <= lead) {
        [self realignRow:row];
        NSRange now = [self contentsOfRow:row];
        NSString* laid = [self.string substringWithRange:now];
        self.selectedRange = NSMakeRange(now.location + leadingSpace(laid), 0);
        return;
    }
    [self insertText:[self indentUnit] replacementRange:self.selectedRange];
}

- (void)insertText:(id)text replacementRange:(NSRange)range {
    [super insertText:text replacementRange:range];

    id<CodeViewHost> host = self.host;
    if (host == nil || ![host laysOut]) return;
    NSString* typed = [text isKindOfClass:[NSAttributedString class]]
                          ? [(NSAttributedString*)text string] : (NSString*)text;
    if (typed.length != 1) return;
    unichar just = [typed characterAtIndex:0];
    if (just != '}' && just != '#' && just != ':') return;

    // A closing brace or a '#' re-lays its line only when it is the first
    // thing on it; a ':' may end a case label anywhere.
    NSInteger row = [self caretRow];
    if (just != ':') {
        NSRange contents = [self contentsOfRow:row];
        NSString* line = [self.string substringWithRange:contents];
        NSInteger column = [self caretColumn] - 1;
        for (NSInteger i = 0; i < column && i < (NSInteger)line.length; ++i) {
            unichar c = [line characterAtIndex:(NSUInteger)i];
            if (c != ' ' && c != '\t') return;
        }
    }
    [self realignRow:row];
}

- (void)reindentSelectionOrAll {
    id<CodeViewHost> host = self.host;
    if (host == nil) return;
    NSString* all = self.string;
    NSString* laid = Take(ride_reindent(Utf8(all), [host indentWidth],
                                            [host indentTabs], [host indentCase],
                                            [host indentDialect]));
    if ([laid isEqualToString:all]) return;

    NSRange selection = self.selectedRange;
    NSInteger caretRow = [self caretRow];
    NSInteger caretColumn = [self caretColumn];

    NSArray<NSString*>* was = [all componentsSeparatedByString:@"\n"];
    NSArray<NSString*>* now = [laid componentsSeparatedByString:@"\n"];

    NSInteger first = 0;
    NSInteger last = (NSInteger)was.count - 1;
    if (selection.length > 0) {
        first = [self rowOfIndex:selection.location];
        NSUInteger end = NSMaxRange(selection);
        // A selection ending at the start of a line does not take that line.
        if (end > selection.location && [all characterAtIndex:end - 1] == '\n') --end;
        last = [self rowOfIndex:end];
    }

    // The selection decides which lines are written back, not what is
    // measured; the re-indent keeps the line count, so rows correspond.
    if (now.count != was.count) {
        [self replaceRange:NSMakeRange(0, all.length) with:laid];
    } else {
        NSUInteger start = [self indexOfRow:first];
        NSRange lastLine = [self contentsOfRow:last];
        if (start == NSNotFound || lastLine.location == NSNotFound) return;
        NSRange span = NSMakeRange(start, NSMaxRange(lastLine) - start);
        NSArray* chosen = [now subarrayWithRange:NSMakeRange((NSUInteger)first,
                                                             (NSUInteger)(last - first + 1))];
        [self replaceRange:span with:[chosen componentsJoinedByString:@"\n"]];
    }

    if (selection.length > 0) {
        NSUInteger start = [self indexOfRow:first];
        NSRange lastLine = [self contentsOfRow:last];
        if (start != NSNotFound && lastLine.location != NSNotFound)
            self.selectedRange = NSMakeRange(start, NSMaxRange(lastLine) - start);
    } else {
        NSUInteger start = [self indexOfRow:caretRow];
        if (start == NSNotFound) start = self.string.length;
        NSRange line = [self contentsOfRow:caretRow];
        NSInteger room = line.location == NSNotFound ? 0 : (NSInteger)line.length;
        self.selectedRange = NSMakeRange(start + (NSUInteger)MIN(room, caretColumn), 0);
    }
}

@end
