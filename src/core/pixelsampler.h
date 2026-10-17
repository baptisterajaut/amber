#ifndef PIXELSAMPLER_H
#define PIXELSAMPLER_H

#include <QPointF>
#include <QRectF>
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

// The rect render() draws the frame in, widget coordinates: the widget itself up to the zoom where ViewerContainer
// stops growing it, then larger, panned by the scrolls. Empty when a size is empty.
QRectF viewer_frame_rect(const ViewerFrameGeometry& g);

// A sequence point (origin top-left, unclamped) to the widget position the viewer draws it at, and back. The overlays
// (guides, gizmos, title safe area, rulers) and their mouse hit tests go through these so they stay on the picture at
// any zoom and scroll. viewer_to_sequence() gives (0, 0) when a size is empty.
QPointF sequence_to_viewer(const ViewerFrameGeometry& g, const QPointF& seq);
QPointF viewer_to_sequence(const ViewerFrameGeometry& g, const QPointF& pos);

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
