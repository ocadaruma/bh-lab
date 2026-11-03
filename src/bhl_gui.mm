// Cocoa view for bh-lab: a piano bed showing the chord being generated, and the control
// octave with its assignments drawn onto the keys.
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
constexpr uint32_t kViewHeight = 288;

// The chord keyboard spans C1..C6, which covers the default zones with room either side.
constexpr int kBedLow = 36;
constexpr int kBedHigh = 96;

constexpr CGFloat kPad = 18;
constexpr CGFloat kControlKeyboardWidth = 430;

// The zone steppers sit top-right of the sections they own.
constexpr CGFloat kStepperWidth = 170;

NSRect triggerStartStepperRect(CGFloat viewWidth)
{
  return NSMakeRect(viewWidth - kPad - kStepperWidth, 68, kStepperWidth, 20);
}

NSRect controlRootStepperRect(CGFloat viewWidth)
{
  return NSMakeRect(viewWidth - kPad - kStepperWidth, 176, kStepperWidth, 20);
}

NSRect controlKeyboardRect()
{
  return NSMakeRect(kPad, 194, kControlKeyboardWidth, 70);
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

NSColor *rootAccent()  // The key that was actually pressed.
{
  return [NSColor colorWithCalibratedRed:0.98 green:0.64 blue:0.24 alpha:1.0];
}

NSColor *chordGhostWhite()  // Notes of a held chord that are not sounding.
{
  return [NSColor colorWithCalibratedRed:0.70 green:0.79 blue:0.94 alpha:1.0];
}

NSColor *chordGhostBlack()
{
  return [NSColor colorWithCalibratedRed:0.24 green:0.33 blue:0.50 alpha:1.0];
}

NSColor *selectAccent()  // Latched control-key selection.
{
  return [NSColor colorWithCalibratedRed:0.36 green:0.76 blue:0.55 alpha:1.0];
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

// "Em7", or "Em7/G" when something other than the root is in the bass.
NSString *chordSymbol(int rootKey, Quality quality, int bassNote)
{
  NSString *symbol = [NSString stringWithFormat:@"%@%s", pitchName(rootKey),
                                                chordTypeSymbol(quality)];
  if (bassNote < 0 || ((bassNote - rootKey) % 12 + 12) % 12 == 0) return symbol;
  return [NSString stringWithFormat:@"%@/%@", symbol, pitchName(bassNote)];
}

// Lowest set bit of the held-chord mask, or -1 when nothing is held.
int lowestChordNote(const uint64_t chord[2])
{
  for (int word = 0; word < 2; ++word)
  {
    uint64_t bits = chord[word];
    if (bits == 0) continue;
    int bit = __builtin_ctzll(bits);
    return word * 64 + bit;
  }
  return -1;
}

// What the UI last drew, so the timer can skip redundant redraws.
struct Snapshot
{
  uint64_t sounding[2] = {};
  uint64_t chord[2] = {};
  int rootKey = -2;
  int quality = -1;
  int inversion = -1;
  int controlRoot = -1;
  int triggerStart = -1;

  bool operator==(const Snapshot &o) const
  {
    return sounding[0] == o.sounding[0] && sounding[1] == o.sounding[1] &&
           chord[0] == o.chord[0] && chord[1] == o.chord[1] &&
           rootKey == o.rootKey && quality == o.quality && inversion == o.inversion &&
           controlRoot == o.controlRoot && triggerStart == o.triggerStart;
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
  s.quality = state->quality.load(std::memory_order_relaxed);
  s.inversion = state->inversion.load(std::memory_order_relaxed);
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

// Draws one keyboard. `controlOctave` switches on the quality/inversion labelling.
- (void)drawKeyboardInRect:(NSRect)area
                       low:(int)low
                      high:(int)high
             controlOctave:(BOOL)controlOctave
{
  const int whites = whiteCount(low, high);
  if (whites <= 0) return;

  const CGFloat ww = area.size.width / whites;
  const int rootKey = _state->rootKey.load(std::memory_order_relaxed);
  const int quality = _state->quality.load(std::memory_order_relaxed);
  const int inversion = _state->inversion.load(std::memory_order_relaxed);
  const auto currentQuality = static_cast<Quality>(quality);

  // White keys first; black keys are drawn over them.
  for (int key = low; key <= high; ++key)
  {
    if (!isWhiteNote(key)) continue;

    const CGFloat x = area.origin.x + whitesBefore(low, key) * ww;
    const NSRect r = NSMakeRect(x, area.origin.y, ww - 1.0, area.size.height);

    ControlHit hit = {};
    if (controlOctave) hit = mapControlKey(key - low);

    NSColor *fill = gray(0.90);
    if (!controlOctave && _state->isChordNote(key)) fill = chordGhostWhite();
    if (_state->isSounding(key)) fill = key == rootKey ? rootAccent() : accent();
    if (controlOctave && hit.action == ControlAction::SetQuality && hit.value == quality)
      fill = selectAccent();

    [fill setFill];
    NSRectFill(r);
    [gray(0.45) setFill];
    NSFrameRect(r);

    if (controlOctave && hit.action == ControlAction::SetQuality)
    {
      const bool selected = hit.value == quality;
      drawText([NSString stringWithUTF8String:chordTypeName(static_cast<Quality>(hit.value))],
               NSMakeRect(r.origin.x, NSMaxY(r) - 22, r.size.width, 16), 10.5,
               selected ? gray(1.0) : gray(0.30), NSTextAlignmentCenter,
               selected ? NSFontWeightBold : NSFontWeightRegular);
    }
    else if (!controlOctave && key % 12 == 0)
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

    ControlHit hit = {};
    if (controlOctave) hit = mapControlKey(key - low);

    NSColor *fill = gray(0.17);
    if (!controlOctave && _state->isChordNote(key)) fill = chordGhostBlack();
    if (_state->isSounding(key)) fill = key == rootKey ? rootAccent() : accentDark();
    // A three-note chord has no third inversion, so that key is not offered for one.
    const bool inversionOffered =
        hit.action != ControlAction::SetInversion ||
        inversionAvailable(currentQuality, static_cast<Inversion>(hit.value));

    if (controlOctave && inversionOffered && hit.action == ControlAction::SetInversion &&
        hit.value == inversion)
      fill = selectAccent();

    [fill setFill];
    NSRectFill(r);
    [gray(0.05) setFill];
    NSFrameRect(r);

    if (controlOctave && hit.action == ControlAction::SetInversion)
    {
      const bool selected = inversionOffered && hit.value == inversion;
      drawText([NSString stringWithUTF8String:inversionShortName(static_cast<Inversion>(hit.value))],
               NSMakeRect(r.origin.x - 4, NSMaxY(r) - 16, r.size.width + 8, 13), 9,
               selected ? gray(1.0) : gray(inversionOffered ? 0.72 : 0.30), NSTextAlignmentCenter,
               selected ? NSFontWeightBold : NSFontWeightRegular);
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
  const auto quality = static_cast<Quality>(_lastDrawn.quality);
  const auto inversion = static_cast<Inversion>(_lastDrawn.inversion);

  [gray(0.13) setFill];
  NSRectFill([self bounds]);

  // Header: what the next key press will produce, and whether one is being played.
  const bool holding = (_lastDrawn.chord[0] || _lastDrawn.chord[1]) && rootKey >= 0;

  // The chord as it would be written down: Em7, or Em7/G once something other than the
  // root is in the bass.
  NSString *headline =
      rootKey >= 0 ? chordSymbol(rootKey, quality,
                                 lowestChordNote(_lastDrawn.chord))
                   : [NSString stringWithUTF8String:chordTypeName(quality)];

  drawText(headline, NSMakeRect(kPad, 14, 420, 26), 20, holding ? gray(0.96) : gray(0.62),
           NSTextAlignmentLeft, NSFontWeightSemibold);

  // MIDI numbering is unambiguous; octave naming is not, so give both.
  NSString *rootText =
      rootKey >= 0
          ? [NSString stringWithFormat:@"Root: %@ · MIDI %d", noteName(rootKey), rootKey]
          : @"Root: —";
  drawText([NSString stringWithFormat:@"%@  ·  %s", rootText, inversionName(inversion)],
           NSMakeRect(kPad, 42, 560, 18), 12, gray(0.62), NSTextAlignmentLeft);

  drawText(holding ? @"playing" : @"ready",
           NSMakeRect([self bounds].size.width - kPad - 200, 18, 200, 18), 11,
           holding ? accent() : gray(0.42), NSTextAlignmentRight);

  // Chord keyboard.
  drawText(@"GENERATED CHORD", NSMakeRect(kPad, 72, 400, 14), 9.5, gray(0.45),
           NSTextAlignmentLeft, NSFontWeightSemibold);
  [self drawStepper:triggerStartStepperRect([self bounds].size.width)
              label:[NSString stringWithFormat:@"TRIGGER @ %@", noteName(_lastDrawn.triggerStart)]];
  [self drawKeyboardInRect:NSMakeRect(kPad, 90, [self bounds].size.width - 2 * kPad, 72)
                       low:kBedLow
                      high:kBedHigh
             controlOctave:NO];

  // Control octave.
  drawText([NSString stringWithFormat:@"CONTROL OCTAVE  (%@)", noteName(_lastDrawn.controlRoot)],
           NSMakeRect(kPad, 176, 400, 14), 9.5, gray(0.45), NSTextAlignmentLeft,
           NSFontWeightSemibold);
  [self drawStepper:controlRootStepperRect([self bounds].size.width)
              label:[NSString stringWithFormat:@"CONTROL @ %@", noteName(_lastDrawn.controlRoot)]];
  [self drawKeyboardInRect:controlKeyboardRect()
                       low:_lastDrawn.controlRoot
                      high:_lastDrawn.controlRoot + 11
             controlOctave:YES];

  // Legend.
  const CGFloat lx = kPad + kControlKeyboardWidth + 26;
  const CGFloat lw = [self bounds].size.width - lx - kPad;
  drawText(@"White keys select the chord type, black keys the inversion. They are silent "
           @"and stay in effect until changed.",
           NSMakeRect(lx, 194, lw, 48), 11, gray(0.58), NSTextAlignmentLeft);
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

  // The control octave is playable with the mouse: the same white-keys-are-qualities,
  // black-keys-are-inversions mapping as the keyboard itself.
  {
    const int controlRoot = _state->controlRoot.load(std::memory_order_relaxed);
    const int key = keyAtPoint(p, controlKeyboardRect(), controlRoot, controlRoot + 11);
    if (key >= 0)
    {
      const ControlHit hit = mapControlKey(key - controlRoot);
      if (hit.action == ControlAction::SetQuality)
      {
        _state->request(GuiState::Request::Quality, hit.value);
      }
      else if (hit.action == ControlAction::SetInversion)
      {
        // A chord with three notes has no third inversion; that key does nothing.
        const auto currentQuality =
            static_cast<Quality>(_state->quality.load(std::memory_order_relaxed));
        if (inversionAvailable(currentQuality, static_cast<Inversion>(hit.value)))
          _state->request(GuiState::Request::Inversion, hit.value);
      }
      [self setNeedsDisplay:YES];
      return;
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

ViewRect guiControlKeyRect(int semitoneOffset)
{
  const NSRect area = controlKeyboardRect();
  const CGFloat ww = area.size.width / whiteCount(0, 11);

  if (isWhiteNote(semitoneOffset))
    return {area.origin.x + whitesBefore(0, semitoneOffset) * ww, area.origin.y, ww - 1.0,
            area.size.height};

  const CGFloat bw = ww * 0.62;
  return {area.origin.x + whitesBefore(0, semitoneOffset) * ww - bw / 2, area.origin.y, bw,
          area.size.height * 0.62};
}

void guiPreferredSize(const GuiState *state, uint32_t *width, uint32_t *height)
{
  (void)state;
  *width = kViewWidth;
  *height = kViewHeight;
}

}  // namespace bhl
