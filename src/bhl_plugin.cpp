// bh-lab: an AU note effect that turns single keys into chords.
//
// Keys in the control octave latch chord quality (white keys) and inversion (black
// keys); keys in the trigger zone each generate a close-position chord rooted on the
// key pressed.
//
// Written against the plain CLAP C API; the AUv2 wrapper turns it into a .component.

#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>  // strcasecmp

#include "bhl_entry.h"
#include "bhl_gui.h"
#include "chord_engine.h"

using namespace bhl;

namespace
{

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

enum ParamId : clap_id
{
  kParamQuality = 0,
  kParamInversion = 1,
  kParamControlRoot = 2,
  kParamTriggerStart = 3,
  kNumParams
};

struct ParamDef
{
  clap_id id;
  const char *name;
  double min;
  double max;
  double def;
  bool stepped;
};

const ParamDef kParamDefs[kNumParams] = {
    {kParamQuality, "Chord Type", 0, static_cast<double>(Quality::Count) - 1, 0, true},
    {kParamInversion, "Inversion", 0, static_cast<double>(Inversion::Count) - 1, 0, true},
    // Note names follow Logic's convention, where middle C (MIDI 60) is C3.
    {kParamControlRoot, "Control Zone Root", 0, 115, 48, true},    // C2
    {kParamTriggerStart, "Trigger Zone Start", 0, 127, 60, true},  // C3
};

static_assert(sizeof(kParamDefs) / sizeof(kParamDefs[0]) == kNumParams,
              "parameter table out of sync with the ids");

// Parameter ids are the host's handle on automation: a lane written against an id will be
// replayed into whatever that id means later. New parameters are appended at the end,
// never inserted.
static_assert(kParamQuality == 0 && kParamInversion == 1, "ids are frozen");
static_assert(kParamControlRoot == 2 && kParamTriggerStart == 3, "ids are frozen");

const char *kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// MIDI note 0 is C-2 in the octave numbering Logic uses.
void formatNoteName(int key, char *out, size_t cap)
{
  if (key < 0 || key > 127)
  {
    snprintf(out, cap, "--");
    return;
  }
  snprintf(out, cap, "%s%d", kNoteNames[key % 12], (key / 12) - 2);
}

// ---------------------------------------------------------------------------
// Voice tracking
// ---------------------------------------------------------------------------

// One entry per held trigger key. Chords must be released with exactly the note ids
// they were started with, or the instrument downstream is left with stuck notes.
struct HeldChord
{
  bool active = false;
  uint32_t serial = 0;
  double velocity = 0.8;
  int16_t channel = 0;
  int16_t key = 0;
  int count = 0;
  struct Voice
  {
    int32_t noteId = -1;
    int16_t key = 0;
  } voices[kMaxVoices];
};

constexpr int kMaxHeldChords = 32;

struct Bhl
{
  clap_plugin_t plugin = {};
  const clap_host_t *host = nullptr;
  const clap_host_params_t *hostParams = nullptr;
  const clap_host_state_t *hostState = nullptr;
  std::atomic<bool> pendingRescan = {false};

  std::atomic<double> params[kNumParams];

  HeldChord held[kMaxHeldChords];
  int32_t nextNoteId = 0;
  int heldCount = 0;

  double sampleRate = 48000.0;
  // Note-offs the host would not accept. Dropping one leaves a note ringing downstream
  // forever, so they are kept and retried on the next block instead.
  struct PendingOff
  {
    bool active = false;
    uint16_t type = 0;
    int32_t noteId = -1;
    int16_t channel = 0;
    int16_t key = 0;
  };
  static constexpr int kMaxPendingOffs = kMaxHeldChords * kMaxVoices;
  PendingOff pendingOffs[kMaxPendingOffs];

  // Keys that were forwarded to the instrument untouched. A note-on and its note-off have
  // to be treated the same way even if the zone boundaries moved in between. Counted
  // rather than flagged, since a key can legitimately be struck again before it is
  // released and both note-offs must be forwarded.
  uint8_t forwarded[16][128] = {};

  GuiState gui;
  void *guiView = nullptr;

  double paramValue(ParamId id) const
  {
    return params[id].load(std::memory_order_relaxed);
  }
  int paramInt(ParamId id) const
  {
    // Round away from zero: truncating a negative value (-1 + 0.5 -> -0.5 -> 0) would
    // quietly swallow the whole negative half of every bipolar parameter.
    const double value = paramValue(id);
    return static_cast<int>(value + (value < 0 ? -0.5 : 0.5));
  }

  // Mirrors the parameters the view draws. Cheap enough to call per event.
  void publishToGui()
  {
    gui.quality.store(paramInt(kParamQuality), std::memory_order_relaxed);
    gui.inversion.store(paramInt(kParamInversion), std::memory_order_relaxed);
    gui.controlRoot.store(paramInt(kParamControlRoot), std::memory_order_relaxed);
    gui.triggerStart.store(paramInt(kParamTriggerStart), std::memory_order_relaxed);
  }
};

// Every route into a parameter -- host automation, saved state, the UI -- goes through
// here, so no path can leave a value the rest of the code would have to defend against.
double sanitizeParam(clap_id id, double value)
{
  if (id >= kNumParams) return 0.0;
  const ParamDef &def = kParamDefs[id];

  if (!std::isfinite(value)) return def.def;
  if (value < def.min) return def.min;
  if (value > def.max) return def.max;
  if (def.stepped) return std::round(value);
  return value;
}

Bhl *self(const clap_plugin_t *plugin)
{
  return static_cast<Bhl *>(plugin->plugin_data);
}

// ---------------------------------------------------------------------------
// Event helpers
// ---------------------------------------------------------------------------

bool pushNote(const clap_output_events_t *out, uint16_t type, uint32_t time, int32_t noteId,
              int16_t channel, int16_t key, double velocity)
{
  clap_event_note_t ev = {};
  ev.header.size = sizeof(ev);
  ev.header.time = time;
  ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev.header.type = type;
  ev.header.flags = 0;
  ev.note_id = noteId;
  ev.port_index = 0;
  ev.channel = channel;
  ev.key = key;
  ev.velocity = velocity;
  return out->try_push(out, &ev.header);
}

// Returns false if the host would not take the whole gesture, in which case its idea of
// the value is stale and it needs to re-read.
bool pushParam(const clap_output_events_t *out, uint32_t time, clap_id paramId, double value)
{
  clap_event_param_gesture_t gesture = {};
  gesture.header.size = sizeof(gesture);
  gesture.header.time = time;
  gesture.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  gesture.header.type = CLAP_EVENT_PARAM_GESTURE_BEGIN;
  gesture.param_id = paramId;
  bool delivered = out->try_push(out, &gesture.header);

  clap_event_param_value_t ev = {};
  ev.header.size = sizeof(ev);
  ev.header.time = time;
  ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev.header.type = CLAP_EVENT_PARAM_VALUE;
  ev.param_id = paramId;
  ev.cookie = nullptr;
  ev.note_id = -1;
  ev.port_index = -1;
  ev.channel = -1;
  ev.key = -1;
  ev.value = value;
  delivered = out->try_push(out, &ev.header) && delivered;

  gesture.header.type = CLAP_EVENT_PARAM_GESTURE_END;
  delivered = out->try_push(out, &gesture.header) && delivered;
  return delivered;
}

// Sends a note-off, holding on to it if the host's queue is full.
void emitNoteOff(Bhl *rc, const clap_output_events_t *out, uint16_t type, uint32_t time,
                 int32_t noteId, int16_t channel, int16_t key, double velocity)
{
  if (pushNote(out, type, time, noteId, channel, key, velocity)) return;

  for (auto &pending : rc->pendingOffs)
  {
    if (pending.active) continue;
    pending = {true, type, noteId, channel, key};
    return;
  }
}

// Retries anything the host refused earlier. Runs before the block's own events, so a
// stuck note is released at the first opportunity.
void drainPendingOffs(Bhl *rc, const clap_output_events_t *out, uint32_t time)
{
  for (auto &pending : rc->pendingOffs)
  {
    if (!pending.active) continue;
    if (!pushNote(out, pending.type, time, pending.noteId, pending.channel, pending.key, 0.0))
      return;  // Still full: leave the rest for the next block.
    pending.active = false;
  }
}

// Forwards an event unchanged. Returns whether the host took it, since anything we
// remember about a forwarded note is only true if it was actually sent.
bool passThrough(const clap_output_events_t *out, const clap_event_header_t *ev)
{
  return out->try_push(out, ev);
}

// ---------------------------------------------------------------------------
// Chord voice bookkeeping
// ---------------------------------------------------------------------------

HeldChord *findHeld(Bhl *rc, int16_t channel, int16_t key)
{
  for (auto &h : rc->held)
  {
    if (!h.active) continue;
    if (h.key != key) continue;
    // A channel of -1 in the incoming event is a wildcard.
    if (channel >= 0 && h.channel != channel) continue;
    return &h;
  }
  return nullptr;
}

HeldChord *allocHeld(Bhl *rc)
{
  for (auto &h : rc->held)
    if (!h.active) return &h;
  return nullptr;
}

// Emits note-offs for every voice of a held chord and frees the slot.
void releaseHeld(Bhl *rc, HeldChord *held, const clap_output_events_t *out, uint32_t time,
                 double velocity, uint16_t type = CLAP_EVENT_NOTE_OFF)
{
  for (int i = 0; i < held->count; ++i)
  {
    rc->gui.chordOff(held->voices[i].key);
    if (held->voices[i].noteId < 0) continue;
    emitNoteOff(rc, out, type, time, held->voices[i].noteId, held->channel,
                held->voices[i].key, velocity);
    rc->gui.noteOff(held->voices[i].key);
  }
  if (held->active) --rc->heldCount;
  held->active = false;
  held->count = 0;
}

void markForwarded(Bhl *rc, int16_t channel, int16_t key)
{
  if (key < 0 || key > 127) return;
  const int ch = channel < 0 ? 0 : (channel & 0xf);
  if (rc->forwarded[ch][key] < 255) ++rc->forwarded[ch][key];
}

// Was this key forwarded on the way in? A channel of -1 matches any. Clears the mark.
bool takeForwarded(Bhl *rc, int16_t channel, int16_t key)
{
  if (key < 0 || key > 127) return false;

  for (int ch = 0; ch < 16; ++ch)
  {
    if (channel >= 0 && ch != (channel & 0xf)) continue;
    if (rc->forwarded[ch][key] == 0) continue;
    --rc->forwarded[ch][key];
    return true;
  }
  return false;
}

// Releases every chord matching (channel, key). Handles the wildcard channel and the
// (illegal but survivable) case of duplicate slots.
// Returns true if anything was actually released.
bool releaseAllMatching(Bhl *rc, int16_t channel, int16_t key, const clap_output_events_t *out,
                        uint32_t time, double velocity, uint16_t type = CLAP_EVENT_NOTE_OFF)
{
  bool released = false;
  while (HeldChord *h = findHeld(rc, channel, key))
  {
    releaseHeld(rc, h, out, time, velocity, type);
    released = true;
  }
  return released;
}

void triggerChord(Bhl *rc, const clap_event_note_t *note, const clap_output_events_t *out)
{
  // Re-triggering a key that is already sounding releases the previous chord first.
  releaseAllMatching(rc, note->channel, note->key, out, note->header.time, 0.0);

  HeldChord *held = allocHeld(rc);
  if (!held) return;  // Out of slots: better to drop the chord than to leak notes.

  const auto quality = static_cast<Quality>(rc->paramInt(kParamQuality));
  const auto inversion = static_cast<Inversion>(rc->paramInt(kParamInversion));
  Chord chord = buildChord(note->key, quality, inversion);

  if (chord.count == 0) return;

  const int16_t channel = note->channel < 0 ? 0 : note->channel;

  held->active = true;
  ++rc->heldCount;
  held->serial = 0;
  held->velocity = note->velocity;
  held->channel = channel;
  held->key = note->key;
  held->count = chord.count;

  for (int i = 0; i < chord.count; ++i)
  {
    const int32_t noteId = rc->nextNoteId++;
    if (rc->nextNoteId < 0) rc->nextNoteId = 0;

    held->voices[i].key = static_cast<int16_t>(chord.notes[i]);
    held->voices[i].noteId = -1;
    rc->gui.chordOn(chord.notes[i]);

    if (!pushNote(out, CLAP_EVENT_NOTE_ON, note->header.time, noteId, channel,
                  static_cast<int16_t>(chord.notes[i]), note->velocity))
      continue;  // Never sounded, so it is not ours to release.

    held->voices[i].noteId = noteId;
    rc->gui.noteOn(chord.notes[i]);
  }

  rc->gui.rootKey.store(note->key, std::memory_order_relaxed);
}

// Sets a parameter without telling the host a gesture happened. Used when the UI and the
// audio thread share one change, so the host is not told twice.
void setParamQuietly(Bhl *rc, ParamId id, double value)
{
  rc->params[id].store(sanitizeParam(id, value), std::memory_order_relaxed);
}

// Asks the host to re-read every parameter, and to notice the project has changed. Both
// are main-thread calls, so they are deferred.
void requestValueRescan(Bhl *rc)
{
  rc->pendingRescan.store(true, std::memory_order_relaxed);
  if (rc->host && rc->host->request_callback) rc->host->request_callback(rc->host);
}

void drainGuiRequests(Bhl *rc, const clap_output_events_t *out, uint32_t time)
{
  GuiState::PendingRequest request;
  while (rc->gui.popRequest(&request))
  {
    ParamId id = kParamQuality;
    switch (request.what)
    {
      case GuiState::Request::Quality:
        id = kParamQuality;
        break;
      case GuiState::Request::Inversion:
        id = kParamInversion;
        break;
      case GuiState::Request::ControlRoot:
        id = kParamControlRoot;
        break;
      case GuiState::Request::TriggerStart:
        id = kParamTriggerStart;
        break;
    }

    const double value = sanitizeParam(id, request.value);
    rc->params[id].store(value, std::memory_order_relaxed);
    if (!pushParam(out, time, id, value)) requestValueRescan(rc);
  }
}

// ---------------------------------------------------------------------------
// Zone classification
// ---------------------------------------------------------------------------

enum class Zone
{
  Control,
  Trigger,
  Below
};

Zone zoneOf(Bhl *rc, int key)
{
  const int controlRoot = rc->paramInt(kParamControlRoot);
  const int triggerStart = rc->paramInt(kParamTriggerStart);

  // The control zone wins if the two zones are configured to overlap.
  if (key >= controlRoot && key < controlRoot + 12) return Zone::Control;
  if (key >= triggerStart) return Zone::Trigger;
  return Zone::Below;
}

void handleControlKey(Bhl *rc, const clap_event_note_t *note, const clap_output_events_t *out)
{
  const int offset = note->key - rc->paramInt(kParamControlRoot);
  const ControlHit hit = mapControlKey(offset);

  switch (hit.action)
  {
    case ControlAction::SetQuality:
      setParamQuietly(rc, kParamQuality, hit.value);
      pushParam(out, note->header.time, kParamQuality, hit.value);
      break;
    case ControlAction::SetInversion:
      setParamQuietly(rc, kParamInversion, hit.value);
      pushParam(out, note->header.time, kParamInversion, hit.value);
      break;
    case ControlAction::None:
      break;
  }
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------

void handleEvent(Bhl *rc, const clap_event_header_t *hdr, const clap_output_events_t *out)
{
  if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

  switch (hdr->type)
  {
    case CLAP_EVENT_PARAM_VALUE:
    {
      const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
      if (ev->param_id >= kNumParams) break;
      setParamQuietly(rc, static_cast<ParamId>(ev->param_id), ev->value);
      break;
    }

    case CLAP_EVENT_NOTE_ON:
    {
      const auto *note = reinterpret_cast<const clap_event_note_t *>(hdr);
      switch (zoneOf(rc, note->key))
      {
        case Zone::Control:
          handleControlKey(rc, note, out);  // Latches state; never sounds.
          break;
        case Zone::Trigger:
          triggerChord(rc, note, out);
          break;
        case Zone::Below:
          // Forwarded untouched, and remembered as forwarded if it went.
          if (passThrough(out, hdr)) markForwarded(rc, note->channel, note->key);
          break;
      }
      break;
    }

    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE:
    {
      const auto *note = reinterpret_cast<const clap_event_note_t *>(hdr);
      const uint16_t outType =
          hdr->type == CLAP_EVENT_NOTE_CHOKE ? CLAP_EVENT_NOTE_CHOKE : CLAP_EVENT_NOTE_OFF;

      // Release by what the key owns rather than by where the zones sit now: the
      // boundaries are parameters and may have moved since the key went down. Deciding
      // from the current zones instead is how a chord gets left ringing.
      if (releaseAllMatching(rc, note->channel, note->key, out, hdr->time, note->velocity,
                             outType))
        break;

      if (takeForwarded(rc, note->channel, note->key))
      {
        passThrough(out, hdr);
        break;
      }

      // Nothing was sounded for this key -- a control press, or a note that arrived
      // before the plugin was listening. Either way there is nothing to release.
      break;
    }

    case CLAP_EVENT_NOTE_EXPRESSION:
    {
      // Apply the expression to every voice the source key generated.
      const auto *exp = reinterpret_cast<const clap_event_note_expression_t *>(hdr);
      HeldChord *held = findHeld(rc, exp->channel, exp->key);
      if (!held)
      {
        passThrough(out, hdr);
        break;
      }
      for (int i = 0; i < held->count; ++i)
      {
        clap_event_note_expression_t copy = *exp;
        copy.note_id = held->voices[i].noteId;
        copy.channel = held->channel;
        copy.key = held->voices[i].key;
        out->try_push(out, &copy.header);
      }
      break;
    }

    // Controllers, pitch bend, aftertouch and friends arrive as raw MIDI regardless of
    // the note dialect; they are channel-wide, so forward them untouched.
    case CLAP_EVENT_MIDI:
    case CLAP_EVENT_MIDI_SYSEX:
    case CLAP_EVENT_MIDI2:
      passThrough(out, hdr);
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// clap_plugin_params
// ---------------------------------------------------------------------------

uint32_t CLAP_ABI paramsCount(const clap_plugin_t *)
{
  return kNumParams;
}

bool CLAP_ABI paramsGetInfo(const clap_plugin_t *, uint32_t index, clap_param_info_t *info)
{
  if (index >= kNumParams) return false;
  const ParamDef &def = kParamDefs[index];

  *info = {};
  info->id = def.id;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
  info->cookie = nullptr;
  snprintf(info->name, CLAP_NAME_SIZE, "%s", def.name);
  snprintf(info->module, CLAP_PATH_SIZE, "%s", "");
  info->min_value = def.min;
  info->max_value = def.max;
  info->default_value = def.def;
  return true;
}

bool CLAP_ABI paramsGetValue(const clap_plugin_t *plugin, clap_id id, double *out)
{
  if (id >= kNumParams) return false;
  *out = self(plugin)->paramValue(static_cast<ParamId>(id));
  return true;
}

bool CLAP_ABI paramsValueToText(const clap_plugin_t *, clap_id id, double value, char *out,
                                uint32_t cap)
{
  const int v = static_cast<int>(value + (value < 0 ? -0.5 : 0.5));

  switch (id)
  {
    case kParamQuality:
      snprintf(out, cap, "%s", chordTypeName(static_cast<Quality>(v)));
      return true;
    case kParamInversion:
      snprintf(out, cap, "%s", inversionName(static_cast<Inversion>(v)));
      return true;
    case kParamControlRoot:
    case kParamTriggerStart:
    {
      char note[16];
      formatNoteName(v, note, sizeof(note));
      snprintf(out, cap, "%s (%d)", note, v);
      return true;
    }
    default:
      return false;
  }
}

bool CLAP_ABI paramsTextToValue(const clap_plugin_t *, clap_id id, const char *text, double *out)
{
  if (id >= kNumParams) return false;

  switch (id)
  {
    case kParamQuality:
      for (int i = 0; i < static_cast<int>(Quality::Count); ++i)
        if (strcasecmp(text, chordTypeName(static_cast<Quality>(i))) == 0)
        {
          *out = i;
          return true;
        }
      break;
    case kParamInversion:
      for (int i = 0; i < static_cast<int>(Inversion::Count); ++i)
        if (strcasecmp(text, inversionName(static_cast<Inversion>(i))) == 0)
        {
          *out = i;
          return true;
        }
      break;
    default:
      break;
  }

  // Fall back to a plain number so typing "48" into the zone fields works.
  char *end = nullptr;
  const double v = strtod(text, &end);
  if (end == text) return false;
  *out = v;
  return true;
}

void CLAP_ABI paramsFlush(const clap_plugin_t *plugin, const clap_input_events_t *in,
                          const clap_output_events_t *out)
{
  Bhl *rc = self(plugin);
  drainGuiRequests(rc, out, 0);
  const uint32_t count = in->size(in);
  for (uint32_t i = 0; i < count; ++i)
  {
    const clap_event_header_t *hdr = in->get(in, i);
    if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE)
    {
      const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
      if (ev->param_id >= kNumParams) continue;
      setParamQuietly(rc, static_cast<ParamId>(ev->param_id), ev->value);
    }
  }
  rc->publishToGui();
}

const clap_plugin_params_t kParamsExtension = {paramsCount,        paramsGetInfo,
                                               paramsGetValue,     paramsValueToText,
                                               paramsTextToValue,  paramsFlush};

// ---------------------------------------------------------------------------
// clap_plugin_note_ports / audio_ports
// ---------------------------------------------------------------------------

uint32_t CLAP_ABI notePortsCount(const clap_plugin_t *, bool)
{
  return 1;
}

bool CLAP_ABI notePortsGet(const clap_plugin_t *, uint32_t index, bool isInput,
                           clap_note_port_info_t *info)
{
  if (index != 0) return false;
  *info = {};
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  snprintf(info->name, CLAP_NAME_SIZE, "%s", isInput ? "Keys" : "Chords");
  return true;
}

const clap_plugin_note_ports_t kNotePortsExtension = {notePortsCount, notePortsGet};

// The plugin touches no audio, but AUv2 hosts (and auval) expect a MIDI processor to
// still present a main audio bus pair; with none, AudioUnitInitialize fails with
// kAudioUnitErr_InvalidElement (-10877). Do not remove these ports without re-testing
// that. Audio is copied straight through -- see passThroughAudio.
uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t *, bool)
{
  return 1;
}

bool CLAP_ABI audioPortsGet(const clap_plugin_t *, uint32_t index, bool isInput,
                            clap_audio_port_info_t *info)
{
  if (index != 0) return false;
  *info = {};
  info->id = 0;
  snprintf(info->name, CLAP_NAME_SIZE, "%s", isInput ? "In" : "Out");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = 0;
  return true;
}

const clap_plugin_audio_ports_t kAudioPortsExtension = {audioPortsCount, audioPortsGet};

// ---------------------------------------------------------------------------
// clap_plugin_state
// ---------------------------------------------------------------------------

constexpr uint32_t kStateVersion = 1;

bool CLAP_ABI stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream)
{
  Bhl *rc = self(plugin);

  double blob[2 + kNumParams];
  blob[0] = kStateVersion;
  blob[1] = kNumParams;
  for (int i = 0; i < kNumParams; ++i) blob[2 + i] = rc->paramValue(static_cast<ParamId>(i));

  const auto *bytes = reinterpret_cast<const uint8_t *>(blob);
  uint64_t remaining = sizeof(blob);
  while (remaining > 0)
  {
    const int64_t written = stream->write(stream, bytes, remaining);
    if (written <= 0) return false;
    bytes += written;
    remaining -= static_cast<uint64_t>(written);
  }
  return true;
}

bool CLAP_ABI stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream)
{
  Bhl *rc = self(plugin);

  // Seeded with the values in force, so a stream that ends early leaves those parameters
  // as they are instead of collapsing them to their minimum.
  double blob[2 + kNumParams];
  blob[0] = 0;
  blob[1] = 0;
  for (int i = 0; i < kNumParams; ++i) blob[2 + i] = rc->paramValue(static_cast<ParamId>(i));

  auto *bytes = reinterpret_cast<uint8_t *>(blob);
  uint64_t remaining = sizeof(blob);
  while (remaining > 0)
  {
    const int64_t read = stream->read(stream, bytes, remaining);
    if (read < 0) return false;
    if (read == 0) break;  // Truncated; caught below.
    bytes += read;
    remaining -= static_cast<uint64_t>(read);
  }

  // Refuse anything written by a different layout, or cut short: keeping the defaults is
  // recoverable, loading the wrong values into the wrong parameters is not.
  if (remaining != 0) return false;
  if (blob[0] != kStateVersion) return false;
  if (blob[1] != kNumParams) return false;

  for (int i = 0; i < kNumParams; ++i)
    rc->params[i].store(sanitizeParam(i, blob[2 + i]), std::memory_order_relaxed);
  return true;
}

const clap_plugin_state_t kStateExtension = {stateSave, stateLoad};

// ---------------------------------------------------------------------------
// clap_plugin_gui -- a fixed-size Cocoa view; see bhl_gui.mm
// ---------------------------------------------------------------------------

#if defined(__APPLE__)

bool CLAP_ABI guiIsApiSupported(const clap_plugin_t *, const char *api, bool isFloating)
{
  return !isFloating && !strcmp(api, CLAP_WINDOW_API_COCOA);
}

bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t *, const char **api, bool *isFloating)
{
  *api = CLAP_WINDOW_API_COCOA;
  *isFloating = false;
  return true;
}

bool CLAP_ABI guiCreateView(const clap_plugin_t *plugin, const char *api, bool isFloating)
{
  if (!guiIsApiSupported(plugin, api, isFloating)) return false;
  Bhl *rc = self(plugin);
  if (rc->guiView) return true;
  rc->publishToGui();
  rc->guiView = guiCreate(&rc->gui);
  return rc->guiView != nullptr;
}

void CLAP_ABI guiDestroyView(const clap_plugin_t *plugin)
{
  Bhl *rc = self(plugin);
  guiDestroy(rc->guiView);
  rc->guiView = nullptr;
}

bool CLAP_ABI guiSetScaleFn(const clap_plugin_t *, double)
{
  return false;  // The AU host works in logical pixels; Cocoa handles the backing scale.
}

bool CLAP_ABI guiGetSizeFn(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height)
{
  guiPreferredSize(&self(plugin)->gui, width, height);
  return true;
}

bool CLAP_ABI guiCanResizeFn(const clap_plugin_t *)
{
  return false;
}

bool CLAP_ABI guiGetResizeHintsFn(const clap_plugin_t *, clap_gui_resize_hints_t *)
{
  return false;
}

bool CLAP_ABI guiAdjustSizeFn(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height)
{
  return guiGetSizeFn(plugin, width, height);
}

bool CLAP_ABI guiSetSizeFn(const clap_plugin_t *, uint32_t, uint32_t)
{
  return true;
}

bool CLAP_ABI guiSetParentFn(const clap_plugin_t *plugin, const clap_window_t *window)
{
  Bhl *rc = self(plugin);
  if (!rc->guiView || !window || strcmp(window->api, CLAP_WINDOW_API_COCOA)) return false;
  guiSetParent(rc->guiView, window->cocoa);
  return true;
}

bool CLAP_ABI guiSetTransientFn(const clap_plugin_t *, const clap_window_t *)
{
  return false;
}

void CLAP_ABI guiSuggestTitleFn(const clap_plugin_t *, const char *)
{
}

bool CLAP_ABI guiShowFn(const clap_plugin_t *plugin)
{
  Bhl *rc = self(plugin);
  if (!rc->guiView) return false;
  guiShow(rc->guiView);
  return true;
}

bool CLAP_ABI guiHideFn(const clap_plugin_t *plugin)
{
  Bhl *rc = self(plugin);
  if (!rc->guiView) return false;
  guiHide(rc->guiView);
  return true;
}

const clap_plugin_gui_t kGuiExtension = {guiIsApiSupported,
                                         guiGetPreferredApi,
                                         guiCreateView,
                                         guiDestroyView,
                                         guiSetScaleFn,
                                         guiGetSizeFn,
                                         guiCanResizeFn,
                                         guiGetResizeHintsFn,
                                         guiAdjustSizeFn,
                                         guiSetSizeFn,
                                         guiSetParentFn,
                                         guiSetTransientFn,
                                         guiSuggestTitleFn,
                                         guiShowFn,
                                         guiHideFn};
#endif  // __APPLE__

// ---------------------------------------------------------------------------
// clap_plugin
// ---------------------------------------------------------------------------

const char *kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    "com.mayreh.bhlab",
    "bh-lab",
    "mayreh",
    "https://github.com/",
    "",
    "",
    "0.1.0",
    "Turns single keys into chords, with quality and inversion chosen from a control octave.",
    kFeatures};

void requestHostFlush(void *context)
{
  Bhl *rc = static_cast<Bhl *>(context);
  if (rc->hostParams && rc->hostParams->request_flush) rc->hostParams->request_flush(rc->host);
}

bool CLAP_ABI pluginInit(const clap_plugin_t *plugin)
{
  Bhl *rc = self(plugin);
  rc->hostParams =
      static_cast<const clap_host_params_t *>(rc->host->get_extension(rc->host, CLAP_EXT_PARAMS));
  rc->hostState =
      static_cast<const clap_host_state_t *>(rc->host->get_extension(rc->host, CLAP_EXT_STATE));
  rc->gui.requestFlush = requestHostFlush;
  rc->gui.requestFlushContext = rc;
  return true;
}

void CLAP_ABI pluginDestroy(const clap_plugin_t *plugin)
{
  Bhl *rc = self(plugin);
#if defined(__APPLE__)
  if (rc->guiView) guiDestroyView(plugin);
#endif
  delete rc;
}

bool CLAP_ABI pluginActivate(const clap_plugin_t *plugin, double sampleRate, uint32_t, uint32_t)
{
  Bhl *rc = self(plugin);
  rc->sampleRate = sampleRate > 0 ? sampleRate : 48000.0;
  return true;
}

void CLAP_ABI pluginDeactivate(const clap_plugin_t *)
{
}

bool CLAP_ABI pluginStartProcessing(const clap_plugin_t *)
{
  return true;
}

void CLAP_ABI pluginStopProcessing(const clap_plugin_t *)
{
}

void CLAP_ABI pluginReset(const clap_plugin_t *plugin)
{
  // The host is telling us the timeline jumped; it is responsible for silencing the
  // notes we emitted, so just forget them rather than tracking phantom voices.
  Bhl *rc = self(plugin);
  for (auto &h : rc->held)
  {
    h.active = false;
    h.count = 0;
  }
  rc->heldCount = 0;
  for (auto &pending : rc->pendingOffs) pending.active = false;
  std::memset(rc->forwarded, 0, sizeof(rc->forwarded));
  rc->gui.allNotesOff();
}

// The ports exist only to satisfy the host; whatever arrives is handed straight on. In
// place (the usual case, since the ports declare themselves as a pair) there is nothing
// to do -- but a host is free to give us separate buffers, and then an untouched output
// would be silence or worse.
void passThroughAudio(const clap_process_t *process)
{
  const uint32_t ports = process->audio_outputs_count;
  for (uint32_t port = 0; port < ports; ++port)
  {
    const clap_audio_buffer_t &out = process->audio_outputs[port];
    const bool haveInput = port < process->audio_inputs_count;
    const clap_audio_buffer_t *in = haveInput ? &process->audio_inputs[port] : nullptr;

    for (uint32_t channel = 0; channel < out.channel_count; ++channel)
    {
      const bool copyable = in && channel < in->channel_count;

      if (out.data32)
      {
        float *target = out.data32[channel];
        if (!target) continue;
        const float *source = copyable && in->data32 ? in->data32[channel] : nullptr;
        if (source == target) continue;  // In place: already done.
        if (source)
          std::memcpy(target, source, process->frames_count * sizeof(float));
        else
          std::memset(target, 0, process->frames_count * sizeof(float));
      }
      else if (out.data64)
      {
        double *target = out.data64[channel];
        if (!target) continue;
        const double *source = copyable && in->data64 ? in->data64[channel] : nullptr;
        if (source == target) continue;
        if (source)
          std::memcpy(target, source, process->frames_count * sizeof(double));
        else
          std::memset(target, 0, process->frames_count * sizeof(double));
      }
    }
  }
}

clap_process_status CLAP_ABI pluginProcess(const clap_plugin_t *plugin,
                                           const clap_process_t *process)
{
  passThroughAudio(process);

  Bhl *rc = self(plugin);

  const clap_input_events_t *in = process->in_events;
  const clap_output_events_t *out = process->out_events;
  if (!in || !out) return CLAP_PROCESS_CONTINUE;

  drainPendingOffs(rc, out, 0);
  drainGuiRequests(rc, out, 0);

  const uint32_t events = in->size(in);
  for (uint32_t i = 0; i < events; ++i) handleEvent(rc, in->get(in, i), out);

  rc->publishToGui();
  return CLAP_PROCESS_CONTINUE;
}

const void *CLAP_ABI pluginGetExtension(const clap_plugin_t *, const char *id)
{
  if (!strcmp(id, CLAP_EXT_PARAMS)) return &kParamsExtension;
  if (!strcmp(id, CLAP_EXT_NOTE_PORTS)) return &kNotePortsExtension;
  if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPortsExtension;
  if (!strcmp(id, CLAP_EXT_STATE)) return &kStateExtension;
#if defined(__APPLE__)
  if (!strcmp(id, CLAP_EXT_GUI)) return &kGuiExtension;
#endif
  return nullptr;
}

void CLAP_ABI pluginOnMainThread(const clap_plugin_t *plugin)
{
  Bhl *rc = self(plugin);
  if (!rc->pendingRescan.exchange(false, std::memory_order_relaxed)) return;

  // Both of these are main-thread only, which is why the audio thread asked for a
  // callback rather than making the calls itself.
  if (rc->hostParams && rc->hostParams->rescan)
    rc->hostParams->rescan(rc->host, CLAP_PARAM_RESCAN_VALUES);
  if (rc->hostState && rc->hostState->mark_dirty) rc->hostState->mark_dirty(rc->host);
}

const clap_plugin_t *createPlugin(const clap_host_t *host)
{
  auto *rc = new Bhl();
  rc->host = host;

  for (int i = 0; i < kNumParams; ++i) rc->params[i].store(kParamDefs[i].def);

  rc->plugin.desc = &kDescriptor;
  rc->plugin.plugin_data = rc;
  rc->plugin.init = pluginInit;
  rc->plugin.destroy = pluginDestroy;
  rc->plugin.activate = pluginActivate;
  rc->plugin.deactivate = pluginDeactivate;
  rc->plugin.start_processing = pluginStartProcessing;
  rc->plugin.stop_processing = pluginStopProcessing;
  rc->plugin.reset = pluginReset;
  rc->plugin.process = pluginProcess;
  rc->plugin.get_extension = pluginGetExtension;
  rc->plugin.on_main_thread = pluginOnMainThread;

  return &rc->plugin;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t *)
{
  return 1;
}

const clap_plugin_descriptor_t *CLAP_ABI factoryGetPluginDescriptor(const clap_plugin_factory_t *,
                                                                    uint32_t index)
{
  return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *CLAP_ABI factoryCreatePlugin(const clap_plugin_factory_t *,
                                                  const clap_host_t *host, const char *pluginId)
{
  if (!clap_version_is_compatible(host->clap_version)) return nullptr;
  if (strcmp(pluginId, kDescriptor.id)) return nullptr;
  return createPlugin(host);
}

const clap_plugin_factory_t kFactory = {factoryGetPluginCount, factoryGetPluginDescriptor,
                                        factoryCreatePlugin};

}  // namespace

extern "C"
{
  bool bhl_entry_init(const char *)
  {
    return true;
  }

  void bhl_entry_deinit()
  {
  }

  const void *bhl_entry_get_factory(const char *factoryId)
  {
    if (!strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) return &kFactory;
    return nullptr;
  }
}
