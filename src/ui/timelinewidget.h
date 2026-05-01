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

#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QTimer>
#include <QWidget>
#include <QScrollBar>
#include <QPainter>
#include <QApplication>
#include <QScreen>

#include "engine/sequence.h"
#include "engine/clip.h"
#include "project/footage.h"
#include "project/media.h"
#include "engine/undo/undo.h"
#include "timelinetools.h"

class Timeline;

struct TimelineTrackHeight {
  int index;
  int height;
};

bool same_sign(int a, int b);
void draw_waveform(ClipPtr clip, const FootageStream *ms, long media_length, QPainter* p, const QRect& clip_rect, int waveform_start, int waveform_limit, double zoom);

class TimelineWidget : public QWidget {
  Q_OBJECT
public:
  explicit TimelineWidget(QWidget *parent = nullptr);
  QScrollBar* scrollBar;

public slots:

protected:
  void paintEvent(QPaintEvent*) override;

  void resizeEvent(QResizeEvent *event) override;

  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;

  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent* event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;

  void wheelEvent(QWheelEvent *event) override;
private:
  void init_ghosts();
  void update_ghosts(const QPoint& mouse_pos, bool lock_frame);
  bool is_track_visible(int track);
  int getTrackFromScreenPoint(int y);
  int getScreenPointFromTrack(int track);
  int getClipIndexFromCoords(long frame, int track);

  void VerifyTransitionHelper();

  // mouseMoveEvent per-action handlers
  void mouseMoveSelecting(bool alt);
  void mouseMoveHandMoving(QMouseEvent* event);
  void mouseMoveMovingInit(QMouseEvent* event);
  void mouseMoveSplitting(bool alt);
  void mouseMoveRectSelect(QMouseEvent* event, bool alt);
  void mouseMoveHoverTrimDetection(QMouseEvent* event);
  void mouseMoveHoverTransition(QMouseEvent* event);

  // update_ghosts sub-handlers
  void updateGhostsSnap(int effective_tool, long& frame_diff);
  void updateGhostsValidate(int effective_tool, bool clips_are_movable, long& frame_diff);
  void updateGhostsApply(int effective_tool, bool clips_are_movable, long frame_diff,
                          int track_diff, long& earliest_in_point);
  void updateGhostsApplySelections(int effective_tool, bool clips_are_movable, long frame_diff, int track_diff);
  void updateGhostsTooltip(const QPoint& mouse_pos, long frame_diff, long earliest_in_point);

  // paintEvent sub-handlers
  void drawClips(QPainter& p);
  void drawRecordingClip(QPainter& p);
  void drawTrackLines(QPainter& p, int video_track_limit, int audio_track_limit);
  void drawSelections(QPainter& p);
  void drawGhosts(QPainter& p);
  void drawSplittingCursor(QPainter& p);
  void drawPlayhead(QPainter& p);
  void drawEditCursor(QPainter& p);

  // mousePressEvent sub-handlers
  void mousePressCreating();
  void mousePressPointer(int hovered_clip, bool shift, bool alt, int effective_tool);

  // mouseReleaseEvent sub-handlers
  bool mouseReleaseCreating(ComboAction* ca, bool shift);
  bool mouseReleaseMoving(ComboAction* ca, bool alt, bool ctrl);
  bool mouseReleaseTransition(ComboAction* ca);
  bool mouseReleaseSplitting(ComboAction* ca, bool alt);

  bool track_resizing;
  int track_target;

  QVector<ClipPtr> pre_clips;
  QVector<ClipPtr> post_clips;

  Media* rc_reveal_media;

  SequencePtr self_created_sequence;

  QTimer tooltip_timer;
  int tooltip_clip;

  int scroll;

  SetSelectionsCommand* selection_command;
signals:

public slots:
  void setScroll(int);

private slots:
  void reveal_media();
  void show_context_menu(const QPoint& pos);
  void toggle_autoscale();
  void tooltip_timer_timeout();
  void open_sequence_properties();
  void show_clip_properties();
};

#endif // TIMELINEWIDGET_H
