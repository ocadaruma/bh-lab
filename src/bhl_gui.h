// The plugin's view: the selected scale, the chord currently being generated, and the
// control octave with its held notes and the selected scale's notes drawn on it.
//
// The Cocoa implementation lives in bhl_gui.mm; everything here is plain C++ so the
// plugin core stays portable.
#pragma once

#include <atomic>
#include <cstdint>

namespace bhl
{

// Written by the audio thread, read by the UI timer. Plain atomics: the UI only ever
// needs a recent snapshot, never a consistent one, so no locking is involved.
struct GuiState
{
  std::atomic<uint64_t> sounding[2] = {};  // Bitmask of notes the plugin is sounding.
  std::atomic<uint64_t> chord[2] = {};     // The whole chord currently held.
  std::atomic<int> rootKey = {-1};         // Trigger key of the most recent chord.
  std::atomic<int> selection = {0};        // ScaleSelection: family * 12 + key pitch class.
  std::atomic<uint64_t> heldControl[2] = {};  // Control-zone keys currently held.
  std::atomic<int> voicing = {0};
  std::atomic<int> controlRoot = {48};
  std::atomic<int> triggerStart = {60};

  // Clicks are handed to the audio thread, which turns them into real parameter changes
  // the host can record. A small queue rather than a single slot, so two quick clicks
  // cannot lose one. requestFlush asks the host to call the plugin even when the
  // transport is idle, so a click lands immediately instead of on the next note.
  enum class Request : int
  {
    Voicing,
    ControlRoot,
    TriggerStart
  };

  struct PendingRequest
  {
    Request what = Request::Voicing;
    double value = 0;
  };

  static constexpr int kRequestCapacity = 64;
  PendingRequest requests[kRequestCapacity];
  std::atomic<uint32_t> requestWrite = {0};
  std::atomic<uint32_t> requestRead = {0};

  void (*requestFlush)(void *context) = nullptr;
  void *requestFlushContext = nullptr;

  void request(Request what, double value)
  {
    const uint32_t write = requestWrite.load(std::memory_order_relaxed);
    const uint32_t read = requestRead.load(std::memory_order_acquire);

    // A full queue means the audio thread has not run for the length of sixty-four
    // interactions -- it is not processing at all. Drop the request rather than touching
    // a slot that has already been published.
    if (write - read >= static_cast<uint32_t>(kRequestCapacity)) return;

    requests[write % kRequestCapacity] = {what, value};
    requestWrite.store(write + 1, std::memory_order_release);

    // Mirror it locally so the view updates on the click rather than a block later.
    switch (what)
    {
      case Request::Voicing:
        voicing.store(static_cast<int>(value), std::memory_order_relaxed);
        break;
      case Request::ControlRoot:
        controlRoot.store(static_cast<int>(value), std::memory_order_relaxed);
        break;
      case Request::TriggerStart:
        triggerStart.store(static_cast<int>(value), std::memory_order_relaxed);
        break;
    }

    if (requestFlush) requestFlush(requestFlushContext);
  }

  bool popRequest(PendingRequest *out)
  {
    const uint32_t read = requestRead.load(std::memory_order_relaxed);
    if (read == requestWrite.load(std::memory_order_acquire)) return false;
    *out = requests[read % kRequestCapacity];
    requestRead.store(read + 1, std::memory_order_relaxed);
    return true;
  }

  void noteOn(int key)
  {
    if (key < 0 || key > 127) return;
    sounding[key >> 6].fetch_or(1ull << (key & 63), std::memory_order_relaxed);
  }

  void noteOff(int key)
  {
    if (key < 0 || key > 127) return;
    sounding[key >> 6].fetch_and(~(1ull << (key & 63)), std::memory_order_relaxed);
  }

  void chordOn(int key)
  {
    if (key < 0 || key > 127) return;
    chord[key >> 6].fetch_or(1ull << (key & 63), std::memory_order_relaxed);
  }

  void chordOff(int key)
  {
    if (key < 0 || key > 127) return;
    chord[key >> 6].fetch_and(~(1ull << (key & 63)), std::memory_order_relaxed);
  }

  void controlOn(int key)
  {
    if (key < 0 || key > 127) return;
    heldControl[key >> 6].fetch_or(1ull << (key & 63), std::memory_order_relaxed);
  }

  void controlOff(int key)
  {
    if (key < 0 || key > 127) return;
    heldControl[key >> 6].fetch_and(~(1ull << (key & 63)), std::memory_order_relaxed);
  }

  bool isChordNote(int key) const
  {
    if (key < 0 || key > 127) return false;
    return (chord[key >> 6].load(std::memory_order_relaxed) >> (key & 63)) & 1ull;
  }

  bool isSounding(int key) const
  {
    if (key < 0 || key > 127) return false;
    return (sounding[key >> 6].load(std::memory_order_relaxed) >> (key & 63)) & 1ull;
  }

  bool isControlHeld(int key) const
  {
    if (key < 0 || key > 127) return false;
    return (heldControl[key >> 6].load(std::memory_order_relaxed) >> (key & 63)) & 1ull;
  }

  void allNotesOff()
  {
    sounding[0].store(0, std::memory_order_relaxed);
    sounding[1].store(0, std::memory_order_relaxed);
    chord[0].store(0, std::memory_order_relaxed);
    chord[1].store(0, std::memory_order_relaxed);
    heldControl[0].store(0, std::memory_order_relaxed);
    heldControl[1].store(0, std::memory_order_relaxed);
    rootKey.store(-1, std::memory_order_relaxed);
  }
};

// Test support: the rects of the selectable controls, so a test can click them without
// duplicating the layout arithmetic.
enum class StepperId
{
  ControlRoot,
  TriggerStart
};

struct ViewRect
{
  double x = 0, y = 0, w = 0, h = 0;
};

ViewRect guiStepperRect(StepperId stepper, double viewWidth);

// Test support: a voicing row button, by its index.
ViewRect guiVoicingButtonRect(int index, double viewWidth);

// Test support: a key of the control octave, by its absolute MIDI key.
ViewRect guiControlKeyRect(int key, int controlRoot);

// Platform view lifecycle. The returned handle is an NSView* on macOS.
void *guiCreate(GuiState *state);
void guiDestroy(void *view);
void guiSetParent(void *view, void *parentNSView);
void guiShow(void *view);
void guiHide(void *view);
void guiPreferredSize(const GuiState *state, uint32_t *width, uint32_t *height);

}  // namespace bhl
