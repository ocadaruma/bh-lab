// An interactive standalone host for bh-lab: the real plugin core and the real plugin
// view, driven by the computer keyboard, so UI and behaviour changes can be iterated
// in seconds without touching Logic.
//
// Keys (US layout):
//   z x c v b n m , . /   -> C2..B2   (the default control octave)
//   a s d f g h j k l ; '  -> C3..B3   (the default trigger zone)
//   q w e r t y u i o p [ \ -> C4..B4
// ESC or closing the window quits. Generated notes are printed to the terminal.
//
// Usage: dev-host

#import <Cocoa/Cocoa.h>

#include <cstdio>

#include "bhl_gui.h"
#include "clap_test_host.h"

using namespace bhl;

namespace
{

const char *kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

int noteForKey(NSEvent *event)
{
  NSString *chars = event.charactersIgnoringModifiers;
  if (chars.length != 1) return -1;
  const unichar c = [chars characterAtIndex:0];

  int note = -1;
  switch (c)
  {
    // Home row: the default trigger zone.
    case 'a': note = 60; break;
    case 's': note = 61; break;
    case 'd': note = 62; break;
    case 'f': note = 63; break;
    case 'g': note = 64; break;
    case 'h': note = 65; break;
    case 'j': note = 66; break;
    case 'k': note = 67; break;
    case 'l': note = 68; break;
    case ';': note = 69; break;
    case '\'': note = 70; break;
    // Top row: above the trigger zone.
    case 'q': note = 72; break;
    case 'w': note = 73; break;
    case 'e': note = 74; break;
    case 'r': note = 75; break;
    case 't': note = 76; break;
    case 'y': note = 77; break;
    case 'u': note = 78; break;
    case 'i': note = 79; break;
    case 'o': note = 80; break;
    case 'p': note = 81; break;
    case '[': note = 82; break;
    case '\\': note = 83; break;
    // Bottom row: the default control octave.
    case 'z': note = 48; break;
    case 'x': note = 49; break;
    case 'c': note = 50; break;
    case 'v': note = 51; break;
    case 'b': note = 52; break;
    case 'n': note = 53; break;
    case 'm': note = 54; break;
    case ',': note = 55; break;
    case '.': note = 56; break;
    case '/': note = 57; break;
  }
  return note;
}

void printEmitted()
{
  for (const auto &e : bhl_test::g_emitted)
    printf("%c MIDI %d (%s%d)\n", e.on ? '+' : '-', e.key, kNoteNames[e.key % 12],
           e.key / 12 - 2);
  bhl_test::g_emitted.clear();
}

}  // namespace

// There is no app menu, so closing the window means quitting. (ObjC declarations must be
// at global scope, hence outside the namespace above.)
@interface DevHostDelegate : NSObject <NSWindowDelegate>
- (void)windowWillClose:(NSNotification *)note;
@end

@implementation DevHostDelegate
- (void)windowWillClose:(NSNotification *)note
{
  (void)note;
  [NSApp stop:nil];
}
@end

int main(int argc, const char **)
{
  @autoreleasepool
  {
    setvbuf(stdout, nullptr, _IONBF, 0);  // Keystroke log lines should not wait on a buffer.
    NSApplicationLoad();
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    const clap_plugin_t *plugin = bhl_test::makePlugin();
    GuiState *state = (GuiState *)bhl_dev_gui_state(plugin);

    uint32_t width = 0, height = 0;
    guiPreferredSize(state, &width, &height);

    NSWindow *window =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                    backing:NSBackingStoreBuffered
                                    defer:NO];
    [window setTitle:@"bh-lab dev host"];
    [window setDelegate:[[DevHostDelegate alloc] init]];

    NSView *view = (NSView *)guiCreate(state);
    [window setContentView:view];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    printf("Keys:  z x c v b n m , . /    -> control octave (C2)\n"
           "       a s d f g h j k l ; '  -> trigger zone  (C3)\n"
           "       q w e r t y u i o p [ \\ -> above it      (C4)\n"
           "ESC or closing the window quits.\n");

    // Each key press runs one block through the real plugin: that drains the view's
    // request queue (mouse clicks on keys and steppers become real parameter changes)
    // and publishes a fresh snapshot for the view's timer.
    __block bhl_test::Timeline timeline(plugin);
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp
                                          handler:^NSEvent *(NSEvent *event) {
      NSString *chars = event.charactersIgnoringModifiers;
      const unichar c = (chars.length == 1) ? [chars characterAtIndex:0] : 0;
      if (event.type == NSEventTypeKeyDown && c == 27)
      {
        [NSApp stop:nil];
        return nil;
      }
      if ([event modifierFlags] & NSEventModifierFlagCommand) return nil;
      if (event.isARepeat) return nil;

      const int note = noteForKey(event);
      if (note < 0) return nil;

      bhl_test::InputEvents input;
      input.addNote(event.type == NSEventTypeKeyDown ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF,
                    0, static_cast<int16_t>(note),
                    event.type == NSEventTypeKeyDown ? 0.8 : 0.0);
      timeline.run(&input);
      printEmitted();
      return nil;
    }];

    [NSApp run];

    guiDestroy(view);
    plugin->destroy(plugin);
    bhl_entry_deinit();
  }
  return 0;
}
