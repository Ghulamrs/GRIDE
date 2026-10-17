#import "LineNumbers.h"

#import "CodeView.h"

@implementation LineNumbers {
    NSDictionary* numberAttributes_;
}

- (instancetype)initWithTextView:(NSTextView*)textView {
    self = [super initWithScrollView:textView.enclosingScrollView
                         orientation:NSVerticalRuler];
    if (self) {
        self.clientView = textView;
        _errorLines = [NSIndexSet indexSet];
        _warningLines = [NSIndexSet indexSet];
        [self remeasure];

        // Scrolling moves the lines under the numbers; the ruler is redrawn
        // with the clip view rather than on the next keystroke.
        NSClipView* clip = textView.enclosingScrollView.contentView;
        clip.postsBoundsChangedNotifications = YES;
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(scrolled:)
                                                     name:NSViewBoundsDidChangeNotification
                                                   object:clip];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (BOOL)isFlipped { return YES; }

- (void)scrolled:(NSNotification*)note {
    (void)note;
    self.needsDisplay = YES;
}

- (void)setErrorLines:(NSIndexSet*)lines {
    _errorLines = [lines copy] ?: [NSIndexSet indexSet];
    self.needsDisplay = YES;
}

- (void)setWarningLines:(NSIndexSet*)lines {
    _warningLines = [lines copy] ?: [NSIndexSet indexSet];
    self.needsDisplay = YES;
}

- (NSFont*)numberFont {
    NSTextView* text = (NSTextView*)self.clientView;
    CGFloat size = text.font != nil ? text.font.pointSize - 1 : 11;
    return [NSFont monospacedDigitSystemFontOfSize:MAX(9, size) weight:NSFontWeightRegular];
}

- (NSUInteger)lineCount {
    NSTextView* text = (NSTextView*)self.clientView;
    if ([text isKindOfClass:[CodeView class]]) return (NSUInteger)[(CodeView*)text lineCount];
    return 1;
}

// The width follows the digits and the font both: a bigger font keeps the digit count and still
// wants a wider gutter, which the early return for an unchanged count used to miss (M6).
- (void)remeasure {
    numberAttributes_ = @{
        NSFontAttributeName : [self numberFont],
        NSForegroundColorAttributeName : [NSColor secondaryLabelColor],
    };
    NSUInteger digits = 1;
    for (NSUInteger n = [self lineCount]; n >= 10; n /= 10) ++digits;
    if (digits < 3) digits = 3;
    NSSize one = [@"8" sizeWithAttributes:numberAttributes_];
    CGFloat thickness = ceil(one.width * digits + 22);
    if (thickness != self.ruleThickness) {
        self.ruleThickness = thickness;
        [self.scrollView tile];
    }
}

- (void)textDidChange {
    [self remeasure];
    self.needsDisplay = YES;
}

// The rows showing, found through the view's row index rather than by counting newlines from the
// top of the file on every frame (H3), and a row being what the core calls one (M2).
- (void)drawHashMarksAndLabelsInRect:(NSRect)rect {
    (void)rect;
    NSTextView* text = (NSTextView*)self.clientView;
    NSLayoutManager* layout = text.layoutManager;
    NSTextContainer* container = text.textContainer;
    if (layout == nil || container == nil || ![text isKindOfClass:[CodeView class]]) return;
    CodeView* code = (CodeView*)text;

    [[NSColor textBackgroundColor] setFill];
    NSRectFill(self.bounds);
    [[NSColor separatorColor] setFill];
    NSRectFill(NSMakeRect(NSMaxX(self.bounds) - 1, NSMinY(self.bounds), 1,
                          NSHeight(self.bounds)));

    if (numberAttributes_ == nil) [self remeasure];

    NSUInteger length = text.string.length;
    NSRect visible = text.visibleRect;
    NSRange glyphs = [layout glyphRangeForBoundingRect:visible inTextContainer:container];
    NSRange chars = [layout characterRangeForGlyphRange:glyphs actualGlyphRange:NULL];
    NSInteger rows = [code lineCount];
    CGFloat inset = text.textContainerOrigin.y;
    CGFloat width = NSWidth(self.bounds);
    NSDictionary* white = nil;

    for (NSInteger row = [code rowOfIndex:chars.location]; row < rows; ++row) {
        NSUInteger at = [code indexOfRow:row];
        if (at == NSNotFound || at > NSMaxRange(chars)) break;
        NSRect fragment;
        if (at < length) {
            NSUInteger glyph = [layout glyphIndexForCharacterAtIndex:at];
            fragment = [layout lineFragmentRectForGlyphAtIndex:glyph effectiveRange:NULL];
        } else {
            // The empty last row, after a final newline or in an empty file.
            fragment = layout.extraLineFragmentRect;
            if (NSIsEmptyRect(fragment)) break;
        }

        NSPoint top = [self convertPoint:NSMakePoint(0, NSMinY(fragment) + inset) fromView:text];
        CGFloat height = NSHeight(fragment);
        NSUInteger line = (NSUInteger)row + 1;

        NSColor* mark = nil;
        if ([_errorLines containsIndex:line]) mark = [NSColor systemRedColor];
        else if ([_warningLines containsIndex:line]) mark = [NSColor systemOrangeColor];
        NSDictionary* attributes = numberAttributes_;
        if (mark != nil) {
            [[mark colorWithAlphaComponent:0.85] setFill];
            NSRect badge = NSMakeRect(2, top.y + 1, width - 6, MAX(1, height - 2));
            [[NSBezierPath bezierPathWithRoundedRect:badge xRadius:3 yRadius:3] fill];
            if (white == nil) {
                NSMutableDictionary* made = [numberAttributes_ mutableCopy];
                made[NSForegroundColorAttributeName] = [NSColor whiteColor];
                white = made;
            }
            attributes = white;
        }

        NSString* number = [NSString stringWithFormat:@"%lu", (unsigned long)line];
        NSSize size = [number sizeWithAttributes:attributes];
        [number drawAtPoint:NSMakePoint(width - size.width - 10,
                                        top.y + (height - size.height) / 2)
             withAttributes:attributes];
    }
}

@end
