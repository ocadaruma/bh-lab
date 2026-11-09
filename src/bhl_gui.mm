// Cocoa view for bh-lab: the selected scale, the chord being generated, and the control
// octave with the held notes and the selected scale's notes drawn onto the keys.
//
// Compiled without ARC (the CMake default for .mm), so the view is retained and released
// manually by guiCreate/guiDestroy.

#import <Cocoa/Cocoa.h>

#include "bhl_gui.h"
#include "chord_engine.h"

using namespace bhl;

namespace
{

constexpr uint32_t kViewWidth = 700;
constexpr uint32_t kViewHeight = 452;

// The chord keyboard spans C1..C6, which covers the default zones with room either side.
constexpr int kBedLow = 36;
constexpr int kBedHigh = 96;

constexpr CGFloat kPad = 18;
constexpr CGFloat kControlKeyboardWidth = 430;
constexpr CGFloat kLabelWidth = 60;
constexpr CGFloat kBorrowLabelWidth = 82;
constexpr CGFloat kStepperWidth = 170;
constexpr CGFloat kButtonGap = 8;

// Row geometry.
constexpr CGFloat kFamilyRowY = 66;
constexpr CGFloat kVoicingRowY = 102;
constexpr CGFloat kBorrowTopRowY = 138;
constexpr CGFloat kBorrowSecondRowY = 174;
constexpr CGFloat kBorrowThirdRowY = 210;
constexpr CGFloat kRowHeight = 30;

constexpr CGFloat kChordLabelY = 246;
constexpr CGFloat kChordKeyboardY = 262;
constexpr CGFloat kChordKeyboardHeight = 72;

constexpr CGFloat kControlLabelY = 340;
constexpr CGFloat kControlKeyboardY = 356;
constexpr CGFloat kControlKeyboardHeight = 70;

NSRect familyButtonRect(int index, CGFloat viewWidth)
{
  const CGFloat x0 = kPad + kLabelWidth + 6;
  const CGFloat total = viewWidth - kPad - x0;
  const int count = static_cast<int>(ScaleFamily::Count);
  const CGFloat w = (total - kButtonGap * (count - 1)) / count;
  return NSMakeRect(x0 + index * (w + kButtonGap), kFamilyRowY, w, kRowHeight);
}

NSRect voicingButtonRect(int index, CGFloat viewWidth)
{
  const CGFloat x0 = kPad + kLabelWidth + 6;
  const CGFloat total = viewWidth - kPad - x0;
  const int count = static_cast<int>(VoicingType::Count);
  const CGFloat w = (total - kButtonGap * (count - 1)) / count;
  return NSMakeRect(x0 + index * (w + kButtonGap), kVoicingRowY, w, kRowHeight);
}

NSRect borrowButtonRect(int row, int index, CGFloat viewWidth)
{
  static const CGFloat kRowY[3] = {kBorrowTopRowY, kBorrowSecondRowY, kBorrowThirdRowY};
  const CGFloat x0 = kPad + kBorrowLabelWidth + 6;
  const CGFloat total = viewWidth - kPad - x0;
  const int count = static_cast<int>(BorrowDir::Count);
  const CGFloat w = (total - kButtonGap * (count - 1)) / count;
  return NSMakeRect(x0 + index * (w + kButtonGap), kRowY[row], w, kRowHeight);
}

NSRect triggerStartStepperRect(CGFloat viewWidth)
{
  return NSMakeRect(viewWidth - kPad - kStepperWidth, kChordLabelY - 2, kStepperWidth, 20);
}

NSRect controlRootStepperRect(CGFloat viewWidth)
{
  return NSMakeRect(viewWidth - kPad - kStepperWidth, kControlLabelY - 2, kStepperWidth, 20);
}

NSRect controlKeyboardRect()
{
  return NSMakeRect(kPad, kControlKeyboardY, kControlKeyboardWidth, kControlKeyboardHeight);
}

NSColor *gray(CGFloat v, CGFloat a = 1.0)
{
  return [NSColor colorWithCalibratedWhite:v alpha:a];
}

NSColor *accent()  // Sounding notes.
{
  return [NSColor colorWithCalibratedRed:0.27 green:0.56 blue:0.96 alpha:1.0];
}

NSColor *accentDark()
{
  return [NSColor colorWithCalibratedRed:0.20 green:0.42 blue:0.80 alpha:1.0];
}

NSColor *rootAccent()  // The key that was actually pressed / the scale's root.
{
  return [NSColor colorWithCalibratedRed:0.98 green:0.64 blue:0.24 alpha:1.0];
}

NSColor *inScaleWhite()  // Notes of the selected scale, on white keys.
{
  return [NSColor colorWithCalibratedRed:0.78 green:0.88 blue:0.99 alpha:1.0];
}

NSColor *inScaleBlack()
{
  return [NSColor colorWithCalibratedRed:0.30 green:0.45 blue:0.68 alpha:1.0];
}

NSColor *chordGhostWhite()  // Notes of a held chord that are not sounding.
{
  return [NSColor colorWithCalibratedRed:0.70 green:0.79 blue:0.94 alpha:1.0];
}

NSColor *chordGhostBlack()
{
  return [NSColor colorWithCalibratedRed:0.24 green:0.33 blue:0.50 alpha:1.0];
}

void drawText(NSString *text, NSRect rect, CGFloat size, NSColor *color, NSTextAlignment align,
              NSFontWeight weight = NSFontWeightRegular)
{
  NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
  [style setAlignment:align];
  [style setLineBreakMode:NSLineBreakByTruncatingTail];

  NSDictionary *attrs = @{
    NSFontAttributeName : [NSFont systemFontOfSize:size weight:weight],
    NSForegroundColorAttributeName : color,
    NSParagraphStyleAttributeName : style
  };
  [text drawInRect:rect withAttributes:attrs];
  [style release];
}

bool isWhiteNote(int key)
{
  static const bool kWhite[12] = {true, false, true, false, true, true,
                                  false, true, false, true, false, true};
  return kWhite[((key % 12) + 12) % 12];
}

int whitesBefore(int low, int key)
{
  int count = 0;
  for (int i = low; i < key; ++i)
    if (isWhiteNote(i)) ++count;
  return count;
}

int whiteCount(int low, int high)
{
  int count = 0;
  for (int i = low; i <= high; ++i)
    if (isWhiteNote(i)) ++count;
  return count;
}

// Mirrors the geometry drawKeyboardInRect draws with. Black keys are tested first, since
// they sit on top of the white ones.
int keyAtPoint(NSPoint p, NSRect area, int low, int high)
{
  if (!NSPointInRect(p, area)) return -1;

  const int whites = whiteCount(low, high);
  if (whites <= 0) return -1;
  const CGFloat ww = area.size.width / whites;
  const CGFloat bw = ww * 0.62;
  const CGFloat bh = area.size.height * 0.62;

  for (int key = low; key <= high; ++key)
  {
    if (isWhiteNote(key)) continue;
    const NSRect r = NSMakeRect(area.origin.x + whitesBefore(low, key) * ww - bw / 2,
                                area.origin.y, bw, bh);
    if (NSPointInRect(p, r)) return key;
  }

  for (int key = low; key <= high; ++key)
  {
    if (!isWhiteNote(key)) continue;
    const NSRect r = NSMakeRect(area.origin.x + whitesBefore(low, key) * ww, area.origin.y,
                                ww - 1.0, area.size.height);
    if (NSPointInRect(p, r)) return key;
  }
  return -1;
}

const char *kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Note names follow Logic's convention, where middle C (MIDI 60) is C3.
NSString *noteName(int key)
{
  return [NSString stringWithFormat:@"%s%d", kNoteNames[key % 12], (key / 12) - 2];
}

NSString *pitchName(int key)
{
  return [NSString stringWithUTF8String:kNoteNames[((key % 12) + 12) % 12]];
}

bool inScale(int pc, const ScaleSelection &sel)
{
  const int *offsets = scaleOffsets(sel.family);
  if (!offsets) return false;
  const int d = (pc - sel.keyPc + 12) % 12;
  for (int i = 0; i < kScaleNoteCount; ++i)
    if (offsets[i] == d) return true;
  return false;
}

// The plugin encodes the selection as family * 12 + key pitch class; decode it the same
// way here.
ScaleSelection decodeSelectionLocal(int encoded)
{
  return {static_cast<ScaleFamily>(encoded / 12), encoded % 12};
}

// What the UI last drew, so the timer can skip redundant redraws.
struct Snapshot
{
  uint64_t sounding[2] = {};
  uint64_t chord[2] = {};
  int rootKey = -2;
  int selection = -1;
  uint64_t heldControl[2] = {};
  int voicing = -1;
  int borrowTop = -1;
  int borrowSecond = -1;
  int borrowThird = -1;
  int controlRoot = -1;
  int triggerStart = -1;

  bool operator==(const Snapshot &o) const
  {
    return sounding[0] == o.sounding[0] && sounding[1] == o.sounding[1] &&
           chord[0] == o.chord[0] && chord[1] == o.chord[1] &&
           rootKey == o.rootKey && selection == o.selection &&
           heldControl[0] == o.heldControl[0] && heldControl[1] == o.heldControl[1] &&
           voicing == o.voicing && borrowTop == o.borrowTop &&
           borrowSecond == o.borrowSecond && borrowThird == o.borrowThird &&
           controlRoot == o.controlRoot &&
           triggerStart == o.triggerStart;
  }
};

Snapshot takeSnapshot(GuiState *state)
{
  Snapshot s;
  s.sounding[0] = state->sounding[0].load(std::memory_order_relaxed);
  s.sounding[1] = state->sounding[1].load(std::memory_order_relaxed);
  s.chord[0] = state->chord[0].load(std::memory_order_relaxed);
  s.chord[1] = state->chord[1].load(std::memory_order_relaxed);
  s.rootKey = state->rootKey.load(std::memory_order_relaxed);
  s.selection = state->selection.load(std::memory_order_relaxed);
  s.heldControl[0] = state->heldControl[0].load(std::memory_order_relaxed);
  s.heldControl[1] = state->heldControl[1].load(std::memory_order_relaxed);
  s.voicing = state->voicing.load(std::memory_order_relaxed);
  s.borrowTop = state->borrowTop.load(std::memory_order_relaxed);
  s.borrowSecond = state->borrowSecond.load(std::memory_order_relaxed);
  s.borrowThird = state->borrowThird.load(std::memory_order_relaxed);
  s.controlRoot = state->controlRoot.load(std::memory_order_relaxed);
  s.triggerStart = state->triggerStart.load(std::memory_order_relaxed);
  return s;
}

}  // namespace

@interface BhlView : NSView
{
  bhl::GuiState *_state;
  NSTimer *_timer;
  Snapshot _lastDrawn;
}
- (id)initWithState:(bhl::GuiState *)state;
- (void)stopTimer;
@end

@implementation BhlView

- (id)initWithState:(bhl::GuiState *)state
{
  uint32_t width = kViewWidth, height = kViewHeight;
  bhl::guiPreferredSize(state, &width, &height);

  self = [super initWithFrame:NSMakeRect(0, 0, width, height)];
  if (!self) return nil;

  _state = state;
  _lastDrawn = Snapshot();
  _timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
                                             target:self
                                           selector:@selector(tick:)
                                           userInfo:nil
                                            repeats:YES] retain];
  return self;
}

- (void)stopTimer
{
  if (_timer)
  {
    [_timer invalidate];
    [_timer release];
    _timer = nil;
  }
}

- (void)dealloc
{
  [self stopTimer];
  [super dealloc];
}

// Top-left origin keeps the layout arithmetic readable.
- (BOOL)isFlipped
{
  return YES;
}

- (void)tick:(NSTimer *)timer
{
  (void)timer;

  const Snapshot snapshot = takeSnapshot(_state);
  if (snapshot == _lastDrawn) return;
  [self setNeedsDisplay:YES];
}

// A button in a selectable row: filled when selected, dim when the row is not in play.
- (void)drawButton:(NSRect)r title:(NSString *)title selected:(BOOL)selected inert:(BOOL)inert
{
  NSColor *fill = selected ? accent() : (inert ? gray(0.20) : gray(0.28));
  [fill setFill];
  NSRectFill(r);
  [gray(0.10) setFill];
  NSFrameRect(r);
  drawText(title, NSMakeRect(r.origin.x, NSMidY(r) - 7, r.size.width, 14), 10.5,
           selected ? gray(1.0) : gray(0.72), NSTextAlignmentCenter,
           selected ? NSFontWeightSemibold : NSFontWeightRegular);
}

// Draws one keyboard. `controlOctave` draws the held / in-scale / root highlighting;
// `scaleInView` gates the scale tinting and the root, which only make sense while the
// held control notes put a scale in force. The bed draws the chord ghost and the
// sounding notes.
- (void)drawKeyboardInRect:(NSRect)area
                       low:(int)low
                      high:(int)high
             controlOctave:(BOOL)controlOctave
               scaleInView:(BOOL)scaleInView
{
  const int whites = whiteCount(low, high);
  if (whites <= 0) return;

  const CGFloat ww = area.size.width / whites;
  const int rootKey = _state->rootKey.load(std::memory_order_relaxed);
  const ScaleSelection sel = decodeSelectionLocal(_state->selection.load(std::memory_order_relaxed));

  // White keys first; black keys are drawn over them.
  for (int key = low; key <= high; ++key)
  {
    if (!isWhiteNote(key)) continue;

    const CGFloat x = area.origin.x + whitesBefore(low, key) * ww;
    const NSRect r = NSMakeRect(x, area.origin.y, ww - 1.0, area.size.height);

    NSColor *fill = gray(0.90);
    if (!controlOctave && _state->isChordNote(key)) fill = chordGhostWhite();
    if (controlOctave && scaleInView && inScale(key % 12, sel)) fill = inScaleWhite();
    if (controlOctave && _state->isControlHeld(key)) fill = accent();
    if (_state->isSounding(key)) fill = key == rootKey ? rootAccent() : accent();
    if (controlOctave && scaleInView && key % 12 == sel.keyPc) fill = rootAccent();

    [fill setFill];
    NSRectFill(r);
    [gray(0.45) setFill];
    NSFrameRect(r);

    if (controlOctave)
    {
      drawText(noteName(key), NSMakeRect(r.origin.x, NSMaxY(r) - 16, r.size.width, 13), 8.5,
               gray(0.35), NSTextAlignmentCenter);
    }
    else if (key % 12 == 0)
    {
      drawText(noteName(key), NSMakeRect(r.origin.x, NSMaxY(r) - 16, r.size.width, 13), 8.5,
               gray(0.45), NSTextAlignmentCenter);
    }
  }

  const CGFloat bw = ww * 0.62;
  const CGFloat bh = area.size.height * 0.62;

  for (int key = low; key <= high; ++key)
  {
    if (isWhiteNote(key)) continue;

    const CGFloat x = area.origin.x + whitesBefore(low, key) * ww - bw / 2.0;
    const NSRect r = NSMakeRect(x, area.origin.y, bw, bh);

    NSColor *fill = gray(0.17);
    if (!controlOctave && _state->isChordNote(key)) fill = chordGhostBlack();
    if (controlOctave && scaleInView && inScale(key % 12, sel)) fill = inScaleBlack();
    if (controlOctave && _state->isControlHeld(key)) fill = accentDark();
    if (_state->isSounding(key)) fill = key == rootKey ? rootAccent() : accentDark();
    if (controlOctave && scaleInView && key % 12 == sel.keyPc) fill = rootAccent();

    [fill setFill];
    NSRectFill(r);
    [gray(0.05) setFill];
    NSFrameRect(r);

    if (controlOctave)
    {
      drawText(pitchName(key), NSMakeRect(r.origin.x - 4, NSMaxY(r) - 16, r.size.width + 8, 13),
               9, gray(0.85), NSTextAlignmentCenter);
    }
  }
}

// A value with an arrow either side: click the arrows to step it.
- (void)drawStepper:(NSRect)r label:(NSString *)label
{
  [gray(0.22) setFill];
  NSRectFill(r);
  [gray(0.32) setFill];
  NSFrameRect(r);
  drawText(@"◀", NSMakeRect(r.origin.x + 6, NSMidY(r) - 7, 14, 14), 9.5, gray(0.75),
           NSTextAlignmentLeft);
  drawText(@"▶", NSMakeRect(NSMaxX(r) - 20, NSMidY(r) - 7, 14, 14), 9.5, gray(0.75),
           NSTextAlignmentRight);
  drawText(label, NSMakeRect(r.origin.x, NSMidY(r) - 7, r.size.width, 14), 9.5, gray(0.85),
           NSTextAlignmentCenter, NSFontWeightSemibold);
}

- (void)drawRect:(NSRect)dirty
{
  (void)dirty;
  _lastDrawn = takeSnapshot(_state);

  const int rootKey = _lastDrawn.rootKey;
  const ScaleSelection sel = decodeSelectionLocal(_lastDrawn.selection);
  const auto voicing = static_cast<VoicingType>(_lastDrawn.voicing);

  // Pass-through mode: no control notes held, so no scale is in force and the
  // readouts that describe one go quiet.
  const bool anyHeld = (_lastDrawn.heldControl[0] | _lastDrawn.heldControl[1]) != 0;

  [gray(0.13) setFill];
  NSRectFill([self bounds]);

  // Header: the scale currently in force, and whether a chord is being played.
  const bool holding = (_lastDrawn.chord[0] || _lastDrawn.chord[1]) && rootKey >= 0;

  NSString *headline =
      anyHeld
          ? [NSString stringWithFormat:@"%@ %@", pitchName(sel.keyPc),
                               [NSString stringWithUTF8String:scaleFamilyName(sel.family)]]
          : @"";
  drawText(headline, NSMakeRect(kPad, 14, 420, 26), 20, holding ? gray(0.96) : gray(0.62),
           NSTextAlignmentLeft, NSFontWeightSemibold);

  NSString *subline = [NSString stringWithFormat:@"Voicing: %s", voicingTypeName(voicing)];
  if (rootKey >= 0)
    subline =
        [NSString stringWithFormat:@"Voicing: %s  ·  Top: %@ (MIDI %d)",
                             voicingTypeName(voicing), noteName(rootKey), rootKey];
  drawText(subline, NSMakeRect(kPad, 42, 560, 18), 12, gray(0.62), NSTextAlignmentLeft);

  drawText(holding ? @"playing" : @"ready",
           NSMakeRect([self bounds].size.width - kPad - 200, 18, 200, 18), 11,
           holding ? accent() : gray(0.42), NSTextAlignmentRight);

  // Scale family row: display only; the family is identified by the held control notes.
  drawText(@"SCALE", NSMakeRect(kPad, kFamilyRowY + 9, kLabelWidth, 14), 9.5, gray(0.45),
           NSTextAlignmentLeft, NSFontWeightSemibold);
  for (int i = 0; i < static_cast<int>(ScaleFamily::Count); ++i)
    [self drawButton:familyButtonRect(i, [self bounds].size.width)
               title:[NSString stringWithUTF8String:scaleFamilyShortName(
                          static_cast<ScaleFamily>(i))]
            selected:anyHeld && i == static_cast<int>(sel.family)
               inert:i != static_cast<int>(sel.family)];

  // Voicing row: the only selectable stacking right now.
  drawText(@"VOICING", NSMakeRect(kPad, kVoicingRowY + 9, kLabelWidth, 14), 9.5, gray(0.45),
           NSTextAlignmentLeft, NSFontWeightSemibold);
  for (int i = 0; i < static_cast<int>(VoicingType::Count); ++i)
    [self drawButton:voicingButtonRect(i, [self bounds].size.width)
               title:[NSString stringWithUTF8String:voicingTypeName(
                          static_cast<VoicingType>(i))]
            selected:i == _lastDrawn.voicing
               inert:NO];

  // Borrow rows: each of the top three voices, Prev / None / Next.
  static const char *kBorrowRowLabels[3] = {"BORROW TOP", "BORROW 2ND", "BORROW 3RD"};
  static const CGFloat kRowY[3] = {kBorrowTopRowY, kBorrowSecondRowY, kBorrowThirdRowY};
  const int kBorrowRowValues[3] = {_lastDrawn.borrowTop, _lastDrawn.borrowSecond,
                                   _lastDrawn.borrowThird};
  for (int row = 0; row < 3; ++row)
  {
    const int value = kBorrowRowValues[row];
    drawText([NSString stringWithUTF8String:kBorrowRowLabels[row]],
             NSMakeRect(kPad, kRowY[row] + 9, kBorrowLabelWidth, 14), 9.5, gray(0.45),
             NSTextAlignmentLeft, NSFontWeightSemibold);
    for (int i = 0; i < static_cast<int>(BorrowDir::Count); ++i)
      [self drawButton:borrowButtonRect(row, i, [self bounds].size.width)
                 title:[NSString stringWithUTF8String:borrowDirName(static_cast<BorrowDir>(i))]
              selected:i == value
                 inert:NO];
  }

  // Chord keyboard.
  drawText(@"GENERATED CHORD", NSMakeRect(kPad, kChordLabelY, 400, 14), 9.5, gray(0.45),
           NSTextAlignmentLeft, NSFontWeightSemibold);
  [self drawStepper:triggerStartStepperRect([self bounds].size.width)
              label:[NSString stringWithFormat:@"TRIGGER @ %@", noteName(_lastDrawn.triggerStart)]];
  [self drawKeyboardInRect:NSMakeRect(kPad, kChordKeyboardY, [self bounds].size.width - 2 * kPad,
                                     kChordKeyboardHeight)
                       low:kBedLow
                      high:kBedHigh
             controlOctave:NO
               scaleInView:NO];

  // Control octave.
  drawText([NSString stringWithFormat:@"CONTROL OCTAVE  (%@)", noteName(_lastDrawn.controlRoot)],
           NSMakeRect(kPad, kControlLabelY, 400, 14), 9.5, gray(0.45), NSTextAlignmentLeft,
           NSFontWeightSemibold);
  [self drawStepper:controlRootStepperRect([self bounds].size.width)
              label:[NSString stringWithFormat:@"CONTROL @ %@", noteName(_lastDrawn.controlRoot)]];
  [self drawKeyboardInRect:controlKeyboardRect()
                       low:_lastDrawn.controlRoot
                      high:_lastDrawn.controlRoot + 11
             controlOctave:YES
               scaleInView:anyHeld];

  // Legend, in explicit lines so it is never clipped.
  const CGFloat lx = kPad + kControlKeyboardWidth + 26;
  const CGFloat lw = [self bounds].size.width - lx - kPad;
  const CGFloat ly = kControlKeyboardY + 2;
  drawText(@"Held control notes identify the scale", NSMakeRect(lx, ly, lw, 14), 11,
           gray(0.58), NSTextAlignmentLeft);
  drawText(@"(6dim > m6dim > 7dim > 7♭5dim); trigger keys sound", NSMakeRect(lx, ly + 15, lw, 14), 11,
           gray(0.58), NSTextAlignmentLeft);
  drawText(@"the stack with the pressed note on top;", NSMakeRect(lx, ly + 30, lw, 14), 11,
           gray(0.58), NSTextAlignmentLeft);
  drawText(@"out-of-scale notes pass through.", NSMakeRect(lx, ly + 45, lw, 14), 11, gray(0.58),
           NSTextAlignmentLeft);
}

// Steps a zone stepper: the left third moves it down, the right third up, the middle is
// just the readout.
- (int)stepZoneAt:(NSPoint)p rect:(NSRect)r current:(int)current low:(int)low high:(int)high
{
  const double third = r.size.width / 3;
  if (p.x < r.origin.x + third)
    return current - 1;
  if (p.x > NSMaxX(r) - third)
    return current + 1;
  return current;
}

- (void)mouseDown:(NSEvent *)event
{
  const NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
  const CGFloat viewWidth = [self bounds].size.width;

  // The voicing row and the borrow rows are clickable; the family row is a readout,
  // since the held control notes decide it.
  {
    const int count = static_cast<int>(VoicingType::Count);
    for (int i = 0; i < count; ++i)
    {
      if (NSPointInRect(p, voicingButtonRect(i, viewWidth)))
      {
        _state->request(GuiState::Request::Voicing, i);
        [self setNeedsDisplay:YES];
        return;
      }
    }
  }

  {
    static const GuiState::Request kBorrowRowRequests[3] = {GuiState::Request::BorrowTop,
                                                            GuiState::Request::BorrowSecond,
                                                            GuiState::Request::BorrowThird};
    for (int row = 0; row < 3; ++row)
      for (int i = 0; i < static_cast<int>(BorrowDir::Count); ++i)
      {
        if (NSPointInRect(p, borrowButtonRect(row, i, viewWidth)))
        {
          _state->request(kBorrowRowRequests[row], i);
          [self setNeedsDisplay:YES];
          return;
        }
      }
  }

  {
    const NSRect r = triggerStartStepperRect(viewWidth);
    if (NSPointInRect(p, r))
    {
      int start = _state->triggerStart.load(std::memory_order_relaxed);
      start = [self stepZoneAt:p rect:r current:start low:0 high:127];
      if (start < 0) start = 127;
      if (start > 127) start = 0;
      _state->request(GuiState::Request::TriggerStart, start);
      [self setNeedsDisplay:YES];
      return;
    }
  }

  {
    const NSRect r = controlRootStepperRect(viewWidth);
    if (NSPointInRect(p, r))
    {
      int root = _state->controlRoot.load(std::memory_order_relaxed);
      root = [self stepZoneAt:p rect:r current:root low:0 high:115];
      if (root < 0) root = 115;
      if (root > 115) root = 0;
      _state->request(GuiState::Request::ControlRoot, root);
      [self setNeedsDisplay:YES];
      return;
    }
  }
}

@end

namespace bhl
{

void *guiCreate(GuiState *state)
{
  @autoreleasepool
  {
    return [[BhlView alloc] initWithState:state];
  }
}

void guiDestroy(void *view)
{
  if (!view) return;
  @autoreleasepool
  {
    BhlView *v = (BhlView *)view;
    [v stopTimer];
    [v removeFromSuperview];
    [v release];
  }
}

void guiSetParent(void *view, void *parentNSView)
{
  if (!view || !parentNSView) return;
  @autoreleasepool
  {
    BhlView *v = (BhlView *)view;
    NSView *parent = (NSView *)parentNSView;
    [v setFrame:[parent bounds]];
    [v setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [parent addSubview:v];
  }
}

void guiShow(void *view)
{
  if (view) [(BhlView *)view setHidden:NO];
}

void guiHide(void *view)
{
  if (view) [(BhlView *)view setHidden:YES];
}

ViewRect guiStepperRect(StepperId stepper, double viewWidth)
{
  const NSRect r = stepper == StepperId::ControlRoot ? controlRootStepperRect(viewWidth)
                                                     : triggerStartStepperRect(viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

ViewRect guiVoicingButtonRect(int index, double viewWidth)
{
  const NSRect r = voicingButtonRect(index, viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

ViewRect guiBorrowButtonRect(int row, int index, double viewWidth)
{
  if (row < 0 || row > 2) return {};
  const NSRect r = borrowButtonRect(row, index, viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

ViewRect guiControlKeyRect(int key, int controlRoot)
{
  const NSRect area = controlKeyboardRect();
  if (key < controlRoot || key > controlRoot + 11) return {};

  const CGFloat ww = area.size.width / whiteCount(controlRoot, controlRoot + 11);

  if (isWhiteNote(key))
    return {area.origin.x + whitesBefore(controlRoot, key) * ww, area.origin.y, ww - 1.0,
            area.size.height};

  const CGFloat bw = ww * 0.62;
  return {area.origin.x + whitesBefore(controlRoot, key) * ww - bw / 2, area.origin.y, bw,
          area.size.height * 0.62};
}

void guiPreferredSize(const GuiState *state, uint32_t *width, uint32_t *height)
{
  (void)state;
  *width = kViewWidth;
  *height = kViewHeight;
}

}  // namespace bhl
