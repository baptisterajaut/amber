#include <QtTest>

#include "engine/clip.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/clipboard.h"
#include "project/clipboard_serializer.h"

namespace {

ClipPtr make_clip(const QString& name, int track, long in, long out) {
  ClipPtr c = std::make_shared<Clip>(nullptr);
  c->set_media(nullptr, 0);
  c->set_name(name);
  c->set_track(track);
  c->set_timeline_in(in);
  c->set_timeline_out(out);
  c->set_clip_in(0);
  c->set_cached_frame_rate(30.0);
  return c;
}

ClipPtr clip_at(int i) { return std::static_pointer_cast<Clip>(clipboard.at(i)); }

}  // namespace

class TestClipboardSerializer : public QObject {
  Q_OBJECT
 private slots:
  void cleanup() { clear_clipboard(); }
  void clipAttributesRoundTrip();
  void displayRotationCrossesProjects();
};

void TestClipboardSerializer::clipAttributesRoundTrip() {
  clear_clipboard();
  clipboard_type = CLIPBOARD_TYPE_CLIP;

  ClipPtr a = make_clip("A", -1, 0, 30);
  a->set_color_label(0);  // no label: attribute omitted, must come back as 0

  ClipPtr b = make_clip("B", 0, 0, 30);
  b->set_color_label(5);
  ClipSpeed speed;
  speed.value = 0.5;
  speed.maintain_audio_pitch = true;
  b->set_speed(speed);
  b->set_reversed(true);

  ClipPtr c = make_clip("C", -2, 10, 40);
  c->set_loop_mode(kLoopClamp);

  // Links are clipboard indices
  a->linked = {1, 2};
  b->linked = {0, 2};
  c->linked = {0, 1};

  clipboard.append(a);
  clipboard.append(b);
  clipboard.append(c);

  const QByteArray xml = amber::serialize_clipboard_to_xml();
  QVERIFY(!xml.isEmpty());

  QVector<MediaPtr> imported;
  QVERIFY(amber::deserialize_clipboard_from_xml(xml, imported));
  QVERIFY(imported.isEmpty());
  QCOMPARE(clipboard_type, CLIPBOARD_TYPE_CLIP);
  QCOMPARE(clipboard.size(), 3);

  ClipPtr ra = clip_at(0);
  ClipPtr rb = clip_at(1);
  ClipPtr rc = clip_at(2);

  QCOMPARE(ra->name(), QString("A"));
  QCOMPARE(rb->name(), QString("B"));
  QCOMPARE(rc->name(), QString("C"));
  QCOMPARE(rc->track(), -2);
  QCOMPARE(rc->timeline_in(), 10L);
  QCOMPARE(rc->timeline_out(), 40L);

  QCOMPARE(ra->color_label(), 0);
  QCOMPARE(rb->color_label(), 5);

  QCOMPARE(rb->speed().value, 0.5);
  QCOMPARE(rb->speed().maintain_audio_pitch, true);
  QCOMPARE(rb->reversed(), true);
  QCOMPARE(ra->speed().value, 1.0);
  QCOMPARE(ra->speed().maintain_audio_pitch, false);
  QCOMPARE(ra->reversed(), false);

  QCOMPARE(ra->loop_mode(), int(kLoopNone));
  QCOMPARE(rc->loop_mode(), int(kLoopClamp));

  QCOMPARE(ra->linked, QVector<int>({1, 2}));
  QCOMPARE(rb->linked, QVector<int>({0, 2}));
  QCOMPARE(rc->linked, QVector<int>({0, 1}));
}

// A clip pasted into another project brings its media as a stub: the stub keeps whether the display rotation applies,
// so a phone video imported before the fix doesn't turn upright in the other project (and one imported after stays
// upright). A clipboard from an Amber without the attribute pastes with the rotation off, like the project it came from
void TestClipboardSerializer::displayRotationCrossesProjects() {
  clear_clipboard();
  clipboard_type = CLIPBOARD_TYPE_CLIP;
  Media rotated, legacy;
  auto rotated_footage = std::make_shared<Footage>();
  rotated_footage->url = "/nonexistent/new-import.mp4";
  rotated_footage->display_rotation = true;
  rotated.set_footage(rotated_footage);
  auto legacy_footage = std::make_shared<Footage>();
  legacy_footage->url = "/nonexistent/old-import.mp4";
  legacy_footage->display_rotation = false;
  legacy.set_footage(legacy_footage);

  ClipPtr a = make_clip("A", -1, 0, 30);
  a->set_media(&rotated, 0);
  ClipPtr b = make_clip("B", -1, 30, 60);
  b->set_media(&legacy, 0);
  clipboard.append(a);
  clipboard.append(b);
  const QByteArray xml = amber::serialize_clipboard_to_xml();
  clear_clipboard();  // the clips point at the Media above

  QVector<MediaPtr> imported;
  QVERIFY(amber::deserialize_clipboard_from_xml(xml, imported));
  QCOMPARE(imported.size(), 2);
  QCOMPARE(imported.at(0)->to_footage()->url, rotated_footage->url);
  QCOMPARE(imported.at(0)->to_footage()->display_rotation, true);
  QCOMPARE(imported.at(1)->to_footage()->display_rotation, false);
  clear_clipboard();

  QByteArray old_xml = xml;
  old_xml.replace(" displayRotation=\"1\"", "");
  QVERIFY(old_xml != xml);
  imported.clear();
  QVERIFY(amber::deserialize_clipboard_from_xml(old_xml, imported));
  QCOMPARE(imported.size(), 2);
  QCOMPARE(imported.at(0)->to_footage()->display_rotation, false);
  clear_clipboard();
}

QTEST_GUILESS_MAIN(TestClipboardSerializer)
#include "test_clipboard_serializer.moc"
