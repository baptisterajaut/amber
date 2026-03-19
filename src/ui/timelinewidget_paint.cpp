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

void draw_waveform(ClipPtr clip, const FootageStream* ms, long media_length, QPainter *p, const QRect& clip_rect, int waveform_start, int waveform_limit, double zoom) {
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
  int divider = ms->audio_channels*2;

  int channel_height = clip_rect.height()/ms->audio_channels;

  int last_waveform_index = -1;
  int preview_size = ms->audio_preview.size();

  for (int i=waveform_start;i<waveform_limit;i++) {
    int waveform_index = qFloor((((clip->clip_in() + (double(i)/zoom))/media_length) * preview_size)/divider)*divider;

    if (clip->reversed()) {
      waveform_index = preview_size - waveform_index - (ms->audio_channels * 2);
    }

    if (waveform_index < 0 || waveform_index >= preview_size) continue;

    if (last_waveform_index < 0) last_waveform_index = waveform_index;

    for (int j=0;j<ms->audio_channels;j++) {
      int mid = (amber::CurrentConfig.rectified_waveforms) ? clip_rect.top()+channel_height*(j+1) : clip_rect.top()+channel_height*j+(channel_height/2);

      int offset_range_start = last_waveform_index+(j*2);
      int offset_range_end = waveform_index+(j*2);
      int offset_range_min = qMin(offset_range_start, offset_range_end);
      int offset_range_max = qMax(offset_range_start, offset_range_end);

      if (offset_range_min < 0 || offset_range_min + 1 >= preview_size) continue;

      qint8 min = qint8(qRound(double(ms->audio_preview.at(offset_range_min)) / 128.0 * (channel_height/2)));
      qint8 max = qint8(qRound(double(ms->audio_preview.at(offset_range_min+1)) / 128.0 * (channel_height/2)));

      if ((offset_range_max + 1) < preview_size) {

        // for waveform drawings, we get the maximum below 0 and maximum above 0 for this waveform range
        for (int k=offset_range_min+2;k<=offset_range_max;k+=2) {
          min = qMin(min, qint8(qRound(double(ms->audio_preview.at(k)) / 128.0 * (channel_height/2))));
          max = qMax(max, qint8(qRound(double(ms->audio_preview.at(k+1)) / 128.0 * (channel_height/2))));
        }

        // draw waveforms
        if (amber::CurrentConfig.rectified_waveforms)  {

          // rectified waveforms start from the bottom and draw upwards
          p->drawLine(clip_rect.left()+i, mid, clip_rect.left()+i, mid - (max - min));
        } else {

          // non-rectified waveforms start from the center and draw outwards
          p->drawLine(clip_rect.left()+i, mid+min, clip_rect.left()+i, mid+max);

        }
      }
    }
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
      text_rect.setX(text_rect.x()+transition_width);
    } else {
      tr_x = clip_rect.right()-transition_width;
      text_rect.setWidth(text_rect.width()-transition_width);
    }
    QRect transition_rect = QRect(tr_x, tr_y, transition_width, transition_height);
    p.fillRect(transition_rect, transition_color);
    QRect transition_text_rect(transition_rect.x() + amber::timeline::kClipTextPadding, transition_rect.y() + amber::timeline::kClipTextPadding, transition_rect.width() - amber::timeline::kClipTextPadding, transition_rect.height() - amber::timeline::kClipTextPadding);
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

void TimelineWidget::drawClips(QPainter& p) {
  for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
    ClipPtr clip = amber::ActiveSequence->clips.at(i);
    if (clip != nullptr && is_track_visible(clip->track())) {
      QRect clip_rect(panel_timeline->getTimelineScreenPointFromFrame(clip->timeline_in()), getScreenPointFromTrack(clip->track()), getScreenPointFromFrame(panel_timeline->zoom, clip->length()), panel_timeline->GetTrackHeight(clip->track()));
      QRect text_rect(clip_rect.left() + amber::timeline::kClipTextPadding, clip_rect.top() + amber::timeline::kClipTextPadding, clip_rect.width() - amber::timeline::kClipTextPadding - 1, clip_rect.height() - amber::timeline::kClipTextPadding - 1);
      if (clip_rect.left() < width() && clip_rect.right() >= 0 && clip_rect.top() < height() && clip_rect.bottom() >= 0) {
        QRect actual_clip_rect = clip_rect;
        if (actual_clip_rect.x() < 0) actual_clip_rect.setX(0);
        if (actual_clip_rect.right() > width()) actual_clip_rect.setRight(width());
        if (actual_clip_rect.y() < 0) actual_clip_rect.setY(0);
        if (actual_clip_rect.bottom() > height()) actual_clip_rect.setBottom(height());
        p.fillRect(actual_clip_rect, (clip->enabled()) ? clip->display_color() : QColor(96, 96, 96));

        int thumb_x = clip_rect.x() + 1;

        if (clip->media() != nullptr && clip->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
          bool draw_checkerboard = false;
          QRect checkerboard_rect(clip_rect);
          FootageStream* ms = clip->media_stream();
          if (ms == nullptr) {
            draw_checkerboard = true;
          } else if (ms->preview_done) {
            // draw top and tail triangles
            int triangle_size = amber::timeline::kTrackMinHeight >> 2;
            if (!ms->infinite_length && clip_rect.width() > triangle_size) {
              p.setPen(Qt::NoPen);
              p.setBrush(QColor(80, 80, 80));
              if (clip->clip_in() == 0
                  && clip_rect.x() + triangle_size > 0
                  && clip_rect.y() + triangle_size > 0
                  && clip_rect.x() < width()
                  && clip_rect.y() < height()) {
                const QPoint points[3] = {
                  QPoint(clip_rect.x(), clip_rect.y()),
                  QPoint(clip_rect.x() + triangle_size, clip_rect.y()),
                  QPoint(clip_rect.x(), clip_rect.y() + triangle_size)
                };
                p.drawPolygon(points, 3);
                text_rect.setLeft(text_rect.left() + (triangle_size >> 2));
              }
              if (clip->timeline_out() - clip->timeline_in() + clip->clip_in() == clip->media_length()
                  && clip_rect.right() - triangle_size < width()
                  && clip_rect.y() + triangle_size > 0
                  && clip_rect.right() > 0
                  && clip_rect.y() < height()) {
                const QPoint points[3] = {
                  QPoint(clip_rect.right(), clip_rect.y()),
                  QPoint(clip_rect.right() - triangle_size, clip_rect.y()),
                  QPoint(clip_rect.right(), clip_rect.y() + triangle_size)
                };
                p.drawPolygon(points, 3);
                text_rect.setRight(text_rect.right() - (triangle_size >> 2));
              }
            }

            p.setBrush(Qt::NoBrush);

            // draw thumbnail/waveform
            long media_length = clip->media_length();

            if (clip->track() < 0) {
              // draw thumbnail
              int thumb_y = p.fontMetrics().height()+amber::timeline::kClipTextPadding+amber::timeline::kClipTextPadding;
              if (thumb_x < width() && thumb_y < height()) {
                int space_for_thumb = clip_rect.width()-1;
                if (clip->opening_transition != nullptr) {
                  int ot_width = getScreenPointFromFrame(panel_timeline->zoom, clip->opening_transition->get_true_length());
                  thumb_x += ot_width;
                  space_for_thumb -= ot_width;
                }
                if (clip->closing_transition != nullptr) {
                  space_for_thumb -= getScreenPointFromFrame(panel_timeline->zoom, clip->closing_transition->get_true_length());
                }
                int thumb_height = clip_rect.height()-thumb_y;
                int thumb_width = qRound(thumb_height*(double(ms->video_preview.width())/double(ms->video_preview.height())));
                if (thumb_x + thumb_width >= 0
                    && thumb_height > thumb_y
                    && thumb_y + thumb_height >= 0
                    && space_for_thumb > MAX_TEXT_WIDTH) {
                  int thumb_clip_width = qMin(thumb_width, space_for_thumb);
                  p.drawImage(QRect(thumb_x,
                                    clip_rect.y()+thumb_y,
                                    thumb_clip_width,
                                    thumb_height),
                              ms->video_preview,
                              QRect(0,
                                    0,
                                    qRound(thumb_clip_width*(double(ms->video_preview.width())/double(thumb_width))),
                                    ms->video_preview.height()
                                    )
                              );
                }
              }
              if (clip->timeline_out() - clip->timeline_in() + clip->clip_in() > clip->media_length()) {
                draw_checkerboard = true;
                checkerboard_rect.setLeft(panel_timeline->getTimelineScreenPointFromFrame(clip->media_length() + clip->timeline_in() - clip->clip_in()));
              }
            } else if (clip_rect.height() > amber::timeline::kTrackMinHeight) {
              // draw waveform
              p.setPen(QColor(80, 80, 80));

              int waveform_start = -qMin(clip_rect.x(), 0);
              int waveform_limit = qMin(clip_rect.width(), getScreenPointFromFrame(panel_timeline->zoom, media_length - clip->clip_in()));

              if ((clip_rect.x() + waveform_limit) > width()) {
                waveform_limit -= (clip_rect.x() + waveform_limit - width());
              } else if (waveform_limit < clip_rect.width()) {
                draw_checkerboard = true;
                if (waveform_limit > 0) checkerboard_rect.setLeft(checkerboard_rect.left() + waveform_limit);
              }

              draw_waveform(clip, ms, media_length, &p, clip_rect, waveform_start, waveform_limit, panel_timeline->zoom);
            }
          }
          if (draw_checkerboard) {
            checkerboard_rect.setLeft(qMax(checkerboard_rect.left(), 0));
            checkerboard_rect.setRight(qMin(checkerboard_rect.right(), width()));
            checkerboard_rect.setTop(qMax(checkerboard_rect.top(), 0));
            checkerboard_rect.setBottom(qMin(checkerboard_rect.bottom(), height()));

            if (checkerboard_rect.left() < width()
                && checkerboard_rect.right() >= 0
                && checkerboard_rect.top() < height()
                && checkerboard_rect.bottom() >= 0) {
              // draw "error lines" if media stream is missing
              p.setPen(QPen(QColor(64, 64, 64), 2));
              int limit = checkerboard_rect.width();
              int clip_height = checkerboard_rect.height();
              for (int j=-clip_height;j<limit;j+=15) {
                int lines_start_x = checkerboard_rect.left()+j;
                int lines_start_y = checkerboard_rect.bottom();
                int lines_end_x = lines_start_x + clip_height;
                int lines_end_y = checkerboard_rect.top();
                if (lines_start_x < checkerboard_rect.left()) {
                  lines_start_y -= (checkerboard_rect.left() - lines_start_x);
                  lines_start_x = checkerboard_rect.left();
                }
                if (lines_end_x > checkerboard_rect.right()) {
                  lines_end_y -= (checkerboard_rect.right() - lines_end_x);
                  lines_end_x = checkerboard_rect.right();
                }
                p.drawLine(lines_start_x, lines_start_y, lines_end_x, lines_end_y);
              }
            }
          }
        }

        // draw clip markers
        for (int j=0;j<clip->get_markers().size();j++) {
          const Marker& m = clip->get_markers().at(j);

          // convert marker time (in clip time) to sequence time
          long marker_time = m.frame + clip->timeline_in() - clip->clip_in();
          int marker_x = panel_timeline->getTimelineScreenPointFromFrame(marker_time);
          if (marker_x > clip_rect.x() && marker_x < clip_rect.right()) {
            draw_marker(p, marker_x, clip_rect.bottom() - p.fontMetrics().height(), clip_rect.bottom(), false,
                        m.color_label);
          }
        }
        p.setBrush(Qt::NoBrush);

        // draw clip transitions
        draw_transition(p, clip, clip_rect, text_rect, kTransitionOpening);
        draw_transition(p, clip, clip_rect, text_rect, kTransitionClosing);

        // top left bevel
        p.setPen(Qt::white);
        if (clip_rect.x() >= 0 && clip_rect.x() < width()) p.drawLine(clip_rect.bottomLeft(), clip_rect.topLeft());
        if (clip_rect.y() >= 0 && clip_rect.y() < height()) p.drawLine(QPoint(qMax(0, clip_rect.left()), clip_rect.top()), QPoint(qMin(width(), clip_rect.right()), clip_rect.top()));

        // draw text
        if (text_rect.width() > MAX_TEXT_WIDTH && text_rect.right() > 0 && text_rect.left() < width()) {
          if (!clip->enabled()) {
            p.setPen(Qt::gray);
          } else if (clip->color().lightness() > 160) {
            // set to black if color is bright
            p.setPen(Qt::black);
          }
          if (clip->linked.size() > 0) {
            int underline_y = amber::timeline::kClipTextPadding + p.fontMetrics().height() + clip_rect.top();
              int underline_width = qMin(text_rect.width() - 1, p.fontMetrics().horizontalAdvance(clip->name()));
            p.drawLine(text_rect.x(), underline_y, text_rect.x() + underline_width, underline_y);
          }
          QString name = clip->name();
          if (clip->speed().value != 1.0 || clip->reversed()) {
            name += " (";
            if (clip->reversed()) name += "-";
            name += QString::number(clip->speed().value*100) + "%)";
          }
          p.drawText(text_rect, 0, name, &text_rect);
        }

        // bottom right gray
        p.setPen(QColor(0, 0, 0, 128));
        if (clip_rect.right() >= 0 && clip_rect.right() < width()) p.drawLine(clip_rect.bottomRight(), clip_rect.topRight());
        if (clip_rect.bottom() >= 0 && clip_rect.bottom() < height()) p.drawLine(QPoint(qMax(0, clip_rect.left()), clip_rect.bottom()), QPoint(qMin(width(), clip_rect.right()), clip_rect.bottom()));

        // draw transition tool
        if (panel_timeline->tool == TIMELINE_TOOL_TRANSITION) {

          bool shared_transition = (panel_timeline->transition_tool_open_clip > -1
                                    && panel_timeline->transition_tool_close_clip > -1);

          QRect transition_tool_rect = clip_rect;
          bool draw_transition_tool_rect = false;

          if (panel_timeline->transition_tool_open_clip == i) {
            if (shared_transition) {
              transition_tool_rect.setWidth(TRANSITION_BETWEEN_RANGE);
            } else {
              transition_tool_rect.setWidth(transition_tool_rect.width()>>2);
            }
            draw_transition_tool_rect = true;
          } else if (panel_timeline->transition_tool_close_clip == i) {
            if (shared_transition) {
              transition_tool_rect.setLeft(transition_tool_rect.right() - TRANSITION_BETWEEN_RANGE);
            } else {
              transition_tool_rect.setLeft(transition_tool_rect.left() + (3*(transition_tool_rect.width()>>2)));
            }
            draw_transition_tool_rect = true;
          }

          if (draw_transition_tool_rect
              && transition_tool_rect.left() < width()
              && transition_tool_rect.right() > 0) {
            if (transition_tool_rect.left() < 0) {
              transition_tool_rect.setLeft(0);
            }
            if (transition_tool_rect.right() > width()) {
              transition_tool_rect.setRight(width());
            }
            p.fillRect(transition_tool_rect, QColor(0, 0, 0, 128));
          }
        }
      }
    }
  }
}

void TimelineWidget::drawRecordingClip(QPainter& p) {
  if (panel_sequence_viewer->is_recording_cued() && is_track_visible(panel_sequence_viewer->recording_track)) {
    int rec_track_x = panel_timeline->getTimelineScreenPointFromFrame(panel_sequence_viewer->recording_start);
    int rec_track_y = getScreenPointFromTrack(panel_sequence_viewer->recording_track);
    int rec_track_height = panel_timeline->GetTrackHeight(panel_sequence_viewer->recording_track);
    if (panel_sequence_viewer->recording_start != panel_sequence_viewer->recording_end) {
      QRect rec_rect(
            rec_track_x,
            rec_track_y,
            getScreenPointFromFrame(panel_timeline->zoom, panel_sequence_viewer->recording_end - panel_sequence_viewer->recording_start),
            rec_track_height
            );
      p.setPen(QPen(QColor(96, 96, 96), 2));
      p.fillRect(rec_rect, QColor(192, 192, 192));
      p.drawRect(rec_rect);
    }
    QRect active_rec_rect(
          rec_track_x,
          rec_track_y,
          getScreenPointFromFrame(panel_timeline->zoom, panel_sequence_viewer->seq->playhead - panel_sequence_viewer->recording_start),
          rec_track_height
          );
    p.setPen(QPen(QColor(192, 0, 0), 2));
    p.fillRect(active_rec_rect, QColor(255, 96, 96));
    p.drawRect(active_rec_rect);

    p.setPen(Qt::NoPen);

    if (!panel_sequence_viewer->playing) {
      int rec_marker_size = 6;
      int rec_track_midY = rec_track_y + (rec_track_height >> 1);
      p.setBrush(Qt::white);
      QPoint cue_marker[3] = {
        QPoint(rec_track_x, rec_track_midY - rec_marker_size),
        QPoint(rec_track_x + rec_marker_size, rec_track_midY),
        QPoint(rec_track_x, rec_track_midY + rec_marker_size)
      };
      p.drawPolygon(cue_marker, 3);
    }
  }
}

void TimelineWidget::drawTrackLines(QPainter& p, int video_track_limit, int audio_track_limit) {
  if (amber::CurrentConfig.show_track_lines) {
    p.setPen(QColor(0, 0, 0, 96));
    audio_track_limit++;
    if (video_track_limit == 0) video_track_limit--;

    if (bottom_align) {
      // only draw lines for video tracks
      for (int i=video_track_limit;i<0;i++) {
        int line_y = getScreenPointFromTrack(i) - 1;
        p.drawLine(0, line_y, rect().width(), line_y);
      }
    } else {
      // only draw lines for audio tracks
      for (int i=0;i<audio_track_limit;i++) {
        int line_y = getScreenPointFromTrack(i) + panel_timeline->GetTrackHeight(i);
        p.drawLine(0, line_y, rect().width(), line_y);
      }
    }
  }
}

void TimelineWidget::drawSelections(QPainter& p) {
  for (const auto & s : amber::ActiveSequence->selections) {
    if (is_track_visible(s.track)) {
      int selection_y = getScreenPointFromTrack(s.track);
      int selection_x = panel_timeline->getTimelineScreenPointFromFrame(s.in);
      p.setPen(Qt::NoPen);
      p.setBrush(Qt::NoBrush);
      p.fillRect(selection_x, selection_y, panel_timeline->getTimelineScreenPointFromFrame(s.out) - selection_x, panel_timeline->GetTrackHeight(s.track), QColor(0, 0, 0, 64));
    }
  }

  // draw rectangle select
  if (panel_timeline->rect_select_proc) {
    QRect rect_select = panel_timeline->rect_select_rect;

    if (bottom_align) {
      rect_select.translate(0, height());
    }

    draw_selection_rectangle(p, rect_select);
  }
}

void TimelineWidget::drawGhosts(QPainter& p) {
  if (!panel_timeline->ghosts.isEmpty()) {
    QVector<int> insert_points;
    long first_ghost = LONG_MAX;
    for (int i=0;i<panel_timeline->ghosts.size();i++) {
      const Ghost& g = panel_timeline->ghosts.at(i);
      first_ghost = qMin(first_ghost, g.in);
      if (is_track_visible(g.track)) {
        int ghost_x = panel_timeline->getTimelineScreenPointFromFrame(g.in);
        int ghost_y = getScreenPointFromTrack(g.track);
        int ghost_width = panel_timeline->getTimelineScreenPointFromFrame(g.out) - ghost_x - 1;
        int ghost_height = panel_timeline->GetTrackHeight(g.track) - 1;

        insert_points.append(ghost_y + (ghost_height>>1));

        p.setPen(QColor(255, 255, 0));
        for (int j=0;j<amber::timeline::kGhostThickness;j++) {
          p.drawRect(ghost_x+j, ghost_y+j, ghost_width-j-j, ghost_height-j-j);
        }
      }
    }

    // draw insert indicator
    if (panel_timeline->move_insert && !insert_points.isEmpty()) {
      p.setBrush(Qt::white);
      p.setPen(Qt::NoPen);
      int insert_x = panel_timeline->getTimelineScreenPointFromFrame(first_ghost);
      int tri_size = amber::timeline::kTrackMinHeight>>2;

      for (int insert_point : insert_points) {
        QPoint points[3] = {
          QPoint(insert_x, insert_point - tri_size),
          QPoint(insert_x + tri_size, insert_point),
          QPoint(insert_x, insert_point + tri_size)
        };
        p.drawPolygon(points, 3);
      }
    }
  }
}

void TimelineWidget::drawSplittingCursor(QPainter& p) {
  if (panel_timeline->splitting) {
    for (int i=0;i<panel_timeline->split_tracks.size();i++) {
      if (is_track_visible(panel_timeline->split_tracks.at(i))) {
        int cursor_x = panel_timeline->getTimelineScreenPointFromFrame(panel_timeline->drag_frame_start);
        int cursor_y = getScreenPointFromTrack(panel_timeline->split_tracks.at(i));

        p.setPen(QColor(64, 64, 64));
        p.drawLine(cursor_x, cursor_y, cursor_x, cursor_y + panel_timeline->GetTrackHeight(panel_timeline->split_tracks.at(i)));
      }
    }
  }
}

void TimelineWidget::drawPlayhead(QPainter& p) {
  p.setPen(Qt::red);
  int playhead_x = panel_timeline->getTimelineScreenPointFromFrame(amber::ActiveSequence->playhead);
  p.drawLine(playhead_x, rect().top(), playhead_x, rect().bottom());

  // Draw single frame highlight
  int playhead_frame_width = panel_timeline->getTimelineScreenPointFromFrame(amber::ActiveSequence->playhead+1) - playhead_x;
  if (playhead_frame_width > 5){ //hardcoded for now, maybe better way to do this?
      QRectF singleFrameRect(playhead_x, rect().top(), playhead_frame_width, rect().bottom());
      p.fillRect(singleFrameRect, QColor(255,255,255,15));
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

    // start by adding a track height worth of padding
    int panel_height = amber::timeline::kTrackDefaultHeight;

    // loop through tracks for maximum panel height
    if (bottom_align) {
      for (int i=-1;i>=video_track_limit;i--) {
        panel_height += panel_timeline->GetTrackHeight(i);
      }
    } else {
      for (int i=0;i<=audio_track_limit;i++) {
        panel_height += panel_timeline->GetTrackHeight(i);
      }
    }
    if (bottom_align) {
      scrollBar->setMinimum(qMin(0, - panel_height + height()));
    } else {
      scrollBar->setMaximum(qMax(0, panel_height - height()));
    }

    drawClips(p);
    drawRecordingClip(p);
    drawTrackLines(p, video_track_limit, audio_track_limit);
    drawSelections(p);
    drawGhosts(p);
    drawSplittingCursor(p);
    drawPlayhead(p);

    // draw border
    p.setPen(QColor(0, 0, 0, 64));
    int edge_y = (bottom_align) ? rect().height()-1 : 0;
    p.drawLine(0, edge_y, rect().width(), edge_y);

    // draw snap point
    if (panel_timeline->snapped) {
      p.setPen(Qt::white);
      int snap_x = panel_timeline->getTimelineScreenPointFromFrame(panel_timeline->snap_point);
      p.drawLine(snap_x, 0, snap_x, height());
    }

    drawEditCursor(p);
  }
}
