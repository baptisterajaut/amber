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

#ifndef TIMELINEHEADER_H
#define TIMELINEHEADER_H

#include <QWidget>
#include <QFontMetrics>
class Viewer;
class QScrollBar;

bool center_scroll_to_playhead(QScrollBar* bar, double zoom, long playhead);

class TimelineHeader : public QWidget
{
	Q_OBJECT
public:
	explicit TimelineHeader(QWidget *parent = nullptr);
	void set_in_point(long p);
	void set_out_point(long p);

	Viewer* viewer;

	bool snapping{true};

	void show_text(bool enable);
	double get_zoom();
	void delete_markers();
	void set_scrollbar_max(QScrollBar* bar, long sequence_end_frame, int offset);

public slots:
	void update_zoom(double z);
	void set_scroll(int);
	void set_visible_in(long i);
	void show_context_menu(const QPoint &pos);
	void resized_scroll_listener(double d);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void focusOutEvent(QFocusEvent*) override;

private:
	void update_parents();

	bool dragging{false};

	bool resizing_workarea{false};
	bool resizing_workarea_in;
	long temp_workarea_in;
	long temp_workarea_out;
	long sequence_end;

	double zoom{1};

	long in_visible{0};

	void set_playhead(int mouse_x);

	int get_marker_offset();

	QFontMetrics fm;

	int drag_start;
	bool dragging_markers{false};
	QVector<int> selected_markers;
	QVector<long> selected_marker_original_times;

	long getHeaderFrameFromScreenPoint(int x);
	int getHeaderScreenPointFromFrame(long frame);

	int scroll{0};

	int height_actual;
	bool text_enabled;

signals:
};

#endif // TIMELINEHEADER_H
