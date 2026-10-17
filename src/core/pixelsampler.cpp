#include "core/pixelsampler.h"

#include <QtGlobal>

namespace amber {

bool map_viewer_point(const ViewerFrameGeometry& g, const QPointF& pos, const QSize& buffer, FramePoint* out) {
  if (g.widget.isEmpty() || g.sequence.isEmpty() || buffer.isEmpty() || g.zoom <= 0) return false;

  // render()'s quad: zoom_factor = zoom * seq_w / widget_w scales both axes, so the frame is drawn
  // zoom_factor * widget size, shifted by the scroll share of its overflow
  const double zf = g.zoom * g.sequence.width() / g.widget.width();
  const double drawn_w = zf * g.widget.width();
  const double drawn_h = zf * g.widget.height();
  const double left = -(drawn_w - g.widget.width()) * g.x_scroll;
  const double top = -(drawn_h - g.widget.height()) * g.y_scroll;

  const double u = (pos.x() - left) / drawn_w;
  const double v = (pos.y() - top) / drawn_h;
  if (u < 0 || u > 1 || v < 0 || v > 1) return false;

  out->seq_x = qMin(int(u * g.sequence.width()), g.sequence.width() - 1);
  out->seq_y = qMin(int(v * g.sequence.height()), g.sequence.height() - 1);
  out->buf_x = qMin(int(u * buffer.width()), buffer.width() - 1);
  out->buf_y = qMin(int(v * buffer.height()), buffer.height() - 1);
  return true;
}

QString format_pixel_sample(int x, int y, int r, int g, int b, int a) {
  const QChar figure_space(0x2007);
  auto pad = [&](int n, int width) { return QString::number(n).rightJustified(width, figure_space); };
  return QStringLiteral("X %1  Y %2   R %3  G %4  B %5  A %6")
      .arg(pad(x, 4), pad(y, 4), pad(r, 3), pad(g, 3), pad(b, 3), pad(a, 3));
}

}  // namespace amber
