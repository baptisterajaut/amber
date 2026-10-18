// .ove round trip: LoadThread on a hand-written project, a few non-default values set through the API, then
// save (projectsaver) -> reload -> save again. The two saves must be byte-identical, and the reloaded model must
// still hold the values. Proves save and load agree on everything the saver writes, headless.

#include <QtTest>

#include <QGuiApplication>
#include <QPointer>
#include <QTemporaryDir>

#include "core/keyframe.h"
#include "effects/effect.h"
#include "effects/effectfield.h"
#include "effects/effectrow.h"
#include "effects/transition.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "global/projectio.h"
#include "project/footage.h"
#include "project/loadthread.h"
#include "project/media.h"
#include "project/previewgenerator.h"
#include "project/projectmodel.h"
#include "project/projectsaver.h"
#include "tests/test_appcontext_stub.h"

// External entry points from effectloaders.cpp (no header export), as in test_render_harness.cpp
extern void load_internal_effects();
extern void load_shader_effects();

namespace {

// Folder 1 holds footage 1 and sequence "Inner"; "Outer" (open) nests "Inner" and sits at the root.
// Inner: footage clip on V1 linked to a Title (Transform + Text) clip on V2 with a Cross Dissolve opening.
QString fixture(const QString& url, const QString& footage) {
  return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<project>
    <version>%1</version>
    <url>%2</url>
    <previewresolution>1</previewresolution>
    <folders>
        <folder id="1" name="Bin" parent="0" label="2"/>
    </folders>
    <media>
        <footage id="1" folder="1" name="clip" url="%3">
            <marker frame="3" name="footage marker" duration="-4"/>
        </footage>
    </media>
    <sequences>
        <sequence id="1" folder="1" name="Inner" width="64" height="64" framerate="30" afreq="48000" alayout="3">
            <guide orientation="0" position="16"/>
            <clip id="0" enabled="1" name="clip" clipin="0" in="0" out="30" track="-1" r="128" g="128" b="192"
                  autoscale="1" type="0" media="1" stream="0" speed="1" maintainpitch="0" reverse="0">
                <linked><link id="1"/></linked>
            </clip>
            <clip id="1" enabled="1" name="title" clipin="0" in="0" out="30" track="-2" r="192" g="128" b="128"
                  autoscale="1" speed="1" maintainpitch="0" reverse="0">
                <marker frame="2" name="clip marker" duration="10"/>
                <linked><link id="0"/></linked>
                <opening length="10" name="/Cross Dissolve" enabled="1"></opening>
                <effect name="Distort/Transform" enabled="1"></effect>
                <effect name="Render/Text" enabled="1"></effect>
            </clip>
            <marker frame="5" name="sequence marker" label="3" duration="45"/>
            <marker frame="60" name="point marker"/>
        </sequence>
        <sequence id="2" folder="0" name="Outer" width="64" height="64" framerate="30" afreq="48000" alayout="3"
                  open="1">
            <clip id="0" enabled="1" name="Inner" clipin="0" in="0" out="30" track="-1" r="128" g="128" b="128"
                  autoscale="1" type="%4" sequence="1" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
        </sequence>
    </sequences>
</project>
)")
      .arg(QString::number(amber::kSaveVersion), url.toHtmlEscaped(), footage.toHtmlEscaped(),
           QString::number(MEDIA_TYPE_SEQUENCE));
}

EffectField* field_by_id(Effect* e, const QString& id) {
  for (int i = 0; i < e->row_count(); i++) {
    EffectRow* row = e->row(i);
    for (int j = 0; j < row->FieldCount(); j++) {
      if (row->Field(j)->id() == id) return row->Field(j);
    }
  }
  return nullptr;
}

Media* find_media(const QString& name, Media* parent = nullptr) {
  for (int i = 0; i < amber::project_model.childCount(parent); i++) {
    Media* m = amber::project_model.child(i, parent);
    if (m->get_name() == name) return m;
    if (m->get_type() == MEDIA_TYPE_FOLDER) {
      if (Media* found = find_media(name, m)) return found;
    }
  }
  return nullptr;
}

EffectKeyframe keyframe(long frame, double value) {
  EffectKeyframe key;
  key.time = frame;
  key.data = value;
  return key;
}

QString first_difference(const QByteArray& a, const QByteArray& b) {
  const QList<QByteArray> la = a.split('\n'), lb = b.split('\n');
  for (int i = 0; i < qMax(la.size(), lb.size()); i++) {
    const QByteArray x = la.value(i), y = lb.value(i);
    if (x != y) return QString("line %1:\n  a: %2\n  b: %3").arg(i + 1).arg(QString(x), QString(y));
  }
  return {};
}

}  // namespace

class TestProjectSaver : public QObject {
  Q_OBJECT

 private:
  TestAppContext ctx_;
  ProjectIO* pio_ = nullptr;
  QTemporaryDir dir_;

  // LoadThread on a worker + this event loop, then wait for every footage probe, as the app does before saving
  bool load(const QString& path) {
    bool succeeded = false;
    auto* lt = new LoadThread(path, false);
    QPointer<LoadThread> guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    if (!QTest::qWaitFor([&guard]() { return guard.isNull(); }, 10000)) return false;
    Media* footage = find_media("clip");
    if (footage == nullptr) return false;
    QPointer<PreviewGenerator> gen(footage->to_footage()->preview_gen.load());
    if (!QTest::qWaitFor([&gen]() { return gen.isNull(); }, 10000)) return false;
    return succeeded;
  }

  void reset_project() {
    amber::ActiveSequence = nullptr;
    amber::project_model.clear();
  }

  ClipPtr title_clip() {
    SequencePtr inner = find_media("Inner")->to_sequence();
    for (const ClipPtr& c : inner->clips) {
      if (c != nullptr && c->name() == "title") return c;
    }
    return nullptr;
  }

 private slots:
  void initTestCase() {
    QVERIFY(dir_.isValid());
    qputenv("XDG_DATA_HOME", dir_.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", dir_.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", dir_.filePath("config").toUtf8());
    amber::app_ctx = &ctx_;
    pio_ = new ProjectIO();
    amber::project_io = pio_;
    // Effect registry, the way test_render_harness.cpp fills it: without it LoadThread turns every <effect> and
    // transition into a row-less VoidEffect and the round trip compares nothing
    qputenv("AMBER_EFFECTS_PATH", QByteArray(SOURCE_DIR "/effects/shaders"));
    amber::CurrentConfig = {};
    if (effects.empty()) {
      load_internal_effects();
      load_shader_effects();
    }
    QVERIFY(get_meta_from_name("Render/Text") != nullptr);
  }

  void cleanupTestCase() {
    reset_project();
    amber::project_io = nullptr;
    delete pio_;
    amber::app_ctx = nullptr;
  }

  void roundTripIsStable() {
    const QString footage = QStringLiteral(SOURCE_DIR "/tests/fixtures/t1_yuv_input.mp4");
    QVERIFY(QFileInfo::exists(footage));
    const QString fixture_path = dir_.filePath("fixture.ove");
    {
      QFile f(fixture_path);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(fixture(fixture_path, footage).toUtf8());
    }
    QVERIFY(load(fixture_path));

    // Non-default values, set through the model API
    ClipPtr title = title_clip();
    QVERIFY(title != nullptr);
    QCOMPARE(title->effects.size(), 2);
    Effect* transform = title->effects.at(0).get();
    Effect* text = title->effects.at(1).get();
    field_by_id(text, "text")->SetValueAt(0, QString("Round trip"));
    field_by_id(text, "reveal")->SetValueAt(0, 40.0);
    field_by_id(text, "animatein")->SetValueAt(0, true);
    field_by_id(text, "animateduration")->SetValueAt(0, 1.25);
    // SetValueAt only writes keyframes once the field already has one (EffectField::HasKeyframes), and the UI path
    // (EffectRow::SetKeyframingEnabled) goes through the undo stack: append the keyframes the way the loader does
    EffectField* posx = field_by_id(transform, "posx");
    posx->GetParentRow()->SetKeyframingInternal(true);
    posx->keyframes.append(keyframe(0, 32.0));
    posx->keyframes.append(keyframe(30, 48.0));  // frame 30 = 1 s at 30 fps
    // GetValueAt takes seconds. It also caches the interpolated value in persistent_data_, which the saver writes as
    // the field's value="48" next to the keys
    QCOMPARE(posx->GetValueAt(1.0).toDouble(), 48.0);
    // Track header toggles on Inner, set the way the header does; Outer keeps none
    SequencePtr inner_seq = find_media("Inner")->to_sequence();
    TrackState hidden, muted, solo, locked;
    hidden.hidden = true;
    muted.muted = true;
    solo.solo = true;
    locked.locked = true;
    inner_seq->set_track_state(-2, hidden);
    inner_seq->set_track_state(0, muted);
    inner_seq->set_track_state(1, solo);
    inner_seq->set_track_state(-1, locked);

    const QString a = dir_.filePath("a.ove");
    const QString b = dir_.filePath("b.ove");
    SequencePtr outer = find_media("Outer")->to_sequence();
    QCOMPARE(amber::ActiveSequence, outer);  // open="1" exercised on both saves
    QVERIFY(amber::save_project_file(a, a));

    reset_project();
    QVERIFY(load(a));
    QVERIFY(amber::ActiveSequence != nullptr);
    QCOMPARE(amber::ActiveSequence->name, QString("Outer"));
    QVERIFY(amber::save_project_file(b, a));

    QFile fa(a), fb(b);
    QVERIFY(fa.open(QFile::ReadOnly));
    QVERIFY(fb.open(QFile::ReadOnly));
    const QByteArray ba = fa.readAll(), bb = fb.readAll();
    QVERIFY2(ba == bb, qPrintable(first_difference(ba, bb)));
    // Range markers write a duration, point markers don't (the negative footage one loads as a point marker)
    int marker_durations = 0;
    for (const QByteArray& line : ba.split('\n')) {
      if (line.contains("<marker ") && line.contains(" duration=")) marker_durations++;
    }
    QCOMPARE(marker_durations, 2);
    QVERIFY(ba.contains(R"(name="sequence marker" label="3" duration="45")"));
    QVERIFY(ba.contains(R"(name="clip marker" duration="10")"));
    QVERIFY(ba.contains(R"(name="point marker"/>)"));
    QVERIFY(ba.contains(R"(name="footage marker"/>)"));
    // One <track> per non-default track, in index order, only the true flags; none for Outer
    QList<QByteArray> track_lines;
    for (const QByteArray& line : ba.split('\n')) {
      if (line.contains("<track ")) track_lines.append(line.trimmed());
    }
    QCOMPARE(track_lines.size(), 4);
    QCOMPARE(track_lines.at(0), QByteArray(R"(<track index="-2" hidden="1"/>)"));
    QCOMPARE(track_lines.at(1), QByteArray(R"(<track index="-1" locked="1"/>)"));
    QCOMPARE(track_lines.at(2), QByteArray(R"(<track index="0" muted="1"/>)"));
    QCOMPARE(track_lines.at(3), QByteArray(R"(<track index="1" solo="1"/>)"));
    const int outer_at = ba.indexOf(R"(name="Outer")");
    QVERIFY(outer_at > 0);
    QVERIFY(!ba.mid(outer_at, ba.indexOf("</sequence>", outer_at) - outer_at).contains("<track"));

    // The reloaded model holds what was set
    title = title_clip();
    QVERIFY(title != nullptr);
    QCOMPARE(title->effects.size(), 2);
    transform = title->effects.at(0).get();
    text = title->effects.at(1).get();
    QCOMPARE(field_by_id(text, "text")->GetValueAt(0).toString(), QString("Round trip"));
    QCOMPARE(field_by_id(text, "reveal")->GetValueAt(0).toDouble(), 40.0);
    QCOMPARE(field_by_id(text, "animatein")->GetValueAt(0).toBool(), true);
    QCOMPARE(field_by_id(text, "animateduration")->GetValueAt(0).toDouble(), 1.25);
    posx = field_by_id(transform, "posx");
    QCOMPARE(posx->keyframes.size(), 2);
    QCOMPARE(posx->GetValueAt(1.0).toDouble(), 48.0);
    QVERIFY(title->opening_transition != nullptr);
    QCOMPARE(title->opening_transition->get_true_length(), 10);
    QCOMPARE(title->get_markers().size(), 1);
    QCOMPARE(title->get_markers().at(0).duration, 10L);
    QCOMPARE(title->linked.size(), 1);

    SequencePtr inner = find_media("Inner")->to_sequence();
    QCOMPARE(inner->guides.size(), 1);
    QCOMPARE(inner->markers.size(), 2);
    QCOMPARE(inner->markers.at(0).color_label, 3);
    QCOMPARE(inner->markers.at(0).duration, 45L);
    QCOMPARE(inner->markers.at(1).duration, 0L);
    QCOMPARE(inner->track_states.size(), 4);
    QVERIFY(inner->track_state(-2).hidden);
    QVERIFY(inner->track_state(-1).locked);
    QVERIFY(inner->track_state(0).muted);
    QVERIFY(inner->track_state(1).solo);
    QVERIFY(!inner->track_state(-2).locked);  // only the flags that were written
    QVERIFY(find_media("Outer")->to_sequence()->track_states.isEmpty());
    SequencePtr outer2 = find_media("Outer")->to_sequence();
    QCOMPARE(outer2->clips.size(), 1);
    QVERIFY(outer2->clips.at(0)->media() != nullptr);
    QCOMPARE(outer2->clips.at(0)->media()->to_sequence(), inner);
    QCOMPARE(find_media("clip")->to_footage()->markers.size(), 1);
    QCOMPARE(find_media("clip")->to_footage()->markers.at(0).duration, 0L);
  }

  // A new effect shows its defaults: otherwise its reset arrows are lit before any edit and Reset changes the
  // picture (Text's Shadow Opacity was set to 100, then given a default of 80)
  void freshEffectsStartAtDefaults() {
    ClipPtr title = title_clip();
    QVERIFY(title != nullptr);
    for (const EffectMeta& em : effects) {
      if (em.type != EFFECT_TYPE_EFFECT) continue;
      EffectPtr e = Effect::Create(title.get(), &em);
      QVERIFY2(e != nullptr, qPrintable(em.name));
      for (int i = 0; i < e->row_count(); i++) {
        EffectRow* row = e->row(i);
        for (int j = 0; j < row->FieldCount(); j++) {
          EffectField* f = row->Field(j);
          if (!f->HasDefault()) continue;
          QVERIFY2(f->GetValueAt(0) == f->GetDefaultData(), qPrintable(em.name + " / " + f->id()));
        }
      }
    }
  }

  // A failed save must report false and leave the previous file untouched (disk full, read-only share). Before
  // QSaveFile the old file was truncated on open and a write error still returned true
  void failedSaveKeepsPreviousFile() {
    reset_project();
    const QString missing_dir = dir_.filePath("no-such-dir/p.ove");
    QVERIFY(!amber::save_project_file(missing_dir, missing_dir));

    // /dev/full: open succeeds, every write fails with ENOSPC
    if (QFileInfo::exists("/dev/full")) {
      QVERIFY(!amber::save_project_file("/dev/full", "/dev/full"));
    }

    // Writable file in a read-only directory: QSaveFile has nowhere to put its temp file, so it falls back to writing
    // the file in place (setDirectWriteFallback) and the save still succeeds, as it did with QFile
    const QString ro_dir = dir_.filePath("readonly");
    QVERIFY(QDir().mkpath(ro_dir));
    const QString existing = ro_dir + "/old.ove";
    {
      QFile f(existing);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write("previous project");
    }
    QFile::setPermissions(ro_dir, QFile::ReadOwner | QFile::ExeOwner);
    QFile probe(ro_dir + "/probe");
    const bool dir_writable = probe.open(QFile::WriteOnly);  // root ignores the mode bits
    const bool saved = !dir_writable && amber::save_project_file(existing, existing);
    QFile::setPermissions(ro_dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (dir_writable) QSKIP("directory permissions not enforced (running as root?)");
    QVERIFY(saved);
    QFile f(existing);
    QVERIFY(f.open(QFile::ReadOnly));
    QVERIFY(f.readAll().startsWith("<?xml"));
  }
};

// TextEffect's FontField queries QFontDatabase, which needs a QGuiApplication (same main as the GPU tests)
int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  TestProjectSaver test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_projectsaver.moc"
