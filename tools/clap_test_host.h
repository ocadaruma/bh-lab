// A minimal CLAP host for tests: runs the plugin in-process with a transport we control,
// and records every note it emits with the sample it landed on.
#pragma once

#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bhl_entry.h"

namespace bhl_test
{

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlock = 512;
constexpr double kTempo = 120.0;

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *)
{
  return nullptr;
}
void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

clap_host_t g_host = {CLAP_VERSION,     nullptr,            "basics-test",
                      "bh-lab",         "",                 "0.0.1",
                      hostGetExtension, hostRequestRestart, hostRequestProcess,
                      hostRequestCallback};

// --- event plumbing ---------------------------------------------------------

struct InputEvents
{
  clap_input_events_t api = {};
  std::vector<clap_event_note_t> notes;
  std::vector<clap_event_param_value_t> params;
  std::vector<const clap_event_header_t *> order;

  InputEvents()
  {
    api.ctx = this;
    api.size = [](const clap_input_events_t *list) {
      return static_cast<uint32_t>(static_cast<InputEvents *>(list->ctx)->order.size());
    };
    api.get = [](const clap_input_events_t *list, uint32_t index) {
      return static_cast<InputEvents *>(list->ctx)->order[index];
    };
  }

  // Rebuilt after filling, since the vectors may reallocate.
  void seal()
  {
    order.clear();
    for (const auto &note : notes) order.push_back(&note.header);
    for (const auto &param : params) order.push_back(&param.header);
  }

  void addNote(uint16_t type, uint32_t time, int16_t key, double velocity)
  {
    clap_event_note_t ev = {};
    ev.header.size = sizeof(ev);
    ev.header.time = time;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = type;
    ev.note_id = -1;
    ev.port_index = 0;
    ev.channel = 0;
    ev.key = key;
    ev.velocity = velocity;
    notes.push_back(ev);
  }

  void addParam(clap_id id, double value)
  {
    clap_event_param_value_t ev = {};
    ev.header.size = sizeof(ev);
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.param_id = id;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = value;
    params.push_back(ev);
  }
};

struct Emitted
{
  uint64_t sample = 0;  // Absolute, not relative to the block.
  int key = 0;
  bool on = false;
};

std::vector<Emitted> g_emitted;
uint64_t g_blockStart = 0;

bool CLAP_ABI collectEvent(const clap_output_events_t *, const clap_event_header_t *header)
{
  if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return true;
  if (header->type != CLAP_EVENT_NOTE_ON && header->type != CLAP_EVENT_NOTE_OFF &&
      header->type != CLAP_EVENT_NOTE_CHOKE) return true;

  const auto *note = reinterpret_cast<const clap_event_note_t *>(header);
  g_emitted.push_back({g_blockStart + header->time, note->key,
                       header->type == CLAP_EVENT_NOTE_ON});
  return true;
}

// --- the harness ------------------------------------------------------------

class Timeline
{
 public:
  explicit Timeline(const clap_plugin_t *plugin, uint32_t block = kBlock, double tempo = kTempo)
      : _plugin(plugin), _block(block), _tempo(tempo)
  {
  }

  // Runs one block, with the transport where the timeline currently sits.
  void run(InputEvents *input)
  {
    clap_event_transport_t transport = {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                      CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
    transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
    transport.tempo = _tempo;
    transport.song_pos_beats = static_cast<clap_beattime>(_beats * CLAP_BEATTIME_FACTOR);
    transport.tsig_num = 4;
    transport.tsig_denom = 4;
    transport.bar_number = static_cast<int32_t>(_beats / 4.0);
    transport.bar_start =
        static_cast<clap_beattime>(std::floor(_beats / 4.0) * 4.0 * CLAP_BEATTIME_FACTOR);

    InputEvents empty;
    InputEvents *events = input ? input : &empty;
    events->seal();

    clap_output_events_t out = {nullptr, collectEvent};
    clap_process_t process = {};
    process.frames_count = _block;
    process.transport = &transport;
    process.in_events = &events->api;
    process.out_events = &out;

    g_blockStart = _sample;
    _plugin->process(_plugin, &process);

    _sample += _block;
    _beats += _block * _tempo / (60.0 * kSampleRate);
  }

  void runFor(int blocks)
  {
    for (int i = 0; i < blocks; ++i) run(nullptr);
  }

  uint64_t sample() const
  {
    return _sample;
  }

 private:
  const clap_plugin_t *_plugin;
  uint32_t _block = kBlock;
  double _tempo = kTempo;
  uint64_t _sample = 0;
  double _beats = 0;
};

std::vector<Emitted> noteOns()
{
  std::vector<Emitted> ons;
  for (const auto &e : g_emitted)
    if (e.on) ons.push_back(e);
  return ons;
}

inline int g_failures = 0;

inline void expect(const char *what, bool ok, const std::string &detail = "")
{
  printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " \u2014 ",
         detail.c_str());
  if (!ok) ++g_failures;
}

// Creates and activates the plugin, ready to be driven.
inline const clap_plugin_t *makePlugin()
{
  bhl_entry_init("");
  const auto *factory = static_cast<const clap_plugin_factory_t *>(
      bhl_entry_get_factory(CLAP_PLUGIN_FACTORY_ID));
  const clap_plugin_t *plugin =
      factory->create_plugin(factory, &g_host, factory->get_plugin_descriptor(factory, 0)->id);
  plugin->init(plugin);
  plugin->activate(plugin, kSampleRate, 1, kBlock);
  plugin->start_processing(plugin);
  return plugin;
}

// Parameter ids, matching the plugin's table.
constexpr clap_id kVoicing = 0;
constexpr clap_id kControlRoot = 1;
constexpr clap_id kTriggerStart = 2;
constexpr clap_id kBorrowTop = 3;
constexpr clap_id kBorrowSecond = 4;
constexpr clap_id kBorrowThird = 5;

}  // namespace bhl_test
