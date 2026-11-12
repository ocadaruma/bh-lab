// Cocoa view for bh-lab: the selected scale in the header, and a single 88-key board
// in which the control zone, the trigger zone, and the active scale / chord / notes
// are drawn by color.
//
// Compiled without ARC (the CMake default for .mm), so the view is retained and released
// manually by guiCreate/guiDestroy.

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "bhl_gui.h"
#include "chord_engine.h"

using namespace bhl;

namespace
{

constexpr uint32_t kViewWidth = 810;
constexpr uint32_t kViewHeight = 394;

// The keyboard is a full 88-key board (A0..G8), covering every zone with room either
// side.
constexpr int kKeyboardLow = 21;
constexpr int kKeyboardHigh = 108;

constexpr CGFloat kPad = 18;
constexpr CGFloat kLabelWidth = 60;
constexpr CGFloat kBorrowLabelWidth = 82;
constexpr CGFloat kStepperWidth = 170;
constexpr CGFloat kButtonGap = 8;

// Row geometry.
constexpr CGFloat kVoicingRowY = 72;
constexpr CGFloat kBorrowTopRowY = 108;
constexpr CGFloat kBorrowSecondRowY = 144;
constexpr CGFloat kBorrowThirdRowY = 180;
constexpr CGFloat kContraryRowY = 216;
constexpr CGFloat kVelocityRowY = 252;
constexpr CGFloat kRowHeight = 30;

// The keyboard sits a gap below the stacking rows: it controls the zones, which is a
// different role than the voicing / borrow picks above.
constexpr CGFloat kKeyboardLabelY = 288;
constexpr CGFloat kKeyboardY = 304;
constexpr CGFloat kKeyboardHeight = 72;

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

// The contrary motion row: a single toggle switch, off by default.
constexpr CGFloat kToggleWidth = 92;
constexpr CGFloat kToggleHeight = 22;

// The velocity row: one percentage stepper per generated note of one trigger --
// the contrary note first, then the four chord notes low to high.
constexpr int kVelStepperCount = kStackedChordNotes + 1;
constexpr CGFloat kVelStepperHeight = 24;

// The note indices behind the five steppers: 0 bottom, 1 second, 2 third, 3 top,
// 4 contrary note (the same order as the parameters). The slots read left to right
// with the contrary note first.
const char *kVelNames[kVelStepperCount] = {"BOTTOM", "2ND", "3RD", "TOP", "CONTR."};
const int kVelDisplayOrder[kVelStepperCount] = {4, 0, 1, 2, 3};

NSRect velStepperRect(int index, CGFloat viewWidth)
{
  const CGFloat x0 = kPad + kLabelWidth + 6;
  const CGFloat total = viewWidth - kPad - x0;
  const CGFloat w = (total - kButtonGap * (kVelStepperCount - 1)) / kVelStepperCount;
  return NSMakeRect(x0 + index * (w + kButtonGap),
                    kVelocityRowY + (kRowHeight - kVelStepperHeight) / 2, w,
                    kVelStepperHeight);
}

NSRect contraryToggleRect(CGFloat viewWidth)
{
  const CGFloat x0 = kPad + kLabelWidth + 6;
  return NSMakeRect(x0, kContraryRowY + (kRowHeight - kToggleHeight) / 2, kToggleWidth,
                    kToggleHeight);
}

// The borrow buttons read Prev | None | Next, left to right.
const int kBorrowDisplayOrder[3] = {static_cast<int>(BorrowDir::Prev),
                                    static_cast<int>(BorrowDir::None),
                                    static_cast<int>(BorrowDir::Next)};

// The two zone steppers share the keyboard label row, side by side on the right: the
// control start on the left, the trigger start at the far right.
NSRect triggerStartStepperRect(CGFloat viewWidth)
{
  return NSMakeRect(viewWidth - kPad - kStepperWidth, kKeyboardLabelY - 2, kStepperWidth,
                    20);
}

NSRect controlRootStepperRect(CGFloat viewWidth)
{
  return NSMakeRect(viewWidth - kPad - kStepperWidth * 2 - kButtonGap, kKeyboardLabelY - 2,
                    kStepperWidth, 20);
}

NSRect keyboardRect(CGFloat viewWidth)
{
  return NSMakeRect(kPad, kKeyboardY, viewWidth - 2 * kPad, kKeyboardHeight);
}

NSColor *gray(CGFloat v, CGFloat a = 1.0)
{
  return [NSColor colorWithCalibratedWhite:v alpha:a];
}

NSColor *rgb(CGFloat r, CGFloat g, CGFloat b)
{
  return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0];
}

NSColor *accent()  // Sounding notes in the plain zone; general UI accent.
{
  return rgb(0.27, 0.56, 0.96);
}

// A key's zone: the control octave, else everything at or above the trigger start,
// else the plain zone. The control zone wins where the two overlap.
enum class Zone
{
  Normal,
  Control,
  Trigger
};

NSColor *zoneBase(Zone zone, bool white)  // Idle keys, tinted by zone.
{
  switch (zone)
  {
    case Zone::Control: return white ? rgb(0.80, 0.91, 0.83) : rgb(0.22, 0.38, 0.27);
    case Zone::Trigger: return white ? rgb(0.98, 0.92, 0.77) : rgb(0.53, 0.41, 0.16);
    default:            return gray(white ? 0.90 : 0.17);
  }
}

NSColor *zoneActive(Zone zone)  // Chord notes, pressed or generated alike; the scale in
                               // the control zone.
{
  switch (zone)
  {
    case Zone::Control: return rgb(0.19, 0.66, 0.38);
    case Zone::Trigger: return rgb(0.94, 0.58, 0.14);
    default:            return rgb(0.27, 0.56, 0.96);
  }
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

// The velocity atomic behind a stepper index (0 bottom, 1 second, 2 third, 3 top,
// 4 contrary), so the row's code can treat the five values uniformly.
const std::atomic<double> *velValue(GuiState *state, int index)
{
  switch (index)
  {
    case 0: return &state->velBottom;
    case 1: return &state->velSecond;
    case 2: return &state->velThird;
    case 3: return &state->velTop;
    default: return &state->velContrary;
  }
}

GuiState::Request velRequest(int index)
{
  switch (index)
  {
    case 0: return GuiState::Request::VelBottom;
    case 1: return GuiState::Request::VelSecond;
    case 2: return GuiState::Request::VelThird;
    case 3: return GuiState::Request::VelTop;
    default: return GuiState::Request::VelContrary;
  }
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
  int contraryMotion = -1;
  int controlRoot = -1;
  int triggerStart = -1;
  double vel[kVelStepperCount] = {};

  bool operator==(const Snapshot &o) const
  {
    return sounding[0] == o.sounding[0] && sounding[1] == o.sounding[1] &&
           chord[0] == o.chord[0] && chord[1] == o.chord[1] &&
           rootKey == o.rootKey && selection == o.selection &&
           heldControl[0] == o.heldControl[0] && heldControl[1] == o.heldControl[1] &&
           voicing == o.voicing && borrowTop == o.borrowTop &&
           borrowSecond == o.borrowSecond && borrowThird == o.borrowThird &&
           contraryMotion == o.contraryMotion &&
           controlRoot == o.controlRoot &&
           triggerStart == o.triggerStart &&
           vel[0] == o.vel[0] && vel[1] == o.vel[1] && vel[2] == o.vel[2] &&
           vel[3] == o.vel[3] && vel[4] == o.vel[4];
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
  s.contraryMotion = state->contraryMotion.load(std::memory_order_relaxed);
  s.controlRoot = state->controlRoot.load(std::memory_order_relaxed);
  s.triggerStart = state->triggerStart.load(std::memory_order_relaxed);
  s.vel[0] = state->velBottom.load(std::memory_order_relaxed);
  s.vel[1] = state->velSecond.load(std::memory_order_relaxed);
  s.vel[2] = state->velThird.load(std::memory_order_relaxed);
  s.vel[3] = state->velTop.load(std::memory_order_relaxed);
  s.vel[4] = state->velContrary.load(std::memory_order_relaxed);
  return s;
}

}  // namespace

@interface BhlView : NSView <NSTextFieldDelegate>
{
  bhl::GuiState *_state;
  NSTimer *_timer;
  Snapshot _lastDrawn;
  NSTextField *_velField;  // The inline editor over a velocity stepper, nil when not editing.
  int _velFieldIndex;
}
- (id)initWithState:(bhl::GuiState *)state;
- (void)stopTimer;
- (void)beginVelEdit:(int)index;
- (void)endVelEdit;
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

// Dismounts the inline editor. The typed text is a percentage of the trigger note's
// velocity (a trailing "%" is fine); anything unparseable is ignored and the current
// value stands.
- (void)endVelEdit
{
  if (!_velField) return;

  const char *cstr = [_velField stringValue].UTF8String;
  char *end = nullptr;
  const double raw = cstr ? strtod(cstr, &end) : -1.0;
  if (cstr && end != cstr && raw >= 0.0)
    _state->request(velRequest(_velFieldIndex), raw / 100.0);

  [_velField removeFromSuperview];
  [_velField release];
  _velField = nil;
  _velFieldIndex = -1;
  [self setNeedsDisplay:YES];
}

// Puts an inline editor over a velocity stepper, pre-filled with the current
// percentage, so a value can be typed directly. Return -- or a click elsewhere, which
// ends the edit -- commits it.
- (void)beginVelEdit:(int)index
{
  if (_velField) return;
  _velFieldIndex = index;

  const NSRect r = NSInsetRect(velStepperRect(index, [self bounds].size.width), 1, 1);
  _velField = [[NSTextField alloc] initWithFrame:r];
  [_velField setAlignment:NSTextAlignmentCenter];
  [_velField setFont:[NSFont systemFontOfSize:9.5 weight:NSFontWeightSemibold]];
  [_velField setDelegate:self];
  [_velField setStringValue:[NSString stringWithFormat:@"%.0f",
                             velValue(_state, index)->load(std::memory_order_relaxed) *
                                 100.0]];
  [self addSubview:_velField];
  if ([self window]) [[self window] makeFirstResponder:_velField];
  [_velField selectText:nil];
}

- (void)dealloc
{
  [self stopTimer];
  if (_velField)
  {
    [_velField removeFromSuperview];
    [_velField release];
    _velField = nil;
  }
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

// A borrow direction button: Prev / Next are drawn as arrows pointing their way, None
// as a plain button.
- (void)drawBorrowButton:(NSRect)r dir:(int)dir selected:(BOOL)selected
{
  if (dir == static_cast<int>(BorrowDir::None))
  {
    [self drawButton:r title:@"None" selected:selected inert:NO];
    return;
  }

  const CGFloat tip = 12;
  // Inset by half the stroke width so the stroked edge lands exactly on the rect,
  // like NSFrameRect does for the plain buttons.
  const NSRect ir = NSInsetRect(r, 0.5, 0.5);
  NSBezierPath *path = [NSBezierPath bezierPath];
  if (dir == static_cast<int>(BorrowDir::Prev))
  {
    [path moveToPoint:NSMakePoint(ir.origin.x + tip, ir.origin.y)];
    [path lineToPoint:NSMakePoint(NSMaxX(ir), ir.origin.y)];
    [path lineToPoint:NSMakePoint(NSMaxX(ir), NSMaxY(ir))];
    [path lineToPoint:NSMakePoint(ir.origin.x + tip, NSMaxY(ir))];
    [path lineToPoint:NSMakePoint(ir.origin.x, NSMidY(ir))];
  }
  else
  {
    [path moveToPoint:NSMakePoint(ir.origin.x, ir.origin.y)];
    [path lineToPoint:NSMakePoint(NSMaxX(ir) - tip, ir.origin.y)];
    [path lineToPoint:NSMakePoint(NSMaxX(ir), NSMidY(ir))];
    [path lineToPoint:NSMakePoint(NSMaxX(ir) - tip, NSMaxY(ir))];
    [path lineToPoint:NSMakePoint(ir.origin.x, NSMaxY(ir))];
  }
  [path closePath];

  NSColor *fill = selected ? accent() : gray(0.28);
  [fill setFill];
  [path fill];
  // NB: after attributed text drawing the context's stroke color is left as the text
  // color, so set the stroke explicitly rather than relying on the fill.
  [gray(0.10) setStroke];
  [path setLineWidth:1];
  [path stroke];
  drawText(dir == static_cast<int>(BorrowDir::Prev) ? @"Prev" : @"Next",
           NSMakeRect(r.origin.x, NSMidY(r) - 7, r.size.width, 14), 10.5,
           selected ? gray(1.0) : gray(0.72), NSTextAlignmentCenter,
           selected ? NSFontWeightSemibold : NSFontWeightRegular);
}

// The fill for one key: notes generated by the trigger read out in the trigger zone's
// orange everywhere, so they stay distinct from the scale's green inside the control
// zone; pass-through presses read out in their own zone's active color; in the control
// zone the held notes and the scale itself share that zone's highlight.
- (NSColor *)fillForKey:(int)key zone:(Zone)zone
{
  const ScaleSelection sel =
      decodeSelectionLocal(_state->selection.load(std::memory_order_relaxed));
  const bool anyHeld =
      (_state->heldControl[0].load(std::memory_order_relaxed) |
       _state->heldControl[1].load(std::memory_order_relaxed)) != 0;

  if (_state->isChordNote(key)) return zoneActive(Zone::Trigger);
  if (_state->isSounding(key)) return zoneActive(zone);

  if (zone == Zone::Control &&
      (_state->isControlHeld(key) || (anyHeld && inScale(key % 12, sel))))
    return zoneActive(Zone::Control);

  return zoneBase(zone, isWhiteNote(key));
}

// The horizontal span of a key range, in the keyboard's white-key geometry.
NSRect zoneSpan(NSRect area, int low, int high, int lo, int hi)
{
  const int whites = whiteCount(low, high);
  const CGFloat ww = area.size.width / whites;
  const CGFloat xStart = area.origin.x + std::min(whites, whitesBefore(low, lo)) * ww;
  const CGFloat xEnd = area.origin.x + std::min(whites, whitesBefore(low, hi + 1)) * ww;
  return NSMakeRect(xStart, 0, xEnd - xStart, 0);
}

// Draws the single 88-key board: the control and trigger zones are tinted, a colored
// bar marks each zone's span along the top, and the active notes are highlighted per
// zone.
- (void)drawKeyboardInRect:(NSRect)area low:(int)low high:(int)high
{
  const int whites = whiteCount(low, high);
  if (whites <= 0) return;

  const CGFloat ww = area.size.width / whites;
  const int controlRoot = _state->controlRoot.load(std::memory_order_relaxed);
  const int triggerStart = _state->triggerStart.load(std::memory_order_relaxed);

  auto zoneOfKey = [controlRoot, triggerStart](int key) -> Zone
  {
    if (key >= controlRoot && key <= controlRoot + 11) return Zone::Control;
    if (key >= triggerStart) return Zone::Trigger;
    return Zone::Normal;
  };

  // White keys first; black keys are drawn over them.
  for (int key = low; key <= high; ++key)
  {
    if (!isWhiteNote(key)) continue;

    const NSRect r =
        NSMakeRect(area.origin.x + whitesBefore(low, key) * ww, area.origin.y, ww - 1.0,
                   area.size.height);
    NSColor *fill = [self fillForKey:key zone:zoneOfKey(key)];
    [fill setFill];
    NSRectFill(r);
    [gray(0.45) setFill];
    NSFrameRect(r);
  }

  const CGFloat bw = ww * 0.62;
  const CGFloat bh = area.size.height * 0.62;

  for (int key = low; key <= high; ++key)
  {
    if (isWhiteNote(key)) continue;

    const NSRect r = NSMakeRect(area.origin.x + whitesBefore(low, key) * ww - bw / 2.0,
                                area.origin.y, bw, bh);
    NSColor *fill = [self fillForKey:key zone:zoneOfKey(key)];
    [fill setFill];
    NSRectFill(r);
    [gray(0.05) setFill];
    NSFrameRect(r);
  }

  // Zone bars along the top edge, so the zones read at a glance. The trigger bar is
  // split where the control octave sits on top of it.
  const int cLo = std::max(controlRoot, low);
  const int cHi = std::min(controlRoot + 11, high);
  if (cLo <= cHi)
  {
    NSRect span = zoneSpan(area, low, high, cLo, cHi);
    [zoneActive(Zone::Control) setFill];
    NSRectFill(NSMakeRect(span.origin.x, area.origin.y, span.size.width, 6));
  }

  if (triggerStart >= low && triggerStart <= high)
  {
    NSColor *bar = zoneActive(Zone::Trigger);
    [bar setFill];
    auto fillBar = [&](int lo, int hi)
    {
      if (lo > hi) return;
      NSRect span = zoneSpan(area, low, high, std::max(lo, low), std::min(hi, high));
      NSRectFill(NSMakeRect(span.origin.x, area.origin.y, span.size.width, 6));
    };
    fillBar(std::max(triggerStart, low), cLo - 1);
    fillBar(std::max(triggerStart, cHi + 1), high);
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

  NSString *subline;
  if (holding)
    subline = [NSString stringWithFormat:@"Voicing: %s  ·  Top: %@ (MIDI %d)",
                          voicingTypeName(voicing), noteName(rootKey), rootKey];
  else
    subline = @"";  // Nothing is sounding: the voicing / top readout goes quiet.
  drawText(subline, NSMakeRect(kPad, 42, 560, 18), 12, gray(0.62), NSTextAlignmentLeft);

  drawText(holding ? @"playing" : @"ready",
           NSMakeRect([self bounds].size.width - kPad - 200, 18, 200, 18), 11,
           holding ? accent() : gray(0.42), NSTextAlignmentRight);

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
    for (int slot = 0; slot < 3; ++slot)
    {
      const int dir = kBorrowDisplayOrder[slot];
      [self drawBorrowButton:borrowButtonRect(row, slot, [self bounds].size.width)
                         dir:dir
                    selected:dir == value];
    }
  }

  // Contrary motion row: a single on/off toggle switch.
  drawText(@"CONTRARY", NSMakeRect(kPad, kContraryRowY + 9, kLabelWidth, 14), 9.5,
           gray(0.45), NSTextAlignmentLeft, NSFontWeightSemibold);
  {
    const NSRect r = contraryToggleRect([self bounds].size.width);
    const bool on = _lastDrawn.contraryMotion != 0;

    NSBezierPath *track =
        [NSBezierPath bezierPathWithRoundedRect:r xRadius:r.size.height / 2
                                          yRadius:r.size.height / 2];
    // NB: after attributed text drawing the context's stroke color is left as the
    // text color, so set the stroke explicitly rather than relying on the fill.
    [on ? accent() : gray(0.30) setFill];
    [track fill];
    [gray(0.10) setStroke];
    [track setLineWidth:1];
    [track stroke];

    const CGFloat d = r.size.height - 4;  // The knob's diameter.
    const CGFloat kx = on ? NSMaxX(r) - d - 2 : r.origin.x + 2;
    NSBezierPath *knob =
        [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(kx, r.origin.y + 2, d, d)];
    [gray(on ? 1.0 : 0.85) setFill];
    [knob fill];

    // A small readout in the space the knob vacates.
    const CGFloat freeW = r.size.width - (d + 4);
    if (on)
      drawText(@"Off", NSMakeRect(r.origin.x + 2, NSMidY(r) - 7, freeW, 14), 9.5, gray(0.62),
               NSTextAlignmentCenter);
    else
      drawText(@"On", NSMakeRect(r.origin.x + d + 2, NSMidY(r) - 7, freeW, 14), 9.5,
               gray(0.72), NSTextAlignmentCenter);
  }

  // Velocity row: the percentage of the trigger note's velocity for each note one
  // trigger generates -- the contrary note first, then the chord notes low to high.
  drawText(@"VELOCITY", NSMakeRect(kPad, kVelocityRowY + 9, kLabelWidth, 14), 9.5,
           gray(0.45), NSTextAlignmentLeft, NSFontWeightSemibold);
  {
    for (int slot = 0; slot < kVelStepperCount; ++slot)
    {
      const int i = kVelDisplayOrder[slot];
      const NSString *label = [NSString stringWithFormat:@"%@ %.0f%%",
                               [NSString stringWithUTF8String:kVelNames[i]],
                               _lastDrawn.vel[i] * 100.0];
      [self drawStepper:velStepperRect(slot, [self bounds].size.width) label:(NSString *)label];
    }
  }

  // Keyboard: the single 88-key board with the control and trigger zones tinted.
  drawText(@"KEYBOARD", NSMakeRect(kPad, kKeyboardLabelY, 400, 14), 9.5, gray(0.45),
           NSTextAlignmentLeft, NSFontWeightSemibold);
  [self drawStepper:triggerStartStepperRect([self bounds].size.width)
              label:[NSString stringWithFormat:@"TRIGGER @ %@", noteName(_lastDrawn.triggerStart)]];
  [self drawStepper:controlRootStepperRect([self bounds].size.width)
              label:[NSString stringWithFormat:@"CONTROL @ %@", noteName(_lastDrawn.controlRoot)]];
  [self drawKeyboardInRect:keyboardRect([self bounds].size.width)
                       low:kKeyboardLow
                      high:kKeyboardHigh];
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

// Return -- and a click away, which ends the field's edit -- commit the typed value.
- (void)controlTextDidEndEditing:(NSControl *)control
{
  (void)control;
  [self endVelEdit];
}

- (void)mouseDown:(NSEvent *)event
{
  const NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
  const CGFloat viewWidth = [self bounds].size.width;

  // If the inline editor is up and the click lands outside it, end the edit first:
  // the click away commits the typed value, then the click itself is handled as usual.
  if (_velField && !NSPointInRect(p, [_velField frame])) [self endVelEdit];

  // The voicing row and the borrow rows are clickable; the scale itself is a readout,
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
      for (int slot = 0; slot < 3; ++slot)
      {
        if (NSPointInRect(p, borrowButtonRect(row, slot, viewWidth)))
        {
          _state->request(kBorrowRowRequests[row], kBorrowDisplayOrder[slot]);
          [self setNeedsDisplay:YES];
          return;
        }
      }
  }

  {
    const NSRect r = contraryToggleRect(viewWidth);
    if (NSPointInRect(p, r))
    {
      const int current = _state->contraryMotion.load(std::memory_order_relaxed);
      _state->request(GuiState::Request::ContraryMotion, current ? 0 : 1);
      [self setNeedsDisplay:YES];
      return;
    }
  }

  {
    for (int slot = 0; slot < kVelStepperCount; ++slot)
    {
      const int i = kVelDisplayOrder[slot];
      const NSRect r = velStepperRect(slot, viewWidth);
      if (!NSPointInRect(p, r)) continue;

      // The left third steps down 5%, the right third up, the middle is the readout --
      // and takes direct number input: click it to type the percentage.
      const double current = velValue(_state, i)->load(std::memory_order_relaxed);
      const double third = r.size.width / 3;
      if (p.x >= r.origin.x + third && p.x <= NSMaxX(r) - third)
      {
        [self beginVelEdit:i];
        return;
      }
      double next = current;
      if (p.x < r.origin.x + third) next = current - 0.05;
      if (p.x > NSMaxX(r) - third) next = current + 0.05;

      // Snap to the step and stay on the parameter's 0..2 range.
      next = std::round(next / 0.05) * 0.05;
      if (next < 0.0) next = 0.0;
      if (next > 2.0) next = 2.0;
      _state->request(velRequest(i), next);
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

ViewRect guiVoicingButtonRect(int index, double viewWidth)
{
  const NSRect r = voicingButtonRect(index, viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

ViewRect guiBorrowButtonRect(int row, int index, double viewWidth)
{
  // The index is the borrow direction (None, Prev, Next); the display slots read
  // Prev | None | Next.
  if (row < 0 || row > 2) return {};
  if (index < 0 || index >= static_cast<int>(BorrowDir::Count)) return {};
  const int slot = index == 0 ? 1 : index;
  const NSRect r = borrowButtonRect(row, slot, viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

ViewRect guiContraryToggleRect(double viewWidth)
{
  const NSRect r = contraryToggleRect(viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

ViewRect guiVelStepperRect(int index, double viewWidth)
{
  // The index is the note (0 bottom, 1 second, 2 third, 3 top, 4 contrary), not the
  // display slot: the slots read CONTR. | BOTTOM | 2ND | 3RD | TOP.
  if (index < 0 || index >= kVelStepperCount) return {};
  int slot = 0;
  while (slot < kVelStepperCount && kVelDisplayOrder[slot] != index) ++slot;
  const NSRect r = velStepperRect(slot, viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

void guiPreferredSize(const GuiState *state, uint32_t *width, uint32_t *height)
{
  (void)state;
  *width = kViewWidth;
  *height = kViewHeight;
}

}  // namespace bhl
