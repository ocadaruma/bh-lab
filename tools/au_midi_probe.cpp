// Drives the installed Audio Unit outside of Logic, the way Logic does: instance,
// stream formats, render callback, MIDI output callback, Initialize, render.
//
// The point is the enable-time behaviour a host cannot see any other way: nothing
// may come out of the AU's MIDI output before the host sends a note, including the
// state a previous instance saved (Logic restores it before Initialize on every
// project open), and control keys must stay silent.
//
// Usage: au-midi-probe            (runs the built-in scenarios)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct MidiMessage
{
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  uint64_t sample;
};

std::vector<MidiMessage> g_received;
uint64_t g_blockStart = 0;

OSStatus midiOutputCallback(void *, const AudioTimeStamp *, UInt32, const MIDIPacketList *pktlist)
{
  const MIDIPacket *packet = &pktlist->packet[0];
  for (UInt32 i = 0; i < pktlist->numPackets; ++i)
  {
    for (UInt16 b = 0; b + 2 < packet->length + 1 && b < packet->length; b += 3)
      g_received.push_back({packet->data[b], packet->data[b + 1], packet->data[b + 2],
                            g_blockStart});
    packet = MIDIPacketNext(packet);
  }
  return noErr;
}

OSStatus silenceCallback(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32,
                         UInt32 frames, AudioBufferList *io)
{
  if (!io) return noErr;
  for (UInt32 i = 0; i < io->mNumberBuffers; ++i)
    memset(io->mBuffers[i].mData, 0, sizeof(float) * frames);
  return noErr;
}

bool check(OSStatus status, const char *what)
{
  if (status == noErr) return true;
  printf("ERROR: %s failed with %d\n", what, static_cast<int>(status));
  return false;
}

int g_failures = 0;

void expectNothing(const char *what, const std::vector<MidiMessage> &got)
{
  const bool ok = got.empty();
  printf("%s %s", ok ? "PASS" : "FAIL", what);
  if (!ok)
  {
    for (const auto &m : got)
      printf(" %02X %02X %02X", m.status, m.data1, m.data2);
    ++g_failures;
  }
  printf("\n");
}

constexpr UInt32 kFrames = 512;

class Probe
{
 public:
  bool open()
  {
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_MIDIProcessor;
    desc.componentSubType = 'BhLb';
    desc.componentManufacturer = 'Mayh';

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp)
    {
      printf("ERROR: bh-lab (aumi/BhLb/Mayh) is not installed\n");
      return false;
    }
    if (!check(AudioComponentInstanceNew(comp, &_au), "AudioComponentInstanceNew")) return false;

    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate = 48000;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                       kAudioFormatFlagIsNonInterleaved;
    fmt.mBytesPerPacket = sizeof(float);
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = sizeof(float);
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;

    AudioUnitSetProperty(_au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt,
                         sizeof(fmt));
    AudioUnitSetProperty(_au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt,
                         sizeof(fmt));

    AURenderCallbackStruct input = {silenceCallback, nullptr};
    AudioUnitSetProperty(_au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                         &input, sizeof(input));

    AUMIDIOutputCallbackStruct midiOut = {midiOutputCallback, nullptr};
    if (!check(AudioUnitSetProperty(_au, kAudioUnitProperty_MIDIOutputCallback,
                                    kAudioUnitScope_Global, 0, &midiOut, sizeof(midiOut)),
               "set MIDIOutputCallback"))
      return false;

    _buffers.resize(2 * kFrames, 0.f);
    return true;
  }

  // Logic restores a project's plugin state this way: Initialize the fresh instance,
  // then set ClassInfo on it (set before Initialize fails with Uninitialized).
  bool restoreState(const CFPropertyListRef &saved)
  {
    if (!saved) return false;
    return check(AudioUnitSetProperty(_au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global,
                                     0, &saved, sizeof(saved)),
                 "set ClassInfo");
  }

  bool initialize()
  {
    // Logic sets the bypass state before Initialize on project restore.
    UInt32 bypass = 0;
    AudioUnitSetProperty(_au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &bypass,
                         sizeof(bypass));
    return check(AudioUnitInitialize(_au), "AudioUnitInitialize");
  }

  void dispose()
  {
    if (_au)
    {
      AudioUnitUninitialize(_au);
      AudioComponentInstanceDispose(_au);
      _au = nullptr;
    }
  }

  CFPropertyListRef savedClassInfo()
  {
    CFPropertyListRef saved = nullptr;
    UInt32 size = sizeof(saved);
    if (!check(AudioUnitGetProperty(_au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                   &saved, &size),
               "get ClassInfo"))
      return nullptr;
    return saved;
  }

  void setParam(AudioUnitParameterID id, float value)
  {
    check(AudioUnitSetParameter(_au, id, kAudioUnitScope_Global, 0, value, 0),
          "AudioUnitSetParameter");
  }

  void sendNoteOn(uint8_t key, uint8_t velocity = 100, uint8_t channel = 0)
  {
    MusicDeviceMIDIEvent(_au, 0x90 | channel, key, velocity, 0);
  }
  void sendNoteOff(uint8_t key, uint8_t channel = 0)
  {
    MusicDeviceMIDIEvent(_au, 0x80 | channel, key, 0, 0);
  }

  std::vector<MidiMessage> render(int blocks = 1, bool verbose = false)
  {
    std::vector<MidiMessage> all;
    for (int i = 0; i < blocks; ++i)
    {
      g_received.clear();

      AudioBufferList *abl = reinterpret_cast<AudioBufferList *>(_ablStorage);
      abl->mNumberBuffers = 2;
      for (UInt32 i = 0; i < 2; ++i)
      {
        abl->mBuffers[i].mNumberChannels = 1;
        abl->mBuffers[i].mDataByteSize = kFrames * sizeof(float);
        abl->mBuffers[i].mData = _buffers.data() + i * kFrames;
      }

      AudioTimeStamp ts = {};
      ts.mSampleTime = _sampleTime;
      ts.mFlags = kAudioTimeStampSampleTimeValid;

      AudioUnitRenderActionFlags flags = 0;
      g_blockStart = static_cast<uint64_t>(_sampleTime);
      const OSStatus res = AudioUnitRender(_au, &flags, &ts, 0, kFrames, abl);
      if (res != noErr) printf("  (render returned %d)\n", static_cast<int>(res));

      _sampleTime += kFrames;
      all.insert(all.end(), g_received.begin(), g_received.end());
    }
    if (verbose)
      for (const auto &m : all) printf("  in> %02X %02X %02X\n", m.status, m.data1, m.data2);
    return all;
  }

 private:
  AudioUnit _au = nullptr;
  std::vector<float> _buffers;
  Float64 _sampleTime = 0;
  uint8_t _ablStorage[sizeof(AudioBufferList) + sizeof(AudioBuffer)] = {};
};

}  // namespace

int main()
{
  {
    // --- enable time: a fresh instance must not emit anything -------------------
    Probe probe;
    if (!probe.open()) return 2;
    if (!probe.initialize()) return 2;
    expectNothing("fresh instance, enable time emits nothing", probe.render(16));
    probe.dispose();
  }

  {
    // --- restart: restored state before Initialize must not emit anything -------
    // Instance A edits a parameter, saves its state the way a host persists it.
    CFPropertyListRef saved = nullptr;
    {
      Probe a;
      if (!a.open()) return 2;
      if (!a.initialize()) return 2;
      a.setParam(1, 57.0f);  // Control Zone Root: A2
      a.setParam(2, 72.0f);  // Trigger Zone Start: C4
      a.render();
      saved = a.savedClassInfo();
      if (!saved) return 2;
      a.dispose();
    }

    // Instance B is the "Logic restart": fresh instance, initialized, then its state
    // restored. Nothing may come out before a note is sent.
    Probe b;
    if (!b.open()) return 2;
    if (!b.initialize()) return 2;
    if (!b.restoreState(saved)) return 2;
    expectNothing("restored state, enable time emits nothing", b.render(16));

    // The restored state must actually be in force: A2 control octave, C4 trigger.
    for (int k : {57, 60, 64, 66}) b.sendNoteOn(k, 100);  // A2 C3 E3 F#3 -> A minor 6th
    b.sendNoteOn(72, 100);  // C4 trigger
    {
      const auto got = b.render(true);
      const std::vector<uint8_t> keys = {64, 66, 69, 72};  // A m6: E3 F#3 A3 C4
      bool chord = got.size() >= 4;
      if (chord)
        for (size_t i = 0; i < 4; ++i)
          if (got[i].status != 0x90 || got[i].data1 != keys[i]) chord = false;
      printf("%s restored state is in force (C4 -> %s)\n", chord ? "PASS" : "FAIL",
             chord ? "E3 F#3 A3 C4" : "wrong chord");
      if (!chord)
      {
        for (const auto &m : got) printf(" %02X %02X %02X", m.status, m.data1, m.data2);
        ++g_failures;
      }
    }
    b.sendNoteOff(72);
    for (int k : {57, 60, 64, 66}) b.sendNoteOff(k);
    {
      const auto got = b.render(true);
      const bool ok = got.size() == 4 && got[0].status == 0x80 && got[0].data1 == 64;
      printf("%s the chord releases, control notes stay silent\n", ok ? "PASS" : "FAIL");
      if (!ok) { for (const auto &m : got) printf(" %02X %02X %02X\n", m.status, m.data1, m.data2); ++g_failures; }
    }
    b.dispose();
    if (saved) CFRelease(saved);
  }

  {
    // --- same zones via explicit parameters, no state restore -------------------
    Probe c;
    if (!c.open()) return 2;
    if (!c.initialize()) return 2;
    c.setParam(1, 57.0f);  // Control Zone Root: A2
    c.setParam(2, 72.0f);  // Trigger Zone Start: C4
    c.render();
    for (int k : {57, 60, 64, 66}) c.sendNoteOn(k, 100);
    c.sendNoteOn(72, 100);
    {
      const auto got = c.render(true);
      const std::vector<uint8_t> keys = {64, 66, 69, 72};
      bool chord = got.size() >= 4;
      if (chord)
        for (size_t i = 0; i < 4; ++i)
          if (got[i].status != 0x90 || got[i].data1 != keys[i]) chord = false;
      printf("%s explicit params: C4 -> A m6 stack\n", chord ? "PASS" : "FAIL");
      if (!chord) { for (const auto &m : got) printf(" %02X %02X %02X\n", m.status, m.data1, m.data2); ++g_failures; }
    }
    c.sendNoteOff(72);
    for (int k : {57, 60, 64, 66}) c.sendNoteOff(k);
    c.render();
    c.dispose();
  }

  {
    // --- behaviour: control silent, trigger chords, pass-through ------------------
    Probe probe;
    if (!probe.open()) return 2;
    if (!probe.initialize()) return 2;

    // A held control key identifies the scale and emits nothing.
    probe.sendNoteOn(48, 100);  // C2 -> C major 6th
    expectNothing("C2 held is silent", probe.render());

    probe.sendNoteOn(60, 100);  // C3 -> C6 stack with C3 on top: E3 G3 A3 C3
    {
      const auto got = probe.render();
      const std::vector<uint8_t> keys = {52, 55, 57, 60};
      bool chord = got.size() >= 4;
      if (chord)
        for (size_t i = 0; i < 4; ++i)
          if (got[i].status != 0x90 || got[i].data1 != keys[i]) chord = false;
      printf("%s C3 -> C6 stack E3 G3 A3 C3\n", chord ? "PASS" : "FAIL");
      if (!chord)
      {
        for (const auto &m : got) printf(" %02X %02X %02X", m.status, m.data1, m.data2);
        ++g_failures;
      }
    }
    probe.sendNoteOff(60);
    probe.render();
    probe.sendNoteOff(48);
    expectNothing("control release is silent", probe.render());

    // With nothing held, the plugin is in pass-through mode. The velocity must
    // round-trip, not come back a step low.
    probe.sendNoteOn(60, 100);
    {
      const auto got = probe.render();
      const bool passthrough = got.size() == 1 && got[0].status == 0x90 && got[0].data1 == 60 &&
                              got[0].data2 == 100;
      printf("%s with nothing held, C3 passes through untouched (vel 100 -> 100)\n",
             passthrough ? "PASS" : "FAIL");
      if (!passthrough)
      {
        for (const auto &m : got) printf(" %02X %02X %02X", m.status, m.data1, m.data2);
        ++g_failures;
      }
    }
    probe.sendNoteOff(60);
    {
      const auto got = probe.render(true);
      const bool ok = got.size() == 1 && got[0].status == 0x80 && got[0].data1 == 60;
      printf("%s its note-off passes through too\n", ok ? "PASS" : "FAIL");
      if (!ok) { for (const auto &m : got) printf(" %02X %02X %02X\n", m.status, m.data1, m.data2); ++g_failures; }
    }

    probe.dispose();
  }

  if (g_failures)
  {
    printf("\n%d probe check(s) failed\n", g_failures);
    return 1;
  }
  printf("\nall probe checks passed\n");
  return 0;
}
