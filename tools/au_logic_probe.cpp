// Emulates what Logic Pro does with an AUv2 plugin and dumps every MIDI event
// the AU emits, as UMP words, so input/output translation can be inspected
// outside Logic.
//
//   - queries kAudioUnitProperty_AudioUnitMIDIProtocol
//   - sets kAudioUnitProperty_HostMIDIProtocol
//   - installs kAudioUnitProperty_MIDIOutputEventListCallback (Logic's modern
//     output path, preferred by the wrapper over the legacy callback)
//   - also installs the legacy kAudioUnitProperty_MIDIOutputCallback to catch
//     anything that still goes the old way
//
// Input is fed three ways: legacy MusicDeviceMIDIEvent, UMP
// MusicDeviceMIDIEventList (protocol 1.0, MT 0x2), and the extended-note
// MusicDeviceStartNote/StopNote API.

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr UInt32 kFrames = 512;

void dumpUmpWord(uint32_t w, const char *tag)
{
  const uint32_t mt = (w >> 28) & 0xF;
  const uint32_t grp = (w >> 24) & 0xF;
  if (mt == 0x2)
    printf("%s MT2 grp=%u status=%02X d1=%02X d2=%02X\n", tag, grp, (w >> 16) & 0xFF,
            (w >> 8) & 0xFF, w & 0xFF);
  else if (mt == 0x4)
    printf("%s MT4 grp=%u status=%02X idx=%u word1=%08X\n", tag, grp, (w >> 16) & 0xFF,
            (w >> 8) & 0x7F, 0);
  else
    printf("%s MT%X word=%08X\n", tag, mt, w);
}

OSStatus eventListCallback(AUEventSampleTime eventSampleTime, uint8_t cable,
                          const MIDIEventList *evtlist)
{
  printf("  [out-list] refSample=%lld cable=%u numPackets=%u protocol=0x%08X\n",
         (long long)eventSampleTime, cable, evtlist->numPackets, evtlist->protocol);
  const MIDIEventPacket *pkt = &evtlist->packet[0];
  for (UInt32 p = 0; p < evtlist->numPackets; ++p)
  {
    printf("    pkt off=%llu words:", (unsigned long long)pkt->timeStamp);
    for (UInt32 i = 0; i < pkt->wordCount; ++i) printf(" %08X", pkt->words[i]);
    printf("\n");
    pkt = MIDIEventPacketNext(pkt);
  }
  return noErr;
}

OSStatus legacyMidiCallback(void *, const AudioTimeStamp *, UInt32, const MIDIPacketList *pktlist)
{
  printf("  [out-legacy]");
  const MIDIPacket *packet = &pktlist->packet[0];
  for (UInt32 i = 0; i < pktlist->numPackets; ++i)
  {
    for (UInt16 b = 0; b + 2 < packet->length + 1 && b < packet->length; b += 3)
      printf(" %02X %02X %02X", packet->data[b], packet->data[b + 1], packet->data[b + 2]);
    packet = MIDIPacketNext(packet);
  }
  printf("\n");
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
    if (!comp) { printf("ERROR: bh-lab not installed\n"); return false; }
    if (AudioComponentInstanceNew(comp, &_au) != noErr) return false;

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

    // Logic installs the modern UMP output block
    AUMIDIEventListBlock blk = ^(AUEventSampleTime t, uint8_t cable, const MIDIEventList *list) {
      return eventListCallback(t, cable, list);
    };
    AudioUnitSetProperty(_au, kAudioUnitProperty_MIDIOutputEventListCallback,
                         kAudioUnitScope_Global, 0, &blk, sizeof(blk));

    // legacy callback too, to see if anything still uses it
    AUMIDIOutputCallbackStruct midiOut = {legacyMidiCallback, nullptr};
    AudioUnitSetProperty(_au, kAudioUnitProperty_MIDIOutputCallback, kAudioUnitScope_Global, 0,
                         &midiOut, sizeof(midiOut));

    SInt32 proto = 0;
    UInt32 protoSize = sizeof(proto);
    AudioUnitGetProperty(_au, kAudioUnitProperty_AudioUnitMIDIProtocol, kAudioUnitScope_Global, 0, &proto,
                         &protoSize);
    printf("AU reports MIDI protocol: 0x%08X (%s)\n", proto,
           proto == kMIDIProtocol_2_0 ? "2.0" : (proto == kMIDIProtocol_1_0 ? "1.0" : "?"));

    SInt32 hostProto = kHostProto;
    AudioUnitSetProperty(_au, kAudioUnitProperty_HostMIDIProtocol, kAudioUnitScope_Global, 0,
                         &hostProto, sizeof(hostProto));

    _buffers.resize(2 * kFrames, 0.f);
    return true;
  }

  bool initialize()
  {
    UInt32 bypass = 0;
    AudioUnitSetProperty(_au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &bypass,
                         sizeof(bypass));
    return AudioUnitInitialize(_au) == noErr;
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

  // legacy 3-byte MIDI input
  void midi1(uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
  {
    MusicDeviceMIDIEvent(_au, status, d1, d2, 0);
  }

  // UMP protocol-1.0 input list: one MT 0x2 word per call
  void ump1(uint8_t status, uint8_t d1, uint8_t d2)
  {
    static uint8_t buf[256];
    MIDIEventList *list = (MIDIEventList *)buf;
    MIDIEventPacket *cur = MIDIEventListInit(list, kMIDIProtocol_1_0);
    uint32_t word = (0x2u << 28) | (0x0u << 24) | (uint32_t(status) << 16) |
                    (uint32_t(d1) << 8) | d2;
    cur = MIDIEventListAdd(list, sizeof(buf), cur, 0, 1, &word);
    (void)cur;
    MusicDeviceMIDIEventList(_au, 0, list);
  }

  void startNote(float pitch, float velocity, uint8_t channel = 0)
  {
    NoteInstanceID id = 0;
    MusicDeviceNoteParams params = {};
    params.mPitch = pitch;
    params.mVelocity = velocity;
    MusicDeviceStartNote(_au, kMusicNoteEvent_Unused, channel, &id, 0, &params);
    _lastNoteId = id;
  }
  void stopNote()
  {
    MusicDeviceStopNote(_au, 0, _lastNoteId, 0);
  }

  void render(int blocks = 1)
  {
    for (int i = 0; i < blocks; ++i)
    {
      AudioBufferList *abl = reinterpret_cast<AudioBufferList *>(_ablStorage);
      abl->mNumberBuffers = 2;
      for (UInt32 c = 0; c < 2; ++c)
      {
        abl->mBuffers[c].mNumberChannels = 1;
        abl->mBuffers[c].mDataByteSize = kFrames * sizeof(float);
        abl->mBuffers[c].mData = _buffers.data() + c * kFrames;
      }
      AudioTimeStamp ts = {};
      ts.mSampleTime = _sampleTime;
      ts.mFlags = kAudioTimeStampSampleTimeValid;
      AudioUnitRenderActionFlags flags = 0;
      AudioUnitRender(_au, &flags, &ts, 0, kFrames, abl);
      _sampleTime += kFrames;
    }
  }

  static SInt32 kHostProto;

 private:
  AudioUnit _au = nullptr;
  std::vector<float> _buffers;
  uint8_t _ablStorage[sizeof(AudioBufferList) + sizeof(AudioBuffer)] = {};
  Float64 _sampleTime = 0;
  NoteInstanceID _lastNoteId = 0;
};

SInt32 Probe::kHostProto = kMIDIProtocol_1_0;

}  // namespace

int main(int argc, char **argv)
{
  const SInt32 hostProto = (argc > 1 && !strcmp(argv[1], "2")) ? kMIDIProtocol_2_0 : kMIDIProtocol_1_0;
  Probe::kHostProto = hostProto;
  printf("== host protocol set to %s ==\n", hostProto == kMIDIProtocol_2_0 ? "2.0" : "1.0");

  {
    Probe p;
    if (!p.open() || !p.initialize()) return 2;
    printf("\n-- legacy MIDI1 input: note-on 60 + pitch bend 0xE0 0x00 0x40 (center) --\n");
    p.midi1(0x90, 60, 100);
    p.midi1(0xE0, 0x00, 0x40);
    p.render();
    p.midi1(0x80, 60, 0);
    p.render();
    p.dispose();
  }
  {
    Probe p;
    if (!p.open() || !p.initialize()) return 2;
    printf("\n-- UMP protocol-1.0 input list: note-on 60 + pitch bend 0xE0 0x00 0x40 --\n");
    p.ump1(0x90, 60, 100);
    p.ump1(0xE0, 0x00, 0x40);
    p.render();
    p.ump1(0x80, 60, 0);
    p.render();
    p.dispose();
  }
  {
    Probe p;
    if (!p.open() || !p.initialize()) return 2;
    printf("\n-- extended-note API: StartNote pitch 60.0 vel 100 --\n");
    p.startNote(60.0, 100);
    p.render();
    p.stopNote();
    p.render();
    p.dispose();
  }
  {
    Probe p;
    if (!p.open() || !p.initialize()) return 2;
    printf("\n-- extended-note API: StartNote pitch 60.5 vel 100 (fractional) --\n");
    p.startNote(60.5, 100);
    p.render();
    p.stopNote();
    p.render();
    p.dispose();
  }
  {
    Probe p;
    if (!p.open() || !p.initialize()) return 2;
    printf("\n-- pass-through mode with a non-center bend 0xE0 0x10 0x00 (+1 semitone) --\n");
    p.midi1(0xE0, 0x10, 0x00);
    p.render();
    p.midi1(0x90, 60, 100);
    p.render();
    p.midi1(0x80, 60, 0);
    p.render();
    p.dispose();
  }
  return 0;
}
