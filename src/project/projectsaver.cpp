#include "project/projectsaver.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamWriter>

#include "effects/effect.h"
#include "effects/transition.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/projectmodel.h"

namespace {

// Per-save counters, formerly Project members (folder_id, media_id, sequence_id, proj_dir)
struct SaveState {
  int folder_id = 1;
  int media_id = 1;
  int sequence_id = 1;
  QDir proj_dir;
};

}  // namespace

static void save_marker(QXmlStreamWriter& stream, const Marker& m) {
  stream.writeStartElement("marker");
  stream.writeAttribute("frame", QString::number(m.frame));
  stream.writeAttribute("name", m.name);
  if (m.color_label > 0) {
    stream.writeAttribute("label", QString::number(m.color_label));
  }
  if (m.duration > 0) {
    stream.writeAttribute("duration", QString::number(m.duration));
  }
  stream.writeEndElement();
}

static void save_clip_media_attributes(QXmlStreamWriter& stream, const ClipPtr& c) {
  if (c->media() == nullptr) return;
  stream.writeAttribute("type", QString::number(c->media()->get_type()));
  switch (c->media()->get_type()) {
    case MEDIA_TYPE_FOOTAGE:
      stream.writeAttribute("media", QString::number(c->media()->to_footage()->save_id));
      stream.writeAttribute("stream", QString::number(c->media_stream_index()));
      break;
    case MEDIA_TYPE_SEQUENCE:
      stream.writeAttribute("sequence", QString::number(c->media()->to_sequence()->save_id));
      break;
  }
}

static void save_clip_transitions(QXmlStreamWriter& stream, const ClipPtr& c, int j,
                                  QVector<TransitionPtr>& transition_save_cache,
                                  QVector<int>& transition_clip_save_cache) {
  for (int t = kTransitionOpening; t <= kTransitionClosing; t++) {
    TransitionPtr transition = (t == kTransitionOpening) ? c->opening_transition : c->closing_transition;
    if (transition == nullptr) continue;

    stream.writeStartElement((t == kTransitionOpening) ? "opening" : "closing");
    int transition_cache_index = transition_save_cache.indexOf(transition);
    if (transition_cache_index > -1) {
      stream.writeAttribute("shared", QString::number(transition_clip_save_cache.at(transition_cache_index)));
    } else {
      transition->save(stream);
      transition_save_cache.append(transition);
      transition_clip_save_cache.append(j);
    }
    stream.writeEndElement();
  }
}

static void save_sequence_clips(QXmlStreamWriter& stream, Sequence* s) {
  QVector<TransitionPtr> transition_save_cache;
  QVector<int> transition_clip_save_cache;

  for (int j = 0; j < s->clips.size(); j++) {
    const ClipPtr& c = s->clips.at(j);
    if (c == nullptr) continue;

    stream.writeStartElement("clip");
    stream.writeAttribute("id", QString::number(j));
    stream.writeAttribute("enabled", QString::number(c->enabled()));
    stream.writeAttribute("name", c->name());
    stream.writeAttribute("clipin", QString::number(c->clip_in()));
    stream.writeAttribute("in", QString::number(c->timeline_in()));
    stream.writeAttribute("out", QString::number(c->timeline_out()));
    stream.writeAttribute("track", QString::number(c->track()));

    stream.writeAttribute("r", QString::number(c->color().red()));
    stream.writeAttribute("g", QString::number(c->color().green()));
    stream.writeAttribute("b", QString::number(c->color().blue()));
    if (c->color_label() > 0) stream.writeAttribute("label", QString::number(c->color_label()));

    stream.writeAttribute("autoscale", QString::number(c->autoscaled()));
    stream.writeAttribute("speed", QString::number(c->speed().value, 'f', 10));
    stream.writeAttribute("maintainpitch", QString::number(c->speed().maintain_audio_pitch));
    stream.writeAttribute("reverse", QString::number(c->reversed()));
    if (c->loop_mode() != kLoopNone) stream.writeAttribute("loop", QString::number(c->loop_mode()));

    save_clip_media_attributes(stream, c);

    if (c->media() == nullptr) {
      for (const auto& k : c->get_markers()) {
        save_marker(stream, k);
      }
    }

    stream.writeStartElement("linked");
    for (int k : c->linked) {
      stream.writeStartElement("link");
      stream.writeAttribute("id", QString::number(k));
      stream.writeEndElement();
    }
    stream.writeEndElement();

    save_clip_transitions(stream, c, j, transition_save_cache, transition_clip_save_cache);

    for (const auto& effect : c->effects) {
      stream.writeStartElement("effect");
      effect->save(stream);
      stream.writeEndElement();
    }

    stream.writeEndElement();  // clip
  }
}

static void save_footage_item(QXmlStreamWriter& stream, Media* m, int media_id_val, const QDir& proj_dir_ref) {
  Footage* f = m->to_footage();
  int folder = m->parentItem()->temp_id;

  stream.writeStartElement("footage");
  stream.writeAttribute("id", QString::number(media_id_val));
  stream.writeAttribute("folder", QString::number(folder));
  stream.writeAttribute("name", f->name);
  stream.writeAttribute("url", proj_dir_ref.relativeFilePath(QDir::cleanPath(f->url)));
  stream.writeAttribute("duration", QString::number(f->length));
  stream.writeAttribute("using_inout", QString::number(f->using_inout));
  stream.writeAttribute("in", QString::number(f->in));
  stream.writeAttribute("out", QString::number(f->out));
  stream.writeAttribute("speed", QString::number(f->speed));
  stream.writeAttribute("alphapremul", QString::number(f->alpha_is_premultiplied));
  stream.writeAttribute("startnumber", QString::number(f->start_number));
  stream.writeAttribute("proxy", QString::number(f->proxy));
  stream.writeAttribute("proxypath", f->proxy_path);
  if (m->color_label() > 0) {
    stream.writeAttribute("label", QString::number(m->color_label()));
  }

  for (const auto& ms : f->video_tracks) {
    stream.writeStartElement("video");
    stream.writeAttribute("id", QString::number(ms.file_index));
    stream.writeAttribute("width", QString::number(ms.video_width));
    stream.writeAttribute("height", QString::number(ms.video_height));
    stream.writeAttribute("framerate", QString::number(ms.video_frame_rate, 'f', 10));
    stream.writeAttribute("infinite", QString::number(ms.infinite_length));
    stream.writeEndElement();
  }

  for (const auto& ms : f->audio_tracks) {
    stream.writeStartElement("audio");
    stream.writeAttribute("id", QString::number(ms.file_index));
    stream.writeAttribute("channels", QString::number(ms.audio_channels));
    stream.writeAttribute("layout", QString::number(ms.audio_layout));
    stream.writeAttribute("frequency", QString::number(ms.audio_frequency));
    stream.writeEndElement();
  }

  for (const auto& marker : f->markers) {
    save_marker(stream, marker);
  }

  stream.writeEndElement();  // footage
}

static void save_sequence_item(QXmlStreamWriter& stream, Media* m, int folder) {
  Sequence* s = m->to_sequence().get();
  stream.writeStartElement("sequence");
  stream.writeAttribute("id", QString::number(s->save_id));
  stream.writeAttribute("folder", QString::number(folder));
  stream.writeAttribute("name", s->name);
  stream.writeAttribute("width", QString::number(s->width));
  stream.writeAttribute("height", QString::number(s->height));
  stream.writeAttribute("framerate", QString::number(s->frame_rate, 'f', 10));
  stream.writeAttribute("afreq", QString::number(s->audio_frequency));
  stream.writeAttribute("alayout", QString::number(s->audio_layout));
  if (s == amber::ActiveSequence.get()) stream.writeAttribute("open", "1");
  stream.writeAttribute("workarea", QString::number(s->using_workarea));
  stream.writeAttribute("workareaIn", QString::number(s->workarea_in));
  stream.writeAttribute("workareaOut", QString::number(s->workarea_out));
  if (m->color_label() > 0) {
    stream.writeAttribute("label", QString::number(m->color_label()));
  }

  for (const auto& guide : s->guides) {
    stream.writeStartElement("guide");
    stream.writeAttribute("orientation", QString::number(guide.orientation));
    stream.writeAttribute("position", QString::number(guide.position));
    if (guide.mirror) stream.writeAttribute("mirror", "1");
    stream.writeEndElement();
  }

  save_sequence_clips(stream, s);

  for (const auto& marker : s->markers) {
    save_marker(stream, marker);
  }

  // Track header toggles, only the non-default tracks (QMap order), only the true flags. 1.x skips the element.
  for (auto it = s->track_states.constBegin(); it != s->track_states.constEnd(); ++it) {
    const TrackState& t = it.value();
    stream.writeStartElement("track");
    stream.writeAttribute("index", QString::number(it.key()));
    if (t.hidden) stream.writeAttribute("hidden", "1");
    if (t.muted) stream.writeAttribute("muted", "1");
    if (t.solo) stream.writeAttribute("solo", "1");
    if (t.locked) stream.writeAttribute("locked", "1");
    stream.writeEndElement();
  }
  stream.writeEndElement();  // sequence
}

static void save_folder_item(SaveState& st, QXmlStreamWriter& stream, Media* m, const QModelIndex& item,
                             bool set_ids_only) {
  if (set_ids_only) {
    m->temp_id = st.folder_id++;
    return;
  }
  stream.writeStartElement("folder");
  stream.writeAttribute("name", m->get_name());
  stream.writeAttribute("id", QString::number(m->temp_id));
  if (!item.parent().isValid()) {
    stream.writeAttribute("parent", "0");
  } else {
    stream.writeAttribute("parent", QString::number(amber::project_model.getItem(item.parent())->temp_id));
  }
  if (m->color_label() > 0) {
    stream.writeAttribute("label", QString::number(m->color_label()));
  }
  stream.writeEndElement();
}

static void save_footage_or_sequence_item(SaveState& st, QXmlStreamWriter& stream, Media* m, int type,
                                          bool set_ids_only) {
  int folder = m->parentItem()->temp_id;
  if (type == MEDIA_TYPE_FOOTAGE) {
    m->to_footage()->save_id = st.media_id;
    save_footage_item(stream, m, st.media_id, st.proj_dir);
    st.media_id++;
  } else if (type == MEDIA_TYPE_SEQUENCE) {
    Sequence* s = m->to_sequence().get();
    if (set_ids_only) {
      s->save_id = st.sequence_id++;
    } else {
      save_sequence_item(stream, m, folder);
    }
  }
}

static void save_folder(SaveState& st, QXmlStreamWriter& stream, int type, bool set_ids_only,
                        const QModelIndex& parent = QModelIndex()) {
  for (int i = 0; i < amber::project_model.rowCount(parent); i++) {
    const QModelIndex& item = amber::project_model.index(i, 0, parent);
    Media* m = amber::project_model.getItem(item);

    if (type == m->get_type()) {
      if (m->get_type() == MEDIA_TYPE_FOLDER) {
        save_folder_item(st, stream, m, item, set_ids_only);
      } else {
        save_footage_or_sequence_item(st, stream, m, type, set_ids_only);
      }
    }

    if (m->get_type() == MEDIA_TYPE_FOLDER) {
      save_folder(st, stream, type, set_ids_only, item);
    }
  }
}

void amber::save_project_xml(QXmlStreamWriter& stream, const QString& project_file) {
  SaveState st;
  stream.setAutoFormatting(true);
  stream.writeStartDocument();  // doc

  stream.writeStartElement("project");  // project

  stream.writeTextElement("version", QString::number(amber::kSaveVersion));

  stream.writeTextElement("url", project_file);
  stream.writeTextElement("previewresolution", QString::number(amber::CurrentConfig.preview_resolution_divider));
  st.proj_dir = QFileInfo(project_file).absoluteDir();

  save_folder(st, stream, MEDIA_TYPE_FOLDER, true);

  stream.writeStartElement("folders");  // folders
  save_folder(st, stream, MEDIA_TYPE_FOLDER, false);
  stream.writeEndElement();  // folders

  stream.writeStartElement("media");  // media
  save_folder(st, stream, MEDIA_TYPE_FOOTAGE, false);
  stream.writeEndElement();  // media

  save_folder(st, stream, MEDIA_TYPE_SEQUENCE, true);

  stream.writeStartElement("sequences");  // sequences
  save_folder(st, stream, MEDIA_TYPE_SEQUENCE, false);
  stream.writeEndElement();  // sequences

  stream.writeEndElement();  // project

  stream.writeEndDocument();  // doc
}

bool amber::save_project_file(const QString& path, const QString& project_file) {
  // QSaveFile writes to a temp file and renames it over `path` on commit(): a failed save leaves the old file intact
  QSaveFile file(path);
  // A writable file in a read-only directory has no room for the temp file: write it in place there, as before
  file.setDirectWriteFallback(true);
  if (!file.open(QIODevice::WriteOnly)) return false;
  QXmlStreamWriter stream(&file);
  save_project_xml(stream, project_file);
  if (stream.hasError()) return false;  // no commit(): the destructor discards the temp file
  return file.commit();
}
