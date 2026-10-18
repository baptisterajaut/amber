#include <QtTest>

#include "core/math.h"
#include "effects/effect.h"
#include "effects/fields/doublefield.h"
#include "effects/internal/subtitleeffect.h"
#include "effects/internal/voideffect.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo_effect.h"
#include "test_appcontext_stub.h"

#ifndef NOFREI0R
#include "effects/internal/frei0reffect.h"
#endif

namespace {
// Effect::FieldChanged() and ~Effect call into amber::app_ctx: install a silent one for the test's lifetime. Declared
// before the effects so it is destroyed after them.
struct ScopedAppContext {
  TestAppContext ctx;
  ScopedAppContext() { amber::app_ctx = &ctx; }
  ~ScopedAppContext() { amber::app_ctx = nullptr; }
};
}  // namespace

class TestEffects : public QObject {
  Q_OBJECT
 private slots:

  void doubleFieldLinearInterpolation() {
    double before = 10.0;
    double after = 20.0;
    double progress = 0.5;
    QCOMPARE(double_lerp(before, after, progress), 15.0);
  }

  void colorInterpolation() {
    QColor before(100, 0, 0);
    QColor after(200, 100, 50);
    double progress = 0.5;
    int r = lerp(before.red(), after.red(), progress);
    int g = lerp(before.green(), after.green(), progress);
    int b = lerp(before.blue(), after.blue(), progress);
    QCOMPARE(r, 150);
    QCOMPARE(g, 50);
    QCOMPARE(b, 25);
  }

  void boolStringConversion() {
    QCOMPARE(QString("1") == "1", true);
    QCOMPARE(QString("0") == "1", false);
    QCOMPARE(QString::number(true), "1");
    QCOMPARE(QString::number(false), "0");
  }

  void doubleStringConversion() {
    QString s("3.14");
    QCOMPARE(s.toDouble(), 3.14);
    QVariant v(3.14);
    QCOMPARE(QString::number(v.toDouble()), "3.14");
  }

  void colorStringConversion() {
    QColor original(255, 128, 0);
    QString name = original.name();
    QColor restored(name);
    QCOMPARE(restored, original);
  }

  // The Subtitle Editor's undo step: redo installs the new cue list, undo restores the old one, each bumps the revision
  // the editor watches
  void subtitleCuesUndoRedo() {
    EffectMeta meta;
    meta.name = "Subtitle";
    meta.category = "Render";
    meta.internal = EFFECT_INTERNAL_SUBTITLE;
    meta.type = EFFECT_TYPE_EFFECT;
    ScopedAppContext app_ctx;
    Sequence seq;
    Clip clip(&seq);
    SubtitleEffect effect(&clip, &meta);

    const QVector<SubtitleCue> before = {{0, 1000, "One"}, {2000, 3000, "Two"}};
    effect.SetCues(before);
    const int rev = effect.cues_revision();

    QVector<SubtitleCue> after = before;
    after[1].text = "Two<br>edited";
    after.append({4000, 4500, "Three"});

    SetSubtitleCues action(&effect, after, "Edit Subtitle");
    QCOMPARE(action.text(), QString("Edit Subtitle"));
    QCOMPARE(effect.cues().size(), 2);  // nothing changes before redo

    action.redo();
    QCOMPARE(effect.cues().size(), 3);
    QCOMPARE(effect.cues()[1].text, QString("Two<br>edited"));
    QVERIFY(effect.cues_revision() > rev);

    action.undo();
    QCOMPARE(effect.cues().size(), 2);
    QCOMPARE(effect.cues()[1].text, QString("Two"));

    action.redo();
    QCOMPARE(effect.cues()[2].start_ms, qint64(4000));
  }

  // Edited cues go through the unchanged <cues> save format: save() then load() on a fresh effect gives them back
  void subtitleCuesSaveLoad() {
    EffectMeta meta;
    meta.name = "Subtitle";
    meta.category = "Render";
    meta.internal = EFFECT_INTERNAL_SUBTITLE;
    meta.type = EFFECT_TYPE_EFFECT;
    ScopedAppContext app_ctx;
    Sequence seq;
    Clip clip(&seq);
    SubtitleEffect effect(&clip, &meta);
    const QVector<SubtitleCue> cues = {{40, 1500, "A &amp; <i>b</i>"}, {1500, 99000, "line<br>break"}};
    effect.SetCues(cues);

    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement("effect");
    effect.save(writer);
    writer.writeEndElement();

    SubtitleEffect loaded(&clip, &meta);
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
      reader.readNext();
      if (reader.isStartElement() && reader.name() == QLatin1String("effect")) {
        loaded.load(reader);
        break;
      }
    }

    QCOMPARE(loaded.cues().size(), cues.size());
    for (int i = 0; i < cues.size(); i++) {
      QCOMPARE(loaded.cues()[i].start_ms, cues[i].start_ms);
      QCOMPARE(loaded.cues()[i].end_ms, cues[i].end_ms);
      QCOMPARE(loaded.cues()[i].text, cues[i].text);
    }
  }

#ifndef NOFREI0R
  // A Frei0r effect processes the frame it is handed, whatever the clip's declared media size: a proxied clip decodes
  // smaller frames than its footage stream says (here a 64x64 sequence-sized clip receives a 32x16 frame)
  void frei0rProcessesActualFrameSize() {
    const QString dir = "/usr/lib/frei0r-1";
    if (!QFileInfo::exists(dir + "/invert0r.so")) QSKIP("invert0r Frei0r plugin not installed");
    EffectMeta meta;
    meta.name = "invert0r";
    meta.path = dir;
    meta.filename = "invert0r.so";
    meta.internal = EFFECT_INTERNAL_FREI0R;

    Sequence seq;
    seq.width = 64;
    seq.height = 64;
    Clip clip(&seq);
    QCOMPARE(clip.media_width(), 64);
    Frei0rEffect effect(&clip, &meta);

    constexpr int w = 32;
    constexpr int h = 16;
    QByteArray in(w * h * 4, char(40));
    QByteArray out(w * h * 4, char(0));
    effect.process_image(0.0, reinterpret_cast<uint8_t*>(in.data()), reinterpret_cast<uint8_t*>(out.data()), w, h,
                         w * 4);
    QCOMPARE(int(uchar(out[0])), 255 - 40);
    QCOMPARE(int(uchar(out[out.size() - 4])), 255 - 40);

    // Rows padded for alignment (a width that isn't a multiple of the decoder's alignment): each row is processed in
    // place, the padding left alone. A row value per line shows a shear if the stride is ignored.
    constexpr int pw = 30;
    constexpr int ph = 8;
    constexpr int stride = 32 * 4;
    QByteArray pin(stride * ph, char(7));
    QByteArray pout(stride * ph, char(0));
    for (int y = 0; y < ph; y++) memset(pin.data() + y * stride, 10 + 20 * y, pw * 4);
    effect.process_image(0.0, reinterpret_cast<uint8_t*>(pin.data()), reinterpret_cast<uint8_t*>(pout.data()), pw, ph,
                         stride);
    for (int y = 0; y < ph; y++) {
      for (int x = 0; x < pw; x++) QCOMPARE(int(uchar(pout[y * stride + x * 4])), 255 - (10 + 20 * y));
      QCOMPARE(int(uchar(pout[y * stride + pw * 4])), 0);  // padding untouched
    }
  }

  // A fresh Frei0r effect starts at the plugin's own defaults, in the fields' units, and Reset to Defaults goes back
  // there: a new Perspective keeps the image whole (its corners at the frame corners, not all at 0,0). A project
  // saves every field, so one saved with the old all-zero values still loads them.
  void frei0rFreshEffectsStartAtPluginDefaults() {
    const QString dir = "/usr/lib/frei0r-1";
    for (const char* so : {"perspective.so", "tint0r.so", "vignette.so", "medians.so"}) {
      if (!QFileInfo::exists(dir + "/" + so)) QSKIP(qPrintable(QString("%1 Frei0r plugin not installed").arg(so)));
    }
    ScopedAppContext app_ctx;
    Sequence seq;
    seq.width = 64;
    seq.height = 64;
    Clip clip(&seq);
    auto meta_for = [&](const char* so) {
      EffectMeta m;
      m.name = so;
      m.category = "Frei0r";
      m.path = dir;
      m.filename = so;
      m.internal = EFFECT_INTERNAL_FREI0R;
      return m;
    };
    auto value = [](Effect& e, QStringView id) { return e.FindFieldById(id)->GetValueAt(0); };
    auto is_default = [](Effect& e, QStringView id) {
      EffectField* f = e.FindFieldById(id);
      return f->HasDefault() && f->GetValueAt(0) == f->GetDefaultData();
    };

    const EffectMeta perspective = meta_for("perspective.so");
    Frei0rEffect fresh(&clip, &perspective);
    QCOMPARE(value(fresh, u"0X").toDouble(), 0.0);
    QCOMPARE(value(fresh, u"0Y").toDouble(), 0.0);
    QCOMPARE(value(fresh, u"1X").toDouble(), 100.0);
    QCOMPARE(value(fresh, u"1Y").toDouble(), 0.0);
    QCOMPARE(value(fresh, u"2X").toDouble(), 0.0);
    QCOMPARE(value(fresh, u"2Y").toDouble(), 100.0);
    QCOMPARE(value(fresh, u"3X").toDouble(), 100.0);
    QCOMPARE(value(fresh, u"3Y").toDouble(), 100.0);
    QVERIFY(is_default(fresh, u"3X"));
    QCOMPARE(static_cast<DoubleField*>(fresh.FindFieldById(u"3X"))->GetDefault(), 100.0);  // what its slider resets to
    // and renders the frame untouched in the middle, where all-zero corners left nothing
    QByteArray in(64 * 64 * 4, char(200));
    QByteArray out(64 * 64 * 4, char(0));
    fresh.process_image(0.0, reinterpret_cast<uint8_t*>(in.data()), reinterpret_cast<uint8_t*>(out.data()), 64, 64,
                        64 * 4);
    QCOMPARE(int(uchar(out[(32 * 64 + 32) * 4])), 200);

    const EffectMeta tint = meta_for("tint0r.so");  // colours and a double
    Frei0rEffect tint0r(&clip, &tint);
    QCOMPARE(value(tint0r, u"0").value<QColor>(), QColor::fromRgbF(0, 0, 0));
    QCOMPARE(value(tint0r, u"1").value<QColor>(), QColor::fromRgbF(0.5f, 1, 0.5f));
    QCOMPARE(value(tint0r, u"2").toDouble(), 25.0);
    QVERIFY(is_default(tint0r, u"1"));
    const EffectMeta vig = meta_for("vignette.so");
    Frei0rEffect vignette(&clip, &vig);
    QCOMPARE(value(vignette, u"2").toDouble(), 60.0);
    const EffectMeta med = meta_for("medians.so");  // a string
    Frei0rEffect medians(&clip, &med);
    QCOMPARE(value(medians, u"0").toString(), QString("Square3x3"));
    QVERIFY(is_default(medians, u"0"));

    auto save = [](Effect& e) {
      QByteArray xml;
      QXmlStreamWriter writer(&xml);
      writer.writeStartElement("effect");
      e.save(writer);
      writer.writeEndElement();
      return xml;
    };
    auto load = [](Frei0rEffect& e, const QByteArray& xml) {
      QXmlStreamReader reader(xml);
      while (reader.readNextStartElement() && reader.name() != QLatin1String("effect")) {
      }
      e.load(reader);
    };
    // Saved by an older build: every field written, at the old all-zero default, with or without the unit marker
    for (const QByteArray& marker : {QByteArray("<positionunit value=\"percent\"/>"), QByteArray()}) {
      Frei0rEffect old_project(&clip, &perspective);
      load(old_project,
           "<effect name=\"Frei0r/Perspective\" enabled=\"1\">"
           "<row><field id=\"0X\" value=\"0\"/><field id=\"0Y\" value=\"0\"/></row>"
           "<row><field id=\"1X\" value=\"0\"/><field id=\"1Y\" value=\"0\"/></row>"
           "<row><field id=\"2X\" value=\"0\"/><field id=\"2Y\" value=\"0\"/></row>"
           "<row><field id=\"3X\" value=\"0\"/><field id=\"3Y\" value=\"0\"/></row>" +
               marker + "</effect>");
      for (const char16_t* id : {u"1X", u"2Y", u"3X", u"3Y"}) QCOMPARE(value(old_project, id).toDouble(), 0.0);
    }
    // A fresh effect saved and reopened, also through a Missing Effect, keeps the defaults (not scaled again)
    const QByteArray saved = save(fresh);
    VoidEffect missing(&clip, "Frei0r/Perspective");
    {
      QXmlStreamReader reader(saved);
      reader.readNextStartElement();
      missing.load(reader);
    }
    for (const QByteArray& xml : {saved, save(missing)}) {
      Frei0rEffect reopened(&clip, &perspective);
      load(reopened, xml);
      QCOMPARE(value(reopened, u"1X").toDouble(), 100.0);
      QCOMPARE(value(reopened, u"3Y").toDouble(), 100.0);
    }
  }

  // A plugin that fails to load (moved, uninstalled) leaves an effect with no rows that passes frames through
  void frei0rMissingPluginPassesThrough() {
    EffectMeta meta;
    meta.name = "Gone";
    meta.category = "Frei0r";
    meta.path = "/nonexistent";
    meta.filename = "gone.so";
    meta.internal = EFFECT_INTERNAL_FREI0R;
    ScopedAppContext app_ctx;
    Sequence seq;
    Clip clip(&seq);
    Frei0rEffect effect(&clip, &meta);
    QCOMPARE(effect.row_count(), 0);
    QByteArray in(8 * 8 * 4, char(40));
    QByteArray out(8 * 8 * 4, char(0));
    effect.process_image(0.0, reinterpret_cast<uint8_t*>(in.data()), reinterpret_cast<uint8_t*>(out.data()), 8, 8, 32);
    QCOMPARE(out, in);
  }

  // Positions saved before they were scaled from the fields' 0-100 to the plugin's 0-1 were sent as is, so only 0-1
  // meant anything: loading such data scales them by 100, static values, keyframes and their handles alike. Data
  // saved now carries a marker and loads unchanged, even a position within 1% of the corner.
  void frei0rOldPositionsScaledOnLoad() {
    const QString dir = "/usr/lib/frei0r-1";
    if (!QFileInfo::exists(dir + "/perspective.so")) QSKIP("perspective Frei0r plugin not installed");
    EffectMeta meta;
    meta.name = "Perspective";
    meta.category = "Frei0r";
    meta.path = dir;
    meta.filename = "perspective.so";
    meta.internal = EFFECT_INTERNAL_FREI0R;
    ScopedAppContext app_ctx;
    Sequence seq;
    Clip clip(&seq);

    auto load = [](Frei0rEffect& e, const QByteArray& xml) {
      QXmlStreamReader reader(xml);
      while (reader.readNextStartElement() && reader.name() != QLatin1String("effect")) {
      }
      e.load(reader);
    };
    auto value = [](Frei0rEffect& e, QStringView id) { return e.FindFieldById(id)->persistent_data_.toDouble(); };

    // Perspective's params are four positions: 0 Top Left, 1 Top Right, 2 Bottom Left, 3 Bottom Right
    const QByteArray old_xml =
        "<effect name=\"Frei0r/Perspective\" enabled=\"1\">"
        "<row><field id=\"0X\" value=\"0.2\"/><field id=\"0Y\" value=\"0.1\"/></row>"
        "<row><field id=\"1X\" value=\"1\"/><field id=\"1Y\" value=\"0\"/></row>"
        "<row><field id=\"2X\" value=\"0\"><key value=\"0\" frame=\"0\" type=\"2\" prehx=\"0\" prehy=\"0\" "
        "posthx=\"5\" posthy=\"0.25\"/><key value=\"0.5\" frame=\"10\" type=\"2\" prehx=\"-5\" prehy=\"-0.25\" "
        "posthx=\"0\" posthy=\"0\"/></field><field id=\"2Y\" value=\"1\"/></row>"
        "<row><field id=\"3X\" value=\"75\"/><field id=\"3Y\" value=\"1\"/></row>"  // 75: already 0-100
        "</effect>";
    Frei0rEffect old_effect(&clip, &meta);
    QCOMPARE(old_effect.row_count(), 4);
    load(old_effect, old_xml);
    QCOMPARE(value(old_effect, u"0X"), 20.0);
    QCOMPARE(value(old_effect, u"0Y"), 10.0);
    QCOMPARE(value(old_effect, u"1X"), 100.0);
    QCOMPARE(value(old_effect, u"1Y"), 0.0);
    EffectField* kx = old_effect.FindFieldById(u"2X");
    QCOMPARE(kx->keyframes.size(), 2);
    QCOMPARE(kx->keyframes.at(1).data.toDouble(), 50.0);
    QCOMPARE(kx->keyframes.at(0).post_handle_y, 25.0);
    QCOMPARE(kx->keyframes.at(1).pre_handle_y, -25.0);
    QCOMPARE(kx->keyframes.at(1).pre_handle_x, -5.0);  // time offsets unchanged
    QCOMPARE(value(old_effect, u"2Y"), 100.0);
    QCOMPARE(value(old_effect, u"3X"), 75.0);  // a pair with a value above 1 is left whole
    QCOMPARE(value(old_effect, u"3Y"), 1.0);

    // Saved again (with the marker) and reloaded: nothing is scaled twice, a tiny 0-100 pair stays tiny. Also through
    // a Missing Effect (plugin not installed when the project was re-saved), which keeps the marker.
    old_effect.FindFieldById(u"0X")->persistent_data_ = 0.5;
    old_effect.FindFieldById(u"0Y")->persistent_data_ = 0.25;
    auto save = [](Effect& e) {
      QByteArray xml;
      QXmlStreamWriter writer(&xml);
      writer.writeStartElement("effect");
      e.save(writer);
      writer.writeEndElement();
      return xml;
    };
    const QByteArray saved = save(old_effect);
    VoidEffect missing(&clip, "Frei0r/Perspective");
    {
      QXmlStreamReader reader(saved);
      reader.readNextStartElement();
      missing.load(reader);
    }
    for (const QByteArray& xml : {saved, save(missing)}) {
      Frei0rEffect reloaded(&clip, &meta);
      load(reloaded, xml);
      QCOMPARE(value(reloaded, u"0X"), 0.5);
      QCOMPARE(value(reloaded, u"0Y"), 0.25);
      QCOMPARE(value(reloaded, u"1X"), 100.0);
      QCOMPARE(reloaded.FindFieldById(u"2X")->keyframes.at(1).data.toDouble(), 50.0);
    }
  }
#endif

  // --- lerp boundary tests ---

  void lerpBoundaries() {
    QCOMPARE(lerp(0, 100, 0.0), 0);
    QCOMPARE(lerp(0, 100, 1.0), 100);
    QCOMPARE(lerp(-50, 50, 0.5), 0);
  }

  void doubleLerpBoundaries() {
    QCOMPARE(double_lerp(0.0, 1.0, 0.0), 0.0);
    QCOMPARE(double_lerp(0.0, 1.0, 1.0), 1.0);
    QCOMPARE(double_lerp(-10.0, 10.0, 0.25), -5.0);
  }

  // --- Quadratic bezier ---

  void quadBezierEndpoints() {
    QCOMPARE(quad_from_t(0, 50, 100, 0.0), 0.0);
    QCOMPARE(quad_from_t(0, 50, 100, 1.0), 100.0);
  }

  void quadBezierLinearControlPoint() {
    // Linear control point at midpoint of [0,100] => result is linear => 50 at t=0.5
    QCOMPARE(quad_from_t(0, 50, 100, 0.5), 50.0);
  }

  void quadBezierRoundtrip() {
    double t = 0.3;
    double x = quad_from_t(0, 25, 100, t);
    double t_back = quad_t_from_x(x, 0, 25, 100);
    QVERIFY(qAbs(t_back - t) < 0.001);
  }

  // --- Cubic bezier ---

  void cubicBezierEndpoints() {
    QCOMPARE(cubic_from_t(0, 0, 100, 100, 0.0), 0.0);
    QCOMPARE(cubic_from_t(0, 0, 100, 100, 1.0), 100.0);
  }

  void cubicBezierLinearishControlPoints() {
    // Control points at 33 and 66 out of [0,100]: nearly linear => ~49.625 at t=0.5
    double result = cubic_from_t(0, 33, 66, 100, 0.5);
    QVERIFY(qAbs(result - 49.625) < 0.001);
  }

  void cubicBezierRoundtrip() {
    double t = 0.4;
    double x = cubic_from_t(0, 30, 70, 100, t);
    double t_back = cubic_t_from_x(x, 0, 30, 70, 100);
    QVERIFY(qAbs(t_back - t) < 0.001);
  }

  void cubicBezierEaseIn() {
    // Control points at 0 and 0: steep acceleration, value at t=0.5 should be well below 50
    double result = cubic_from_t(0, 0, 0, 100, 0.5);
    QVERIFY(result < 50.0);
  }

  // --- dB conversion ---

  void amplitudeToDbUnity() { QCOMPARE(amplitude_to_db(1.0), 0.0); }

  void dbToAmplitudeUnity() { QCOMPARE(db_to_amplitude(0.0), 1.0); }

  void dbRoundtrip() {
    double amp = 0.5;
    QVERIFY(qAbs(db_to_amplitude(amplitude_to_db(amp)) - amp) < 0.01);
  }

  void amplitudeToDbHalf() {
    // 0.5 amplitude ≈ -6.02 dB
    QVERIFY(qAbs(amplitude_to_db(0.5) - (-6.0206)) < 0.01);
  }

  void dbToAmplitudeMinusSix() {
    // -6 dB ≈ 0.501 amplitude
    QVERIFY(qAbs(db_to_amplitude(-6.0) - 0.501) < 0.01);
  }

  void dbToAmplitudeMinusTwenty() {
    // -20 dB = 0.1 amplitude (exact)
    QVERIFY(qAbs(db_to_amplitude(-20.0) - 0.1) < 0.01);
  }

  void dbToAmplitudePlusTwenty() {
    // +20 dB = 10.0 amplitude (exact)
    QVERIFY(qAbs(db_to_amplitude(20.0) - 10.0) < 0.01);
  }

  // --- Color interpolation edge cases ---

  void colorInterpolationIdentical() {
    QColor c(100, 200, 50);
    int r = lerp(c.red(), c.red(), 0.5);
    int g = lerp(c.green(), c.green(), 0.5);
    int b = lerp(c.blue(), c.blue(), 0.5);
    QCOMPARE(r, c.red());
    QCOMPARE(g, c.green());
    QCOMPARE(b, c.blue());
  }

  void colorInterpolationAtZero() {
    QColor first(10, 20, 30);
    QColor second(100, 200, 50);
    QCOMPARE(lerp(first.red(), second.red(), 0.0), first.red());
    QCOMPARE(lerp(first.green(), second.green(), 0.0), first.green());
    QCOMPARE(lerp(first.blue(), second.blue(), 0.0), first.blue());
  }

  void colorInterpolationAtOne() {
    QColor first(10, 20, 30);
    QColor second(100, 200, 50);
    QCOMPARE(lerp(first.red(), second.red(), 1.0), second.red());
    QCOMPARE(lerp(first.green(), second.green(), 1.0), second.green());
    QCOMPARE(lerp(first.blue(), second.blue(), 1.0), second.blue());
  }
};

QTEST_MAIN(TestEffects)
#include "test_effects.moc"
