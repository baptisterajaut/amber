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

#include "timelinewidget.h"

#include <QPainter>

#include "global/config.h"
#include "panels/panels.h"
#include "rendering/renderfunctions.h"
#include "timeline/marker.h"
#include "ui/rectangleselect.h"
#include "ui/viewerwidget.h"

#define MAX_TEXT_WIDTH 20
#define TRANSITION_BETWEEN_RANGE 40

bool current_tool_shows_cursor();

// Returns the waveform amplitude range [min, max] (as qint8 scaled values) for a
// pixel column spanning [offset_range_min, offset_range_max] in the preview buffer.
static void waveform_column_range(const QVector<char>& preview, int offset_range_min, int offset_range_max,
                                  int preview_size, int channel_height, qint8& out_min, qint8& out_max) {
  out_min = qint8(qRound(double(preview.at(offset_range_min)) / 128.0 * (channel_height / 2)));
  out_max = qint8(qRound(double(preview.at(offset_range_min + 1)) / 128.0 * (channel_height / 2)));
  for (int k = offset_range_min + 2; k <= offset_range_max; k += 2) {
    if (k + 1 >= preview_size) break;
    out_min = qMin(out_min, qint8(qRound(double(preview.at(k)) / 128.0 * (channel_height / 2))));
    out_max = qMax(out_max, qint8(qRound(double(preview.at(k + 1)) / 128.0 * (channel_height / 2))));
  }
}

static void draw_waveform_channels(const FootageStream* ms, const QRect& clip_rect, int channel_height,
                                   int preview_size, int last_waveform_index, int waveform_index, int i, QPainter* p) {
  for (int j = 0; j < ms->audio_channels; j++) {
    int mid = (amber::CurrentConfig.rectified_waveforms) ? clip_rect.top() + channel_height * (j + 1)
                                                         : clip_rect.top() + channel_height * j + (channel_height / 2);

    int offset_range_start = last_waveform_index + (j * 2);
    int offset_range_end = waveform_index + (j * 2);
    int offset_range_min = qMin(offset_range_start, offset_range_end);
    int offset_range_max = qMax(offset_range_start, offset_range_end);

    if (offset_range_min < 0 || offset_range_min + 1 >= preview_size) continue;
    if (offset_range_max + 1 >= preview_size) continue;

    qint8 min_val, max_val;
    waveform_column_range(ms->audio_preview, offset_range_min, offset_range_max, preview_size, channel_height, min_val,
                          max_val);

    if (amber::CurrentConfig.rectified_waveforms) {
      p->drawLine(clip_rect.left() + i, mid, clip_rect.left() + i, mid - (max_val - min_val));
    } else {
      p->drawLine(clip_rect.left() + i, mid + min_val, clip_rect.left() + i, mid + max_val);
    }
  }
}

void draw_waveform(ClipPtr clip, const FootageStream* ms, long media_length, QPainter* p, const QRect& clip_rect,
                   int waveform_start, int waveform_limit, double zoom) {
  if (!ms) {
    qWarning() << "draw_waveform: ms is null";
    return;
  }
  if (!p) {
    qWarning() << "draw_waveform: p is null";
    return;
  }
  if (ms->audio_channels <= 0 || ms->audio_preview.size() < 2) return;

  // audio channels multiplied by the number of bytes in a 16-bit audio sample
  int divider = ms->audio_channels * 2;
  int channel_height = clip_rect.height() / ms->audio_channels;
  int last_waveform_index = -1;
  int preview_size = ms->audio_preview.size();

  for (int i = waveform_start; i < waveform_limit; i++) {
    int waveform_index =
        qFloor((((clip->clip_in() + (double(i) / zoom)) / media_length) * preview_size) / divider) * divider;

    if (clip->reversed()) {
      waveform_index = preview_size - waveform_index - (ms->audio_channels * 2);
    }

    if (waveform_index < 0 || waveform_index >= preview_size) continue;
    if (last_waveform_index < 0) last_waveform_index = waveform_index;

    draw_waveform_channels(ms, clip_rect, channel_height, preview_size, last_waveform_index, waveform_index, i, p);
    last_waveform_index = waveform_index;
  }
}

void draw_transition(QPainter& p, ClipPtr c, const QRect& clip_rect, QRect& text_rect, int transition_type) {
  if (!c) {
    qWarning() << "draw_transition: c is null";
    return;
  }
  TransitionPtr t = (transition_type == kTransitionOpening) ? c->opening_transition : c->closing_transition;
  if (t != nullptr) {
    QColor transition_color(255, 0, 0, 16);
    int transition_width = getScreenPointFromFrame(panel_timeline->zoom, t->get_true_length());
    int transition_height = clip_rect.height();
    int tr_y = clip_rect.y();
    int tr_x = 0;
    if (transition_type == kTransitionOpening) {
      tr_x = clip_rect.x();
      text_rect.setX(text_rect.x() + transition_width);
    } else {
      tr_x = clip_rect.right() - transition_width;
      text_rect.setWidth(text_rect.width() - transition_width);
    }
    QRect transition_rect = QRect(tr_x, tr_y, transition_width, transition_height);
    p.fillRect(transition_rect, transition_color);
    QRect transition_text_rect(transition_rect.x() + amber::timeline::kClipTextPadding,
                               transition_rect.y() + amber::timeline::kClipTextPadding,
                               transition_rect.width() - amber::timeline::kClipTextPadding,
                               transition_rect.height() - amber::timeline::kClipTextPadding);
    if (transition_text_rect.width() > MAX_TEXT_WIDTH) {
      bool draw_text = true;

      p.setPen(QColor(0, 0, 0, 96));
      if (t->secondary_clip == nullptr) {
        if (transition_type == kTransitionOpening) {
          p.drawLine(transition_rect.bottomLeft(), transition_rect.topRight());
        } else {
          p.drawLine(transition_rect.topLeft(), transition_rect.bottomRight());
        }
      } else {
        if (transition_type == kTransitionOpening) {
          p.drawLine(QPoint(transition_rect.left(), transition_rect.center().y()), transition_rect.topRight());
          p.drawLine(QPoint(transition_rect.left(), transition_rect.center().y()), transition_rect.bottomRight());
          draw_text = false;
        } else {
          p.drawLine(QPoint(transition_rect.right(), transition_rect.center().y()), transition_rect.topLeft());
          p.drawLine(QPoint(transition_rect.right(), transition_rect.center().y()), transition_rect.bottomLeft());
        }
      }

      if (draw_text) {
        p.setPen(Qt::white);
        p.drawText(transition_text_rect, 0, t->meta->name, &transition_text_rect);
      }
    }
    p.setPen(Qt::black);
    p.drawRect(transition_rect);
  }
}

// Clamps clip_rect to widget bounds and fills the clip background.
static void drawClipBackground(QPainter& p, ClipPtr clip, const QRect& clip_rect, int widget_width, int widget_height) {
  QRect actual = clip_rect;
  if (actual.x() < 0) actual.setX(0);
  if (actual.right() > widget_width) actual.setRight(widget_width);
  if (actual.y() < 0) actual.setY(0);
  if (actual.bottom() > widget_height) actual.setBottom(widget_height);
  p.fillRect(actual, clip->enabled() ? clip->display_color() : QColor(96, 96, 96));
}

// Draws the top-left and top-right media-end indicator triangles for footage clips.
static void drawClipEndTriangles(QPainter& p, ClipPtr clip, const QRect& clip_rect, QRect& text_rect, int widget_width,
                                 int widget_height) {
  int triangle_size = amber::timeline::kTrackMinHeight >> 2;
  if (clip_rect.width() <= triangle_size) return;

  p.setPen(Qt::NoPen);
  p.setBrush(QColor(80, 80, 80));

  if (clip->clip_in() == 0 && clip_rect.x() + triangle_size > 0 && clip_rect.y() + triangle_size > 0 &&
      clip_rect.x() < widget_width && clip_rect.y() < widget_height) {
    const QPoint points[3] = {QPoint(clip_rect.x(), clip_rect.y()),
                              QPoint(clip_rect.x() + triangle_size, clip_rect.y()),
                              QPoint(clip_rect.x(), clip_rect.y() + triangle_size)};
    p.drawPolygon(points, 3);
    text_rect.setLeft(text_rect.left() + (triangle_size >> 2));
  }

  if (clip->timeline_out() - clip->timeline_in() + clip->clip_in() == clip->media_length() &&
      clip_rect.right() - triangle_size < widget_width && clip_rect.y() + triangle_size > 0 && clip_rect.right() > 0 &&
      clip_rect.y() < widget_height) {
    const QPoint points[3] = {QPoint(clip_rect.right(), clip_rect.y()),
                              QPoint(clip_rect.right() - triangle_size, clip_rect.y()),
                              QPoint(clip_rect.right(), clip_rect.y() + triangle_size)};
    p.drawPolygon(points, 3);
    text_rect.setRight(text_rect.right() - (triangle_size >> 2));
  }

  p.setBrush(Qt::NoBrush);
}

// Draws the video thumbnail image for a video-track footage clip.
static void drawVideoThumbnail(QPainter& p, ClipPtr clip, const FootageStream* ms, const QRect& clip_rect,
                               int widget_width) {
  int thumb_y = p.fontMetrics().height() + amber::timeline::kClipTextPadding + amber::timeline::kClipTextPadding;
  int thumb_x = clip_rect.x() + 1;

  if (thumb_x >= widget_width || thumb_y >= clip_rect.height()) return;

  int space_for_thumb = clip_rect.width() - 1;
  if (clip->opening_transition != nullptr) {
    int ot_width = getScreenPointFromFrame(panel_timeline->zoom, clip->opening_transition->get_true_length());
    thumb_x += ot_width;
    space_for_thumb -= ot_width;
  }
  if (clip->closing_transition != nullptr) {
    space_for_thumb -= getScreenPointFromFrame(panel_timeline->zoom, clip->closing_transition->get_true_length());
  }

  int thumb_height = clip_rect.height() - thumb_y;
  int thumb_width = qRound(thumb_height * (double(ms->video_preview.width()) / double(ms->video_preview.height())));

  if (thumb_x + thumb_width < 0 || thumb_height <= thumb_y || thumb_y + thumb_height < 0 ||
      space_for_thumb <= MAX_TEXT_WIDTH) {
    return;
  }

  int thumb_clip_width = qMin(thumb_width, space_for_thumb);
  p.drawImage(QRect(thumb_x, clip_rect.y() + thumb_y, thumb_clip_width, thumb_height), ms->video_preview,
              QRect(0, 0, qRound(thumb_clip_width * (double(ms->video_preview.width()) / double(thumb_width))),
                    ms->video_preview.height()));
}

// Computes the waveform pixel limit and adjusts the checkerboard rect for audio clips.
// Returns the waveform_limit to pass to draw_waveform. Sets out_waveform_length to the
// corrected audio stream duration (in frames) for waveform index mapping.
static int computeAudioWaveformBounds(ClipPtr clip, const FootageStream* ms, long media_length, const QRect& clip_rect,
                                      int widget_width, bool& draw_checkerboard, QRect& checkerboard_rect,
                                      long& out_waveform_length) {
  out_waveform_length = media_length;
  if (ms->stream_duration > 0 && !qFuzzyIsNull(clip->speed().value)) {
    double fr = clip->sequence->frame_rate / clip->speed().value;
    out_waveform_length = static_cast<long>(
        std::floor((double(ms->stream_duration) / double(AV_TIME_BASE)) * fr / clip->media()->to_footage()->speed));
  }

  int waveform_limit =
      qMin(clip_rect.width(), getScreenPointFromFrame(panel_timeline->zoom, out_waveform_length - clip->clip_in()));

  if ((clip_rect.x() + waveform_limit) > widget_width) {
    waveform_limit -= (clip_rect.x() + waveform_limit - widget_width);
  } else if (waveform_limit < clip_rect.width()) {
    draw_checkerboard = true;
    if (waveform_limit > 0) checkerboard_rect.setLeft(checkerboard_rect.left() + waveform_limit);
  }

  return waveform_limit;
}

// Draws diagonal "error lines" over a checkerboard rect (missing media / overrun).
static void drawCheckerboard(QPainter& p, const QRect& raw_rect, int widget_width, int widget_height) {
  QRect r = raw_rect;
  r.setLeft(qMax(r.left(), 0));
  r.setRight(qMin(r.right(), widget_width));
  r.setTop(qMax(r.top(), 0));
  r.setBottom(qMin(r.bottom(), widget_height));

  if (r.left() >= widget_width || r.right() < 0 || r.top() >= widget_height || r.bottom() < 0) return;

  p.setPen(QPen(QColor(64, 64, 64), 2));
  int limit = r.width();
  int clip_height = r.height();
  for (int j = -clip_height; j < limit; j += 15) {
    int lines_start_x = r.left() + j;
    int lines_start_y = r.bottom();
    int lines_end_x = lines_start_x + clip_height;
    int lines_end_y = r.top();
    if (lines_start_x < r.left()) {
      lines_start_y -= (r.left() - lines_start_x);
      lines_start_x = r.left();
    }
    if (lines_end_x > r.right()) {
      lines_end_y -= (r.right() - lines_end_x);
      lines_end_x = r.right();
    }
    p.drawLine(lines_start_x, lines_start_y, lines_end_x, lines_end_y);
  }
}

// Handles all footage-specific drawing: triangles, thumbnail/waveform, checkerboard.
static void drawFootageContent(QPainter& p, ClipPtr clip, const QRect& clip_rect, QRect& text_rect, int widget_width,
                               int widget_height) {
  bool draw_checkerboard = false;
  QRect checkerboard_rect(clip_rect);
  FootageStream* ms = clip->media_stream();

  if (ms == nullptr) {
    draw_checkerboard = true;
  } else if (ms->preview_done) {
    long media_length = clip->media_length();

    if (!ms->infinite_length) {
      drawClipEndTriangles(p, clip, clip_rect, text_rect, widget_width, widget_height);
    }

    p.setBrush(Qt::NoBrush);

    if (clip->track() < 0) {
      // video track: draw thumbnail
      drawVideoThumbnail(p, clip, ms, clip_rect, widget_width);
      if (clip->timeline_out() - clip->timeline_in() + clip->clip_in() > clip->media_length()) {
        draw_checkerboard = true;
        checkerboard_rect.setLeft(panel_timeline->getTimelineScreenPointFromFrame(
            clip->media_length() + clip->timeline_in() - clip->clip_in()));
      }
    } else if (clip_rect.height() > amber::timeline::kTrackMinHeight) {
      // audio track: draw waveform
      p.setPen(QColor(80, 80, 80));
      int waveform_start = -qMin(clip_rect.x(), 0);
      long waveform_length = 0;
      int waveform_limit = computeAudioWaveformBounds(clip, ms, media_length, clip_rect, widget_width,
                                                      draw_checkerboard, checkerboard_rect, waveform_length);
      draw_waveform(clip, ms, waveform_length, &p, clip_rect, waveform_start, waveform_limit, panel_timeline->zoom);
    }
  }

  if (draw_checkerboard) {
    drawCheckerboard(p, checkerboard_rect, widget_width, widget_height);
  }
}

// Draws all in-clip markers.
static void drawClipMarkers(QPainter& p, ClipPtr clip, const QRect& clip_rect) {
  for (int j = 0; j < clip->get_markers().size(); j++) {
    const Marker& m = clip->get_markers().at(j);
    long marker_time = m.frame + clip->timeline_in() - clip->clip_in();
    int marker_x = panel_timeline->getTimelineScreenPointFromFrame(marker_time);
    if (marker_x > clip_rect.x() && marker_x < clip_rect.right()) {
      draw_marker(p, marker_x, clip_rect.bottom() - p.fontMetrics().height(), clip_rect.bottom(), false, m.color_label);
    }
  }
}

// Draws the clip name label, underline (if linked), and speed annotation.
static void drawClipLabel(QPainter& p, ClipPtr clip, const QRect& clip_rect, QRect& text_rect) {
  if (text_rect.width() <= MAX_TEXT_WIDTH || text_rect.right() <= 0 || text_rect.left() >= p.device()->width()) return;

  if (!clip->enabled()) {
    p.setPen(Qt::gray);
  } else if (clip->color().lightness() > 160) {
    p.setPen(Qt::black);
  }

  if (clip->linked.size() > 0) {
    int underline_y = amber::timeline::kClipTextPadding + p.fontMetrics().height() + clip_rect.top();
    int underline_width = qMin(text_rect.width() - 1, p.fontMetrics().horizontalAdvance(clip->name()));
    p.drawLine(text_rect.x(), underline_y, text_rect.x() + underline_width, underline_y);
  }

  QString name = clip->name();
  if (qFuzzyIsNull(clip->speed().value)) {
    name += " (Frozen)";
  } else if (clip->speed().value != 1.0 || clip->reversed()) {
    name += " (";
    if (clip->reversed()) name += "-";
    name += QString::number(clip->speed().value * 100) + "%)";
  }
  p.drawText(text_rect, 0, name, &text_rect);
}

// Draws the white top-left bevel and dark bottom-right bevel of a clip.
static void drawClipBevels(QPainter& p, const QRect& clip_rect, int widget_width, int widget_height) {
  p.setPen(Qt::white);
  if (clip_rect.x() >= 0 && clip_rect.x() < widget_width) p.drawLine(clip_rect.bottomLeft(), clip_rect.topLeft());
  if (clip_rect.y() >= 0 && clip_rect.y() < widget_height)
    p.drawLine(QPoint(qMax(0, clip_rect.left()), clip_rect.top()),
               QPoint(qMin(widget_width, clip_rect.right()), clip_rect.top()));

  p.setPen(QColor(0, 0, 0, 128));
  if (clip_rect.right() >= 0 && clip_rect.right() < widget_width)
    p.drawLine(clip_rect.bottomRight(), clip_rect.topRight());
  if (clip_rect.bottom() >= 0 && clip_rect.bottom() < widget_height)
    p.drawLine(QPoint(qMax(0, clip_rect.left()), clip_rect.bottom()),
               QPoint(qMin(widget_width, clip_rect.right()), clip_rect.bottom()));
}

// Draws the transition-tool hover overlay for this clip index.
static void drawTransitionToolOverlay(QPainter& p, const QRect& clip_rect, int clip_index, int widget_width) {
  bool shared_transition =
      (panel_timeline->transition_tool_open_clip > -1 && panel_timeline->transition_tool_close_clip > -1);

  QRect transition_tool_rect = clip_rect;
  bool draw_it = false;

  if (panel_timeline->transition_tool_open_clip == clip_index) {
    if (shared_transition) {
      transition_tool_rect.setWidth(TRANSITION_BETWEEN_RANGE);
    } else {
      transition_tool_rect.setWidth(transition_tool_rect.width() >> 2);
    }
    draw_it = true;
  } else if (panel_timeline->transition_tool_close_clip == clip_index) {
    if (shared_transition) {
      transition_tool_rect.setLeft(transition_tool_rect.right() - TRANSITION_BETWEEN_RANGE);
    } else {
      transition_tool_rect.setLeft(transition_tool_rect.left() + (3 * (transition_tool_rect.width() >> 2)));
    }
    draw_it = true;
  }

  if (!draw_it || transition_tool_rect.left() >= widget_width || transition_tool_rect.right() <= 0) return;

  if (transition_tool_rect.left() < 0) transition_tool_rect.setLeft(0);
  if (transition_tool_rect.right() > widget_width) transition_tool_rect.setRight(widget_width);
  p.fillRect(transition_tool_rect, QColor(0, 0, 0, 128));
}

void TimelineWidget::drawClips(QPainter& p) {
  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    ClipPtr clip = amber::ActiveSequence->clips.at(i);
    if (clip == nullptr || !is_track_visible(clip->track())) continue;

    QRect clip_rect(
        panel_timeline->getTimelineScreenPointFromFrame(clip->timeline_in()), getScreenPointFromTrack(clip->track()),
        getScreenPointFromFrame(panel_timeline->zoom, clip->length()), panel_timeline->GetTrackHeight(clip->track()));

    if (clip_rect.left() >= width() || clip_rect.right() < 0 || clip_rect.top() >= height() || clip_rect.bottom() < 0) {
      continue;
    }

    QRect text_rect(clip_rect.left() + amber::timeline::kClipTextPadding,
                    clip_rect.top() + amber::timeline::kClipTextPadding,
                    clip_rect.width() - amber::timeline::kClipTextPadding - 1,
                    clip_rect.height() - amber::timeline::kClipTextPadding - 1);

    drawClipBackground(p, clip, clip_rect, width(), height());

    if (clip->media() != nullptr && clip->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      drawFootageContent(p, clip, clip_rect, text_rect, width(), height());
    }

    drawClipMarkers(p, clip, clip_rect);

    p.setBrush(Qt::NoBrush);

    draw_transition(p, clip, clip_rect, text_rect, kTransitionOpening);
    draw_transition(p, clip, clip_rect, text_rect, kTransitionClosing);

    drawClipBevels(p, clip_rect, width(), height());

    p.setPen(Qt::white);
    drawClipLabel(p, clip, clip_rect, text_rect);

    if (panel_timeline->tool == TIMELINE_TOOL_TRANSITION) {
      drawTransitionToolOverlay(p, clip_rect, i, width());
    }
  }
}

void TimelineWidget::drawRecordingClip(QPainter& p) {
  if (panel_sequence_viewer->is_recording_cued() && is_track_visible(panel_sequence_viewer->recording_track)) {
    int rec_track_x = panel_timeline->getTimelineScreenPointFromFrame(panel_sequence_viewer->recording_start);
    int rec_track_y = getScreenPointFromTrack(panel_sequence_viewer->recording_track);
    int rec_track_height = panel_timeline->GetTrackHeight(panel_sequence_viewer->recording_track);
    if (panel_sequence_viewer->recording_start != panel_sequence_viewer->recording_end) {
      QRect rec_rect(rec_track_x, rec_track_y,
                     getScreenPointFromFrame(panel_timeline->zoom, panel_sequence_viewer->recording_end -
                                                                       panel_sequence_viewer->recording_start),
                     rec_track_height);
      p.setPen(QPen(QColor(96, 96, 96), 2));
      p.fillRect(rec_rect, QColor(192, 192, 192));
      p.drawRect(rec_rect);
    }
    QRect active_rec_rect(rec_track_x, rec_track_y,
                          getScreenPointFromFrame(panel_timeline->zoom, panel_sequence_viewer->seq->playhead -
                                                                            panel_sequence_viewer->recording_start),
                          rec_track_height);
    p.setPen(QPen(QColor(192, 0, 0), 2));
    p.fillRect(active_rec_rect, QColor(255, 96, 96));
    p.drawRect(active_rec_rect);

    p.setPen(Qt::NoPen);

    if (!panel_sequence_viewer->playing) {
      int rec_marker_size = 6;
      int rec_track_midY = rec_track_y + (rec_track_height >> 1);
      p.setBrush(Qt::white);
      QPoint cue_marker[3] = {QPoint(rec_track_x, rec_track_midY - rec_marker_size),
                              QPoint(rec_track_x + rec_marker_size, rec_track_midY),
                              QPoint(rec_track_x, rec_track_midY + rec_marker_size)};
      p.drawPolygon(cue_marker, 3);
    }
  }
}

void TimelineWidget::drawTrackLines(QPainter& p, int video_track_limit, int audio_track_limit) {
  if (amber::CurrentConfig.show_track_lines) {
    p.setPen(QColor(0, 0, 0, 96));
    audio_track_limit++;
    video_track_limit--;

    // draw lines for video tracks
    for (int i = video_track_limit; i < 0; i++) {
      int line_y = getScreenPointFromTrack(i) - 1;
      p.drawLine(0, line_y, rect().width(), line_y);
    }
    // draw lines for audio tracks
    for (int i = 0; i < audio_track_limit; i++) {
      int line_y = getScreenPointFromTrack(i) + panel_timeline->GetTrackHeight(i);
      p.drawLine(0, line_y, rect().width(), line_y);
    }
  }
}

void TimelineWidget::drawSelections(QPainter& p) {
  for (const auto& s : amber::ActiveSequence->selections) {
    if (is_track_visible(s.track)) {
      int selection_y = getScreenPointFromTrack(s.track);
      int selection_x = panel_timeline->getTimelineScreenPointFromFrame(s.in);
      p.setPen(Qt::NoPen);
      p.setBrush(Qt::NoBrush);
      p.fillRect(selection_x, selection_y, panel_timeline->getTimelineScreenPointFromFrame(s.out) - selection_x,
                 panel_timeline->GetTrackHeight(s.track), QColor(0, 0, 0, 64));
    }
  }

  // draw rectangle select
  if (panel_timeline->rect_select_proc) {
    QRect rect_select = panel_timeline->rect_select_rect;
    draw_selection_rectangle(p, rect_select);
  }
}

void TimelineWidget::drawGhosts(QPainter& p) {
  if (!panel_timeline->ghosts.isEmpty()) {
    QVector<int> insert_points;
    long first_ghost = LONG_MAX;
    for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
      const Ghost& g = panel_timeline->ghosts.at(i);
      first_ghost = qMin(first_ghost, g.in);
      if (is_track_visible(g.track)) {
        int ghost_x = panel_timeline->getTimelineScreenPointFromFrame(g.in);
        int ghost_y = getScreenPointFromTrack(g.track);
        int ghost_width = panel_timeline->getTimelineScreenPointFromFrame(g.out) - ghost_x - 1;
        int ghost_height = panel_timeline->GetTrackHeight(g.track) - 1;

        insert_points.append(ghost_y + (ghost_height >> 1));

        p.setPen(QColor(255, 255, 0));
        for (int j = 0; j < amber::timeline::kGhostThickness; j++) {
          p.drawRect(ghost_x + j, ghost_y + j, ghost_width - j - j, ghost_height - j - j);
        }
      }
    }

    // draw insert indicator
    if (panel_timeline->move_insert && !insert_points.isEmpty()) {
      p.setBrush(Qt::white);
      p.setPen(Qt::NoPen);
      int insert_x = panel_timeline->getTimelineScreenPointFromFrame(first_ghost);
      int tri_size = amber::timeline::kTrackMinHeight >> 2;

      for (int insert_point : insert_points) {
        QPoint points[3] = {QPoint(insert_x, insert_point - tri_size), QPoint(insert_x + tri_size, insert_point),
                            QPoint(insert_x, insert_point + tri_size)};
        p.drawPolygon(points, 3);
      }
    }
  }
}

void TimelineWidget::drawSplittingCursor(QPainter& p) {
  if (panel_timeline->splitting) {
    for (int i = 0; i < panel_timeline->split_tracks.size(); i++) {
      if (is_track_visible(panel_timeline->split_tracks.at(i))) {
        int cursor_x = panel_timeline->getTimelineScreenPointFromFrame(panel_timeline->drag_frame_start);
        int cursor_y = getScreenPointFromTrack(panel_timeline->split_tracks.at(i));

        p.setPen(QColor(64, 64, 64));
        p.drawLine(cursor_x, cursor_y, cursor_x,
                   cursor_y + panel_timeline->GetTrackHeight(panel_timeline->split_tracks.at(i)));
      }
    }
  }
}

void TimelineWidget::drawPlayhead(QPainter& p) {
  p.setPen(Qt::red);
  int playhead_x = panel_timeline->getTimelineScreenPointFromFrame(amber::ActiveSequence->playhead);
  p.drawLine(playhead_x, rect().top(), playhead_x, rect().bottom());

  // Draw single frame highlight
  int playhead_frame_width =
      panel_timeline->getTimelineScreenPointFromFrame(amber::ActiveSequence->playhead + 1) - playhead_x;
  if (playhead_frame_width > 5) {  // hardcoded for now, maybe better way to do this?
    QRectF singleFrameRect(playhead_x, rect().top(), playhead_frame_width, rect().bottom());
    p.fillRect(singleFrameRect, QColor(255, 255, 255, 15));
  }
}

void TimelineWidget::drawEditCursor(QPainter& p) {
  if (current_tool_shows_cursor() && is_track_visible(panel_timeline->cursor_track)) {
    int cursor_x = panel_timeline->getTimelineScreenPointFromFrame(panel_timeline->cursor_frame);
    int cursor_y = getScreenPointFromTrack(panel_timeline->cursor_track);

    p.setPen(Qt::gray);
    p.drawLine(cursor_x, cursor_y, cursor_x, cursor_y + panel_timeline->GetTrackHeight(panel_timeline->cursor_track));
  }
}

void TimelineWidget::paintEvent(QPaintEvent*) {
  if (amber::ActiveSequence != nullptr) {
    QPainter p(this);

    // get widget width and height
    int video_track_limit = 0;
    int audio_track_limit = 0;
    for (auto clip : amber::ActiveSequence->clips) {
      if (clip != nullptr) {
        video_track_limit = qMin(video_track_limit, clip->track());
        audio_track_limit = qMax(audio_track_limit, clip->track());
      }
    }

    // padding: one default-height empty track above video and below audio (drop zones)
    int panel_height = amber::timeline::kTrackDefaultHeight * 2;

    for (int i = -1; i >= video_track_limit; i--) panel_height += panel_timeline->GetTrackHeight(i);
    for (int i = 0; i <= audio_track_limit; i++) panel_height += panel_timeline->GetTrackHeight(i);
    scrollBar->setMaximum(qMax(0, panel_height - height()));

    drawClips(p);
    drawRecordingClip(p);
    drawTrackLines(p, video_track_limit, audio_track_limit);
    drawSelections(p);
    drawGhosts(p);
    drawSplittingCursor(p);
    drawPlayhead(p);

    // draw V/A seam line
    const int seam_screen_y = panel_timeline->SeamY() - scroll;
    if (seam_screen_y >= 0 && seam_screen_y < height()) {
      p.setPen(QColor(0, 0, 0, 160));
      p.drawLine(0, seam_screen_y, rect().width(), seam_screen_y);
    }

    // draw snap point
    if (panel_timeline->snapped) {
      p.setPen(Qt::white);
      int snap_x = panel_timeline->getTimelineScreenPointFromFrame(panel_timeline->snap_point);
      p.drawLine(snap_x, 0, snap_x, height());
    }

    drawEditCursor(p);
  }
}
