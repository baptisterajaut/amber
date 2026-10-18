// Test-only LADSPA plugins, built as a module into the test build dir (never installed) so the hosting tests run
// without the LADSPA SDK. They mirror the SDK plugins the tests were written against, same ports and controls:
//   4243001 "Amber Test Mono Gain"    1 in, 1 out, control 0 = gain (like amp_mono)
//   4243002 "Amber Test Stereo Gain"  2 in, 2 out, control 0 = gain (like amp_stereo)
//   4243003 "Amber Test Delay Line"   1 in, 1 out, control 0 = delay (s, 0..5), 1 = dry/wet (like delay_5s), with
//                                     activate() (clears the line) and no deactivate(), as the SDK's delay
// Every instance counts the activate() calls it gets while already active, which ladspa.h forbids;
// amber_test_ladspa_violations() returns the total.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "include/ladspa.h"

namespace {

std::atomic<int> g_violations{0};

constexpr float kMaxDelaySeconds = 5.0f;

struct Gain {
  LADSPA_Data* gain = nullptr;
  LADSPA_Data* in[2] = {nullptr, nullptr};
  LADSPA_Data* out[2] = {nullptr, nullptr};
  int channels = 1;
};

struct Delay {
  std::vector<float> line;
  size_t pos = 0;
  unsigned long rate = 0;
  bool active = false;
  LADSPA_Data* delay = nullptr;
  LADSPA_Data* wet = nullptr;
  LADSPA_Data* in = nullptr;
  LADSPA_Data* out = nullptr;
};

LADSPA_Handle instantiate_gain(const LADSPA_Descriptor* d, unsigned long) {
  auto* g = new Gain;
  g->channels = d->PortCount == 3 ? 1 : 2;
  return g;
}

// Ports: 0 gain, then the inputs, then the outputs
void connect_gain(LADSPA_Handle h, unsigned long port, LADSPA_Data* data) {
  auto* g = static_cast<Gain*>(h);
  if (port == 0) {
    g->gain = data;
  } else if (port <= unsigned(g->channels)) {
    g->in[port - 1] = data;
  } else {
    g->out[port - 1 - g->channels] = data;
  }
}

void run_gain(LADSPA_Handle h, unsigned long frames) {
  auto* g = static_cast<Gain*>(h);
  for (int c = 0; c < g->channels; c++) {
    for (unsigned long i = 0; i < frames; i++) g->out[c][i] = g->in[c][i] * *g->gain;
  }
}

void cleanup_gain(LADSPA_Handle h) { delete static_cast<Gain*>(h); }

LADSPA_Handle instantiate_delay(const LADSPA_Descriptor*, unsigned long rate) {
  auto* d = new Delay;
  d->rate = rate;
  d->line.assign(size_t(kMaxDelaySeconds * rate) + 1, 0.0f);
  return d;
}

void connect_delay(LADSPA_Handle h, unsigned long port, LADSPA_Data* data) {
  auto* d = static_cast<Delay*>(h);
  LADSPA_Data** ports[] = {&d->delay, &d->wet, &d->in, &d->out};
  *ports[port] = data;
}

void activate_delay(LADSPA_Handle h) {
  auto* d = static_cast<Delay*>(h);
  if (d->active) g_violations++;  // no deactivate(): a second activate() on this instance breaks the rule
  d->active = true;
  std::fill(d->line.begin(), d->line.end(), 0.0f);
  d->pos = 0;
}

void run_delay(LADSPA_Handle h, unsigned long frames) {
  auto* d = static_cast<Delay*>(h);
  const size_t size = d->line.size();
  const size_t delay = std::min(size - 1, size_t(std::lrint(std::clamp(*d->delay, 0.0f, kMaxDelaySeconds) * d->rate)));
  const float wet = std::clamp(*d->wet, 0.0f, 1.0f);
  for (unsigned long i = 0; i < frames; i++) {
    d->line[d->pos] = d->in[i];
    const float delayed = d->line[(d->pos + size - delay) % size];
    d->out[i] = (1.0f - wet) * d->in[i] + wet * delayed;
    d->pos = (d->pos + 1) % size;
  }
}

void cleanup_delay(LADSPA_Handle h) { delete static_cast<Delay*>(h); }

const LADSPA_PortDescriptor kControlIn = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
const LADSPA_PortDescriptor kAudioIn = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
const LADSPA_PortDescriptor kAudioOut = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
const LADSPA_PortRangeHint kNoHint = {0, 0, 0};
const LADSPA_PortRangeHint kGainHint = {LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_1, 0,
                                        0};

const LADSPA_PortDescriptor kMonoGainPorts[] = {kControlIn, kAudioIn, kAudioOut};
const char* const kMonoGainNames[] = {"Gain", "Input", "Output"};
const LADSPA_PortRangeHint kMonoGainHints[] = {kGainHint, kNoHint, kNoHint};

const LADSPA_PortDescriptor kStereoGainPorts[] = {kControlIn, kAudioIn, kAudioIn, kAudioOut, kAudioOut};
const char* const kStereoGainNames[] = {"Gain", "Input (Left)", "Input (Right)", "Output (Left)", "Output (Right)"};
const LADSPA_PortRangeHint kStereoGainHints[] = {kGainHint, kNoHint, kNoHint, kNoHint, kNoHint};

const LADSPA_PortDescriptor kDelayPorts[] = {kControlIn, kControlIn, kAudioIn, kAudioOut};
const char* const kDelayNames[] = {"Delay (Seconds)", "Dry/Wet Balance", "Input", "Output"};
const LADSPA_PortRangeHint kDelayHints[] = {
    {LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_1, 0, kMaxDelaySeconds},
    {LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE, 0, 1},
    kNoHint,
    kNoHint};

LADSPA_Descriptor descriptor(unsigned long id, const char* label, const char* name, unsigned long port_count,
                             const LADSPA_PortDescriptor* ports, const char* const* names,
                             const LADSPA_PortRangeHint* hints, bool delay) {
  LADSPA_Descriptor d{};
  d.UniqueID = id;
  d.Label = label;
  d.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
  d.Name = name;
  d.Maker = "Amber tests";
  d.Copyright = "GPLv3";
  d.PortCount = port_count;
  d.PortDescriptors = ports;
  d.PortNames = names;
  d.PortRangeHints = hints;
  d.instantiate = delay ? instantiate_delay : instantiate_gain;
  d.connect_port = delay ? connect_delay : connect_gain;
  d.activate = delay ? activate_delay : nullptr;
  d.run = delay ? run_delay : run_gain;
  d.deactivate = nullptr;
  d.cleanup = delay ? cleanup_delay : cleanup_gain;
  return d;
}

const LADSPA_Descriptor kDescriptors[] = {
    descriptor(4243001, "amber_test_gain_mono", "Amber Test Mono Gain", 3, kMonoGainPorts, kMonoGainNames,
               kMonoGainHints, false),
    descriptor(4243002, "amber_test_gain_stereo", "Amber Test Stereo Gain", 5, kStereoGainPorts, kStereoGainNames,
               kStereoGainHints, false),
    descriptor(4243003, "amber_test_delay", "Amber Test Delay Line", 4, kDelayPorts, kDelayNames, kDelayHints, true),
};

}  // namespace

// Exported as is: mingw exports every symbol of a DLL without dllexport
extern "C" {

const LADSPA_Descriptor* ladspa_descriptor(unsigned long index) {
  return index < sizeof(kDescriptors) / sizeof(kDescriptors[0]) ? &kDescriptors[index] : nullptr;
}

int amber_test_ladspa_violations() { return g_violations.load(); }

}  // extern "C"
