#ifndef PIXELSAMPLER_H
#define PIXELSAMPLER_H

#include <QPointF>
#include <QSize>
#include <QSizeF>

namespace amber {

// Where the viewer draws the frame inside its widget, with the values ViewerWidget::render() builds its quad from
struct ViewerFrameGeometry {
  QSizeF widget;         // widget size, logical pixels
  double zoom{1.0};      // ViewerContainer::zoom: widget pixels per sequence pixel
  double x_scroll{0.0};  // 0..1 when the zoomed frame overflows the widget: 0 shows its left edge
  double y_scroll{0.0};  // 0 shows its top edge
  QSize sequence;
};

// A viewer point on the frame: the sequence pixel under it (origin top-left) and the readback buffer pixel holding its
// colour (sequence size / preview divider)
struct FramePoint {
  int seq_x{0};
  int seq_y{0};
  int buf_x{0};
  int buf_y{0};
};

// Maps a widget position (logical pixels) to the frame pixel the viewer draws there. False when the point is off the
// frame (or a size is empty). A point on the far edge clamps to the last column / row.
bool map_viewer_point(const ViewerFrameGeometry& g, const QPointF& pos, const QSize& buffer, FramePoint* out);

}  // namespace amber

#endif  // PIXELSAMPLER_H
