// The text of the file: an NSTextView that lays C, C++ and Shalimar out as it
// is typed, by asking the core (indent.cpp through winforms/bridge.h) - the
// same rules the terminal editor and the Windows window follow.
#ifndef MACOS_CODE_VIEW_H
#define MACOS_CODE_VIEW_H

#import <Cocoa/Cocoa.h>

// What the view needs to know from the window to lay a line out.
@protocol CodeViewHost <NSObject>
- (int)indentWidth;
- (int)indentTabs;
- (int)indentCase;
// RIDE_DIALECT_*, from the file's language.
- (int)indentDialect;
// Whether layout-as-you-type applies at all (not for plain text or JSON).
- (BOOL)laysOut;
@optional
// An edit replaced `before` rows from `first` with `after` rows, as the line index counts them.
- (void)codeView:(NSTextView*)view rowsFrom:(NSInteger)first before:(NSInteger)before
           after:(NSInteger)after;
@end

@interface CodeView : NSTextView

@property(nonatomic, weak) id<CodeViewHost> host;

// The find bar closed, if it is open: Escape and Go to Line, which left the text greyed under it.
- (BOOL)hideFindBar;

// Rows are the core's, ending at '\n' only (M2), from an index kept as the text is edited (H3).
- (NSInteger)caretRow;
// The caret's column in characters, and in bytes of UTF-8 - which is what compilers count.
- (NSInteger)caretColumn;
- (NSInteger)caretByteColumn;
// 0-based row holding a character index.
- (NSInteger)rowOfIndex:(NSUInteger)index;
// The character index where a 0-based row begins, or NSNotFound past the end.
- (NSUInteger)indexOfRow:(NSInteger)row;
// How many rows, and one row's characters without its '\n'.
- (NSInteger)lineCount;
- (NSRange)contentsOfRow:(NSInteger)row;
// The rows a range touches, whole, with the '\n' after the last.
- (NSRange)rowsOfRange:(NSRange)range;
// After the text storage is swapped for another: the index is made again for it.
- (void)textStorageChanged;
// 1-based, as a compiler names them - the column in bytes of UTF-8 (M1); clamped to the line.
- (void)goToLine:(NSInteger)line column:(NSInteger)column;

// Put one line's leading space where the rules say it belongs.
- (void)realignRow:(NSInteger)row;
// Edit > Re-indent: the selected lines, or the whole file with nothing selected; the whole file is always measured.
- (void)reindentSelectionOrAll;

// Replace a range as a user edit would: undoable, and announced.
- (void)replaceRange:(NSRange)range with:(NSString*)text;

@end

#endif
