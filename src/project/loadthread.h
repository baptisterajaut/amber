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

#ifndef LOADTHREAD_H
#define LOADTHREAD_H

#include <QDir>
#include <QMessageBox>
#include <QMutex>
#include <QPair>
#include <QThread>
#include <QWaitCondition>
#include <QXmlStreamReader>

#include "core/guide.h"
#include "engine/clip.h"
#include "project/projectelements.h"

class LoadThread : public QThread {
  Q_OBJECT
 public:
  LoadThread(const QString& filename, bool autorecovery);
  void run() override;
 public slots:
  void cancel();
 signals:
  void start_question(const QString& title, const QString& text, int buttons);
  void success();
  void error();
  void report_progress(int p);
  void found_invalid_footage(QVector<QPair<Media*, Footage*>> invalid);
 private slots:
  void question_func(const QString& title, const QString& text, int buttons);
  void error_func();
  void success_func();

 private:
  bool autorecovery_;
  QString filename_;

  bool load_worker(QFile& f, QXmlStreamReader& stream, int type);
  bool handle_scalar_load_type(QXmlStreamReader& stream, int type);
  bool parse_collection_child(QXmlStreamReader& stream, int type, const QString& child_search);
  void link_nested_sequence_clips();
  void finalize_loaded_media();
  void load_effect(QXmlStreamReader& stream, Clip* c);

  // Per-element parse helpers (called from load_worker)
  void parse_folder(QXmlStreamReader& stream);
  void parse_footage(QXmlStreamReader& stream, const QStringView& child_search);
  bool parse_sequence(QXmlStreamReader& stream, const QStringView& child_search);
  bool parse_clip(QXmlStreamReader& stream, SequencePtr s);
  Marker parse_marker(QXmlStreamReader& stream);
  Guide parse_guide(QXmlStreamReader& stream);
  QString resolve_footage_url(const QString& raw_url);
  void parse_clip_links(QXmlStreamReader& stream, Clip* c);
  void parse_clip_attributes(QXmlStreamReader& stream, ClipPtr c, int& media_type, int& media_id, int& stream_id);
  void parse_sequence_attributes(QXmlStreamReader& stream, SequencePtr s, Media*& parent);
  bool correct_clip_links(SequencePtr s);

  void read_next(QXmlStreamReader& stream);
  void read_next_start_element(QXmlStreamReader& stream);
  void update_current_element_count(QXmlStreamReader& stream);

  void show_message(const QString& title, const QString& body, int buttons);

  SequencePtr open_seq;
  QVector<Media*> loaded_media_items;
  QDir proj_dir;
  QDir internal_proj_dir;
  QString internal_proj_url;
  bool show_err;
  QString error_str;

  bool is_element(QXmlStreamReader& stream);

  QVector<MediaPtr> loaded_folders;
  QVector<ClipPtr> loaded_clips;
  QVector<Media*> loaded_sequences;
  Media* find_loaded_folder_by_id(int id);
  void OrganizeFolders(int folder = 0);

  int current_element_count;
  int total_element_count;

  QMutex mutex;
  QWaitCondition waitCond;

  bool cancelled_{false};
  bool question_answered_{false};
  bool xml_error;

  QMessageBox::StandardButton question_btn;
};

#endif  // LOADTHREAD_H
