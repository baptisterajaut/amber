// Effect presets (effects/effectpresets): save -> load onto another clip keeps every effect, value and keyframe;
// listing filters, sorts and names presets; an effect this system doesn't know is skipped and counted. Headless.

#include <QtTest>

#include <QGuiApplication>
#include <QTemporaryDir>

#include "core/keyframe.h"
#include "effects/effect.h"
#include "effects/effectfield.h"
#include "effects/effectpresets.h"
#include "effects/effectrow.h"
#include "effects/internal/voideffect.h"
#include "engine/clip.h"
#include "global/config.h"
#include "tests/test_appcontext_stub.h"

// External entry points from effectloaders.cpp (no header export), as in test_render_harness.cpp
extern void load_internal_effects();
extern void load_shader_effects();

namespace {

EffectField* field_by_id(Effect* e, const QString& id) {
  for (int i = 0; i < e->row_count(); i++) {
    EffectRow* row = e->row(i);
    for (int j = 0; j < row->FieldCount(); j++) {
      if (row->Field(j)->id() == id) return row->Field(j);
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

ClipPtr video_clip() {
  ClipPtr c = std::make_shared<Clip>(nullptr);
  c->set_track(-1);
  return c;
}

void write_file(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (f.open(QFile::WriteOnly | QFile::Truncate)) f.write(data);
}

QStringList names(const QVector<EffectPresetInfo>& presets) {
  QStringList l;
  for (const EffectPresetInfo& p : presets) l.append(p.name);
  return l;
}

}  // namespace

class TestEffectPresets : public QObject {
  Q_OBJECT

 private:
  TestAppContext ctx_;
  QTemporaryDir dir_;

 private slots:
  void initTestCase() {
    QVERIFY(dir_.isValid());
    qputenv("XDG_DATA_HOME", dir_.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", dir_.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", dir_.filePath("config").toUtf8());
    amber::app_ctx = &ctx_;
    qputenv("AMBER_EFFECTS_PATH", QByteArray(SOURCE_DIR "/effects/shaders"));
    amber::CurrentConfig = {};
    if (effects.empty()) {
      load_internal_effects();
      load_shader_effects();
    }
    QVERIFY(get_meta_from_name("Blur/Box Blur") != nullptr);
    QVERIFY(get_meta_from_name("Distort/Transform") != nullptr);
    QVERIFY(EffectPresetDir().startsWith(dir_.path()));
  }

  void init() { QDir(EffectPresetDir()).removeRecursively(); }

  void cleanupTestCase() { amber::app_ctx = nullptr; }

  void roundTrip() {
    ClipPtr src = video_clip();
    EffectPtr blur = Effect::Create(src.get(), get_meta_from_name("Blur/Box Blur"));
    EffectPtr transform = Effect::Create(src.get(), get_meta_from_name("Distort/Transform"));
    QVERIFY(blur != nullptr && transform != nullptr);
    blur->SetEnabled(false);
    field_by_id(blur.get(), "radius")->SetValueAt(0, 25.0);
    field_by_id(blur.get(), "horiz_blur")->SetValueAt(0, false);
    // Keyframes appended the way the loader does (see test_projectsaver.cpp)
    EffectField* posx = field_by_id(transform.get(), "posx");
    posx->GetParentRow()->SetKeyframingInternal(true);
    posx->keyframes.append(keyframe(0, 32.0));
    EffectKeyframe bezier = keyframe(30, 48.0);
    bezier.type = 2;
    bezier.post_handle_y = 5;
    posx->keyframes.append(bezier);

    const QString path = EffectPresetPathForName("Round trip");
    QVERIFY(SaveEffectPreset(path, "Round trip", EFFECT_TYPE_VIDEO, {blur.get(), transform.get()}));

    ClipPtr dst = video_clip();
    int missing = -1;
    const QVector<EffectPtr> loaded = LoadEffectPreset(path, dst.get(), &missing);
    QCOMPARE(missing, 0);
    QCOMPARE(loaded.size(), 2);
    const QVector<EffectPtr> saved = {blur, transform};
    for (int i = 0; i < saved.size(); i++) {
      Effect* a = saved.at(i).get();
      Effect* b = loaded.at(i).get();
      QCOMPARE(b->meta, a->meta);
      QCOMPARE(b->parent_clip, dst.get());
      QCOMPARE(b->IsEnabled(), a->IsEnabled());
      QCOMPARE(b->row_count(), a->row_count());
      for (int r = 0; r < a->row_count(); r++) {
        QCOMPARE(b->row(r)->IsKeyframing(), a->row(r)->IsKeyframing());
        for (int j = 0; j < a->row(r)->FieldCount(); j++) {
          EffectField* fa = a->row(r)->Field(j);
          EffectField* fb = b->row(r)->Field(j);
          QVERIFY2(fb->GetValueAt(0) == fa->GetValueAt(0), qPrintable(a->meta->name + " / " + fa->id()));
          QCOMPARE(fb->keyframes.size(), fa->keyframes.size());
          for (int k = 0; k < fa->keyframes.size(); k++) {
            const EffectKeyframe &ka = fa->keyframes.at(k), &kb = fb->keyframes.at(k);
            QCOMPARE(kb.time, ka.time);
            QCOMPARE(kb.data.toDouble(), ka.data.toDouble());
            QCOMPARE(kb.type, ka.type);
            QCOMPARE(kb.post_handle_y, ka.post_handle_y);
          }
        }
      }
    }
    QCOMPARE(loaded.at(0)->IsEnabled(), false);
    QCOMPARE(field_by_id(loaded.at(0).get(), "radius")->GetValueAt(0).toDouble(), 25.0);
    QCOMPARE(field_by_id(loaded.at(0).get(), "horiz_blur")->GetValueAt(0).toBool(), false);
    QCOMPARE(field_by_id(loaded.at(1).get(), "posx")->keyframes.size(), 2);
  }

  void list() {
    QVERIFY(SaveEffectPreset(EffectPresetPathForName("beta"), "beta", EFFECT_TYPE_VIDEO, {}));
    QVERIFY(SaveEffectPreset(EffectPresetPathForName("Alpha"), "Alpha", EFFECT_TYPE_VIDEO, {}));
    QVERIFY(SaveEffectPreset(EffectPresetPathForName("Zeta audio"), "Zeta audio", EFFECT_TYPE_AUDIO, {}));
    const QString accented = QString::fromUtf8("Écho doux/2");
    const QString accented_path = EffectPresetPathForName(accented);
    QVERIFY2(QFileInfo(accented_path).fileName().startsWith("_cho doux_2-"), qPrintable(accented_path));
    QVERIFY(SaveEffectPreset(accented_path, accented, EFFECT_TYPE_VIDEO, {}));
    write_file(EffectPresetDir() + "/garbage.xml", "this is <<< not xml");
    write_file(EffectPresetDir() + "/other root.xml", "<effect name=\"Blur/Box Blur\"/>");
    write_file(EffectPresetDir() + "/nameless.xml",
               QString("<effectpreset version=\"1\" subtype=\"%1\"/>").arg(EFFECT_TYPE_VIDEO).toUtf8());

    const QVector<EffectPresetInfo> video = ListEffectPresets(EFFECT_TYPE_VIDEO);
    // Case-insensitive: "beta" before "nameless"; É folds to é, after every ASCII letter
    QCOMPARE(names(video), QStringList({"Alpha", "beta", "nameless", accented}));
    for (const EffectPresetInfo& p : video) QCOMPARE(p.subtype, int(EFFECT_TYPE_VIDEO));
    QCOMPARE(video.at(3).path, accented_path);
    QCOMPARE(names(ListEffectPresets(EFFECT_TYPE_AUDIO)), QStringList({"Zeta audio"}));
  }

  void namesThatSanitiseAlikeKeepTheirOwnFile() {
    // "Glow!" and "Glow?" both sanitise to "Glow_": the hash suffix keeps them apart, and is stable
    QVERIFY(EffectPresetPathForName("Glow!") != EffectPresetPathForName("Glow?"));
    QCOMPARE(EffectPresetPathForName("Glow!"), EffectPresetPathForName("Glow!"));
    QString replaced;
    for (const QString& name : {QString("Glow!"), QString("Glow?")}) {
      const QString path = EffectPresetSavePath(name, &replaced);
      QVERIFY2(replaced.isEmpty(), qPrintable(name + " would replace " + replaced));
      QVERIFY(SaveEffectPreset(path, name, EFFECT_TYPE_VIDEO, {}));
    }
    QCOMPARE(names(ListEffectPresets(EFFECT_TYPE_VIDEO)), QStringList({"Glow!", "Glow?"}));

    // Saving a name again replaces its own file, and the prompt names it
    QCOMPARE(EffectPresetSavePath("Glow?", &replaced), EffectPresetPathForName("Glow?"));
    QCOMPARE(replaced, QString("Glow?"));
  }

  void presetsSavedBeforeTheHashStillWork() {
    // A file of the old naming (sanitised name only) is listed, and saving its name again replaces it in place
    const QString legacy = EffectPresetDir() + "/Old_one.xml";
    QVERIFY(SaveEffectPreset(legacy, "Old/one", EFFECT_TYPE_VIDEO, {}));
    QCOMPARE(names(ListEffectPresets(EFFECT_TYPE_VIDEO)), QStringList({"Old/one"}));
    QString replaced;
    QCOMPARE(EffectPresetSavePath("Old/one", &replaced), legacy);
    QCOMPARE(replaced, QString("Old/one"));
    // A name that only sanitises like it gets a new file, nothing replaced
    QCOMPARE(EffectPresetSavePath("Old?one", &replaced), EffectPresetPathForName("Old?one"));
    QVERIFY(replaced.isEmpty());
  }

  void missingEffectMetaIsDefined() {
    // A Missing Effect placeholder (no preset action for it) still has a defined subtype for the clipboard's
    // video/audio filter, kept by its clipboard copy, and is no internal effect
    ClipPtr video = video_clip();
    ClipPtr audio = std::make_shared<Clip>(nullptr);
    audio->set_track(0);
    VoidEffect on_video(video.get(), "Gone/Video");
    VoidEffect on_audio(audio.get(), "Gone/Audio");
    QCOMPARE(on_video.meta->subtype, int(EFFECT_TYPE_VIDEO));
    QCOMPARE(on_audio.meta->subtype, int(EFFECT_TYPE_AUDIO));
    QCOMPARE(on_video.meta->internal, -1);
    QCOMPARE(on_audio.copy(nullptr)->meta->subtype, int(EFFECT_TYPE_AUDIO));
  }

  void missingEffect() {
    const QString path = EffectPresetDir() + "/missing.xml";
    write_file(path, QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<effectpreset version="1" name="missing" subtype="%1">
    <effect name="Nowhere/Not An Effect" enabled="1">
        <row><field id="radius" value="99"><key value="1" frame="0" type="0"/></field></row>
    </effect>
    <effect name="Blur/Box Blur" enabled="1">
        <row><field id="radius" value="12"/></row>
    </effect>
</effectpreset>
)")
                         .arg(EFFECT_TYPE_VIDEO)
                         .toUtf8());
    ClipPtr dst = video_clip();
    int missing = -1;
    const QVector<EffectPtr> loaded = LoadEffectPreset(path, dst.get(), &missing);
    QCOMPARE(missing, 1);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0)->meta, get_meta_from_name("Blur/Box Blur"));
    QCOMPARE(field_by_id(loaded.at(0).get(), "radius")->GetValueAt(0).toDouble(), 12.0);

    // Unreadable file: nothing, nothing missing
    missing = -1;
    QVERIFY(LoadEffectPreset(EffectPresetDir() + "/no-such-file.xml", dst.get(), &missing).isEmpty());
    QCOMPARE(missing, 0);
  }
};

// Shader effects and fonts query the GUI side; same main as test_projectsaver.cpp
int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  TestEffectPresets test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_effect_presets.moc"
