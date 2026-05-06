#include "project/clipboard_serializer.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "effects/effect.h"
#include "effects/internal/voideffect.h"
#include "engine/clip.h"
#include "project/clipboard.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/projectmodel.h"

namespace amber {

namespace {

constexpr int kClipboardMimeVersion = 1;

void write_clip(QXmlStreamWriter& stream, const ClipPtr& c, int load_id) {
  stream.writeStartElement("clip");
  stream.writeAttribute("loadid", QString::number(load_id));
  stream.writeAttribute("enabled", QString::number(c->enabled()));
  stream.writeAttribute("name", c->name());
  stream.writeAttribute("clipin", QString::number(c->clip_in()));
  stream.writeAttribute("in", QString::number(c->timeline_in()));
  stream.writeAttribute("out", QString::number(c->timeline_out()));
  stream.writeAttribute("track", QString::number(c->track()));
  stream.writeAttribute("framerate", QString::number(c->cached_frame_rate(), 'f', 10));
  stream.writeAttribute("r", QString::number(c->color().red()));
  stream.writeAttribute("g", QString::number(c->color().green()));
  stream.writeAttribute("b", QString::number(c->color().blue()));
  if (c->color_label() > 0) stream.writeAttribute("label", QString::number(c->color_label()));
  stream.writeAttribute("autoscale", QString::number(c->autoscaled()));
  stream.writeAttribute("speed", QString::number(c->speed().value, 'f', 10));
  stream.writeAttribute("maintainpitch", QString::number(c->speed().maintain_audio_pitch));
  stream.writeAttribute("reverse", QString::number(c->reversed()));
  if (c->loop_mode() != kLoopNone) stream.writeAttribute("loop", QString::number(c->loop_mode()));

  if (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    Footage* f = c->media()->to_footage();
    if (f != nullptr) {
      stream.writeStartElement("media");
      stream.writeAttribute("url", f->url);
      stream.writeAttribute("stream", QString::number(c->media_stream_index()));
      stream.writeAttribute("startNumber", QString::number(f->start_number));
      stream.writeAttribute("name", f->name);
      stream.writeEndElement();
    }
  }

  stream.writeStartElement("linked");
  for (int k : c->linked) {
    stream.writeStartElement("link");
    stream.writeAttribute("id", QString::number(k));
    stream.writeEndElement();
  }
  stream.writeEndElement();

  for (const auto& effect : c->effects) {
    stream.writeStartElement("effect");
    effect->save(stream);
    stream.writeEndElement();
  }

  stream.writeEndElement();  // clip
}

struct ParsedMediaRef {
  QString url;
  QString name;
  int stream{-1};
  int start_number{0};
  bool present{false};
};

void parse_effect_into_clip(QXmlStreamReader& stream, Clip* c) {
  QString effect_name;
  bool effect_enabled = true;
  for (const auto& attr : stream.attributes()) {
    if (attr.name() == QLatin1String("name")) effect_name = attr.value().toString();
    else if (attr.name() == QLatin1String("enabled")) effect_enabled = (attr.value() == QLatin1String("1"));
  }
  const EffectMeta* meta = effect_name.isEmpty() ? nullptr : get_meta_from_name(effect_name);
  if (meta == nullptr) {
    EffectPtr ve(new VoidEffect(c, effect_name));
    ve->SetEnabled(effect_enabled);
    ve->load(stream);
    ve->moveToThread(QApplication::instance()->thread());
    c->effects.append(ve);
  } else {
    EffectPtr e(Effect::Create(c, meta));
    e->SetEnabled(effect_enabled);
    e->load(stream);
    e->moveToThread(QApplication::instance()->thread());
    c->effects.append(e);
  }
}

void parse_clip_links(QXmlStreamReader& stream, Clip* c) {
  while (!(stream.name() == QLatin1String("linked") && stream.isEndElement()) && !stream.atEnd()) {
    stream.readNext();
    if (stream.name() == QLatin1String("link") && stream.isStartElement()) {
      for (const auto& attr : stream.attributes()) {
        if (attr.name() == QLatin1String("id")) {
          c->linked.append(attr.value().toInt());
          break;
        }
      }
    }
  }
}

ClipPtr parse_clip(QXmlStreamReader& stream, ParsedMediaRef& media_ref_out) {
  ClipPtr c = std::make_shared<Clip>(nullptr);
  QColor color;
  ClipSpeed speed_info = c->speed();
  double framerate = 0.0;

  for (const auto& attr : stream.attributes()) {
    const auto n = attr.name();
    if (n == QLatin1String("loadid")) c->load_id = attr.value().toInt();
    else if (n == QLatin1String("enabled")) c->set_enabled(attr.value() == QLatin1String("1"));
    else if (n == QLatin1String("name")) c->set_name(attr.value().toString());
    else if (n == QLatin1String("clipin")) c->set_clip_in(attr.value().toLong());
    else if (n == QLatin1String("in")) c->set_timeline_in(attr.value().toLong());
    else if (n == QLatin1String("out")) c->set_timeline_out(attr.value().toLong());
    else if (n == QLatin1String("track")) c->set_track(attr.value().toInt());
    else if (n == QLatin1String("framerate")) framerate = attr.value().toDouble();
    else if (n == QLatin1String("r")) color.setRed(attr.value().toInt());
    else if (n == QLatin1String("g")) color.setGreen(attr.value().toInt());
    else if (n == QLatin1String("b")) color.setBlue(attr.value().toInt());
    else if (n == QLatin1String("label")) c->set_color_label(attr.value().toInt());
    else if (n == QLatin1String("autoscale")) c->set_autoscaled(attr.value() == QLatin1String("1"));
    else if (n == QLatin1String("speed")) speed_info.value = attr.value().toDouble();
    else if (n == QLatin1String("maintainpitch")) speed_info.maintain_audio_pitch = (attr.value() == QLatin1String("1"));
    else if (n == QLatin1String("reverse")) c->set_reversed(attr.value() == QLatin1String("1"));
    else if (n == QLatin1String("loop")) c->set_loop_mode(attr.value().toInt());
  }
  c->set_color(color);
  c->set_speed(speed_info);
  if (framerate > 0) c->set_cached_frame_rate(framerate);

  while (!(stream.name() == QLatin1String("clip") && stream.isEndElement()) && !stream.atEnd()) {
    stream.readNext();
    if (!stream.isStartElement()) continue;
    if (stream.name() == QLatin1String("media")) {
      for (const auto& attr : stream.attributes()) {
        if (attr.name() == QLatin1String("url")) media_ref_out.url = attr.value().toString();
        else if (attr.name() == QLatin1String("stream")) media_ref_out.stream = attr.value().toInt();
        else if (attr.name() == QLatin1String("startNumber")) media_ref_out.start_number = attr.value().toInt();
        else if (attr.name() == QLatin1String("name")) media_ref_out.name = attr.value().toString();
      }
      media_ref_out.present = true;
    } else if (stream.name() == QLatin1String("linked")) {
      parse_clip_links(stream, c.get());
    } else if (stream.name() == QLatin1String("effect")) {
      parse_effect_into_clip(stream, c.get());
    }
  }
  return c;
}

Media* find_media_by_url_recursive(const QString& url, Media* parent) {
  Media* root = (parent == nullptr) ? amber::project_model.get_root() : parent;
  if (root == nullptr) return nullptr;
  for (int i = 0; i < root->childCount(); i++) {
    Media* m = root->child(i);
    if (m == nullptr) continue;
    if (m->get_type() == MEDIA_TYPE_FOOTAGE) {
      Footage* f = m->to_footage();
      if (f != nullptr && f->url == url) return m;
    } else if (m->get_type() == MEDIA_TYPE_FOLDER) {
      Media* found = find_media_by_url_recursive(url, m);
      if (found != nullptr) return found;
    }
  }
  return nullptr;
}

MediaPtr import_media_stub(const ParsedMediaRef& ref) {
  FootagePtr f = std::make_shared<Footage>();
  f->using_inout = false;
  f->url = ref.url;
  f->name = ref.name.isEmpty() ? QFileInfo(ref.url).fileName() : ref.name;
  f->start_number = ref.start_number;
  MediaPtr item = std::make_shared<Media>();
  item->set_footage(f);
  return item;
}

}  // namespace

QByteArray serialize_clipboard_to_xml() {
  if (clipboard.isEmpty()) return QByteArray();

  QByteArray bytes;
  QXmlStreamWriter stream(&bytes);
  stream.writeStartDocument();
  stream.writeStartElement("amber-clipboard");
  stream.writeAttribute("version", QString::number(kClipboardMimeVersion));
  stream.writeAttribute("type", QString::number(clipboard_type));

  if (clipboard_type == CLIPBOARD_TYPE_CLIP) {
    for (int i = 0; i < clipboard.size(); i++) {
      ClipPtr c = std::static_pointer_cast<Clip>(clipboard.at(i));
      // Skip clips referencing sequences — sequence_id is process-local and not portable.
      if (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE) continue;
      write_clip(stream, c, c->load_id);
    }
  } else if (clipboard_type == CLIPBOARD_TYPE_EFFECT) {
    for (const auto& v : clipboard) {
      EffectPtr e = std::static_pointer_cast<Effect>(v);
      stream.writeStartElement("effect");
      e->save(stream);
      stream.writeEndElement();
    }
  }

  stream.writeEndElement();  // amber-clipboard
  stream.writeEndDocument();
  return bytes;
}

bool deserialize_clipboard_from_xml(const QByteArray& xml, QVector<MediaPtr>& imported_media) {
  QXmlStreamReader stream(xml);
  if (!stream.readNextStartElement()) return false;
  if (stream.name() != QLatin1String("amber-clipboard")) return false;

  int type = CLIPBOARD_TYPE_CLIP;
  for (const auto& attr : stream.attributes()) {
    if (attr.name() == QLatin1String("type")) type = attr.value().toInt();
  }

  clear_clipboard();
  clipboard_type = type;

  // url -> Media* (resolved either to existing or to a freshly-imported stub)
  QHash<QString, Media*> url_to_media;

  while (!stream.atEnd() && !stream.hasError()) {
    stream.readNext();
    if (!stream.isStartElement()) continue;

    if (type == CLIPBOARD_TYPE_CLIP && stream.name() == QLatin1String("clip")) {
      ParsedMediaRef ref;
      ClipPtr c = parse_clip(stream, ref);
      if (ref.present) {
        Media* m = nullptr;
        auto it = url_to_media.constFind(ref.url);
        if (it != url_to_media.constEnd()) {
          m = it.value();
        } else {
          m = find_media_by_url_recursive(ref.url, nullptr);
          if (m == nullptr) {
            MediaPtr stub = import_media_stub(ref);
            imported_media.append(stub);
            m = stub.get();
          }
          url_to_media.insert(ref.url, m);
        }
        c->set_media(m, ref.stream);
      }
      clipboard.append(c);
    } else if (type == CLIPBOARD_TYPE_EFFECT && stream.name() == QLatin1String("effect")) {
      QString effect_name;
      bool effect_enabled = true;
      for (const auto& attr : stream.attributes()) {
        if (attr.name() == QLatin1String("name")) effect_name = attr.value().toString();
        else if (attr.name() == QLatin1String("enabled")) effect_enabled = (attr.value() == QLatin1String("1"));
      }
      const EffectMeta* meta = effect_name.isEmpty() ? nullptr : get_meta_from_name(effect_name);
      if (meta == nullptr) {
        // Skip effects whose meta isn't registered in this process — would deserialize as a void
        // effect but VoidEffect needs a Clip* parent. Effects in the clipboard live unparented;
        // VoidEffect with null parent isn't safe to use elsewhere. Drop it.
        stream.skipCurrentElement();
        continue;
      }
      EffectPtr e(Effect::Create(nullptr, meta));
      e->SetEnabled(effect_enabled);
      e->load(stream);
      e->moveToThread(QApplication::instance()->thread());
      clipboard.append(e);
    }
  }

  return !stream.hasError();
}

void push_clipboard_to_system() {
  QByteArray xml = serialize_clipboard_to_xml();
  if (xml.isEmpty()) return;
  auto* mime = new QMimeData();
  mime->setData(kClipboardMime, xml);
  QGuiApplication::clipboard()->setMimeData(mime);
}

bool pull_clipboard_from_system(QVector<MediaPtr>& imported_media) {
  const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
  if (mime == nullptr || !mime->hasFormat(kClipboardMime)) return false;
  QByteArray xml = mime->data(kClipboardMime);
  // Intra-process roundtrip: globals already match what's on the system clipboard.
  if (xml == serialize_clipboard_to_xml()) return false;
  return deserialize_clipboard_from_xml(xml, imported_media);
}

}  // namespace amber
