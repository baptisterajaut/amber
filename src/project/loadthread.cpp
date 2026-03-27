/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "loadthread.h"

#include "global/global.h"

#include "ui/mainwindow.h"

#include "panels/panels.h"

#include "project/projectelements.h"

#include "effects/effectloaders.h"
#include "effects/internal/voideffect.h"
#include "global/config.h"
#include "global/debug.h"
#include "project/previewgenerator.h"
#include "rendering/renderfunctions.h"

#include <QFile>
#include <QTreeWidgetItem>

LoadThread::LoadThread(const QString& filename, bool autorecovery)
    : filename_(filename),
      autorecovery_(autorecovery)

{
  connect(this, &QThread::finished, this, &QObject::deleteLater);

  connect(this, &LoadThread::success, this, &LoadThread::success_func, Qt::QueuedConnection);

  connect(this, &LoadThread::error, this, &LoadThread::error_func, Qt::QueuedConnection);

  connect(this, &LoadThread::start_question, this, &LoadThread::question_func, Qt::QueuedConnection);
}

void LoadThread::load_effect(QXmlStreamReader& stream, Clip* c) {
  if (!c) {
    qWarning() << "load_effect: c is null";
    return;
  }
  QString tag = stream.name().toString();

  // variables to store effect metadata in
  int effect_id = -1;
  QString effect_name;
  bool effect_enabled = true;
  long effect_length = -1;

  // loop through attributes for effect metadata
  for (int j = 0; j < stream.attributes().size(); j++) {
    const QXmlStreamAttribute& attr = stream.attributes().at(j);
    if (attr.name() == QLatin1String("id")) {
      effect_id = attr.value().toInt();
    } else if (attr.name() == QLatin1String("enabled")) {
      effect_enabled = (attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("name")) {
      effect_name = attr.value().toString();
    } else if (attr.name() == QLatin1String("length")) {
      effect_length = attr.value().toLong();
    } else if (attr.name() == QLatin1String("shared")) {
      // if a transition has this tag, it's sharing a transition with another clip so we don't have to do any processing

      Clip* sharing_clip = nullptr;

      // Find the clip with the ID referenced in the transition
      int clip_id = attr.value().toInt();
      for (const auto& clip : c->sequence->clips) {
        Clip* test_clip = clip.get();
        if (test_clip->load_id == clip_id) {
          sharing_clip = test_clip;
          break;
        }
      }

      if (sharing_clip == nullptr) {
        qWarning() << "Failed to link shared transition. Project may be corrupt.";
      } else if (tag == "opening") {
        c->opening_transition = (sharing_clip->closing_transition);

        // since this is the opened clip, switch secondaries and primaries
        c->opening_transition->secondary_clip = c->opening_transition->parent_clip;
        c->opening_transition->parent_clip = c;
        c->opening_transition->refresh();
      } else if (tag == "closing") {
        c->closing_transition = (sharing_clip->opening_transition);

        // since this is the closed clip, make this clip the secondary
        c->closing_transition->secondary_clip = c;
      }
      return;
    }
  }

  // Effect loading occurs in another thread, and while it's usually very quick, just for safety we wait here
  // for all the effects to finish loading
  effects_loaded_mutex.lock();

  const EffectMeta* meta = nullptr;

  // find effect with this name
  if (!effect_name.isEmpty()) {
    meta = get_meta_from_name(effect_name);
  }

  effects_loaded_mutex.unlock();

  int type;
  if (tag == "opening") {
    type = kTransitionOpening;
  } else if (tag == "closing") {
    type = kTransitionClosing;
  } else {
    type = kTransitionNone;
  }

  // effect construction
  if (cancelled_) return;
  if (type == kTransitionNone) {
    if (meta == nullptr) {
      // create void effect
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
  } else {
    TransitionPtr t = Transition::Create(c, nullptr, meta);
    if (effect_length > -1) t->set_length(effect_length);
    t->SetEnabled(effect_enabled);
    t->load(stream);

    t->moveToThread(QApplication::instance()->thread());

    if (type == kTransitionOpening) {
      c->opening_transition = t;
    } else {
      c->closing_transition = t;
    }
  }
}

void LoadThread::read_next(QXmlStreamReader& stream) {
  stream.readNext();
  update_current_element_count(stream);
}

void LoadThread::read_next_start_element(QXmlStreamReader& stream) {
  stream.readNextStartElement();
  update_current_element_count(stream);
}

void LoadThread::update_current_element_count(QXmlStreamReader& stream) {
  if (is_element(stream)) {
    current_element_count++;
    report_progress((current_element_count * 100) / total_element_count);
  }
}

void LoadThread::show_message(const QString& title, const QString& body, int buttons) {
  question_answered_ = false;
  emit start_question(title, body, buttons);
  while (!question_answered_) {
    waitCond.wait(&mutex);
  }
}

bool LoadThread::is_element(QXmlStreamReader& stream) {
  return stream.isStartElement() &&
         (stream.name() == QLatin1String("folder") || stream.name() == QLatin1String("footage") ||
          stream.name() == QLatin1String("sequence") || stream.name() == QLatin1String("clip") ||
          stream.name() == QLatin1String("effect"));
}

Marker LoadThread::parse_marker(QXmlStreamReader& stream) {
  Marker m;
  for (int j = 0; j < stream.attributes().size(); j++) {
    const QXmlStreamAttribute& attr = stream.attributes().at(j);
    if (attr.name() == QLatin1String("frame")) {
      m.frame = attr.value().toLong();
    } else if (attr.name() == QLatin1String("name")) {
      m.name = attr.value().toString();
    } else if (attr.name() == QLatin1String("label")) {
      m.color_label = attr.value().toInt();
    }
  }
  return m;
}

Guide LoadThread::parse_guide(QXmlStreamReader& stream) {
  Guide g;
  for (int j = 0; j < stream.attributes().size(); j++) {
    const QXmlStreamAttribute& attr = stream.attributes().at(j);
    if (attr.name() == QLatin1String("orientation")) {
      g.orientation = static_cast<Guide::Orientation>(attr.value().toInt());
    } else if (attr.name() == QLatin1String("position")) {
      g.position = attr.value().toInt();
    } else if (attr.name() == QLatin1String("mirror")) {
      g.mirror = attr.value().toInt() != 0;
    }
  }
  return g;
}

QString LoadThread::resolve_footage_url(const QString& raw_url) {
  if (QFileInfo::exists(raw_url)) {
    qInfo() << "Matched" << raw_url << "with absolute path";
    return QFileInfo(raw_url).absoluteFilePath();
  }

  // Resolve relative paths against the project's current folder, normalizing ".." sequences
  // so symlinks in the project path don't cause the resolved path to land in the wrong place.
  QString proj_dir_test = QDir::cleanPath(proj_dir.absoluteFilePath(raw_url));

  // Same resolution against the folder the project was originally saved in
  // (unaffected by moving the project file)
  QString internal_proj_dir_test = QDir::cleanPath(internal_proj_dir.absoluteFilePath(raw_url));

  // tries to locate file using the file name directly in the project's current folder
  QString proj_dir_direct_test = proj_dir.filePath(QFileInfo(raw_url).fileName());

  if (QFileInfo::exists(proj_dir_test)) {
    qInfo() << "Matched" << raw_url << "relative to project's current directory";
    return proj_dir_test;
  } else if (QFileInfo::exists(internal_proj_dir_test)) {
    qInfo() << "Matched" << raw_url << "relative to project's internal directory";
    return internal_proj_dir_test;
  } else if (QFileInfo::exists(proj_dir_direct_test)) {
    qInfo() << "Matched" << raw_url << "directly to project's current directory";
    return proj_dir_direct_test;
  } else if (raw_url.contains('%')) {
    // hack for image sequences (qt won't be able to find the URL with %, but ffmpeg may)
    qInfo() << "Guess image sequence" << raw_url << "path to project's internal directory";
    return internal_proj_dir_test;
  }

  qInfo() << "Failed to match" << raw_url << "to file (tried" << proj_dir_test << "and" << internal_proj_dir_test << ")";
  // Return the cleaned absolute path even though the file doesn't exist — this keeps f->url
  // as an absolute path so subsequent saves produce a correct relative path instead of
  // double-encoding the relative URL and corrupting it further with each save-load cycle.
  return proj_dir_test;
}

void LoadThread::parse_clip_links(QXmlStreamReader& stream, Clip* c) {
  if (!c) {
    qWarning() << "parse_clip_links: c is null";
    return;
  }
  while (!cancelled_ && !(stream.name() == QLatin1String("linked") && stream.isEndElement()) && !stream.atEnd()) {
    read_next(stream);
    if (stream.name() == QLatin1String("link") && stream.isStartElement()) {
      for (int k = 0; k < stream.attributes().size(); k++) {
        const QXmlStreamAttribute& link_attr = stream.attributes().at(k);
        if (link_attr.name() == QLatin1String("id")) {
          c->linked.append(link_attr.value().toInt());
          break;
        }
      }
    }
  }
}

void LoadThread::parse_folder(QXmlStreamReader& stream) {
  MediaPtr folder = panel_project->create_folder_internal(nullptr);
  folder->temp_id2 = 0;
  for (int j = 0; j < stream.attributes().size(); j++) {
    const QXmlStreamAttribute& attr = stream.attributes().at(j);
    if (attr.name() == QLatin1String("id")) {
      folder->temp_id = attr.value().toInt();
    } else if (attr.name() == QLatin1String("name")) {
      folder->set_name(attr.value().toString());
    } else if (attr.name() == QLatin1String("parent")) {
      folder->temp_id2 = attr.value().toInt();
    }
  }
  loaded_folders.append(folder);
}

void LoadThread::parse_footage(QXmlStreamReader& stream, const QStringView& child_search) {
  int folder = 0;

  MediaPtr item = std::make_shared<Media>();
  FootagePtr f = std::make_shared<Footage>();

  f->using_inout = false;

  for (int j = 0; j < stream.attributes().size(); j++) {
    const QXmlStreamAttribute& attr = stream.attributes().at(j);
    if (attr.name() == QLatin1String("id")) {
      f->save_id = attr.value().toInt();
    } else if (attr.name() == QLatin1String("folder")) {
      folder = attr.value().toInt();
    } else if (attr.name() == QLatin1String("name")) {
      f->name = attr.value().toString();
    } else if (attr.name() == QLatin1String("url")) {
      f->url = resolve_footage_url(attr.value().toString());
    } else if (attr.name() == QLatin1String("duration")) {
      f->length = attr.value().toLongLong();
    } else if (attr.name() == QLatin1String("using_inout")) {
      f->using_inout = (attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("in")) {
      f->in = attr.value().toLong();
    } else if (attr.name() == QLatin1String("out")) {
      f->out = attr.value().toLong();
    } else if (attr.name() == QLatin1String("speed")) {
      f->speed = attr.value().toDouble();
    } else if (attr.name() == QLatin1String("alphapremul")) {
      f->alpha_is_premultiplied = (attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("proxy")) {
      f->proxy = (attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("proxypath")) {
      f->proxy_path = attr.value().toString();
    } else if (attr.name() == QLatin1String("startnumber")) {
      f->start_number = attr.value().toInt();
    }
  }

  while (!cancelled_ && !(stream.name() == child_search && stream.isEndElement()) && !stream.atEnd()) {
    read_next_start_element(stream);
    if (stream.name() == QLatin1String("marker") && stream.isStartElement()) {
      f->markers.append(parse_marker(stream));
    }
  }

  item->set_footage(f);

  amber::project_model.appendChild(find_loaded_folder_by_id(folder), item);

  // analyze media to see if it's the same
  loaded_media_items.append(item.get());
}

void LoadThread::parse_clip_attributes(QXmlStreamReader& stream, ClipPtr c, int& media_type, int& media_id,
                                       int& stream_id) {
  if (!c) {
    qWarning() << "parse_clip_attributes: c is null";
    return;
  }
  QColor clip_color;
  ClipSpeed speed_info = c->speed();

  for (int j = 0; j < stream.attributes().size(); j++) {
    const QXmlStreamAttribute& attr = stream.attributes().at(j);
    if (attr.name() == QLatin1String("name")) {
      c->set_name(attr.value().toString());
    } else if (attr.name() == QLatin1String("enabled")) {
      c->set_enabled(attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("id")) {
      c->load_id = attr.value().toInt();
    } else if (attr.name() == QLatin1String("clipin")) {
      c->set_clip_in(attr.value().toLong());
    } else if (attr.name() == QLatin1String("in")) {
      c->set_timeline_in(attr.value().toLong());
    } else if (attr.name() == QLatin1String("out")) {
      c->set_timeline_out(attr.value().toLong());
    } else if (attr.name() == QLatin1String("track")) {
      c->set_track(attr.value().toInt());
    } else if (attr.name() == QLatin1String("r")) {
      clip_color.setRed(attr.value().toInt());
    } else if (attr.name() == QLatin1String("g")) {
      clip_color.setGreen(attr.value().toInt());
    } else if (attr.name() == QLatin1String("b")) {
      clip_color.setBlue(attr.value().toInt());
    } else if (attr.name() == QLatin1String("autoscale")) {
      c->set_autoscaled(attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("media")) {
      media_type = MEDIA_TYPE_FOOTAGE;
      media_id = attr.value().toInt();
    } else if (attr.name() == QLatin1String("stream")) {
      stream_id = attr.value().toInt();
    } else if (attr.name() == QLatin1String("speed")) {
      speed_info.value = attr.value().toDouble();
    } else if (attr.name() == QLatin1String("maintainpitch")) {
      speed_info.maintain_audio_pitch = (attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("reverse")) {
      c->set_reversed(attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("loop")) {
      c->set_loop_mode(attr.value().toInt());
    } else if (attr.name() == QLatin1String("label")) {
      c->set_color_label(attr.value().toInt());
    } else if (attr.name() == QLatin1String("sequence")) {
      media_type = MEDIA_TYPE_SEQUENCE;

      // since we haven't finished loading sequences, we defer linking this until later
      c->set_media(nullptr, attr.value().toInt());
      loaded_clips.append(c);
    }
  }

  c->set_color(clip_color);
  c->set_speed(speed_info);
}

bool LoadThread::parse_clip(QXmlStreamReader& stream, SequencePtr s) {
  int media_type = -1;
  int media_id = -1;
  int stream_id = -1;

  ClipPtr c = std::make_shared<Clip>(s.get());

  parse_clip_attributes(stream, c, media_type, media_id, stream_id);

  // set media and media stream
  switch (media_type) {
    case MEDIA_TYPE_FOOTAGE:
      if (media_id >= 0 && stream_id >= 0) {
        for (auto loaded_media_item : loaded_media_items) {
          Footage* m = loaded_media_item->to_footage();
          if (m->save_id == media_id) {
            c->set_media(loaded_media_item, stream_id);
            break;
          }
        }
      }
      break;
  }

  // load links and effects
  while (!cancelled_ && !(stream.name() == QLatin1String("clip") && stream.isEndElement()) && !stream.atEnd()) {
    read_next(stream);
    if (stream.isStartElement()) {
      if (stream.name() == QLatin1String("linked")) {
        parse_clip_links(stream, c.get());
        if (cancelled_) return false;
      } else if (stream.isStartElement() && (stream.name() == QLatin1String("effect") ||
                                             stream.name() == QLatin1String("opening") ||
                                             stream.name() == QLatin1String("closing"))) {
        load_effect(stream, c.get());
      } else if (stream.name() == QLatin1String("marker") && stream.isStartElement()) {
        c->get_markers().append(parse_marker(stream));
      }
    }
  }
  if (cancelled_) return false;

  s->clips.append(c);
  return true;
}

void LoadThread::parse_sequence_attributes(QXmlStreamReader& stream, SequencePtr s, Media*& parent) {
  for (int j = 0; j < stream.attributes().size(); j++) {
    const QXmlStreamAttribute& attr = stream.attributes().at(j);
    if (attr.name() == QLatin1String("name")) {
      s->name = attr.value().toString();
    } else if (attr.name() == QLatin1String("folder")) {
      int folder = attr.value().toInt();
      if (folder > 0) parent = find_loaded_folder_by_id(folder);
    } else if (attr.name() == QLatin1String("id")) {
      s->save_id = attr.value().toInt();
    } else if (attr.name() == QLatin1String("width")) {
      s->width = attr.value().toInt();
    } else if (attr.name() == QLatin1String("height")) {
      s->height = attr.value().toInt();
    } else if (attr.name() == QLatin1String("framerate")) {
      s->frame_rate = attr.value().toDouble();
    } else if (attr.name() == QLatin1String("afreq")) {
      s->audio_frequency = attr.value().toInt();
    } else if (attr.name() == QLatin1String("alayout")) {
      s->audio_layout = attr.value().toInt();
    } else if (attr.name() == QLatin1String("open")) {
      open_seq = s;
    } else if (attr.name() == QLatin1String("workarea")) {
      s->using_workarea = (attr.value() == QLatin1String("1"));
    } else if (attr.name() == QLatin1String("workareaIn")) {
      s->workarea_in = attr.value().toLong();
    } else if (attr.name() == QLatin1String("workareaOut")) {
      s->workarea_out = attr.value().toLong();
    }
  }

  // Validate sequence dimensions — prevent division-by-zero cascades
  if (s->width <= 0) s->width = 1920;
  if (s->height <= 0) s->height = 1080;
  if (s->frame_rate <= 0) s->frame_rate = 29.97;
  if (s->audio_frequency <= 0) s->audio_frequency = 48000;
}

bool LoadThread::correct_clip_links(SequencePtr s) {
  if (!s) {
    qWarning() << "correct_clip_links: s is null";
    return false;
  }
  for (int i = 0; i < s->clips.size(); i++) {
    Clip* correct_clip = s->clips.at(i).get();
    for (int j = 0; j < correct_clip->linked.size(); j++) {
      bool found = false;
      for (int k = 0; k < s->clips.size(); k++) {
        if (s->clips.at(k)->load_id == correct_clip->linked.at(j)) {
          correct_clip->linked[j] = k;
          found = true;
          break;
        }
      }
      if (!found) {
        correct_clip->linked.removeAt(j);
        j--;

        show_message(tr("Invalid Clip Link"),
                     tr("This project contains an invalid clip link. It may be corrupt. Would you like "
                        "to continue loading it?"),
                     QMessageBox::Yes | QMessageBox::No);

        if (question_btn == QMessageBox::No) {
          s.reset();
          return false;
        }
      }
    }
  }
  return true;
}

bool LoadThread::parse_sequence(QXmlStreamReader& stream, const QStringView& child_search) {
  Media* parent = nullptr;
  SequencePtr s = std::make_shared<Sequence>();

  parse_sequence_attributes(stream, s, parent);

  // load all clips, markers, and guides
  while (!cancelled_ && !(stream.name() == child_search && stream.isEndElement()) && !stream.atEnd()) {
    read_next_start_element(stream);
    if (stream.name() == QLatin1String("marker") && stream.isStartElement()) {
      s->markers.append(parse_marker(stream));
    } else if (stream.name() == QLatin1String("guide") && stream.isStartElement()) {
      s->guides.append(parse_guide(stream));
    } else if (stream.name() == QLatin1String("clip") && stream.isStartElement()) {
      if (!parse_clip(stream, s)) return false;
    }
  }
  if (cancelled_) return false;

  if (!correct_clip_links(s)) return false;

  MediaPtr m = panel_project->create_sequence_internal(nullptr, s, false, parent);

  loaded_sequences.append(m.get());
  return true;
}

bool LoadThread::load_worker(QFile& f, QXmlStreamReader& stream, int type) {
  f.seek(0);
  stream.setDevice(stream.device());

  QString root_search;
  QString child_search;

  switch (type) {
    case LOAD_TYPE_VERSION:
      root_search = "version";
      break;
    case LOAD_TYPE_URL:
      root_search = "url";
      break;
    case MEDIA_TYPE_FOLDER:
      root_search = "folders";
      child_search = "folder";
      break;
    case MEDIA_TYPE_FOOTAGE:
      root_search = "media";
      child_search = "footage";
      break;
    case MEDIA_TYPE_SEQUENCE:
      root_search = "sequences";
      child_search = "sequence";
      break;
  }

  show_err = true;

  int proj_version = amber::kSaveVersion;

  while (!stream.atEnd() && !cancelled_) {
    read_next_start_element(stream);
    if (stream.name() == root_search) {
      if (type == LOAD_TYPE_VERSION) {
        proj_version = stream.readElementText().toInt();
        if (proj_version < amber::kMinimumSaveVersion || proj_version > amber::kSaveVersion) {
          show_message(tr("Version Mismatch"),
                       tr("This project was saved in a different version of Amber and may not be fully compatible with "
                          "this version. Would you like to attempt loading it anyway?"),
                       QMessageBox::Yes | QMessageBox::No);
          if (question_btn == QMessageBox::No) {
            show_err = false;
            return false;
          }
        }
      } else if (type == LOAD_TYPE_URL) {
        internal_proj_url = stream.readElementText();
        internal_proj_dir = QFileInfo(internal_proj_url).absoluteDir();
      } else {
        while (!cancelled_ && !stream.atEnd() && !(stream.name() == root_search && stream.isEndElement())) {
          read_next(stream);
          if (stream.name() == child_search && stream.isStartElement()) {
            switch (type) {
              case MEDIA_TYPE_FOLDER:
                parse_folder(stream);
                break;
              case MEDIA_TYPE_FOOTAGE:
                parse_footage(stream, child_search);
                break;
              case MEDIA_TYPE_SEQUENCE:
                if (!parse_sequence(stream, child_search)) return false;
                break;
            }
          }
        }
        if (cancelled_) return false;
      }
      break;
    }
  }
  return !cancelled_;
}

Media* LoadThread::find_loaded_folder_by_id(int id) {
  if (id == 0) return nullptr;
  for (const auto& loaded_folder : loaded_folders) {
    Media* parent_item = loaded_folder.get();
    if (parent_item->temp_id == id) {
      return parent_item;
    }
  }
  return nullptr;
}

void LoadThread::OrganizeFolders(int folder) {
  for (auto item : loaded_folders) {
    int parent_id = item->temp_id2;

    if (parent_id == folder) {
      amber::project_model.appendChild(find_loaded_folder_by_id(parent_id), item);

      OrganizeFolders(item->temp_id);
    }
  }
}

void LoadThread::run() {
  mutex.lock();

  QFile file(filename_);
  if (!file.open(QIODevice::ReadOnly)) {
    qCritical() << "Could not open file";
    return;
  }

  /* set up directories to search for media
   * most of the time, these will be the same but in
   * case the project file has moved without the footage,
   * we check both
   */
  proj_dir = QFileInfo(filename_).absoluteDir();
  internal_proj_dir = QFileInfo(filename_).absoluteDir();
  internal_proj_url = filename_;

  QXmlStreamReader stream(&file);

  bool cont = false;
  error_str.clear();
  show_err = true;

  open_seq = nullptr;

  // get "element" count
  current_element_count = 0;
  total_element_count = 0;
  while (!cancelled_ && !stream.atEnd()) {
    stream.readNextStartElement();
    if (is_element(stream)) {
      total_element_count++;
    }
  }
  cont = !cancelled_;

  // find project file version
  if (cont) {
    cont = load_worker(file, stream, LOAD_TYPE_VERSION);
  }

  // find project's internal URL
  if (cont) {
    cont = load_worker(file, stream, LOAD_TYPE_URL);
  }

  // load folders first
  if (cont) {
    cont = load_worker(file, stream, MEDIA_TYPE_FOLDER);
  }

  if (cont) {
    // since folders loaded correctly, organize them appropriately
    OrganizeFolders();

    // load media
    cont = load_worker(file, stream, MEDIA_TYPE_FOOTAGE);
  }

  // load sequences
  if (cont) {
    cont = load_worker(file, stream, MEDIA_TYPE_SEQUENCE);
  }

  if (!cancelled_) {
    if (!cont) {
      xml_error = false;
      if (show_err) emit error();
    } else if (stream.hasError()) {
      error_str =
          tr("%1 - Line: %2 Col: %3")
              .arg(stream.errorString(), QString::number(stream.lineNumber()), QString::number(stream.columnNumber()));
      xml_error = true;
      emit error();
      cont = false;
    } else {
      // attach nested sequence clips to their sequences
      for (const auto& loaded_clip : loaded_clips) {
        for (auto loaded_sequence : loaded_sequences) {
          if (loaded_clip->media() == nullptr &&
              loaded_clip->media_stream_index() == loaded_sequence->to_sequence()->save_id) {
            loaded_clip->set_media(loaded_sequence, loaded_clip->media_stream_index());
            loaded_clip->refresh();
            break;
          }
        }
      }
    }
  }

  if (cont) {
    // Collect invalid footage (files that couldn't be resolved) before analysis
    QVector<QPair<Media*, Footage*>> invalid_footage;
    for (auto loaded_media_item : loaded_media_items) {
      Footage* f = loaded_media_item->to_footage();
      if (!QFileInfo::exists(f->url)) {
        f->invalid = true;
        invalid_footage.append(qMakePair(loaded_media_item, f));
      }
    }

    emit success();  // run in main thread

    if (!invalid_footage.isEmpty()) {
      emit found_invalid_footage(invalid_footage);
    }

    for (auto loaded_media_item : loaded_media_items) {
      PreviewGenerator::AnalyzeMedia(loaded_media_item);
    }
  } else {
    if (error_str.isEmpty()) {
      error_str = tr("User aborted loading");
    }

    emit error();
  }

  file.close();

  mutex.unlock();
}

void LoadThread::cancel() {
  cancelled_ = true;
  mutex.lock();
  question_answered_ = true;
  waitCond.wakeAll();
  mutex.unlock();
}

void LoadThread::question_func(const QString& title, const QString& text, int buttons) {
  mutex.lock();
  question_btn =
      QMessageBox::warning(amber::MainWindow, title, text, static_cast<enum QMessageBox::StandardButton>(buttons));
  question_answered_ = true;
  waitCond.wakeAll();
  mutex.unlock();
}

void LoadThread::error_func() {
  if (xml_error) {
    qCritical() << "Error parsing XML." << error_str;
    QMessageBox::critical(amber::MainWindow, tr("XML Parsing Error"),
                          tr("Couldn't load '%1'. %2").arg(filename_, error_str), QMessageBox::Ok);
  } else {
    QMessageBox::critical(amber::MainWindow, tr("Project Load Error"), tr("Error loading project: %1").arg(error_str),
                          QMessageBox::Ok);
  }
}

void LoadThread::success_func() {
  if (autorecovery_) {
    QString orig_filename = internal_proj_url;
    int insert_index = internal_proj_url.lastIndexOf(".ove", -1, Qt::CaseInsensitive);
    if (insert_index == -1) insert_index = internal_proj_url.length();
    int counter = 1;
    while (QFileInfo::exists(orig_filename)) {
      orig_filename = internal_proj_url;
      QString recover_text = "recovered";
      if (counter > 1) {
        recover_text += " " + QString::number(counter);
      }
      orig_filename.insert(insert_index, " (" + recover_text + ")");
      counter++;
    }

    amber::Global->update_project_filename(orig_filename);
  } else {
    panel_project->add_recent_project(filename_);
  }

  amber::Global->set_modified(autorecovery_);
  if (open_seq != nullptr) {
    amber::Global->set_sequence(open_seq);
  }
}
