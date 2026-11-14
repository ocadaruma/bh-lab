// Cocoa view for bh-lab.
//
// Layout, per the mockup (mockup/ui-mockup.html): a header with the scale in force
// and the playing state; three side-by-side panels -- Borrowing (a 4x3 matrix),
// Voicing (the inversion switch above the voicing switch, single rows, plus the
// contrary-motion toggle), Note Velocity (five faders); a row of control/trigger zone
// steppers; and the 88-key board with the zones drawn by color.
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

constexpr uint32_t kViewWidth = 920;
constexpr uint32_t kViewHeight = 420;

// The keyboard is a full 88-key board (A0..G8), covering every zone with room either
// side.
constexpr int kKeyboardLow = 21;
constexpr int kKeyboardHigh = 108;

// ---- Palette (mockup colours). ----

NSColor *gray(CGFloat v, CGFloat a = 1.0)
{
  return [NSColor colorWithCalibratedWhite:v alpha:a];
}

NSColor *rgb(CGFloat r, CGFloat g, CGFloat b)
{
  return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0];
}

NSColor *viewBg()  // #17181b
{
  return rgb(0.090, 0.094, 0.106);
}

NSColor *panelBg()  // #232428 -- the three panels.
{
  return rgb(0.137, 0.141, 0.157);
}

NSColor *panelFill()  // #1d1e22 -- idle buttons.
{
  return rgb(0.114, 0.118, 0.133);
}

NSColor *lineSoft()  // #2b2c30 -- borders and dividers.
{
  return rgb(0.169, 0.173, 0.188);
}

NSColor *trackBg()  // #141518 -- fader tracks.
{
  return rgb(0.078, 0.082, 0.094);
}

NSColor *textDim()  // #9a9ba1
{
  return rgb(0.604, 0.608, 0.631);
}

NSColor *textFaint()  // #6d6e74
{
  return rgb(0.427, 0.431, 0.455);
}

NSColor *textLit()  // #ececee
{
  return rgb(0.925, 0.925, 0.933);
}

NSColor *accent()  // #e6a23c -- the warm orange: selected buttons, fader fills.
{
  return rgb(0.902, 0.639, 0.235);
}

NSColor *accentDark()  // #b87f27 -- the border of a selected button.
{
  return rgb(0.722, 0.498, 0.153);
}

NSColor *greenLit()  // The control zone's colour, light enough to read as text.
{
  return rgb(0.553, 0.855, 0.682);
}

NSColor *greenFill()  // #6fbf92-ish -- a selected Prev/Next borrow button.
{
  return rgb(0.455, 0.694, 0.439);
}

NSColor *greenDark()  // #3d8259-ish -- its border.
{
  return rgb(0.239, 0.510, 0.353);
}

NSColor *violetFill()  // #a482e6-ish -- the contrary-motion toggle, on.
{
  return rgb(0.553, 0.447, 0.839);
}

NSColor *violetDark()  // #7d57c4-ish -- its border.
{
  return rgb(0.427, 0.302, 0.710);
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

// ---- Geometry: the three panels. ----

constexpr CGFloat kPad = 20;
constexpr CGFloat kPanelGap = 14;
constexpr CGFloat kPanelY = 72;
constexpr CGFloat kPanelH = 196;
constexpr CGFloat kPanelW = (kViewWidth - 2 * kPad - 2 * kPanelGap) / 3;  // 284
constexpr CGFloat kPanelPad = 14;
constexpr CGFloat kInnerW = kPanelW - 2 * kPanelPad;  // 256

CGFloat panelX(int i)
{
  return kPad + i * (kPanelW + kPanelGap);
}

// ---- Voicing panel: the inversion switch (Root | 1st | 2nd | 3rd) above the
// voicing switch (Close | Drop 2 | Drop 3 | Drop 2&4), each a single row of four
// buttons, and below a divider the contrary-motion toggle. ----

constexpr CGFloat kBtnGap = 8;
constexpr CGFloat kSwitchBtnH = 28;
// The switch rows use a tighter gap than the borrow matrix, so the widest label
// (Drop 2&4) has room to breathe.
constexpr CGFloat kSwitchGap = 4;
// Each switch sits under its own small-caps section label, so the inversion reads
// as its own control rather than part of the voicing group.
constexpr CGFloat kInversionLabelY = kPanelY + 32;
constexpr CGFloat kInversionRowY = kPanelY + 48;
constexpr CGFloat kVoicingLabelY = kInversionRowY + kSwitchBtnH + 8;
constexpr CGFloat kVoicingRowY = kVoicingLabelY + 14;

CGFloat switchButtonWidth()
{
  return (kInnerW - 3 * kSwitchGap) / 4;
}

NSRect voicingButtonRect(int index, CGFloat viewWidth)
{
  (void)viewWidth;
  return NSMakeRect(panelX(1) + kPanelPad + index * (switchButtonWidth() + kSwitchGap),
                    kVoicingRowY, switchButtonWidth(), kSwitchBtnH);
}

NSRect inversionButtonRect(int index, CGFloat viewWidth)
{
  (void)viewWidth;
  return NSMakeRect(panelX(1) + kPanelPad + index * (switchButtonWidth() + kSwitchGap),
                    kInversionRowY, switchButtonWidth(), kSwitchBtnH);
}

// The contrary motion switch (its label sits to the right; only the switch is
// clickable).
constexpr CGFloat kToggleWidth = 40;
constexpr CGFloat kToggleHeight = 22;

NSRect contraryToggleRect(CGFloat viewWidth)
{
  (void)viewWidth;
  return NSMakeRect(panelX(1) + kPanelPad, kPanelY + 144, kToggleWidth, kToggleHeight);
}

// ---- Borrowing panel: a 4x3 matrix. The row labels read Top / 2nd / 3rd / 4th; the
// column slots read Prev | None | Next, under a small header row. The four rows are
// a little shorter than the voicing buttons, so the panel keeps its height. ----

constexpr CGFloat kBorrowLabelW = 44;
constexpr CGFloat kBorrowHeadY = kPanelY + 34;
constexpr CGFloat kBorrowRow0Y = kPanelY + 52;
constexpr CGFloat kBorrowBtnH = 28;
constexpr CGFloat kBorrowRowStep = kBorrowBtnH + 4;

CGFloat borrowColWidth()
{
  return (kInnerW - kBorrowLabelW - 2 * kBtnGap) / 3;
}

CGFloat borrowColX(int slot)
{
  return panelX(0) + kPanelPad + kBorrowLabelW + slot * (borrowColWidth() + kBtnGap);
}

NSRect borrowButtonRect(int row, int slot, CGFloat viewWidth)
{
  (void)viewWidth;
  return NSMakeRect(borrowColX(slot), kBorrowRow0Y + row * kBorrowRowStep, borrowColWidth(),
                    kBorrowBtnH);
}

// The slots read Prev | None | Next, left to right.
const int kBorrowSlotDirs[3] = {static_cast<int>(BorrowDir::Prev),
                                static_cast<int>(BorrowDir::None),
                                static_cast<int>(BorrowDir::Next)};

// ---- Note velocity panel: five faders stacked in pitch order (Top, 2nd, 3rd, 4th)
// with the contrary fader below a divider. Click or drag the track to set, in 5% steps;
// double-click resets the note to 100%. ----

constexpr int kVelStepperCount = kStackedChordNotes + 1;
constexpr CGFloat kVelRowH = 24;
constexpr CGFloat kVelTrackH = 8;
constexpr CGFloat kVelLabelW = 56;
constexpr CGFloat kVelValueW = 40;

// Display slot -> note index (0 bottom, 1 second, 2 third, 3 top, 4 contrary). The
// labels count from the top, as in the Drop voicing names.
const int kVelDisplayOrder[kVelStepperCount] = {3, 2, 1, 0, 4};
const char *kVelNames[kVelStepperCount] = {"4TH", "3RD", "2ND", "TOP", "CONTRARY"};

// Row tops: four fader rows, a divider, then the contrary fader.
const CGFloat kVelRowY[kVelStepperCount] = {kPanelY + 36, kPanelY + 65, kPanelY + 94,
                                            kPanelY + 123, kPanelY + 158};

NSRect velTrackRect(int slot, CGFloat viewWidth)
{
  (void)viewWidth;
  const CGFloat x = panelX(2) + kPanelPad + kVelLabelW + 8;
  const CGFloat w = kInnerW - kVelLabelW - 8 - kVelValueW - 8;
  return NSMakeRect(x, kVelRowY[slot] + (kVelRowH - kVelTrackH) / 2, w, kVelTrackH);
}

// The clickable span of a fader row: the track with slack either side.
NSRect velRowRect(int slot, CGFloat viewWidth)
{
  const NSRect t = velTrackRect(slot, viewWidth);
  return NSMakeRect(t.origin.x - 8, kVelRowY[slot], t.size.width + 16, kVelRowH);
}

// The request a note index (0 bottom, 1 second, 2 third, 3 top, 4 contrary) maps to.
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

// ---- Zone steppers: CONTROL @ and TRIGGER @, each [ label | < | value | > ]. ----

constexpr CGFloat kZoneRowY = kPanelY + kPanelH + 14;  // 282
constexpr CGFloat kZoneStepperH = 36;
constexpr CGFloat kZoneLabelW = 74;
constexpr CGFloat kZoneBtnW = 24;
constexpr CGFloat kZoneValueW = 48;
constexpr CGFloat kZoneStepperW = kZoneLabelW + 2 * kZoneBtnW + kZoneValueW;  // 170

NSRect triggerStartStepperRect(CGFloat viewWidth)
{
  (void)viewWidth;
  return NSMakeRect(kPad + kZoneStepperW + kPanelGap, kZoneRowY, kZoneStepperW,
                    kZoneStepperH);
}

NSRect controlRootStepperRect(CGFloat viewWidth)
{
  (void)viewWidth;
  return NSMakeRect(kPad, kZoneRowY, kZoneStepperW, kZoneStepperH);
}

// ---- Keyboard. ----

constexpr CGFloat kKeyboardY = kZoneRowY + kZoneStepperH + 14;  // 332
constexpr CGFloat kKeyboardHeight = 72;

NSRect keyboardRect(CGFloat viewWidth)
{
  return NSMakeRect(kPad, kKeyboardY, viewWidth - 2 * kPad, kKeyboardHeight);
}

void drawText(NSString *text, NSRect rect, CGFloat size, NSColor *color, NSTextAlignment align,
              NSFontWeight weight = NSFontWeightRegular)
{
  NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
  [style setAlignment:align];
  [style setLineBreakMode:NSLineBreakByTruncatingTail];

  NSDictionary *attrs = @{
    NSFontAttributeName : [NSFont monospacedSystemFontOfSize:size weight:weight],
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

// What the UI last drew, so the timer can skip redundant redraws.
struct Snapshot
{
  uint64_t sounding[2] = {};
  uint64_t chord[2] = {};
  int rootKey = -2;
  int selection = -1;
  uint64_t heldControl[2] = {};
  int voicing = -1;
  int inversion = -1;
  int borrowTop = -1;
  int borrowSecond = -1;
  int borrowThird = -1;
  int borrowFourth = -1;
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
           voicing == o.voicing && inversion == o.inversion &&
           borrowTop == o.borrowTop &&
           borrowSecond == o.borrowSecond && borrowThird == o.borrowThird &&
           borrowFourth == o.borrowFourth &&
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
  s.inversion = state->inversion.load(std::memory_order_relaxed);
  s.borrowTop = state->borrowTop.load(std::memory_order_relaxed);
  s.borrowSecond = state->borrowSecond.load(std::memory_order_relaxed);
  s.borrowThird = state->borrowThird.load(std::memory_order_relaxed);
  s.borrowFourth = state->borrowFourth.load(std::memory_order_relaxed);
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

@interface BhlView : NSView
{
  bhl::GuiState *_state;
  NSTimer *_timer;
  Snapshot _lastDrawn;
  int _velDragIndex;  // The fader slot being dragged, -1 when none.
  // The zone stepper button being held: its stepper (0 control, 1 trigger), the step
  // direction, and the repeat timer that keeps stepping while the button is down.
  int _zoneStepIndex;
  int _zoneStepDir;
  NSTimer *_zoneStepTimer;
}
- (id)initWithState:(bhl::GuiState *)state;
- (void)stopTimer;
- (void)drawPanel:(NSRect)r title:(NSString *)title;
- (void)drawSegButton:(NSRect)r title:(NSString *)title selected:(BOOL)selected
               green:(BOOL)green;
- (void)drawSegButton:(NSRect)r title:(NSString *)title selected:(BOOL)selected
               green:(BOOL)green font:(CGFloat)size;
- (void)drawFaderRow:(int)slot value:(double)v;
- (void)drawZoneStepper:(NSRect)r label:(NSString *)label labelColor:(NSColor *)color
                  value:(NSString *)value;
- (void)velSetFromPoint:(NSPoint)p slot:(int)slot;
- (void)zoneStep:(int)index step:(int)step;
- (void)startZoneRepeat:(int)index step:(int)step;
- (void)stopZoneRepeat;
- (void)zoneStepTick:(NSTimer *)timer;
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
  _velDragIndex = -1;
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
  [self stopZoneRepeat];
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

// One of the three panels: a rounded card with a small-caps title.
- (void)drawPanel:(NSRect)r title:(NSString *)title
{
  NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:r xRadius:10 yRadius:10];
  [panelBg() setFill];
  [path fill];
  [lineSoft() setStroke];
  [path setLineWidth:1];
  [path stroke];
  drawText(title, NSMakeRect(r.origin.x + kPanelPad, r.origin.y + 12,
                             r.size.width - 2 * kPanelPad, 14), 9.5, textFaint(),
           NSTextAlignmentLeft, NSFontWeightSemibold);
}

// A button in the voicing panel or the borrow matrix: dark when idle, warm orange
// when selected (green for an active Prev/Next, which moves a voice sideways).
- (void)drawSegButton:(NSRect)r title:(NSString *)title selected:(BOOL)selected
               green:(BOOL)green
{
  [self drawSegButton:r title:title selected:selected green:green font:12];
}

- (void)drawSegButton:(NSRect)r title:(NSString *)title selected:(BOOL)selected
               green:(BOOL)green font:(CGFloat)size
{
  NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:r xRadius:7 yRadius:7];
  if (selected)
  {
    [green ? greenFill() : accent() setFill];
    [path fill];
    // NB: after attributed text drawing the context's stroke color is left as the text
    // color, so set the stroke explicitly rather than relying on the fill.
    [green ? greenDark() : accentDark() setStroke];
  }
  else
  {
    [panelFill() setFill];
    [path fill];
    [lineSoft() setStroke];
  }
  [path setLineWidth:1];
  [path stroke];
  // A box exactly as tall as the font's line height, centered: drawInRect puts the
  // baseline at the box's top plus the ascender, so this centers the glyphs --
  // a fixed-height box would leave stray space below smaller fonts. NB: the
  // factory returns an autoreleased object, so it is not released (unlike the
  // [NSFont fontWithName:size:] style constructors).
  NSFont *font = [NSFont monospacedSystemFontOfSize:size
                                             weight:selected ? NSFontWeightSemibold
                                                            : NSFontWeightRegular];
  const CGFloat lineH = font.ascender - font.descender;
  drawText(title, NSMakeRect(r.origin.x, NSMidY(r) - lineH / 2, r.size.width, lineH), size,
           selected ? rgb(0.10, 0.07, 0.03) : textDim(), NSTextAlignmentCenter,
           selected ? NSFontWeightSemibold : NSFontWeightRegular);
}

// One fader row: label, a track filled up to the knob, the knob, and the percentage
// readout.
- (void)drawFaderRow:(int)slot value:(double)v
{
  const NSRect t = velTrackRect(slot, [self bounds].size.width);
  const CGFloat y = kVelRowY[slot];

  drawText([NSString stringWithUTF8String:kVelNames[kVelDisplayOrder[slot]]],
           NSMakeRect(panelX(2) + kPanelPad, y + (kVelRowH - 14) / 2, kVelLabelW, 14), 9.5,
           textFaint(), NSTextAlignmentRight, NSFontWeightSemibold);

  NSBezierPath *track = [NSBezierPath bezierPathWithRoundedRect:t xRadius:4 yRadius:4];
  [trackBg() setFill];
  [track fill];
  [lineSoft() setStroke];
  [track setLineWidth:1];
  [track stroke];

  const double frac = std::max(0.0, std::min(1.0, v / 2.0));
  if (frac > 0.0)
  {
    NSGraphicsContext *context = [NSGraphicsContext currentContext];
    [context saveGraphicsState];
    [[NSBezierPath bezierPathWithRoundedRect:
             NSMakeRect(t.origin.x, t.origin.y, frac * t.size.width, t.size.height)
             xRadius:3 yRadius:3]
        addClip];
    [accent() setFill];
    NSRectFill(t);
    [context restoreGraphicsState];
  }

  const CGFloat cx = t.origin.x + frac * t.size.width;
  NSRect knob = NSMakeRect(cx - 6, y, 12, kVelRowH);
  NSBezierPath *knobPath = [NSBezierPath bezierPathWithRoundedRect:knob xRadius:3 yRadius:3];
  [gray(0.93) setFill];
  [knobPath fill];
  [gray(0.56) setStroke];
  [knobPath setLineWidth:1];
  [knobPath stroke];
  [gray(0.53) setFill];
  NSRectFill(NSMakeRect(cx - 0.5, NSMidY(knob) - 6, 1, 12));

  drawText([NSString stringWithFormat:@"%.0f%%", v * 100.0],
           NSMakeRect(NSMaxX(t) + 8, y + (kVelRowH - 14) / 2, kVelValueW, 14), 10.5, textLit(),
           NSTextAlignmentRight, NSFontWeightSemibold);
}

// One zone stepper: [ LABEL @ | < | value | > ].
- (void)drawZoneStepper:(NSRect)r label:(NSString *)label labelColor:(NSColor *)color
                  value:(NSString *)value
{
  NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:r xRadius:8 yRadius:8];
  [panelBg() setFill];
  [path fill];
  [lineSoft() setStroke];
  [path setLineWidth:1];
  [path stroke];

  drawText(label, NSMakeRect(r.origin.x + 12, NSMidY(r) - 7, kZoneLabelW, 14), 9.5, color,
           NSTextAlignmentLeft, NSFontWeightSemibold);

  // The separators between label / buttons / value.
  [lineSoft() setFill];
  const CGFloat leftBtn = r.origin.x + kZoneLabelW;
  const CGFloat rightBtn = NSMaxX(r) - kZoneBtnW;
  NSRectFill(NSMakeRect(leftBtn, r.origin.y + 8, 1, r.size.height - 16));
  NSRectFill(NSMakeRect(rightBtn, r.origin.y + 8, 1, r.size.height - 16));

  drawText(@"\u2039", NSMakeRect(leftBtn, NSMidY(r) - 8, kZoneBtnW, 16), 13, textDim(),
           NSTextAlignmentCenter, NSFontWeightSemibold);
  drawText(@"\u203a", NSMakeRect(rightBtn, NSMidY(r) - 8, kZoneBtnW, 16), 13, textDim(),
           NSTextAlignmentCenter, NSFontWeightSemibold);
  drawText(value, NSMakeRect(leftBtn + kZoneBtnW, NSMidY(r) - 8, kZoneValueW, 16), 12.5,
           textLit(), NSTextAlignmentCenter, NSFontWeightSemibold);
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

  [viewBg() setFill];
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
           holding ? greenLit() : gray(0.42), NSTextAlignmentRight);

  // ---- The three panels. ----

  [self drawPanel:NSMakeRect(panelX(0), kPanelY, kPanelW, kPanelH) title:@"BORROWING"];
  // No panel title here: the INVERSION / VOICING section labels name the contents.
  [self drawPanel:NSMakeRect(panelX(1), kPanelY, kPanelW, kPanelH) title:@""];
  [self drawPanel:NSMakeRect(panelX(2), kPanelY, kPanelW, kPanelH) title:@"NOTE VELOCITY"];

  // Voicing: the inversion switch under its own label, then the voicing switch
  // under its own (single rows of four buttons each, so a smaller font than the
  // borrow matrix). The labels match the panel titles' size.
  drawText(@"INVERSION", NSMakeRect(panelX(1) + kPanelPad, kInversionLabelY, kInnerW, 14),
           9.5, textFaint(), NSTextAlignmentLeft, NSFontWeightSemibold);
  static const char *kInversionLabels[4] = {"Root", "1st", "2nd", "3rd"};
  for (int i = 0; i < static_cast<int>(InversionType::Count); ++i)
    [self drawSegButton:inversionButtonRect(i, [self bounds].size.width)
                  title:[NSString stringWithUTF8String:kInversionLabels[i]]
               selected:i == _lastDrawn.inversion
                  green:NO font:12];

  drawText(@"VOICING", NSMakeRect(panelX(1) + kPanelPad, kVoicingLabelY, kInnerW, 14),
           9.5, textFaint(), NSTextAlignmentLeft, NSFontWeightSemibold);
  static const char *kVoicingLabels[4] = {"Close", "Drop 2", "Drop 3", "Drop 2&4"};
  for (int i = 0; i < static_cast<int>(VoicingType::Count); ++i)
    [self drawSegButton:voicingButtonRect(i, [self bounds].size.width)
                  title:[NSString stringWithUTF8String:kVoicingLabels[i]]
               selected:i == _lastDrawn.voicing
                  green:NO font:10.5];

  // The divider, then the contrary-motion toggle with its label to the right.
  [lineSoft() setFill];
  NSRectFill(NSMakeRect(panelX(0) + kPanelPad, kPanelY + 134, kInnerW, 1));
  {
    const NSRect r = contraryToggleRect([self bounds].size.width);
    const bool on = _lastDrawn.contraryMotion != 0;

    NSBezierPath *track =
        [NSBezierPath bezierPathWithRoundedRect:r xRadius:r.size.height / 2
                                          yRadius:r.size.height / 2];
    // NB: after attributed text drawing the context's stroke color is left as the
    // text color, so set the stroke explicitly rather than relying on the fill.
    [on ? violetFill() : rgb(0.227, 0.231, 0.251) setFill];
    [track fill];
    [on ? violetDark() : rgb(0.275, 0.278, 0.302) setStroke];
    [track setLineWidth:1];
    [track stroke];

    const CGFloat d = r.size.height - 4;  // The knob's diameter.
    const CGFloat kx = on ? NSMaxX(r) - d - 2 : r.origin.x + 2;
    NSBezierPath *knob =
        [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(kx, r.origin.y + 2, d, d)];
    [gray(on ? 1.0 : 0.85) setFill];
    [knob fill];

    drawText(@"Contrary motion", NSMakeRect(NSMaxX(r) + 10, NSMidY(r) - 8, 140, 16), 11.5,
             textDim(), NSTextAlignmentLeft);
  }

  // Borrowing: the header row, then the 4x3 matrix.
  {
    static const char *kBorrowColHead[3] = {"\u25c0 PREV", "NONE", "NEXT \u25b6"};
    static const char *kBorrowRowLabels[4] = {"Top", "2nd", "3rd", "4th"};
    const int kBorrowRowValues[4] = {_lastDrawn.borrowTop, _lastDrawn.borrowSecond,
                                     _lastDrawn.borrowThird, _lastDrawn.borrowFourth};
    for (int slot = 0; slot < 3; ++slot)
      drawText([NSString stringWithUTF8String:kBorrowColHead[slot]],
               NSMakeRect(borrowColX(slot), kBorrowHeadY, borrowColWidth(), 12), 8.5,
               textFaint(), NSTextAlignmentCenter, NSFontWeightSemibold);
    for (int row = 0; row < 4; ++row)
    {
      const int value = kBorrowRowValues[row];
      drawText([NSString stringWithUTF8String:kBorrowRowLabels[row]],
               NSMakeRect(panelX(0) + kPanelPad, kBorrowRow0Y + row * kBorrowRowStep +
                                          (kBorrowBtnH - 14) / 2,
                          kBorrowLabelW - 4, 14), 11, textDim(), NSTextAlignmentRight);
      for (int slot = 0; slot < 3; ++slot)
      {
        const int dir = kBorrowSlotDirs[slot];
        // None renders as a dash, like the mockup.
        NSString *title = dir == static_cast<int>(BorrowDir::None)
                              ? @"\u2014"
                              : [NSString stringWithUTF8String:borrowDirName(
                                     static_cast<BorrowDir>(dir))];
        [self drawSegButton:borrowButtonRect(row, slot, [self bounds].size.width)
                       title:title
                    selected:dir == value
                       green:dir != static_cast<int>(BorrowDir::None)];
      }
    }
  }

  // Note velocity: four faders, a divider, then the contrary fader.
  {
    [lineSoft() setFill];
    NSRectFill(NSMakeRect(panelX(2) + kPanelPad, kPanelY + 152, kInnerW, 1));
    for (int slot = 0; slot < kVelStepperCount; ++slot)
      [self drawFaderRow:slot value:_lastDrawn.vel[kVelDisplayOrder[slot]]];
  }

  // ---- Zone steppers. ----
  [self drawZoneStepper:controlRootStepperRect([self bounds].size.width)
                 label:@"CONTROL @" labelColor:greenLit()
                 value:noteName(_lastDrawn.controlRoot)];
  [self drawZoneStepper:triggerStartStepperRect([self bounds].size.width)
                 label:@"TRIGGER @" labelColor:accent()
                 value:noteName(_lastDrawn.triggerStart)];

  // ---- Keyboard: the single 88-key board with the control and trigger zones. ----
  [self drawKeyboardInRect:keyboardRect([self bounds].size.width)
                       low:kKeyboardLow
                      high:kKeyboardHigh];
}

// Sets a fader to the position the pointer is at, snapped to the 5% step and the
// parameter's 0..2 range.
- (void)velSetFromPoint:(NSPoint)p slot:(int)slot
{
  const NSRect t = velTrackRect(slot, [self bounds].size.width);
  double frac = (p.x - t.origin.x) / t.size.width;
  frac = std::max(0.0, std::min(1.0, frac));
  const double value = std::round(frac * 2.0 / 0.05) * 0.05;
  _state->request(velRequest(kVelDisplayOrder[slot]), value);
}

// What a zone stepper's click does: -1 down, +1 up, 0 just the readout.
- (int)zoneStepAt:(NSPoint)p rect:(NSRect)r
{
  const CGFloat leftBtn = r.origin.x + kZoneLabelW;
  const CGFloat rightBtn = NSMaxX(r) - kZoneBtnW;
  if (p.x >= leftBtn && p.x < leftBtn + kZoneBtnW) return -1;
  if (p.x >= rightBtn) return +1;
  return 0;
}

- (void)mouseDown:(NSEvent *)event
{
  const NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
  const CGFloat viewWidth = [self bounds].size.width;

  // The inversion row.
  {
    const int count = static_cast<int>(InversionType::Count);
    for (int i = 0; i < count; ++i)
    {
      if (NSPointInRect(p, inversionButtonRect(i, viewWidth)))
      {
        _state->request(GuiState::Request::Inversion, i);
        [self setNeedsDisplay:YES];
        return;
      }
    }
  }

  // The voicing row.
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

  // The borrow matrix.
  {
    static const GuiState::Request kBorrowRowRequests[4] = {GuiState::Request::BorrowTop,
                                                            GuiState::Request::BorrowSecond,
                                                            GuiState::Request::BorrowThird,
                                                            GuiState::Request::BorrowFourth};
    for (int row = 0; row < 4; ++row)
      for (int slot = 0; slot < 3; ++slot)
      {
        if (NSPointInRect(p, borrowButtonRect(row, slot, viewWidth)))
        {
          _state->request(kBorrowRowRequests[row], kBorrowSlotDirs[slot]);
          [self setNeedsDisplay:YES];
          return;
        }
      }
  }

  // The contrary motion switch.
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

  // The velocity faders: click or drag sets, a double-click resets the note to 100%.
  {
    for (int slot = 0; slot < kVelStepperCount; ++slot)
    {
      if (!NSPointInRect(p, velRowRect(slot, viewWidth))) continue;

      if ([event clickCount] >= 2)
        _state->request(velRequest(kVelDisplayOrder[slot]), 1.0);
      else
      {
        [self velSetFromPoint:p slot:slot];
        _velDragIndex = slot;
      }
      [self setNeedsDisplay:YES];
      return;
    }
  }

  // The zone steppers: a click steps once, and holding the button keeps stepping
  // until release.
  {
    const NSRect r = triggerStartStepperRect(viewWidth);
    const int step = [self zoneStepAt:p rect:r];
    if (step != 0 && NSPointInRect(p, r))
    {
      [self zoneStep:1 step:step];
      [self startZoneRepeat:1 step:step];
      return;
    }
  }

  {
    const NSRect r = controlRootStepperRect(viewWidth);
    const int step = [self zoneStepAt:p rect:r];
    if (step != 0 && NSPointInRect(p, r))
    {
      [self zoneStep:0 step:step];
      [self startZoneRepeat:0 step:step];
      return;
    }
  }
}

- (void)mouseDragged:(NSEvent *)event
{
  if (_velDragIndex < 0) return;
  const NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
  [self velSetFromPoint:p slot:_velDragIndex];
  [self setNeedsDisplay:YES];
}

// Steps one zone stepper by -1 / +1, wrapping around the parameter's range.
// (Index 0 is the control zone root, 1 the trigger zone start.)
- (void)zoneStep:(int)index step:(int)step
{
  if (index == 1)
  {
    int start = _state->triggerStart.load(std::memory_order_relaxed) + step;
    if (start < 0) start = 127;
    if (start > 127) start = 0;
    _state->request(GuiState::Request::TriggerStart, start);
  }
  else
  {
    int root = _state->controlRoot.load(std::memory_order_relaxed) + step;
    if (root < 0) root = 115;
    if (root > 115) root = 0;
    _state->request(GuiState::Request::ControlRoot, root);
  }
  [self setNeedsDisplay:YES];
}

// The held zone-stepper button keeps stepping until it is released, like a held key
// on a keyboard: the first step lands on the click itself, then this timer takes over
// after a short delay. The timer runs in common modes so it fires while the mouse is
// still down and the run loop is tracking it.
- (void)startZoneRepeat:(int)index step:(int)step
{
  [self stopZoneRepeat];
  _zoneStepIndex = index;
  _zoneStepDir = step;
  _zoneStepTimer =
      [[NSTimer alloc] initWithFireDate:[NSDate dateWithTimeIntervalSinceNow:0.35]
                                interval:0.1
                                  target:self
                                selector:@selector(zoneStepTick:)
                                userInfo:nil
                                 repeats:YES];
  [_zoneStepTimer retain];
  [[NSRunLoop currentRunLoop] addTimer:_zoneStepTimer forMode:NSRunLoopCommonModes];
}

- (void)stopZoneRepeat
{
  if (_zoneStepTimer)
  {
    [_zoneStepTimer invalidate];
    [_zoneStepTimer release];
    _zoneStepTimer = nil;
  }
  _zoneStepIndex = -1;
  _zoneStepDir = 0;
}

- (void)zoneStepTick:(NSTimer *)timer
{
  (void)timer;
  if (_zoneStepIndex >= 0) [self zoneStep:_zoneStepIndex step:_zoneStepDir];
}

- (void)mouseUp:(NSEvent *)event
{
  (void)event;
  _velDragIndex = -1;
  [self stopZoneRepeat];
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

ViewRect guiInversionButtonRect(int index, double viewWidth)
{
  const NSRect r = inversionButtonRect(index, viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

ViewRect guiBorrowButtonRect(int row, int index, double viewWidth)
{
  // The index is the borrow direction (None, Prev, Next); the display slots read
  // Prev | None | Next.
  if (row < 0 || row > 3) return {};
  if (index < 0 || index >= static_cast<int>(BorrowDir::Count)) return {};
  const int slot = index == static_cast<int>(BorrowDir::Prev) ? 0
                      : index == static_cast<int>(BorrowDir::None) ? 1 : 2;
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
  // display slot: the slots read TOP | 2ND | 3RD | 4TH | CONTRARY (labels count from
  // the top, as in the Drop voicing names).
  if (index < 0 || index >= kVelStepperCount) return {};
  int slot = 0;
  while (slot < kVelStepperCount && kVelDisplayOrder[slot] != index) ++slot;
  const NSRect r = velTrackRect(slot, viewWidth);
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

void guiPreferredSize(const GuiState *state, uint32_t *width, uint32_t *height)
{
  (void)state;
  *width = kViewWidth;
  *height = kViewHeight;
}

}  // namespace bhl
