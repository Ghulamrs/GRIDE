// The gutter: line numbers down the left of the file, and a red mark on the
// lines the last build complained about.
#ifndef MACOS_LINE_NUMBERS_H
#define MACOS_LINE_NUMBERS_H

#import <Cocoa/Cocoa.h>

@interface LineNumbers : NSRulerView

- (instancetype)initWithTextView:(NSTextView*)textView;

// 1-based line numbers to mark as errors; empty for none.
@property(nonatomic, copy) NSIndexSet* errorLines;
// 1-based line to mark as warnings; drawn in orange.
@property(nonatomic, copy) NSIndexSet* warningLines;

// Re-measure the width after the number of lines changed.
- (void)textDidChange;

@end

#endif
