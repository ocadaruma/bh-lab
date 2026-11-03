// Renders the plugin view to a PNG without a host, so the UI can be eyeballed (and
// diffed) without opening Logic.
//
// Usage: gui-preview <out.png> [idle|playing]

#import <Cocoa/Cocoa.h>

#include <initializer_list>

#include "bhl_gui.h"
#include "chord_engine.h"

int main(int argc, const char **argv)
{
  @autoreleasepool
  {
    if (argc < 2)
    {
      printf("usage: gui-preview <out.png> [idle|playing]\n");
      return 2;
    }
    NSApplicationLoad();

    bhl::GuiState state;
    // The plugin publishes these from its parameters; the preview seeds them by hand.
    state.controlRoot.store(57);  // A2, matching the held notes below
    state.triggerStart.store(60);
    state.voicing.store(0);
    state.selection.store(static_cast<int>(bhl::ScaleFamily::Minor6) * 12 + 9);  // A minor 6th

    const char *mode = argc < 3 ? "playing" : argv[2];

    // A m6 with C4 on top: E3 F#3 A3 C4, and the control notes that identified the
    // scale held down.
    if (strcmp(mode, "idle") != 0)
    {
      const bhl::ScaleSelection sel{bhl::ScaleFamily::Minor6, 9};
      const bhl::Chord chord = bhl::buildStackedChord(sel, 72, bhl::VoicingType::Close);
      for (int i = 0; i < chord.count; ++i)
      {
        state.chordOn(chord.notes[i]);
        state.noteOn(chord.notes[i]);
      }
      state.rootKey.store(72);
      // The notes that identified the scale, held down.
      for (int k : {57, 60, 64, 66}) state.controlOn(k);
    }

    NSView *view = (NSView *)bhl::guiCreate(&state);
    NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
    [view cacheDisplayInRect:[view bounds] toBitmapImageRep:rep];

    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    NSString *path = [NSString stringWithUTF8String:argv[1]];
    if (![png writeToFile:path atomically:YES])
    {
      printf("failed to write %s\n", path.UTF8String);
      return 1;
    }
    printf("wrote %s (%.0fx%.0f)\n", path.UTF8String, [view bounds].size.width,
           [view bounds].size.height);

    bhl::guiDestroy(view);
  }
  return 0;
}
