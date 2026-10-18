#include <QtMath>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "core/audiodsp.h"
#include "effects/internal/audiostreamstate.h"

using namespace amber::audiodsp;

namespace {

constexpr int kRate = 48000;

// n frames of a stereo sine (both channels alike)
std::vector<float> sine(double freq, double amplitude, int n) {
  std::vector<float> v(size_t(n) * 2);
  for (int i = 0; i < n; i++) v[i * 2] = v[i * 2 + 1] = float(amplitude * std::sin(2.0 * M_PI * freq * i / kRate));
  return v;
}

// max |x| over frames [from, end)
double peak(const std::vector<float>& v, int from) {
  double p = 0;
  for (size_t i = size_t(from) * 2; i < v.size(); i++) p = std::max(p, double(std::fabs(v[i])));
  return p;
}

double to_db(double x) { return 20.0 * std::log10(x); }

// Steady-state gain of a biquad on a sine: 1 s in, peak over the last half
double biquad_gain(const BiquadCoeffs& c, double freq) {
  std::vector<float> v = sine(freq, 0.25, kRate);
  Biquad b;
  b.set(c);
  b.process(v.data(), kRate);
  return peak(v, kRate / 2) / 0.25;
}

// What an effect does with one chunk: per-stream delay line, reset on a jump
void delay_chunk(amber::StreamStates<DelayLine>& states, const amber::AudioProcessContext& ctx, double start,
                 float* frames, int n) {
  bool reset;
  std::shared_ptr<DelayLine> line = states.acquire(ctx, start, n, &reset);
  if (reset) line->reset(ctx.sample_rate);
  line->set(10.0, 0.5, 0.5, ctx.sample_rate);
  line->process(frames, n);
}

// Sum of x^2 over both channels, frames [from, to)
double energy(const std::vector<float>& v, int from, int to) {
  double e = 0;
  for (size_t i = size_t(from) * 2; i < size_t(to) * 2; i++) e += double(v[i]) * v[i];
  return e;
}

// n frames of silence with a unit impulse on both channels at frame 0
std::vector<float> impulse(int n) {
  std::vector<float> v(size_t(n) * 2, 0.0f);
  v[0] = v[1] = 1.0f;
  return v;
}

// Impulse response of a fresh reverb, 100 % wet, processed in cacher-sized chunks
std::vector<float> reverb_ir(double room, double damp, double width, int n) {
  std::vector<float> v = impulse(n);
  Reverb r;
  r.reset(kRate);
  r.set(room, damp, width, 1.0);
  for (int i = 0; i < n; i += 4096) r.process(v.data() + size_t(i) * 2, std::min(4096, n - i));
  return v;
}

}  // namespace

class TestAudioDsp : public QObject {
  Q_OBJECT
 private slots:
  void s16RoundTripIsExact() {
    std::vector<quint8> in(8 * 4);
    const qint16 values[] = {0, 1, -1, 32767, -32768, 12345, -12345, 2};
    for (int i = 0; i < 8; i++) {
      const qint16 s = values[i];
      in[i * 2] = quint8(s & 0xFF);
      in[i * 2 + 1] = quint8((s >> 8) & 0xFF);
    }
    std::vector<float> f(8);
    s16_to_float(in.data(), f.data(), 4);  // 4 stereo frames
    std::vector<quint8> out(in.size());
    float_to_s16(f.data(), out.data(), 4);
    QVERIFY(out == in);
    // Saturation
    const float loud[4] = {2.0f, -2.0f, 1.0f, -1.0f};
    quint8 sat[8];
    float_to_s16(loud, sat, 2);
    QCOMPARE(qint16(sat[0] | (sat[1] << 8)), qint16(32767));
    QCOMPARE(qint16(sat[2] | (sat[3] << 8)), qint16(-32768));
    QCOMPARE(qint16(sat[4] | (sat[5] << 8)), qint16(32767));
  }

  void eqZeroGainIsIdentity() {
    std::vector<float> v = sine(1000, 0.5, 4096);
    const std::vector<float> ref = v;
    Biquad lo, mid, hi;
    lo.set(low_shelf(100, 0, kRate));
    mid.set(peaking(1000, 0, 1, kRate));
    hi.set(high_shelf(8000, 0, kRate));
    lo.process(v.data(), 4096);
    mid.process(v.data(), 4096);
    hi.process(v.data(), 4096);
    double d = 0;
    for (size_t i = 0; i < v.size(); i++) d = std::max(d, double(std::fabs(v[i] - ref[i])));
    QVERIFY2(d < 1e-6, qPrintable(QString("max delta %1").arg(d)));
  }

  void eqPeakingGain() {
    const double centre = biquad_gain(peaking(1000, 6, 1, kRate), 1000);
    const double below = biquad_gain(peaking(1000, 6, 1, kRate), 100);
    const double above = biquad_gain(peaking(1000, 6, 1, kRate), 10000);
    qInfo("peaking +6 dB at 1 kHz: centre %.4f, 100 Hz %.4f, 10 kHz %.4f", centre, below, above);
    QVERIFY(qAbs(centre - 1.995) < 1.995 * 0.02);
    QVERIFY(qAbs(below - 1.0) < 0.05);
    QVERIFY(qAbs(above - 1.0) < 0.05);
  }

  void eqShelves() {
    const double low = biquad_gain(low_shelf(100, -12, kRate), 30);
    const double low_pass = biquad_gain(low_shelf(100, -12, kRate), 5000);
    const double high = biquad_gain(high_shelf(8000, 6, kRate), 20000);
    qInfo("low shelf -12 dB at 100 Hz on 30 Hz: %.4f, on 5 kHz: %.4f; high shelf +6 dB at 8 kHz on 20 kHz: %.4f", low,
          low_pass, high);
    QVERIFY(qAbs(low - 0.251) < 0.251 * 0.03);
    QVERIFY(qAbs(low_pass - 1.0) < 0.01);
    QVERIFY(qAbs(high - 1.995) < 1.995 * 0.03);
  }

  void compressorSettlesAtTheRatio() {
    // 0 dBFS in, threshold -20, ratio 4: -20 + 20 / 4 = -15 dBFS out
    std::vector<float> v = sine(1000, 1.0, kRate);
    Compressor c;
    c.set(-20, 4, 10, 100, 0, kRate);
    c.process(v.data(), kRate);
    const double out = to_db(peak(v, kRate / 2));
    qInfo("compressor, 0 dBFS sine, -20 dB 4:1: settles at %.3f dBFS", out);
    QVERIFY(qAbs(out - -15.0) < 0.5);
  }

  void compressorBelowThresholdIsUntouched() {
    std::vector<float> v = sine(1000, 0.05, kRate / 4);  // -26 dBFS
    const std::vector<float> ref = v;
    Compressor c;
    c.set(-20, 4, 10, 100, 0, kRate);
    c.process(v.data(), kRate / 4);
    const double ratio_db = to_db(peak(v, 0) / peak(ref, 0));
    QVERIFY2(qAbs(ratio_db) < 0.01, qPrintable(QString("%1 dB").arg(ratio_db)));
  }

  void limiterNeverExceedsTheCeiling() {
    const double ceiling = std::pow(10.0, -1.0 / 20.0);
    std::vector<float> v = sine(440, ceiling * 2.0, kRate);  // +6 dB over
    Limiter l;
    l.set(-1, 50, kRate);
    l.process(v.data(), kRate);
    const double p = peak(v, 0);
    qInfo("limiter, +6 dB over a -1 dB ceiling: max |x| %.6f (ceiling %.6f)", p, ceiling);
    QVERIFY(p <= ceiling + 1e-6);
    QVERIFY(p > ceiling * 0.99);  // and it does reach it
  }

  void limiterBelowCeilingIsUntouched() {
    std::vector<float> v = sine(440, 0.5, 4096);
    const std::vector<float> ref = v;
    Limiter l;
    l.set(-1, 50, kRate);
    l.process(v.data(), 4096);
    QVERIFY(v == ref);
  }

  void delayEchoes() {
    const int n = kRate;
    std::vector<float> v(size_t(n) * 2, 0.0f);
    v[0] = v[1] = 1.0f;
    DelayLine d;
    d.reset(kRate);
    d.set(250, 0.3, 0.3, kRate);
    d.process(v.data(), n);
    const int lag = int(std::lround(250.0 * kRate / 1000.0));
    QCOMPARE(lag, 12000);
    QCOMPARE(v[0], 1.0f - 0.3f);
    QVERIFY(qAbs(v[size_t(lag) * 2] - 0.3f) < 1e-6);
    QVERIFY(qAbs(v[size_t(lag) * 2 + 1] - 0.3f) < 1e-6);
    QVERIFY(qAbs(v[size_t(lag) * 4] - 0.3f * 0.3f) < 1e-6);
    // Silence everywhere else
    for (int i = 1; i < n; i++) {
      if (i == lag || i == lag * 2 || i == lag * 3) continue;
      QVERIFY2(v[size_t(i) * 2] == 0.0f, qPrintable(QString("frame %1 = %2").arg(i).arg(v[size_t(i) * 2])));
    }
  }

  // Two inner clips of a nested sequence run through one effect instance, interleaved chunk by chunk at the same
  // timecodes: each must come out as if it had its own instance
  void interleavedStreamsMatchSeparateInstances() {
    constexpr int kChunk = 4096;
    constexpr int kChunks = 6;
    const std::vector<float> a = sine(440, 0.5, kChunk * kChunks);
    const std::vector<float> b = sine(3000, 0.3, kChunk * kChunks);
    int stream_a = 0, stream_b = 0;  // any two distinct addresses
    const amber::AudioProcessContext ctx_a{&stream_a, kRate};
    const amber::AudioProcessContext ctx_b{&stream_b, kRate};

    std::vector<float> shared_a = a, shared_b = b, alone_a = a, alone_b = b, mixed_a = a, mixed_b = b;
    amber::StreamStates<DelayLine> shared, only_a, only_b, one_key;
    for (int k = 0; k < kChunks; k++) {
      const double start = double(k) * kChunk / kRate;
      const size_t off = size_t(k) * kChunk * 2;
      delay_chunk(shared, ctx_a, start, shared_a.data() + off, kChunk);
      delay_chunk(shared, ctx_b, start, shared_b.data() + off, kChunk);
      delay_chunk(only_a, ctx_a, start, alone_a.data() + off, kChunk);
      delay_chunk(only_b, ctx_b, start, alone_b.data() + off, kChunk);
      // What state kept per instance would do: one key for both streams
      delay_chunk(one_key, amber::AudioProcessContext{}, start, mixed_a.data() + off, kChunk);
      delay_chunk(one_key, amber::AudioProcessContext{}, start, mixed_b.data() + off, kChunk);
    }
    QVERIFY(shared_a == alone_a);
    QVERIFY(shared_b == alone_b);
    QVERIFY(shared_a != a);       // the delay did something
    QVERIFY(mixed_a != alone_a);  // and the check has teeth
  }

  void jumpResetsContinuousRunDoesNot() {
    amber::StreamStates<int> states;
    const amber::AudioProcessContext ctx{nullptr, kRate};
    bool reset = false;
    *states.acquire(ctx, 0.0, 4800, &reset) = 7;
    QVERIFY(reset);  // new stream
    QCOMPARE(*states.acquire(ctx, 0.1, 4800, &reset), 7);
    QVERIFY(!reset);  // continuous
    *states.acquire(ctx, 0.2005, 4800, &reset) = 8;
    QVERIFY(!reset);  // within the 2 ms tolerance
    states.acquire(ctx, 1.3, 4800, &reset);
    QVERIFY(reset);  // 1 s jump
    *states.acquire(ctx, 1.4, 4800, &reset) = 9;
    QVERIFY(!reset);
    // A step backwards (the rewind at playback start, a scrub grain) resets too
    states.acquire(ctx, 1.49, 4800, &reset);
    QVERIFY(reset);
  }

  void rateChangeResets() {
    amber::StreamStates<int> states;
    bool reset = false;
    *states.acquire(amber::AudioProcessContext{nullptr, 48000}, 0.0, 4800, &reset) = 3;
    states.acquire(amber::AudioProcessContext{nullptr, 44100}, 0.1, 4410, &reset);
    QVERIFY(reset);
  }

  void evictsTheLeastRecentlyUsedStreamPast64() {
    amber::StreamStates<int> states;
    std::vector<int> keys(amber::StreamStates<int>::kMaxStreams + 2);
    auto ctx = [&keys](size_t i) { return amber::AudioProcessContext{&keys[i], kRate}; };
    bool reset = false;
    std::shared_ptr<int> held = states.acquire(ctx(0), 0.0, 4800, &reset);
    *held = 5;
    // 64 more streams: the 65th evicts key 0, the least recently used
    for (size_t i = 1; i <= size_t(amber::StreamStates<int>::kMaxStreams); i++) {
      *states.acquire(ctx(i), 0.0, 4800, &reset) = int(i);
    }
    QCOMPARE(*held, 5);  // still alive in the caller's hands
    // Key 1 survived: same state, no reset
    QCOMPARE(*states.acquire(ctx(1), 0.1, 4800, &reset), 1);
    QVERIFY(!reset);
    // Key 0 was dropped, so it comes back as a new stream (and evicts key 2, now the oldest)
    QCOMPARE(*states.acquire(ctx(0), 0.1, 4800, &reset), 0);
    QVERIFY(reset);
    QCOMPARE(*states.acquire(ctx(3), 0.1, 4800, &reset), 3);
    QVERIFY(!reset);
    states.acquire(ctx(2), 0.1, 4800, &reset);
    QVERIFY(reset);
  }

  void delayResetEmptiesTheBuffer() {
    std::vector<float> v(size_t(kRate) * 2, 0.0f);
    v[0] = v[1] = 1.0f;
    DelayLine d;
    d.reset(kRate);
    d.set(250, 0.5, 0.5, kRate);
    d.process(v.data(), 1000);  // the impulse is in the buffer, its echo not out yet
    d.reset(kRate);             // same rate: cleared in place
    std::fill(v.begin(), v.end(), 0.0f);
    d.process(v.data(), kRate);
    QCOMPARE(peak(v, 0), 0.0);
  }

  void delayFeedbackIsCappedAndTailsFlushToZero() {
    // Feedback 1.0 is capped at 0.95: each echo is 0.95 of the previous one, and after 1000 echoes (5e-23) the
    // tail is flushed to exact zeros instead of running on in denormals
    const int lag = kRate / 100;  // 10 ms
    std::vector<float> v(size_t(kRate) * 12 * 2, 0.0f);
    v[0] = v[1] = 1.0f;
    DelayLine d;
    d.reset(kRate);
    d.set(10, 1.0, 1.0, kRate);
    d.process(v.data(), kRate * 12);
    QVERIFY(qAbs(v[size_t(lag) * 2] - 1.0f) < 1e-6);
    QVERIFY(qAbs(v[size_t(lag) * 4] - 0.95f) < 1e-6);
    QCOMPARE(peak(v, kRate * 11), 0.0);
  }

  void biquadTailFlushesToZero() {
    std::vector<float> v(size_t(kRate) * 2 * 2, 0.0f);
    v[0] = v[1] = 1.0f;
    Biquad b;
    b.set(peaking(1000, 12, 4, kRate));
    b.process(v.data(), kRate * 2);
    QCOMPARE(peak(v, kRate), 0.0);
  }

  void reverbMixZeroIsIdentity() {
    std::vector<float> v = sine(440, 0.5, 8192);
    const std::vector<float> ref = v;
    Reverb r;
    r.reset(kRate);
    r.set(0.5, 0.5, 1.0, 0.0);
    r.process(v.data(), 8192);
    QVERIFY(v == ref);
  }

  void reverbImpulseTailDecays() {
    const std::vector<float> v = reverb_ir(0.5, 0.5, 1.0, kRate * 2);
    const double early = energy(v, kRate / 10, kRate * 6 / 10);
    const double late = energy(v, kRate, kRate * 3 / 2);
    qInfo("reverb impulse, room 50 %%: energy [0.1, 0.6] s %.6g, [1.0, 1.5] s %.6g", early, late);
    QVERIFY(peak(v, kRate / 10) > 0);  // still ringing after 100 ms
    QVERIFY(early > 0);
    QVERIFY(late < early);
  }

  void reverbLargerRoomRingsLonger() {
    const double small = energy(reverb_ir(0.3, 0.5, 1.0, kRate * 2), kRate, kRate * 2);
    const double large = energy(reverb_ir(0.9, 0.5, 1.0, kRate * 2), kRate, kRate * 2);
    qInfo("reverb impulse, energy [1, 2] s: room 30 %% %.6g, room 90 %% %.6g", small, large);
    QVERIFY(large > small);
  }

  void reverbNoiseStaysBounded() {
    // 10 s of full-scale white noise (both channels alike, the loudest tank input) at 100 % wet: finite, and below 4
    // at the default room. The largest, least damped room rings much louder, but still doesn't run away.
    const int n = kRate * 10;
    const struct {
      double room, damp, bound;
    } cases[] = {{0.5, 0.5, 4.0}, {1.0, 0.0, 8.0}};
    for (const auto& tc : cases) {
      std::vector<float> v(size_t(n) * 2);
      std::mt19937 rng(1234);
      std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
      for (int i = 0; i < n; i++) v[i * 2] = v[i * 2 + 1] = dist(rng);
      Reverb r;
      r.reset(kRate);
      r.set(tc.room, tc.damp, 1.0, 1.0);
      for (int i = 0; i < n; i += 4096) r.process(v.data() + size_t(i) * 2, std::min(4096, n - i));
      bool finite = true;
      for (float x : v) finite = finite && std::isfinite(x);
      const double p = peak(v, 0);
      const double rms = std::sqrt(energy(v, n / 2, n) / n);
      qInfo(
          "reverb, 10 s of full-scale noise (rms 0.577), room %.0f %% damping %.0f %%: peak %.4f, rms %.4f (last 5 s)",
          tc.room * 100, tc.damp * 100, p, rms);
      QVERIFY(finite);
      QVERIFY2(p < tc.bound, qPrintable(QString("peak %1").arg(p)));
    }
  }

  void reverbWidthZeroIsMono() {
    const std::vector<float> v = reverb_ir(0.5, 0.5, 0.0, kRate);
    for (int i = 0; i < kRate; i++) {
      QVERIFY2(v[i * 2] == v[i * 2 + 1],
               qPrintable(QString("frame %1: %2 vs %3").arg(i).arg(v[i * 2]).arg(v[i * 2 + 1])));
    }
    // And full width is not
    const std::vector<float> wide = reverb_ir(0.5, 0.5, 1.0, kRate);
    bool differs = false;
    for (int i = 0; i < kRate && !differs; i++) differs = wide[i * 2] != wide[i * 2 + 1];
    QVERIFY(differs);
  }

  void reverbResetEmptiesTheTank() {
    std::vector<float> v = impulse(kRate);
    Reverb r;
    r.reset(kRate);
    r.set(0.5, 0.5, 1.0, 1.0);
    r.process(v.data(), 1000);  // the impulse is in the combs, nothing out yet (the shortest is 1215 frames)
    Reverb control = r;
    r.reset(kRate);  // same rate: cleared in place
    std::fill(v.begin(), v.end(), 0.0f);
    std::vector<float> tail = v;
    r.process(v.data(), kRate);
    control.process(tail.data(), kRate);
    QCOMPARE(peak(v, 0), 0.0);
    QVERIFY(peak(tail, 0) > 0);  // without the reset it rings
  }

  void reverbTailFlushesToZero() {
    const std::vector<float> v = reverb_ir(0.5, 0.5, 1.0, kRate * 12);
    QCOMPARE(peak(v, kRate * 11), 0.0);
  }

  void chorusMixZeroIsIdentity() {
    std::vector<float> v = sine(440, 0.5, 8192);
    const std::vector<float> ref = v;
    Chorus c;
    c.reset(kRate);
    c.set(1.0, 3.0, 0.0, kRate);
    c.process(v.data(), 8192);
    QVERIFY(v == ref);
  }

  void chorusDepthZeroIsAPlainDelay() {
    // Base 7 ms = 336 frames at 48 kHz, an integer delay: the linear interpolation reads the sample itself
    const int lag = int(std::lround(Chorus::kBaseMs * kRate / 1000.0));
    QCOMPARE(lag, 336);
    const int n = 8192;
    std::vector<float> x(size_t(n) * 2);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (float& s : x) s = dist(rng);
    std::vector<float> v = x;
    Chorus c;
    c.reset(kRate);
    c.set(1.0, 0.0, 0.5, kRate);
    c.process(v.data(), n);
    double d = 0;
    for (int i = 0; i < n; i++) {
      for (int ch = 0; ch < 2; ch++) {
        const double delayed = i >= lag ? x[size_t(i - lag) * 2 + ch] : 0.0;
        const double expected = x[size_t(i) * 2 + ch] * 0.5 + delayed * 0.5;
        d = std::max(d, std::fabs(v[size_t(i) * 2 + ch] - expected));
      }
    }
    QVERIFY2(d < 1e-4, qPrintable(QString("max delta %1").arg(d)));
  }

  void chorusSineStaysBoundedAndWide() {
    std::vector<float> v = sine(440, 0.5, kRate);
    Chorus c;
    c.reset(kRate);
    c.set(1.0, 3.0, 0.5, kRate);
    c.process(v.data(), kRate);
    const double p = peak(v, 0);
    double side = 0;
    for (int i = 0; i < kRate; i++) side = std::max(side, double(std::fabs(v[i * 2] - v[i * 2 + 1])));
    qInfo("chorus, 0.5 sine, depth 3 ms, mix 50 %%: peak %.4f, max |L - R| %.4f", p, side);
    QVERIFY(p <= 0.5 + 1e-6);
    QVERIFY(side > 0.01);
  }

  void chorusLfoPersistsAcrossChunks() {
    const std::vector<float> in = sine(440, 0.5, 8192);
    std::vector<float> whole = in, split = in;
    Chorus a, b;
    a.reset(kRate);
    b.reset(kRate);
    a.set(2.0, 5.0, 0.5, kRate);
    b.set(2.0, 5.0, 0.5, kRate);
    a.process(whole.data(), 8192);
    b.process(split.data(), 4096);
    b.process(split.data() + 4096 * 2, 4096);
    QVERIFY(whole == split);
    QVERIFY(whole != in);
  }

  void scopeKeepsTheOutermostContext() {
    QVERIFY(amber::current_audio_context().stream == nullptr);
    QCOMPARE(amber::current_audio_context().sample_rate, 48000);
    int leaf = 0, nest = 0;
    const amber::AudioProcessContext outer{&leaf, 44100};
    const amber::AudioProcessContext inner{&nest, 96000};
    {
      amber::AudioProcessScope s1(&outer);
      QCOMPARE(amber::current_audio_context().stream, static_cast<const void*>(&leaf));
      {
        amber::AudioProcessScope s2(&inner);
        QCOMPARE(amber::current_audio_context().stream, static_cast<const void*>(&leaf));
        QCOMPARE(amber::current_audio_context().sample_rate, 44100);
      }
      QCOMPARE(amber::current_audio_context().stream, static_cast<const void*>(&leaf));
    }
    QVERIFY(amber::current_audio_context().stream == nullptr);
  }
};

QTEST_GUILESS_MAIN(TestAudioDsp)
#include "test_audiodsp.moc"
