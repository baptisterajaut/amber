// LADSPA hosting: port hints to field ranges, discovery from $LADSPA_PATH, and processing through the test-only
// plugins of tests/fixtures/ladspa (built with the test, so these run everywhere) and through the same plugins of the
// LADSPA SDK (amp.so, delay.so) they mirror. A machine without the SDK skips the SDK rows.

#include <QtTest>

#include <QTemporaryDir>

#include <cmath>
#include <deque>
#include <vector>

#include "effects/effect.h"
#include "effects/effectfield.h"
#include "effects/effectrow.h"
#include "effects/fields/doublefield.h"
#include "effects/internal/audiostreamstate.h"
#include "effects/internal/ladspaeffect.h"
#include "effects/internal/voideffect.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "test_appcontext_stub.h"

namespace {

constexpr int kRate = 44100;
constexpr int kChunk = 4096;  // frames per process_audio() call, as the cacher's

struct ScopedAppContext {
  TestAppContext ctx;
  ScopedAppContext() { amber::app_ctx = &ctx; }
  ~ScopedAppContext() { amber::app_ctx = nullptr; }
};

LADSPA_PortRangeHint hint(LADSPA_PortRangeHintDescriptor d, float lower = 0, float upper = 0) {
  LADSPA_PortRangeHint h;
  h.HintDescriptor = d;
  h.LowerBound = lower;
  h.UpperBound = upper;
  return h;
}

// Interleaved S16 stereo: silence for `silence` frames, then a 1 kHz sine at amplitude left / right (of full scale)
std::vector<qint16> tone(int frames, int silence, double left, double right) {
  std::vector<qint16> s(size_t(frames) * 2, 0);
  for (int i = silence; i < frames; i++) {
    const double v = std::sin(2.0 * M_PI * 1000.0 * (i - silence) / kRate);
    s[size_t(i) * 2] = qint16(std::lrint(v * left * 32767.0));
    s[size_t(i) * 2 + 1] = qint16(std::lrint(v * right * 32767.0));
  }
  return s;
}

double rms(const std::vector<qint16>& s, int channel, int from, int to) {
  double sum = 0;
  for (int i = from; i < to; i++) sum += double(s[size_t(i) * 2 + channel]) * s[size_t(i) * 2 + channel];
  return std::sqrt(sum / (to - from));
}

int first_above(const std::vector<qint16>& s, int channel, int threshold) {
  for (size_t i = 0; i < s.size() / 2; i++) {
    if (std::abs(int(s[i * 2 + channel])) > threshold) return int(i);
  }
  return -1;
}

int peak(const std::vector<qint16>& s) {
  int p = 0;
  for (qint16 v : s) p = std::max(p, std::abs(int(v)));
  return p;
}

// Runs s through e in kChunk pieces as the stream `stream` at kRate, the first piece at timecode start (seconds)
void process(Effect* e, std::vector<qint16>& s, const void* stream, double start) {
  const amber::AudioProcessContext ctx{stream, kRate};
  amber::AudioProcessScope scope(&ctx);
  const int frames = int(s.size() / 2);
  for (int done = 0; done < frames; done += kChunk) {
    const int n = std::min(kChunk, frames - done);
    const double t = start + double(done) / kRate;
    e->process_audio(t, t + double(n) / kRate, reinterpret_cast<quint8*>(s.data() + size_t(done) * 2), n * 4, 2);
  }
}

EffectField* field(Effect* e, const QString& id) {
  for (int i = 0; i < e->row_count(); i++) {
    for (int j = 0; j < e->row(i)->FieldCount(); j++) {
      if (e->row(i)->Field(j)->id() == id) return e->row(i)->Field(j);
    }
  }
  return nullptr;
}

}  // namespace

class TestLadspa : public QObject {
  Q_OBJECT

  std::vector<EffectMeta> system_;   // the scan of the default folders (or the user's $LADSPA_PATH)
  std::vector<EffectMeta> fixture_;  // the test plugins, found through $LADSPA_PATH
  QString amp_dir_;                  // folder holding the SDK's amp.so, empty if none
  QString delay_dir_;

  // tests/fixtures/ladspa: gain mono, gain stereo, delay line (amp_mono, amp_stereo, delay_5s lookalikes)
  static constexpr int kTestGainMono = 4243001;
  static constexpr int kTestGainStereo = 4243002;
  static constexpr int kTestDelay = 4243003;
  static bool is_fixture(int id) { return id >= kTestGainMono && id <= kTestDelay; }

  const EffectMeta* plugin(unsigned long id) {
    for (const std::vector<EffectMeta>* list : {&fixture_, &system_}) {
      for (const EffectMeta& em : *list) {
        if (em.plugin_id == id) return &em;
      }
    }
    return nullptr;
  }

  // Rows of the delay line tests: the test plugin always, the SDK's delay_5s when installed
  static void delay_rows() {
    QTest::addColumn<int>("id");
    QTest::newRow("test delay") << kTestDelay;
    QTest::newRow("delay_5s") << 1043;
  }

 private slots:
  void initTestCase() {
    system_ = amber::ladspa::scan(amber::ladspa::search_paths());
    for (const EffectMeta& em : system_) {
      if (em.plugin_id == 1048 && em.filename == "amp.so") amp_dir_ = em.path;
      if (em.plugin_id == 1043 && em.filename == "delay.so") delay_dir_ = em.path;
    }
    qInfo("%d LADSPA plugins in the default folders; amp.so in '%s', delay.so in '%s'", int(system_.size()),
          qPrintable(amp_dir_), qPrintable(delay_dir_));

    const QByteArray saved = qgetenv("LADSPA_PATH");
    qputenv("LADSPA_PATH", QByteArray(AMBER_TEST_LADSPA_DIR));
    fixture_ = amber::ladspa::scan(amber::ladspa::search_paths());
    if (saved.isNull()) {
      qunsetenv("LADSPA_PATH");
    } else {
      qputenv("LADSPA_PATH", saved);
    }
  }

  // The test plugins are found through $LADSPA_PATH, sorted by name, hostable
  void testPluginsDiscovered() {
    QStringList names;
    for (const EffectMeta& em : fixture_) names.append(em.name);
    QCOMPARE(names, QStringList({"Amber Test Delay Line", "Amber Test Mono Gain", "Amber Test Stereo Gain"}));
    for (const EffectMeta& em : fixture_) {
      QCOMPARE(em.internal, int(EFFECT_INTERNAL_LADSPA));
      QCOMPARE(em.subtype, int(EFFECT_TYPE_AUDIO));
    }
  }

  void portRanges_data() {
    QTest::addColumn<int>("descriptor");
    QTest::addColumn<float>("lower");
    QTest::addColumn<float>("upper");
    QTest::addColumn<int>("rate");
    QTest::addColumn<bool>("has_min");
    QTest::addColumn<bool>("has_max");
    QTest::addColumn<double>("min");
    QTest::addColumn<double>("max");
    QTest::addColumn<double>("def");
    QTest::addColumn<int>("decimals");
    QTest::addColumn<bool>("toggled");

    const int B = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE;
    const int L = LADSPA_HINT_LOGARITHMIC;
    QTest::newRow("no hints") << 0 << 0.f << 0.f << kRate << false << false << 0.0 << 0.0 << 0.0 << 2 << false;
    QTest::newRow("no default, bounded") << B << 2.f << 8.f << kRate << true << true << 2.0 << 8.0 << 2.0 << 2 << false;
    QTest::newRow("no default, above only")
        << int(LADSPA_HINT_BOUNDED_ABOVE) << 0.f << -3.f << kRate << false << true << 0.0 << -3.0 << -3.0 << 2 << false;
    QTest::newRow("minimum") << (B | LADSPA_HINT_DEFAULT_MINIMUM) << 2.f << 10.f << kRate << true << true << 2.0 << 10.0
                             << 2.0 << 2 << false;
    QTest::newRow("low") << (B | LADSPA_HINT_DEFAULT_LOW) << 2.f << 10.f << kRate << true << true << 2.0 << 10.0 << 4.0
                         << 2 << false;
    QTest::newRow("middle") << (B | LADSPA_HINT_DEFAULT_MIDDLE) << 2.f << 10.f << kRate << true << true << 2.0 << 10.0
                            << 6.0 << 2 << false;
    QTest::newRow("high") << (B | LADSPA_HINT_DEFAULT_HIGH) << 2.f << 10.f << kRate << true << true << 2.0 << 10.0
                          << 8.0 << 2 << false;
    QTest::newRow("maximum") << (B | LADSPA_HINT_DEFAULT_MAXIMUM) << 2.f << 10.f << kRate << true << true << 2.0 << 10.0
                             << 10.0 << 2 << false;
    QTest::newRow("low, logarithmic") << (B | L | LADSPA_HINT_DEFAULT_LOW) << 1.f << 10000.f << kRate << true << true
                                      << 1.0 << 10000.0 << 10.0 << 2 << false;
    QTest::newRow("middle, logarithmic") << (B | L | LADSPA_HINT_DEFAULT_MIDDLE) << 1.f << 10000.f << kRate << true
                                         << true << 1.0 << 10000.0 << 100.0 << 2 << false;
    QTest::newRow("high, logarithmic") << (B | L | LADSPA_HINT_DEFAULT_HIGH) << 1.f << 10000.f << kRate << true << true
                                       << 1.0 << 10000.0 << 1000.0 << 2 << false;
    QTest::newRow("middle, logarithmic from 0 (linear)") << (B | L | LADSPA_HINT_DEFAULT_MIDDLE) << 0.f << 10.f << kRate
                                                         << true << true << 0.0 << 10.0 << 5.0 << 2 << false;
    QTest::newRow("0") << (B | LADSPA_HINT_DEFAULT_0) << -1.f << 1.f << kRate << true << true << -1.0 << 1.0 << 0.0 << 2
                       << false;
    QTest::newRow("1, unbounded above (amp gain)")
        << (int(LADSPA_HINT_BOUNDED_BELOW) | L | LADSPA_HINT_DEFAULT_1) << 0.f << 0.f << kRate << true << false << 0.0
        << 0.0 << 1.0 << 2 << false;
    QTest::newRow("100") << int(LADSPA_HINT_DEFAULT_100) << 0.f << 0.f << kRate << false << false << 0.0 << 0.0 << 100.0
                         << 2 << false;
    QTest::newRow("440") << int(LADSPA_HINT_DEFAULT_440) << 0.f << 0.f << kRate << false << false << 0.0 << 0.0 << 440.0
                         << 2 << false;
    QTest::newRow("440 clamped into range") << (B | LADSPA_HINT_DEFAULT_440) << 20.f << 200.f << kRate << true << true
                                            << 20.0 << 200.0 << 200.0 << 2 << false;
    QTest::newRow("range <= 1: 3 decimals")
        << (B | LADSPA_HINT_DEFAULT_MIDDLE) << 0.f << 1.f << kRate << true << true << 0.0 << 1.0 << 0.5 << 3 << false;
    QTest::newRow("sample rate") << (B | LADSPA_HINT_SAMPLE_RATE | LADSPA_HINT_DEFAULT_MIDDLE) << 0.f << 0.5f << 48000
                                 << true << true << 0.0 << 24000.0 << 12000.0 << 2 << false;
    QTest::newRow("sample rate, 440 not scaled")
        << (B | LADSPA_HINT_SAMPLE_RATE | LADSPA_HINT_DEFAULT_440) << 0.f << 0.5f << 48000 << true << true << 0.0
        << 24000.0 << 440.0 << 2 << false;
    QTest::newRow("integer") << (B | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_LOW) << 0.f << 10.f << kRate << true
                             << true << 0.0 << 10.0 << 3.0 << 0 << false;  // 2.5 rounded
    QTest::newRow("toggled, default 1") << (int(LADSPA_HINT_TOGGLED) | LADSPA_HINT_DEFAULT_1) << 0.f << 0.f << kRate
                                        << false << false << 0.0 << 0.0 << 1.0 << 0 << true;
    QTest::newRow("toggled, default 0") << (int(LADSPA_HINT_TOGGLED) | LADSPA_HINT_DEFAULT_0) << 0.f << 0.f << kRate
                                        << false << false << 0.0 << 0.0 << 0.0 << 0 << true;
    QTest::newRow("toggled, no default") << int(LADSPA_HINT_TOGGLED) << 0.f << 0.f << kRate << false << false << 0.0
                                         << 0.0 << 0.0 << 0 << true;
  }

  void portRanges() {
    QFETCH(int, descriptor);
    QFETCH(float, lower);
    QFETCH(float, upper);
    QFETCH(int, rate);
    QFETCH(bool, has_min);
    QFETCH(bool, has_max);
    QFETCH(double, min);
    QFETCH(double, max);
    QFETCH(double, def);
    QFETCH(int, decimals);
    QFETCH(bool, toggled);
    const amber::ladspa::PortRange r = amber::ladspa::port_range(hint(descriptor, lower, upper), rate);
    QCOMPARE(r.toggled, toggled);
    QCOMPARE(r.has_min, has_min);
    QCOMPARE(r.has_max, has_max);
    if (has_min) QCOMPARE(r.min, min);
    if (has_max) QCOMPARE(r.max, max);
    QVERIFY2(std::fabs(r.def - def) < 1e-9 * std::max(1.0, std::fabs(def)),
             qPrintable(QString("default %1, want %2").arg(r.def, 0, 'g', 17).arg(def)));
    QCOMPARE(r.decimals, decimals);
  }

  void portRangeClamp() {
    const amber::ladspa::PortRange r = amber::ladspa::port_range(
        hint(LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER, 1, 4), kRate);
    QCOMPARE(r.clamp(-3), 1.0);
    QCOMPARE(r.clamp(2.4), 2.0);
    QCOMPARE(r.clamp(9), 4.0);
    QCOMPARE(r.clamp(qQNaN()), 1.0);  // the default
    const amber::ladspa::PortRange t = amber::ladspa::port_range(hint(LADSPA_HINT_TOGGLED), kRate);
    QCOMPARE(t.clamp(0.0), 0.0);
    QCOMPARE(t.clamp(1.0), 1.0);
  }

  void uniqueNames() {
    std::vector<EffectMeta> metas(4);
    const char* names[] = {"Echo", "Echo", "Amp", "Echo"};
    const unsigned long ids[] = {300, 100, 50, 200};
    for (int i = 0; i < 4; i++) {
      metas[i].name = names[i];
      metas[i].plugin_id = ids[i];
    }
    amber::ladspa::finish_names(metas);
    QCOMPARE(metas.size(), size_t(4));
    QCOMPARE(metas[0].name, QString("Amp"));
    QCOMPARE(metas[1].name, QString("Echo"));  // the lowest UniqueID keeps the plain name
    QCOMPARE(metas[1].plugin_id, 100ul);
    QCOMPARE(metas[2].name, QString("Echo (200)"));
    QCOMPARE(metas[3].name, QString("Echo (300)"));
  }

  // A LADSPA effect is found by the UniqueID it was saved with, whatever name the installed set gives it now
  void savedIdFindsThePlugin() {
    auto meta = [](const char* name, const char* category, unsigned long id) {
      EffectMeta em;
      em.name = name;
      em.category = category;
      em.internal = id != 0 ? EFFECT_INTERNAL_LADSPA : EFFECT_INTERNAL_VOLUME;
      em.plugin_id = id;
      return em;
    };
    // Both installed: the later UniqueID got the suffix. Saved: "LADSPA/Echo" id 100, "LADSPA/Echo (200)" id 200.
    std::deque<EffectMeta> both{meta("Echo", "LADSPA", 100), meta("Echo (200)", "LADSPA", 200), meta("Echo", "", 0)};
    QCOMPARE(find_effect_meta(both, "LADSPA/Echo", 100), &both[0]);
    QCOMPARE(find_effect_meta(both, "LADSPA/Echo (200)", 200), &both[1]);
    // 100 uninstalled: 200 is now plain "Echo". Each saved effect still finds its own plugin or none, never the other
    std::deque<EffectMeta> only_200{meta("Echo", "LADSPA", 200), meta("Echo", "", 0)};
    QCOMPARE(find_effect_meta(only_200, "LADSPA/Echo (200)", 200), &only_200[0]);
    QCOMPARE(find_effect_meta(only_200, "LADSPA/Echo", 100), nullptr);
    // Saved while only 200 was installed ("LADSPA/Echo" id 200), then 100 installed: 200 is "Echo (200)" again
    QCOMPARE(find_effect_meta(both, "LADSPA/Echo", 200), &both[1]);
    // Without an id (a hand-written project): by name, as before
    QCOMPARE(find_effect_meta(both, "LADSPA/Echo", 0), &both[0]);
    QCOMPARE(find_effect_meta(both, "/Echo", 0), &both[0]);  // empty category matches any, first wins
    QCOMPARE(find_effect_meta(both, "Missing/Thing", 0), nullptr);
  }

  // A plugin that isn't installed loads as a Missing Effect that writes its UniqueID back, so reinstalling the plugin
  // finds it again
  void missingPluginKeepsItsId() {
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    QXmlStreamReader reader(QByteArray(R"(<effect name="LADSPA/Gone" enabled="1" ladspa_id="4242999">)"
                                       R"(<row><field id="0" value="0.5"/></row></effect>)"));
    QVERIFY(reader.readNextStartElement());
    QCOMPARE(get_meta_from_attributes(reader.attributes()), nullptr);
    VoidEffect missing(&clip, "LADSPA/Gone");
    missing.load(reader);
    const QByteArray saved = missing.save_to_string();
    QVERIFY2(saved.contains(R"(<effect name="LADSPA/Gone" enabled="1" ladspa_id="4242999">)"), saved.constData());
    QVERIFY2(saved.contains(R"(<field id="0" value="0.5"/>)"), saved.constData());
  }

  // $LADSPA_PATH replaces the default folders; libraries that aren't LADSPA plugins are skipped
  void discoveryFromLadspaPath() {
    if (amp_dir_.isEmpty() || delay_dir_.isEmpty()) QSKIP("the LADSPA SDK's amp.so / delay.so are not installed");
    QTemporaryDir a, b;
    QVERIFY(a.isValid() && b.isValid());
    QVERIFY(QFile::copy(amp_dir_ + "/amp.so", a.filePath("amp.so")));
    QVERIFY(QFile::copy(delay_dir_ + "/delay.so", a.filePath("delay.so")));
    QFile empty(a.filePath("empty.so"));
    QVERIFY(empty.open(QFile::WriteOnly));
    empty.close();
    if (QFileInfo::exists("/usr/lib/libm.so.6")) QVERIFY(QFile::copy("/usr/lib/libm.so.6", a.filePath("libm.so")));
    QVERIFY(QFile::copy(amp_dir_ + "/amp.so", b.filePath("amp.so")));  // same UniqueIDs again: the first folder wins

    const QByteArray saved = qgetenv("LADSPA_PATH");
    const QString sep = QDir::listSeparator();
    qputenv("LADSPA_PATH", (a.path() + sep + b.path() + sep + "/no/such/folder").toLocal8Bit());
    const QStringList dirs = amber::ladspa::search_paths();
    QElapsedTimer timer;
    timer.start();
    const std::vector<EffectMeta> found = amber::ladspa::scan(dirs);
    const qint64 ms = timer.elapsed();
    if (saved.isNull()) {
      qunsetenv("LADSPA_PATH");
    } else {
      qputenv("LADSPA_PATH", saved);
    }
    QCOMPARE(dirs.size(), 3);

    QStringList names;
    for (const EffectMeta& em : found) names.append(em.name);
    qInfo("scan of the scratch folders: %lld ms, %s", ms, qPrintable(names.join(", ")));
    QCOMPARE(names, QStringList({"Mono Amplifier", "Simple Delay Line", "Stereo Amplifier"}));
    const QString canonical_a = QFileInfo(a.path()).canonicalFilePath();
    for (const EffectMeta& em : found) {
      QCOMPARE(em.category, QString("LADSPA"));
      QCOMPARE(em.internal, int(EFFECT_INTERNAL_LADSPA));
      QCOMPARE(em.type, int(EFFECT_TYPE_EFFECT));
      QCOMPARE(em.subtype, int(EFFECT_TYPE_AUDIO));
      QCOMPARE(em.path, canonical_a);
    }
    QCOMPARE(found[0].plugin_id, 1048ul);
    QCOMPARE(found[0].filename, QString("amp.so"));
    QCOMPARE(found[1].plugin_id, 1043ul);
    QCOMPARE(found[2].plugin_id, 1049ul);
    QVERIFY2(found[0].tooltip.endsWith(QString::fromUtf8("— amp_mono (1048)")), qPrintable(found[0].tooltip));
  }

  // The whole default scan, for the record
  void scanTime() {
    QElapsedTimer timer;
    timer.start();
    const std::vector<EffectMeta> found = amber::ladspa::scan(amber::ladspa::search_paths());
    qInfo("default folders: %d hostable plugins in %lld ms", int(found.size()), timer.elapsed());
  }

  void controlRows() {
    const EffectMeta* meta = plugin(1043);
    if (meta == nullptr) QSKIP("delay_5s (1043) not installed");
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    LadspaEffect e(&clip, meta);
    QCOMPARE(e.row_count(), 2);
    QCOMPARE(e.row(0)->name(), QString("Delay (Seconds)"));
    QCOMPARE(e.row(1)->name(), QString("Dry/Wet Balance"));
    auto* delay = qobject_cast<DoubleField*>(field(&e, "0"));
    auto* wet = qobject_cast<DoubleField*>(field(&e, "1"));
    QVERIFY(delay != nullptr && wet != nullptr);
    QCOMPARE(delay->GetMinimum(), 0.0);
    QCOMPARE(delay->GetMaximum(), 5.0);
    QCOMPARE(delay->GetDefault(), 1.0);
    QCOMPARE(delay->GetDecimalPlaces(), 2);
    QCOMPARE(wet->GetDefault(), 0.5);
    QCOMPARE(wet->GetDecimalPlaces(), 3);
    QCOMPARE(delay->GetDoubleAt(0), 1.0);
    // Saved with its UniqueID next to the name
    QVERIFY2(e.save_to_string().contains(R"(<effect name="LADSPA/Simple Delay Line" enabled="1" ladspa_id="1043">)"),
             e.save_to_string().constData());
    // The copy reconstructs through the meta and keeps the values
    wet->SetValueAt(0, 0.25);
    EffectPtr copy = e.copy(&clip);
    QCOMPARE(copy->row_count(), 2);
    QCOMPARE(field(copy.get(), "1")->GetValueAt(0).toDouble(), 0.25);
  }

  void ampHalvesRms_data() {
    QTest::addColumn<int>("id");
    QTest::newRow("test gain mono") << kTestGainMono;
    QTest::newRow("test gain stereo") << kTestGainStereo;
    QTest::newRow("amp_mono") << 1048;
    QTest::newRow("amp_stereo") << 1049;
  }

  // -6 dB of gain halves the RMS of a 1 kHz tone on both channels
  void ampHalvesRms() {
    QFETCH(int, id);
    const EffectMeta* meta = plugin(id);
    if (meta == nullptr && is_fixture(id)) QFAIL("the test plugin amber_test_ladspa was not built");
    if (meta == nullptr) QSKIP("the LADSPA SDK's amp.so is not installed");
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    LadspaEffect e(&clip, meta);
    QCOMPARE(e.row_count(), 1);
    EffectField* gain = field(&e, "0");
    QVERIFY(gain != nullptr);
    QCOMPARE(gain->GetValueAt(0).toDouble(), 1.0);
    gain->SetValueAt(0, std::pow(10.0, -6.0 / 20.0));

    const int frames = kRate;  // 1 s, several chunks
    const std::vector<qint16> in = tone(frames, 0, 0.5, 0.25);
    std::vector<qint16> out = in;
    process(&e, out, &clip, 0.0);
    const double left = rms(out, 0, 0, frames) / rms(in, 0, 0, frames);
    const double right = rms(out, 1, 0, frames) / rms(in, 1, 0, frames);
    qInfo("%s at -6 dB: RMS ratio left %.5f, right %.5f", qPrintable(meta->name), left, right);
    QVERIFY2(std::fabs(left - 0.5) < 0.005, qPrintable(QString::number(left)));
    QVERIFY2(std::fabs(right - 0.5) < 0.005, qPrintable(QString::number(right)));
  }

  // delay_5s at 0.1 s fully wet: the tone starts 4410 samples later on both channels
  void delayShiftsOnset_data() { delay_rows(); }
  void delayShiftsOnset() {
    QFETCH(int, id);
    const EffectMeta* meta = plugin(id);
    if (meta == nullptr && is_fixture(id)) QFAIL("the test plugin amber_test_ladspa was not built");
    if (meta == nullptr) QSKIP("delay_5s (1043) not installed");
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    LadspaEffect e(&clip, meta);
    field(&e, "0")->SetValueAt(0, 0.1);
    field(&e, "1")->SetValueAt(0, 1.0);

    const int silence = 1000;
    const std::vector<qint16> in = tone(kRate / 2, silence, 0.5, 0.5);
    std::vector<qint16> out = in;
    process(&e, out, &clip, 0.0);
    const int in_onset = first_above(in, 0, 100);
    const int out_left = first_above(out, 0, 100);
    const int out_right = first_above(out, 1, 100);
    qInfo("%s 0.1 s: onset in %d, out left %d, right %d (shift %d)", qPrintable(meta->name), in_onset, out_left,
          out_right, out_left - in_onset);
    QCOMPARE(out_left - in_onset, 4410);
    QCOMPARE(out_right - in_onset, 4410);
  }

  // A jump in the timecode (seek) restarts the plugin: the delay line doesn't echo the previous position. Played on
  // without a jump, it does.
  void resetClearsDelayLine_data() { delay_rows(); }
  void resetClearsDelayLine() {
    QFETCH(int, id);
    const EffectMeta* meta = plugin(id);
    if (meta == nullptr && is_fixture(id)) QFAIL("the test plugin amber_test_ladspa was not built");
    if (meta == nullptr) QSKIP("delay_5s (1043) not installed");
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    // ladspa.h: no second activate() without a deactivate() in between. The delay lines have activate() and no
    // deactivate(), so a restart must re-instantiate; the test plugin counts the violations
    QLibrary library(QDir(meta->path).filePath(meta->filename));
    using Violations = int (*)();
    const auto violations = is_fixture(id) && library.load()
                                ? reinterpret_cast<Violations>(library.resolve("amber_test_ladspa_violations"))
                                : nullptr;
    QVERIFY(violations != nullptr || !is_fixture(id));
    const int violations_before = violations ? violations() : 0;
    LadspaEffect e(&clip, meta);
    field(&e, "0")->SetValueAt(0, 0.1);
    field(&e, "1")->SetValueAt(0, 1.0);

    const int frames = kChunk;  // 93 ms of tone, ends inside the delay
    for (bool seek : {false, true}) {
      std::vector<qint16> a = tone(frames, 0, 0.5, 0.5);
      process(&e, a, &clip, seek ? 10.0 : 20.0);
      std::vector<qint16> b(size_t(kRate / 4) * 2, 0);  // 250 ms of silence
      process(&e, b, &clip, seek ? 3.0 : 20.0 + double(frames) / kRate);
      qInfo("silence after a tone, %s: peak %d", seek ? "after a seek" : "contiguous", peak(b));
      if (seek) {
        QCOMPARE(peak(b), 0);
      } else {
        QVERIFY(peak(b) > 10000);
      }
    }
    if (violations) QCOMPARE(violations() - violations_before, 0);
  }

  // Two streams processed alternately (two inner clips of a nest) keep their own delay lines
  void streamsKeepTheirOwnState_data() { delay_rows(); }
  void streamsKeepTheirOwnState() {
    QFETCH(int, id);
    const EffectMeta* meta = plugin(id);
    if (meta == nullptr && is_fixture(id)) QFAIL("the test plugin amber_test_ladspa was not built");
    if (meta == nullptr) QSKIP("delay_5s (1043) not installed");
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    Clip other(&seq);
    LadspaEffect e(&clip, meta);
    field(&e, "0")->SetValueAt(0, 0.1);
    field(&e, "1")->SetValueAt(0, 1.0);

    const int frames = kRate / 2;
    std::vector<qint16> toned = tone(frames, 0, 0.5, 0.5);
    std::vector<qint16> silent(size_t(frames) * 2, 0);
    for (int done = 0; done < frames; done += kChunk) {
      const int n = std::min(kChunk, frames - done);
      std::vector<qint16> x(toned.begin() + done * 2, toned.begin() + (done + n) * 2);
      std::vector<qint16> y(silent.begin() + done * 2, silent.begin() + (done + n) * 2);
      process(&e, x, &clip, double(done) / kRate);
      process(&e, y, &other, double(done) / kRate);
      std::copy(x.begin(), x.end(), toned.begin() + done * 2);
      std::copy(y.begin(), y.end(), silent.begin() + done * 2);
    }
    qInfo("alternate streams: toned onset %d, silent peak %d", first_above(toned, 0, 100), peak(silent));
    QCOMPARE(peak(silent), 0);
    QCOMPARE(first_above(toned, 0, 100) - first_above(tone(frames, 0, 0.5, 0.5), 0, 100), 4410);
  }

  // Every installed plugin at its defaults, two chunks of a tone then a seek: no crash, every field at its default.
  // Third-party code, so only on request: AMBER_TEST_ALL_LADSPA=1
  void everyInstalledPluginRuns() {
    if (qEnvironmentVariableIntValue("AMBER_TEST_ALL_LADSPA") != 1) QSKIP("set AMBER_TEST_ALL_LADSPA=1 to run");
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    int silent = 0;
    for (const EffectMeta& em : system_) {
      LadspaEffect e(&clip, &em);
      for (int i = 0; i < e.row_count(); i++) {
        EffectField* f = e.row(i)->Field(0);
        QVERIFY2(f->GetValueAt(0) == f->GetDefaultData(), qPrintable(em.name + " / " + e.row(i)->name()));
      }
      std::vector<qint16> a = tone(kChunk * 2, 0, 0.5, 0.5);
      process(&e, a, &clip, 0.0);
      std::vector<qint16> b = tone(kChunk, 0, 0.5, 0.5);
      process(&e, b, &clip, 5.0);
      if (peak(a) == 0) silent++;
    }
    qInfo("%d plugins ran, %d gave silence at their defaults", int(system_.size()), silent);
  }

  // A library gone since the scan: an error message (swallowed here), no rows, the audio passes untouched
  void missingLibraryLeavesAudioAlone() {
    EffectMeta meta;
    meta.name = "Gone";
    meta.category = "LADSPA";
    meta.internal = EFFECT_INTERNAL_LADSPA;
    meta.type = EFFECT_TYPE_EFFECT;
    meta.subtype = EFFECT_TYPE_AUDIO;
    meta.path = "/no/such/folder";
    meta.filename = "gone.so";
    meta.plugin_index = 0;
    meta.plugin_id = 1048;
    ScopedAppContext app;
    Sequence seq;
    Clip clip(&seq);
    LadspaEffect e(&clip, &meta);
    QCOMPARE(e.row_count(), 0);
    const std::vector<qint16> in = tone(kChunk, 0, 0.5, 0.5);
    std::vector<qint16> out = in;
    process(&e, out, &clip, 0.0);
    QVERIFY(out == in);
  }
};

QTEST_GUILESS_MAIN(TestLadspa)
#include "test_ladspa.moc"
