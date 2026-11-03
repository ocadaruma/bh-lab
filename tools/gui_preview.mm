// Renders the plugin view to a PNG without a host, so the UI can be eyeballed (and
// diffed) without opening Logic.
//
// Usage: gui-preview <out.png> [idle|playing]

#import <Cocoa/Cocoa.h>

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
    state.controlRoot.store(48);
    state.triggerStart.store(60);
    state.quality.store(static_cast<int>(bhl::Quality::Min7));
    state.inversion.store(static_cast<int>(bhl::Inversion::First));

    const char *mode = argc < 3 ? "playing" : argv[2];

    bhl::Quality quality = bhl::Quality::Min7;
    int root = 62;
    if (strcmp(mode, "idle") == 0)
    {
      // A triad, so the unavailable third inversion can be seen greyed out.
      quality = bhl::Quality::Major;
      root = 60;
      state.quality.store(static_cast<int>(quality));
    }

    if (strcmp(mode, "idle") != 0)
    {
      const bhl::Chord chord =
          bhl::buildChord(root, quality, static_cast<bhl::Inversion>(state.inversion.load()));
      for (int i = 0; i < chord.count; ++i)
      {
        state.chordOn(chord.notes[i]);
        state.noteOn(chord.notes[i]);
      }
      state.rootKey.store(root);
    }

    NSView *view = (NSView *)bhl::guiCreate(&state);
    NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
    [view cacheDisplayInRect:[view bounds] toBitmapImageRep:rep];

    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    NSString *path = [NSString stringWithUTF8String:argv[1]];
    if (![png writeToFile:path atomically:YES])
    {
      printf("failed to write %s\n", argv[1]);
      return 1;
    }
    printf("wrote %s (%.0fx%.0f)\n", argv[1], [view bounds].size.width,
           [view bounds].size.height);

    bhl::guiDestroy(view);
  }
  return 0;
}
